/**
 * collectd - src/write_http.c
 * Copyright (C) 2009       Paul Sadauskas
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2007-2014  Florian octo Forster
 * Copyright (C) 2016       Tolga Ceylan
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 *   Doug MacEachern <dougm@hyperic.com>
 *   Paul Sadauskas <psadauskas@gmail.com>
 *   Tolga Ceylan <tolga.ceylan@gmail.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "utils_cache.h"
#include "utils_format_json.h"

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include <curl/curl.h>

#define WH_HTTP_DEFAULT_BUFFER_SIZE 4096
#define WH_HTTP_MIN_BUFFER_SIZE     1024

#define WH_FORMAT_COMMAND 0
#define WH_FORMAT_JSON    1

/* TODO: log which option failed */
#define CURL_SET_OPT(err, ctx, opt, val) ({ \
    if (!err) { \
        err = curl_easy_setopt((ctx)->curl, (opt), (val)); \
    }\
})

static pthread_key_t   wh_ctx_key;

/*
 * I/O thread specific context
 */
struct wh_ctx_s
{
        CURL *curl;
        struct curl_slist *headers;
        char curl_errbuf[CURL_ERROR_SIZE];

        char  *send_buffer;
        size_t send_buffer_free;
        size_t send_buffer_fill;
        size_t send_buffer_size;
        cdtime_t send_buffer_init_time;
};
typedef struct wh_ctx_s wh_ctx_t;

/*
 * Global Config
 */
struct wh_cfg_s
{
        char *name;
        char *location;
        char *user;
        char *pass;
        char *credentials;
        _Bool verify_peer;
        _Bool verify_host;
        char *cacert;
        char *capath;
        char *clientkey;
        char *clientcert;
        char *clientkeypass;
        long sslversion;
        _Bool store_rates;
        _Bool log_http_error;
        int   low_speed_limit;
        time_t low_speed_time;
        int timeout;
        int format;
        size_t send_buffer_size;
};
typedef struct wh_cfg_s wh_cfg_t;

static int wh_ctx_init (wh_ctx_t *ctx);

/* Global config */
wh_cfg_t *wh_cfg;

static void wh_ctx_destructor (void *data) /* {{{ */
{
        wh_ctx_t *ctx = data;
        if (ctx == NULL)
            return;

        if (ctx->headers != NULL) {
                curl_slist_free_all (ctx->headers);
                ctx->headers = NULL;
        }

        if (ctx->curl != NULL) {
                curl_easy_cleanup (ctx->curl);
                ctx->curl = NULL;
        }

        sfree (ctx->send_buffer);
        ctx->send_buffer = NULL;

        sfree (data);

} /* }}} void wh_ctx_destructor */

static wh_ctx_t *wh_get_ctx(void) /* {{{ */
{
        if (wh_cfg == NULL)
                return NULL;

        wh_ctx_t *ctx = pthread_getspecific (wh_ctx_key);
        if (ctx != NULL)
                return ctx;

        ctx = malloc (sizeof(*ctx));
        if (ctx == NULL) {
                ERROR ("wh_get_ctx: failed to allocate wh context");
                goto fail;
        }

        if (wh_ctx_init(ctx)) {
	            ERROR ("wh_get_ctx: failed to init wh context");
	            goto fail;
        }

        pthread_setspecific (wh_ctx_key, ctx);
        return ctx;

fail:
        wh_ctx_destructor(ctx);
        return NULL;
} /* }}} wh_ctx_t *wh_get_ctx */

static void wh_log_http_error (wh_ctx_t *ctx) /* {{{ */
{
        if (!wh_cfg->log_http_error)
                return;

        long http_code = 0;

        curl_easy_getinfo (ctx->curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code != 200)
                INFO ("write_http plugin: HTTP Error code: %lu", http_code);
} /* }}} void wh_log_http_error */

static void wh_fill_buffer (wh_ctx_t *ctx, char *cmd, size_t cmd_len) /* {{{ */
{
        assert(wh_cfg != NULL);
        assert(ctx != NULL);
        assert(ctx->send_buffer_free >= cmd_len);

        memcpy (ctx->send_buffer + ctx->send_buffer_fill, cmd, cmd_len);
        ctx->send_buffer_fill += cmd_len;
        ctx->send_buffer_free -= cmd_len;

        /* TODO: cdtime uses REALTIME clock, which is not really
         * what we want here since it could go backwards. We
         * really should be using CLOCK_MONOTONIC instead.
         */
        if (ctx->send_buffer_init_time == 0)
                ctx->send_buffer_init_time = cdtime ();

} /* }}} void wh_fill_buffer */

