/**
 * collectd - src/write_http.c
 * Copyright (C) 2009       Paul Sadauskas
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2007-2009  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 *   Doug MacEachern <dougm@hyperic.com>
 *   Paul Sadauskas <psadauskas@gmail.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "utils_cache.h"
#include "utils_parse_option.h"
#include "utils_format_json.h"

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include <curl/curl.h>

/*
 * Private variables
 */
struct wh_callback_s
{
        char *location;

        char *user;
        char *pass;
        char *credentials;
        int   verify_peer;
        int   verify_host;
        char *cacert;
        int   store_rates;

#define WH_FORMAT_COMMAND 0
#define WH_FORMAT_JSON    1
        int format;

        CURL *curl;
        char curl_errbuf[CURL_ERROR_SIZE];

        char   send_buffer[4096];
        size_t send_buffer_free;
        size_t send_buffer_fill;
        cdtime_t send_buffer_init_time;

        pthread_mutex_t send_lock;
};
typedef struct wh_callback_s wh_callback_t;

static void wh_reset_buffer (wh_callback_t *cb)  /* {{{ */
{
        memset (cb->send_buffer, 0, sizeof (cb->send_buffer));
        cb->send_buffer_free = sizeof (cb->send_buffer);
        cb->send_buffer_fill = 0;
        cb->send_buffer_init_time = cdtime ();

        if (cb->format == WH_FORMAT_JSON)
        {
                format_json_initialize (cb->send_buffer,
                                &cb->send_buffer_fill,
                                &cb->send_buffer_free);
        }
} /* }}} wh_reset_buffer */

static int wh_send_buffer (wh_callback_t *cb) /* {{{ */
{
        int status = 0;

        curl_easy_setopt (cb->curl, CURLOPT_POSTFIELDS, cb->send_buffer);
        status = curl_easy_perform (cb->curl);
        if (status != 0)
        {
                ERROR ("write_http plugin: curl_easy_perform failed with "
                                "status %i: %s",
                                status, cb->curl_errbuf);
        }
        return (status);
} /* }}} wh_send_buffer */