static void wh_reset_buffer (wh_ctx_t *ctx)  /* {{{ */
{
        memset (ctx->send_buffer, 0, ctx->send_buffer_size);
        ctx->send_buffer_free = ctx->send_buffer_size;
        ctx->send_buffer_fill = 0;
        ctx->send_buffer_init_time = 0;

        if (wh_cfg->format == WH_FORMAT_JSON) {
                format_json_initialize (ctx->send_buffer,
                                &ctx->send_buffer_fill,
                                &ctx->send_buffer_free);
        }
} /* }}} void wh_reset_buffer */

static int wh_send_buffer (wh_ctx_t *ctx) /* {{{ */
{
        assert(wh_cfg != NULL);
        assert(ctx != NULL);

        int status = 0;

        CURL_SET_OPT(status, ctx, CURLOPT_POSTFIELDS, ctx->send_buffer);
        if (status != CURLE_OK) {
                ERROR ("write_http plugin: curl_set_opt postfields failed with "
                        "status %i", status);
            return status;
        }

        status = curl_easy_perform (ctx->curl);
        wh_log_http_error (ctx);
        if (status != CURLE_OK) {
                ERROR ("write_http plugin: curl_easy_perform failed with "
                        "status %i: %s",
                        status, ctx->curl_errbuf);
        }

        return status;
} /* }}} int wh_send_buffer */