static int wh_callback_init (wh_callback_t *cb) /* {{{ */
{
        struct curl_slist *headers;

        if (cb->curl != NULL)
                return (0);

        cb->curl = curl_easy_init ();
        if (cb->curl == NULL)
        {
                ERROR ("curl plugin: curl_easy_init failed.");
                return (-1);
        }

        curl_easy_setopt (cb->curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt (cb->curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);

        headers = NULL;
        headers = curl_slist_append (headers, "Accept:  */*");
        if (cb->format == WH_FORMAT_JSON)
                headers = curl_slist_append (headers, "Content-Type: application/json");
        else
                headers = curl_slist_append (headers, "Content-Type: text/plain");
        headers = curl_slist_append (headers, "Expect:");
        curl_easy_setopt (cb->curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt (cb->curl, CURLOPT_ERRORBUFFER, cb->curl_errbuf);
        curl_easy_setopt (cb->curl, CURLOPT_URL, cb->location);

        if (cb->user != NULL)
        {
                size_t credentials_size;

                credentials_size = strlen (cb->user) + 2;
                if (cb->pass != NULL)
                        credentials_size += strlen (cb->pass);

                cb->credentials = (char *) malloc (credentials_size);
                if (cb->credentials == NULL)
                {
                        ERROR ("curl plugin: malloc failed.");
                        return (-1);
                }

                ssnprintf (cb->credentials, credentials_size, "%s:%s",
                                cb->user, (cb->pass == NULL) ? "" : cb->pass);
                curl_easy_setopt (cb->curl, CURLOPT_USERPWD, cb->credentials);
                curl_easy_setopt (cb->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
        }

        curl_easy_setopt (cb->curl, CURLOPT_SSL_VERIFYPEER, cb->verify_peer);
        curl_easy_setopt (cb->curl, CURLOPT_SSL_VERIFYHOST,
                        cb->verify_host ? 2 : 0);
        if (cb->cacert != NULL)
                curl_easy_setopt (cb->curl, CURLOPT_CAINFO, cb->cacert);

        wh_reset_buffer (cb);

        return (0);
} /* }}} int wh_callback_init */

static int wh_flush_nolock (cdtime_t timeout, wh_callback_t *cb) /* {{{ */
{
        int status;

        DEBUG ("write_http plugin: wh_flush_nolock: timeout = %.3f; "
                        "send_buffer_fill = %zu;",
                        CDTIME_T_TO_DOUBLE (timeout),
                        cb->send_buffer_fill);

        /* timeout == 0  => flush unconditionally */
        if (timeout > 0)
        {
                cdtime_t now;

                now = cdtime ();
                if ((cb->send_buffer_init_time + timeout) > now)
                        return (0);
        }

        if (cb->format == WH_FORMAT_COMMAND)
        {
                if (cb->send_buffer_fill <= 0)
                {
                        cb->send_buffer_init_time = cdtime ();
                        return (0);
                }

                status = wh_send_buffer (cb);
                wh_reset_buffer (cb);
        }
        else if (cb->format == WH_FORMAT_JSON)
        {
                if (cb->send_buffer_fill <= 2)
                {
                        cb->send_buffer_init_time = cdtime ();
                        return (0);
                }

                status = format_json_finalize (cb->send_buffer,
                                &cb->send_buffer_fill,
                                &cb->send_buffer_free);
                if (status != 0)
                {
                        ERROR ("write_http: wh_flush_nolock: "
                                        "format_json_finalize failed.");
                        wh_reset_buffer (cb);
                        return (status);
                }

                status = wh_send_buffer (cb);
                wh_reset_buffer (cb);
        }
        else
        {
                ERROR ("write_http: wh_flush_nolock: "
                                "Unknown format: %i",
                                cb->format);
                return (-1);
        }

        return (status);
} /* }}} wh_flush_nolock */

static int wh_flush (cdtime_t timeout, /* {{{ */
                const char *identifier __attribute__((unused)),
                user_data_t *user_data)
{
        wh_callback_t *cb;
        int status;

        if (user_data == NULL)
                return (-EINVAL);

        cb = user_data->data;

        pthread_mutex_lock (&cb->send_lock);

        if (cb->curl == NULL)
        {
                status = wh_callback_init (cb);
                if (status != 0)
                {
                        ERROR ("write_http plugin: wh_callback_init failed.");
                        pthread_mutex_unlock (&cb->send_lock);
                        return (-1);
                }
        }

        status = wh_flush_nolock (timeout, cb);
        pthread_mutex_unlock (&cb->send_lock);

        return (status);
} /* }}} int wh_flush */

static void wh_callback_free (void *data) /* {{{ */
{
        wh_callback_t *cb;

        if (data == NULL)
                return;

        cb = data;

        wh_flush_nolock (/* timeout = */ 0, cb);

        curl_easy_cleanup (cb->curl);
        sfree (cb->location);
        sfree (cb->user);
        sfree (cb->pass);
        sfree (cb->credentials);
        sfree (cb->cacert);

        sfree (cb);
} /* }}} void wh_callback_free */

static int wh_write_command (const data_set_t *ds, const value_list_t *vl, /* {{{ */
                wh_callback_t *cb)
{
        char key[10*DATA_MAX_NAME_LEN];
        char values[512];
        char command[1024];
        size_t command_len;

        int status;

        if (0 != strcmp (ds->type, vl->type)) {
                ERROR ("write_http plugin: DS type does not match "
                                "value list type");
                return -1;
        }

        /* Copy the identifier to `key' and escape it. */
        status = FORMAT_VL (key, sizeof (key), vl);
        if (status != 0) {
                ERROR ("write_http plugin: error with format_name");
                return (status);
        }
        escape_string (key, sizeof (key));

        /* Convert the values to an ASCII representation and put that into
         * `values'. */
        status = format_values (values, sizeof (values), ds, vl, cb->store_rates);
        if (status != 0) {
                ERROR ("write_http plugin: error with "
                                "wh_value_list_to_string");
                return (status);
        }

        command_len = (size_t) ssnprintf (command, sizeof (command),
                        "PUTVAL %s interval=%.3f %s\r\n",
                        key,
                        CDTIME_T_TO_DOUBLE (vl->interval),
                        values);
        if (command_len >= sizeof (command)) {
                ERROR ("write_http plugin: Command buffer too small: "
                                "Need %zu bytes.", command_len + 1);
                return (-1);
        }

        pthread_mutex_lock (&cb->send_lock);

        if (cb->curl == NULL)
        {
                status = wh_callback_init (cb);
                if (status != 0)
                {
                        ERROR ("write_http plugin: wh_callback_init failed.");
                        pthread_mutex_unlock (&cb->send_lock);
                        return (-1);
                }
        }

        if (command_len >= cb->send_buffer_free)
        {
                status = wh_flush_nolock (/* timeout = */ 0, cb);
                if (status != 0)
                {
                        pthread_mutex_unlock (&cb->send_lock);
                        return (status);
                }
        }
        assert (command_len < cb->send_buffer_free);

        /* `command_len + 1' because `command_len' does not include the
         * trailing null byte. Neither does `send_buffer_fill'. */
        memcpy (cb->send_buffer + cb->send_buffer_fill,
                        command, command_len + 1);
        cb->send_buffer_fill += command_len;
        cb->send_buffer_free -= command_len;

        DEBUG ("write_http plugin: <%s> buffer %zu/%zu (%g%%) \"%s\"",
                        cb->location,
                        cb->send_buffer_fill, sizeof (cb->send_buffer),
                        100.0 * ((double) cb->send_buffer_fill) / ((double) sizeof (cb->send_buffer)),
                        command);

        /* Check if we have enough space for this command. */
        pthread_mutex_unlock (&cb->send_lock);

        return (0);
} /* }}} int wh_write_command */

static int wh_write_json (const data_set_t *ds, const value_list_t *vl, /* {{{ */
                wh_callback_t *cb)
{
        int status;

        pthread_mutex_lock (&cb->send_lock);

        if (cb->curl == NULL)
        {
                status = wh_callback_init (cb);
                if (status != 0)
                {
                        ERROR ("write_http plugin: wh_callback_init failed.");
                        pthread_mutex_unlock (&cb->send_lock);
                        return (-1);
                }
        }

        status = format_json_value_list (cb->send_buffer,
                        &cb->send_buffer_fill,
                        &cb->send_buffer_free,
                        ds, vl, cb->store_rates);
        if (status == (-ENOMEM))
        {
                status = wh_flush_nolock (/* timeout = */ 0, cb);
                if (status != 0)
                {
                        wh_reset_buffer (cb);
                        pthread_mutex_unlock (&cb->send_lock);
                        return (status);
                }

                status = format_json_value_list (cb->send_buffer,
                                &cb->send_buffer_fill,
                                &cb->send_buffer_free,
                                ds, vl, cb->store_rates);
        }
        if (status != 0)
        {
                pthread_mutex_unlock (&cb->send_lock);
                return (status);
        }

        DEBUG ("write_http plugin: <%s> buffer %zu/%zu (%g%%)",
                        cb->location,
                        cb->send_buffer_fill, sizeof (cb->send_buffer),
                        100.0 * ((double) cb->send_buffer_fill) / ((double) sizeof (cb->send_buffer)));

        /* Check if we have enough space for this command. */
        pthread_mutex_unlock (&cb->send_lock);

        return (0);
} /* }}} int wh_write_json */

static int wh_write (const data_set_t *ds, const value_list_t *vl, /* {{{ */
                user_data_t *user_data)
{
        wh_callback_t *cb;
        int status;

        if (user_data == NULL)
                return (-EINVAL);

        cb = user_data->data;

        if (cb->format == WH_FORMAT_JSON)
                status = wh_write_json (ds, vl, cb);
        else
                status = wh_write_command (ds, vl, cb);

        return (status);
} /* }}} int wh_write */

static int config_set_string (char **ret_string, /* {{{ */
                oconfig_item_t *ci)
{
        char *string;

        if ((ci->values_num != 1)
                        || (ci->values[0].type != OCONFIG_TYPE_STRING))
        {
                WARNING ("write_http plugin: The `%s' config option "
                                "needs exactly one string argument.", ci->key);
                return (-1);
        }

        string = strdup (ci->values[0].value.string);
        if (string == NULL)
        {
                ERROR ("write_http plugin: strdup failed.");
                return (-1);
        }

        if (*ret_string != NULL)
                free (*ret_string);
        *ret_string = string;

        return (0);
} /* }}} int config_set_string */

static int config_set_boolean (int *dest, oconfig_item_t *ci) /* {{{ */
{
        if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
        {
                WARNING ("write_http plugin: The `%s' config option "
                                "needs exactly one boolean argument.", ci->key);
                return (-1);
        }

        *dest = ci->values[0].value.boolean ? 1 : 0;

        return (0);
} /* }}} int config_set_boolean */

static int config_set_format (wh_callback_t *cb, /* {{{ */
                oconfig_item_t *ci)
{
        char *string;

        if ((ci->values_num != 1)
                        || (ci->values[0].type != OCONFIG_TYPE_STRING))
        {
                WARNING ("write_http plugin: The `%s' config option "
                                "needs exactly one string argument.", ci->key);
                return (-1);
        }

        string = ci->values[0].value.string;
        if (strcasecmp ("Command", string) == 0)
                cb->format = WH_FORMAT_COMMAND;
        else if (strcasecmp ("JSON", string) == 0)
                cb->format = WH_FORMAT_JSON;
        else
        {
                ERROR ("write_http plugin: Invalid format string: %s",
                                string);
                return (-1);
        }

        return (0);
} /* }}} int config_set_string */

static int wh_config_url (oconfig_item_t *ci) /* {{{ */
{
        wh_callback_t *cb;
        user_data_t user_data;
        int i;

        cb = malloc (sizeof (*cb));
        if (cb == NULL)
        {
                ERROR ("write_http plugin: malloc failed.");
                return (-1);
        }
        memset (cb, 0, sizeof (*cb));
        cb->location = NULL;
        cb->user = NULL;
        cb->pass = NULL;
        cb->credentials = NULL;
        cb->verify_peer = 1;
        cb->verify_host = 1;
        cb->cacert = NULL;
        cb->format = WH_FORMAT_COMMAND;
        cb->curl = NULL;

        pthread_mutex_init (&cb->send_lock, /* attr = */ NULL);

        config_set_string (&cb->location, ci);
        if (cb->location == NULL)
                return (-1);

        for (i = 0; i < ci->children_num; i++)
        {
                oconfig_item_t *child = ci->children + i;

                if (strcasecmp ("User", child->key) == 0)
                        config_set_string (&cb->user, child);
                else if (strcasecmp ("Password", child->key) == 0)
                        config_set_string (&cb->pass, child);
                else if (strcasecmp ("VerifyPeer", child->key) == 0)
                        config_set_boolean (&cb->verify_peer, child);
                else if (strcasecmp ("VerifyHost", child->key) == 0)
                        config_set_boolean (&cb->verify_host, child);
                else if (strcasecmp ("CACert", child->key) == 0)
                        config_set_string (&cb->cacert, child);
                else if (strcasecmp ("Format", child->key) == 0)
                        config_set_format (cb, child);
                else if (strcasecmp ("StoreRates", child->key) == 0)
                        config_set_boolean (&cb->store_rates, child);
                else
                {
                        ERROR ("write_http plugin: Invalid configuration "
                                        "option: %s.", child->key);
                }
        }

        DEBUG ("write_http: Registering write callback with URL %s",
                        cb->location);

        memset (&user_data, 0, sizeof (user_data));
        user_data.data = cb;
        user_data.free_func = NULL;
        plugin_register_flush ("write_http", wh_flush, &user_data);

        user_data.free_func = wh_callback_free;
        plugin_register_write ("write_http", wh_write, &user_data);

        return (0);
} /* }}} int wh_config_url */

static int wh_config (oconfig_item_t *ci) /* {{{ */
{
        int i;

        for (i = 0; i < ci->children_num; i++)
        {
                oconfig_item_t *child = ci->children + i;

                if (strcasecmp ("URL", child->key) == 0)
                        wh_config_url (child);
                else
                {
                        ERROR ("write_http plugin: Invalid configuration "
                                        "option: %s.", child->key);
                }
        }

        return (0);
} /* }}} int wh_config */

void module_register (void) /* {{{ */
{
        plugin_register_complex_config ("write_http", wh_config);
} /* }}} void module_register */

/* vim: set fdm=marker sw=8 ts=8 tw=78 et : */