static int wh_ctx_init (wh_ctx_t *ctx) /* {{{ */
{
        assert(wh_cfg != NULL);
        assert(ctx != NULL);

        int res = 0;
        struct curl_slist *tmp = NULL;

        memset(ctx, 0, sizeof(*ctx));

        ctx->curl = curl_easy_init ();
        if (ctx->curl == NULL) {
                ERROR ("wh_get_ctx: curl_easy_init failed.");
                return -1;
        }

        ctx->send_buffer_size = wh_cfg->send_buffer_size;
        ctx->send_buffer = malloc (wh_cfg->send_buffer_size);
        if (ctx->send_buffer == NULL) {
                ERROR ("wh_get_ctx: malloc(%zu) failed.", wh_cfg->send_buffer_size);
                return -1;
        }

        wh_reset_buffer (ctx);

        tmp = curl_slist_append (ctx->headers, "Accept:  */*");
        if (tmp == NULL) {
                ERROR ("wh_get_ctx: curl_slist_append (Accept) failed.");
                return -1;
        }
        ctx->headers = tmp;

        if (wh_cfg->format == WH_FORMAT_JSON)
                tmp = curl_slist_append (ctx->headers, "Content-Type: application/json");
        else
                tmp = curl_slist_append (ctx->headers, "Content-Type: text/plain");

        if (tmp == NULL) {
                ERROR ("wh_get_ctx: curl_slist_append (Content-Type) failed.");
                return -1;
        }
        ctx->headers = tmp;

        tmp = curl_slist_append (ctx->headers, "Expect:");
        if (tmp == NULL) {
                ERROR ("wh_get_ctx: curl_slist_append (Content-Type) failed.");
                return -1;
        }
        ctx->headers = tmp;

        if (wh_cfg->low_speed_limit > 0 && wh_cfg->low_speed_time > 0) {
                CURL_SET_OPT (res, ctx, CURLOPT_LOW_SPEED_LIMIT,
                          (long) (wh_cfg->low_speed_limit * wh_cfg->low_speed_time));
                CURL_SET_OPT (res, ctx, CURLOPT_LOW_SPEED_TIME, (long) wh_cfg->low_speed_time);
        }

#ifdef HAVE_CURLOPT_TIMEOUT_MS
        if (wh_cfg->timeout > 0)
                CURL_SET_OPT (res, ctx, CURLOPT_TIMEOUT_MS, (long) wh_cfg->timeout);
#endif

        CURL_SET_OPT (res, ctx, CURLOPT_NOSIGNAL, 1L);
        CURL_SET_OPT (res, ctx, CURLOPT_USERAGENT, COLLECTD_USERAGENT);
        CURL_SET_OPT (res, ctx, CURLOPT_HTTPHEADER, ctx->headers);
        CURL_SET_OPT (res, ctx, CURLOPT_ERRORBUFFER, ctx->curl_errbuf);
        CURL_SET_OPT (res, ctx, CURLOPT_URL, wh_cfg->location);
        CURL_SET_OPT (res, ctx, CURLOPT_FOLLOWLOCATION, 1L);
        CURL_SET_OPT (res, ctx, CURLOPT_MAXREDIRS, 50L);

        if (wh_cfg->user != NULL)
        {
#ifdef HAVE_CURLOPT_USERNAME
                const char *pass = (wh_cfg->pass == NULL) ? "" : wh_cfg->pass;
                CURL_SET_OPT (res, ctx, CURLOPT_USERNAME, wh_cfg->user);
                CURL_SET_OPT (res, ctx, CURLOPT_PASSWORD, pass);
#else
                CURL_SET_OPT (res, ctx, CURLOPT_USERPWD, wh_cfg->credentials);
#endif
                CURL_SET_OPT (res, ctx, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
        }

        CURL_SET_OPT (res, ctx, CURLOPT_SSL_VERIFYPEER, (long) wh_cfg->verify_peer);
        CURL_SET_OPT (res, ctx, CURLOPT_SSL_VERIFYHOST, wh_cfg->verify_host ? 2L : 0L);
        CURL_SET_OPT (res, ctx, CURLOPT_SSLVERSION, wh_cfg->sslversion);

        if (wh_cfg->cacert != NULL)
                CURL_SET_OPT (res, ctx, CURLOPT_CAINFO, wh_cfg->cacert);
        if (wh_cfg->capath != NULL)
                CURL_SET_OPT (res, ctx, CURLOPT_CAPATH, wh_cfg->capath);

        if (wh_cfg->clientkey != NULL && wh_cfg->clientcert != NULL) {
                CURL_SET_OPT (res, ctx, CURLOPT_SSLKEY, wh_cfg->clientkey);
                CURL_SET_OPT (res, ctx, CURLOPT_SSLCERT, wh_cfg->clientcert);

                if (wh_cfg->clientkeypass != NULL)
                        CURL_SET_OPT (res, ctx, CURLOPT_SSLKEYPASSWD, wh_cfg->clientkeypass);
        }

        return res;
} /* }}} int wh_ctx_init */

static int wh_flush_nolock (cdtime_t timeout, wh_ctx_t *ctx) /* {{{ */
{
        assert(wh_cfg != NULL);
        assert(ctx != NULL);

        int status = 0;

        DEBUG ("write_http plugin: wh_flush_nolock: timeout = %.3f; "
                "send_buffer_fill = %zu;",
                CDTIME_T_TO_DOUBLE (timeout),
                ctx->send_buffer_fill);

        if (ctx->send_buffer_init_time == 0)
                return 0;

        /* timeout <= 0 flush unconditionally */
        if (timeout > 0) {
                cdtime_t now = cdtime ();
                if ((ctx->send_buffer_init_time + timeout) > now)
                        return 0;
        }

        if (wh_cfg->format == WH_FORMAT_JSON) {

                status = format_json_finalize (ctx->send_buffer,
                                &ctx->send_buffer_fill,
                                &ctx->send_buffer_free);
                if (status != 0) {
                        ERROR ("write_http: wh_flush_nolock: "
                                        "format_json_finalize failed.");
                        goto done;
                }
        }

        status = wh_send_buffer (ctx);
done:
        wh_reset_buffer (ctx);
        return status;
} /* }}} int wh_flush_nolock */

static int wh_flush (cdtime_t timeout, /* {{{ */
                const char *identifier __attribute__((unused)),
                user_data_t *unused __attribute__((unused)))
{
        wh_ctx_t *ctx = wh_get_ctx();
        if (ctx != NULL)
                return wh_flush_nolock (timeout, ctx);

        ERROR ("write_http plugin: cannot get ctx");
        return (-EINVAL);

} /* }}} int wh_flush */

static void wh_config_free () /* {{{ */
{
        if (wh_cfg == NULL)
                return;

        sfree (wh_cfg->name);
        sfree (wh_cfg->location);
        sfree (wh_cfg->user);
        sfree (wh_cfg->pass);
        sfree (wh_cfg->credentials);
        sfree (wh_cfg->cacert);
        sfree (wh_cfg->capath);
        sfree (wh_cfg->clientkey);
        sfree (wh_cfg->clientcert);
        sfree (wh_cfg->clientkeypass);
        sfree (wh_cfg);

        wh_cfg = NULL;

} /* }}} void wh_config_free */

static int wh_write_command (const data_set_t *ds, const value_list_t *vl, /* {{{ */
        wh_ctx_t *ctx)
{
        assert(wh_cfg != NULL);
        assert(ctx != NULL);

        char key[10*DATA_MAX_NAME_LEN];
        char values[512];
        char command[1024];
        size_t command_len = 0;
        int status = 0;

        /* WARNING: Excessive/nested data copying below: key, values, command,
         * I/O buffer, etc.
         */

        if (0 != strcmp (ds->type, vl->type)) {
                ERROR ("write_http plugin: DS type does not match "
                                "value list type");
                goto fail;
        }

        /* Copy the identifier to `key' and escape it. */
        status = FORMAT_VL (key, sizeof (key), vl);
        if (status != 0) {
                ERROR ("write_http plugin: error with format_name");
                goto fail;
        }

        escape_string (key, sizeof (key));

        /* Convert the values to an ASCII representation and put that into
         * `values'. */
        status = format_values (values, sizeof (values), ds, vl, wh_cfg->store_rates);
        if (status != 0) {
                ERROR ("write_http plugin: error with "
                                "wh_value_list_to_string");
                goto fail;
        }

        command_len = (size_t) ssnprintf (command, sizeof (command),
                        "PUTVAL %s interval=%.3f %s\r\n",
                        key,
                        CDTIME_T_TO_DOUBLE (vl->interval),
                        values);
        if (command_len >= sizeof (command)) {
                ERROR ("write_http plugin: Command buffer too small: "
                                "Need %zu bytes.", command_len + 1);

                goto fail;
        }

        /* `cmd_len + 1' because `cmd_len' does not include the
         * trailing null byte from ssnprintf */
        ++command_len;
        
        if (command_len > ctx->send_buffer_free) {
                status = wh_flush_nolock (/* timeout = */ 0, ctx);
                if (status != 0)
                        return status;
        }

        wh_fill_buffer (ctx, command, command_len);

        DEBUG ("write_http plugin: <%s> buffer %zu/%zu (%g%%) \"%s\"",
                wh_cfg->location,
                ctx->send_buffer_fill, ctx->send_buffer_size,
                100.0 * ((double) ctx->send_buffer_fill) / ((double) ctx->send_buffer_size),
                command);

        return 0;

fail:
        status = wh_flush_nolock (/* timeout = */ 0, ctx);
        return status;
} /* }}} int wh_write_command */

static int wh_write_json (const data_set_t *ds, const value_list_t *vl, /* {{{ */
    wh_ctx_t *ctx)
{
        int status = format_json_value_list (ctx->send_buffer,
                        &ctx->send_buffer_fill,
                        &ctx->send_buffer_free,
                        ds, vl, wh_cfg->store_rates);

        if (ctx->send_buffer_init_time == 0)
                ctx->send_buffer_init_time = cdtime ();

        if (status == (-ENOMEM)) {
                status = wh_flush_nolock (/* timeout = */ 0, ctx);
                if (status != 0)
                        return status;

                status = format_json_value_list (ctx->send_buffer,
                                &ctx->send_buffer_fill,
                                &ctx->send_buffer_free,
                                ds, vl, wh_cfg->store_rates);
        }

        if (status != 0)
                return status;

        DEBUG ("write_http plugin: <%s> buffer %zu/%zu (%g%%)",
                wh_cfg->location,
                ctx->send_buffer_fill, ctx->send_buffer_size,
                100.0 * ((double) ctx->send_buffer_fill) / ((double) wh_cfg->send_buffer_size));

        return 0;
} /* }}} int wh_write_json */

static int wh_write (const data_set_t *ds, const value_list_t *vl, /* {{{ */
                user_data_t *unused __attribute__((unused)))
{
        wh_ctx_t *ctx = wh_get_ctx();
        if (ctx == NULL) {
                ERROR ("write_http plugin: cannot get ctx");
                return (-EINVAL);
        }

        if (wh_cfg->format == WH_FORMAT_JSON)
                return wh_write_json (ds, vl, ctx);
        return wh_write_command (ds, vl, ctx);
} /* }}} int wh_write */

static int config_set_format (wh_cfg_t *cfg, /* {{{ */
                oconfig_item_t *ci)
{
        char *string;

        if ((ci->values_num != 1)
                || (ci->values[0].type != OCONFIG_TYPE_STRING)) {

                WARNING ("write_http plugin: The `%s' config option "
                                "needs exactly one string argument.", ci->key);
                return (-1);
        }

        string = ci->values[0].value.string;
        if (strcasecmp ("Command", string) == 0)
                cfg->format = WH_FORMAT_COMMAND;
        else if (strcasecmp ("JSON", string) == 0)
                cfg->format = WH_FORMAT_JSON;
        else {
                ERROR ("write_http plugin: Invalid format string: %s",
                                string);
                return (-1);
        }

        return (0);
} /* }}} int config_set_format */

static int wh_config_node (oconfig_item_t *ci) /* {{{ */
{
        int buffer_size = 0;
        char callback_name[DATA_MAX_NAME_LEN];
        int status = 0;
        int i;

        if (wh_cfg != NULL)
                return 0;

        wh_cfg = malloc (sizeof (*wh_cfg));
        if (wh_cfg == NULL) {
                ERROR ("write_http plugin: malloc failed.");
                goto fail;
        }

        memset (wh_cfg, 0, sizeof (*wh_cfg));

        wh_cfg->verify_peer = 1;
        wh_cfg->verify_host = 1;
        wh_cfg->format = WH_FORMAT_COMMAND;
        wh_cfg->sslversion = CURL_SSLVERSION_DEFAULT;
        wh_cfg->low_speed_limit = 0;
        wh_cfg->timeout = 0;
        wh_cfg->log_http_error = 0;

        cf_util_get_string (ci, &wh_cfg->name);

        /* FIXME: Remove this legacy mode in version 6. */
        if (strcasecmp ("URL", ci->key) == 0)
                cf_util_get_string (ci, &wh_cfg->location);

        for (i = 0; i < ci->children_num; i++) {

                oconfig_item_t *child = ci->children + i;

                if (strcasecmp ("URL", child->key) == 0)
                        status = cf_util_get_string (child, &wh_cfg->location);
                else if (strcasecmp ("User", child->key) == 0)
                        status = cf_util_get_string (child, &wh_cfg->user);
                else if (strcasecmp ("Password", child->key) == 0)
                        status = cf_util_get_string (child, &wh_cfg->pass);
                else if (strcasecmp ("VerifyPeer", child->key) == 0)
                        status = cf_util_get_boolean (child, &wh_cfg->verify_peer);
                else if (strcasecmp ("VerifyHost", child->key) == 0)
                        status = cf_util_get_boolean (child, &wh_cfg->verify_host);
                else if (strcasecmp ("CACert", child->key) == 0)
                        status = cf_util_get_string (child, &wh_cfg->cacert);
                else if (strcasecmp ("CAPath", child->key) == 0)
                        status = cf_util_get_string (child, &wh_cfg->capath);
                else if (strcasecmp ("ClientKey", child->key) == 0)
                        status = cf_util_get_string (child, &wh_cfg->clientkey);
                else if (strcasecmp ("ClientCert", child->key) == 0)
                        status = cf_util_get_string (child, &wh_cfg->clientcert);
                else if (strcasecmp ("ClientKeyPass", child->key) == 0)
                        status = cf_util_get_string (child, &wh_cfg->clientkeypass);
                else if (strcasecmp ("SSLVersion", child->key) == 0) {

                        char *value = NULL;

                        status = cf_util_get_string (child, &value);
                        if (status != 0)
                                break;

                        if (value == NULL || strcasecmp ("default", value) == 0)
                                wh_cfg->sslversion = CURL_SSLVERSION_DEFAULT;
                        else if (strcasecmp ("SSLv2", value) == 0)
                                wh_cfg->sslversion = CURL_SSLVERSION_SSLv2;
                        else if (strcasecmp ("SSLv3", value) == 0)
                                wh_cfg->sslversion = CURL_SSLVERSION_SSLv3;
                        else if (strcasecmp ("TLSv1", value) == 0)
                                wh_cfg->sslversion = CURL_SSLVERSION_TLSv1;
#if (LIBCURL_VERSION_MAJOR > 7) || (LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 34)
                        else if (strcasecmp ("TLSv1_0", value) == 0)
                                wh_cfg->sslversion = CURL_SSLVERSION_TLSv1_0;
                        else if (strcasecmp ("TLSv1_1", value) == 0)
                                wh_cfg->sslversion = CURL_SSLVERSION_TLSv1_1;
                        else if (strcasecmp ("TLSv1_2", value) == 0)
                                wh_cfg->sslversion = CURL_SSLVERSION_TLSv1_2;
#endif
                        else {
                                ERROR ("write_http plugin: Invalid SSLVersion "
                                                "option: %s.", value);
                                status = EINVAL;
                        }

                        sfree(value);
                }
                else if (strcasecmp ("Format", child->key) == 0)
                        status = config_set_format (wh_cfg, child);
                else if (strcasecmp ("StoreRates", child->key) == 0)
                        status = cf_util_get_boolean (child, &wh_cfg->store_rates);
                else if (strcasecmp ("BufferSize", child->key) == 0)
                        status = cf_util_get_int (child, &buffer_size);
                else if (strcasecmp ("LowSpeedLimit", child->key) == 0)
                        status = cf_util_get_int (child, &wh_cfg->low_speed_limit);
                else if (strcasecmp ("Timeout", child->key) == 0)
                        status = cf_util_get_int (child, &wh_cfg->timeout);
                else if (strcasecmp ("LogHttpError", child->key) == 0)
                        status = cf_util_get_boolean (child, &wh_cfg->log_http_error);
                else {
                        ERROR ("write_http plugin: Invalid configuration "
                                        "option: %s.", child->key);
                        status = EINVAL;
                }

                if (status != 0)
                        break;
        }

        if (status != 0)
                goto fail;

        if (wh_cfg->location == NULL) {
                ERROR ("write_http plugin: no URL defined for instance '%s'",
                        wh_cfg->name);
                goto fail;
        }

#ifndef HAVE_CURLOPT_USERNAME
        if (wh_cfg->user != NULL) {

                size_t credentials_size = strlen (wh_cfg->user) + 2;
                if (wh_cfg->pass != NULL)
                        credentials_size += strlen (wh_cfg->pass);

                wh_cfg->credentials = (char *) malloc (credentials_size);
                if (wh_cfg->credentials == NULL) {
                        ERROR ("write_http plugin: malloc(%zu) credentials failed.",
                                credentials_size);
                        goto fail;
                }

                ssnprintf (wh_cfg->credentials, credentials_size, "%s:%s",
                        wh_cfg->user, (wh_cfg->pass == NULL) ? "" : wh_cfg->pass);
        }
#endif

        if (wh_cfg->low_speed_limit > 0)
                wh_cfg->low_speed_time = CDTIME_T_TO_TIME_T (plugin_get_interval());

        /* Determine send_buffer_size. */
        wh_cfg->send_buffer_size = WH_HTTP_DEFAULT_BUFFER_SIZE;
        if (buffer_size >= WH_HTTP_MIN_BUFFER_SIZE)
                wh_cfg->send_buffer_size = (size_t) buffer_size;
        else if (buffer_size != 0)
                ERROR ("write_http plugin: Ignoring invalid BufferSize setting (%d).",
                        buffer_size);

        ssnprintf (callback_name, sizeof (callback_name), "write_http/%s",
                wh_cfg->name);
        DEBUG ("write_http: Registering write callback '%s' with URL '%s'",
                callback_name, wh_cfg->location);

        plugin_register_flush (callback_name, wh_flush, NULL);
        plugin_register_write (callback_name, wh_write, NULL);
        return (0);

fail:
        wh_config_free();
        return -1;
} /* }}} int wh_config_node */

static int wh_config (oconfig_item_t *ci) /* {{{ */
{
        int ret = 0;
        int i;

        for (i = 0; !ret && i < ci->children_num; ++i) {

                oconfig_item_t *child = ci->children + i;

                if (strcasecmp ("Node", child->key) == 0)
                        ret = wh_config_node (child);
                /* FIXME: Remove this legacy mode in version 6. */
                else if (strcasecmp ("URL", child->key) == 0) {
                        WARNING ("write_http plugin: Legacy <URL> block found. "
                                "Please use <Node> instead.");
                        ret = wh_config_node (child);
                }
                else {
                        ERROR ("write_http plugin: Invalid configuration "
                                        "option: %s.", child->key);
                }
        }

        return ret;
} /* }}} int wh_config */

static int wh_init (void) /* {{{ */
{
        int ret = 0;

        /* Call this while collectd is still single-threaded to avoid
         * initialization issues in libgcrypt. */
        curl_global_init (CURL_GLOBAL_SSL);
    
        ret = pthread_key_create (&wh_ctx_key, wh_ctx_destructor);
        if (ret) {
                ERROR ("write_http plugin: create ctx key failed %d", ret);
        }

        return ret;
} /* }}} int wh_init */

void module_register (void) /* {{{ */
{
        plugin_register_complex_config ("write_http", wh_config);
        plugin_register_init ("write_http", wh_init);
} /* }}} void module_register */

/* vim: set fdm=marker sw=8 ts=8 tw=78 et : */
