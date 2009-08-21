/**
 * collectd - src/http.c
 * Copyright (C) 2009       Paul Sadauskas
 * Copyright (C) 2007-2009  Florian octo Forster
 * Copyright (C) 2009       Doug MacEachern
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

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include <curl/curl.h>

/*
 * Private variables
 */
static const char *config_keys[] =
{
        "URL", "User", "Password"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char *location   = NULL;

char *user;
char *pass;
char *credentials;

CURL *curl;
char curl_errbuf[CURL_ERROR_SIZE];

#define SEND_BUFFER_SIZE 4096
static char   send_buffer[SEND_BUFFER_SIZE];
static size_t send_buffer_free;
static size_t send_buffer_fill;
static time_t send_buffer_init_time;

static pthread_mutex_t  send_lock = PTHREAD_MUTEX_INITIALIZER;

static void http_init_buffer (void)  /* {{{ */
{
        memset (send_buffer, 0, sizeof (send_buffer));
        send_buffer_free = sizeof (send_buffer);
        send_buffer_fill = 0;
        send_buffer_init_time = time (NULL);
} /* }}} http_init_buffer */

static int http_init(void) /* {{{ */
{

        curl = curl_easy_init ();

        if (curl == NULL)
        {
                ERROR ("curl plugin: curl_easy_init failed.");
                return (-1);
        }

        struct curl_slist *headers=NULL;

        curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);

        headers = curl_slist_append(headers, "Accept:  */*");
        headers = curl_slist_append(headers, "Content-Type: text/plain");
        curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, curl_errbuf);
        curl_easy_setopt (curl, CURLOPT_URL, location);

        if (user != NULL)
        {
                size_t credentials_size;

                credentials_size = strlen (user) + 2;
                if (pass != NULL)
                        credentials_size += strlen (pass);

                credentials = (char *) malloc (credentials_size);
                if (credentials == NULL)
                {
                        ERROR ("curl plugin: malloc failed.");
                        return (-1);
                }

                ssnprintf (credentials, credentials_size, "%s:%s",
                                user, (pass == NULL) ? "" : pass);
                curl_easy_setopt (curl, CURLOPT_USERPWD, credentials);
                curl_easy_setopt (curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
        }

        http_init_buffer ();

        return (0);
} /* }}} */

static int http_value_list_to_string (char *buffer, /* {{{ */
                size_t buffer_size,
                const data_set_t *ds, const value_list_t *vl)
{
        size_t offset = 0;
        int status;
        int i;

        assert (0 == strcmp (ds->type, vl->type));

        memset (buffer, 0, buffer_size);

#define BUFFER_ADD(...) do { \
        status = ssnprintf (buffer + offset, buffer_size - offset, \
                        __VA_ARGS__); \
        if (status < 1) \
                return (-1); \
        else if (((size_t) status) >= (buffer_size - offset)) \
                return (-1); \
        else \
                offset += ((size_t) status); \
} while (0)

        BUFFER_ADD ("%lu", (unsigned long) vl->time);

        for (i = 0; i < ds->ds_num; i++)
{
        if (ds->ds[i].type == DS_TYPE_GAUGE)
                BUFFER_ADD (":%f", vl->values[i].gauge);
        else if (ds->ds[i].type == DS_TYPE_COUNTER)
                BUFFER_ADD (":%llu", vl->values[i].counter);
        else if (ds->ds[i].type == DS_TYPE_DERIVE)
                BUFFER_ADD (":%"PRIi64, vl->values[i].derive);
        else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
                BUFFER_ADD (":%"PRIu64, vl->values[i].absolute);
        else
        {
                ERROR ("http plugin: Unknown data source type: %i",
                                ds->ds[i].type);
                return (-1);
        }
} /* for ds->ds_num */

#undef BUFFER_ADD

return (0);
} /* }}} int http_value_list_to_string */

static int http_config (const char *key, const char *value) /* {{{ */
{
        if (strcasecmp ("URL", key) == 0)
        {
                if (location != NULL)
                        free (location);
                location = strdup (value);
                if (location != NULL)
                {
                        int len = strlen (location);
                        while ((len > 0) && (location[len - 1] == '/'))
                        {
                                len--;
                                location[len] = '\0';
                        }
                        if (len <= 0)
                        {
                                free (location);
                                location = NULL;
                        }
                }
        }
        else if (strcasecmp ("User", key) == 0)
        {
                if (user != NULL)
                        free (user);
                user = strdup (value);
                if (user != NULL)
                {
                        int len = strlen (user);
                        while ((len > 0) && (user[len - 1] == '/'))
                        {
                                len--;
                                user[len] = '\0';
                        }
                        if (len <= 0)
                        {
                                free (user);
                                user = NULL;
                        }
                }
        }
        else if (strcasecmp ("Password", key) == 0)
        {
                if (pass != NULL)
                        free (pass);
                pass = strdup (value);
                if (pass != NULL)
                {
                        int len = strlen (pass);
                        while ((len > 0) && (pass[len - 1] == '/'))
                        {
                                len--;
                                pass[len] = '\0';
                        }
                        if (len <= 0)
                        {
                                free (pass);
                                pass = NULL;
                        }
                }
        }
        else
        {
                return (-1);
        }
        return (0);
} /* }}} int http_config */

static int http_send_buffer (char *buffer) /* {{{ */
{
        int status = 0;

        curl_easy_setopt (curl, CURLOPT_POSTFIELDS, buffer);
        status = curl_easy_perform (curl);
        if (status != 0)
        {
                ERROR ("http plugin: curl_easy_perform failed with staus %i: %s",
                                status, curl_errbuf);
        }
        return (status);
} /* }}} http_send_buffer */

static int http_flush_nolock (int timeout) /* {{{ */
{
        int status;

        DEBUG ("http plugin: http_flush_nolock: timeout = %i; "
                        "send_buffer =\n  %s", timeout, send_buffer);

        if (timeout > 0)
        {
                time_t now;

                now = time (NULL);
                if ((send_buffer_init_time + timeout) > now)
                        return (0);
        }

        if (send_buffer_fill <= 0)
        {
                send_buffer_init_time = time (NULL);
                return (0);
        }

        status = http_send_buffer (send_buffer);
        http_init_buffer ();

        return (status);
} /* }}} http_flush_nolock */

static int http_flush (int timeout, /* {{{ */
                const char *identifier __attribute__((unused)),
                user_data_t *user_data __attribute__((unused)))
{
        int status;

        pthread_mutex_lock (&send_lock);
        status = http_flush_nolock (timeout);
        pthread_mutex_unlock (&send_lock);

        return (status);
} /* }}} int http_flush */

static int http_write_command (const data_set_t *ds, const value_list_t *vl) /* {{{ */
{
        char key[10*DATA_MAX_NAME_LEN];
        char values[512];
        char command[1024];
        size_t command_len;

        int status;

        if (0 != strcmp (ds->type, vl->type)) {
                ERROR ("http plugin: DS type does not match value list type");
                return -1;
        }

        /* Copy the identifier to `key' and escape it. */
        status = FORMAT_VL (key, sizeof (key), vl);
        if (status != 0) {
                ERROR ("http plugin: error with format_name");
                return (status);
        }
        escape_string (key, sizeof (key));

        /* Convert the values to an ASCII representation and put that into
         * `values'. */
        status = http_value_list_to_string (values, sizeof (values), ds, vl);
        if (status != 0) {
                ERROR ("http plugin: error with http_value_list_to_string");
                return (status);
        }

        command_len = (size_t) ssnprintf (command, sizeof (command),
                        "PUTVAL %s interval=%i %s\n",
                        key, vl->interval, values);
        if (command_len >= sizeof (command)) {
                ERROR ("http plugin: Command buffer too small: "
                                "Need %zu bytes.", command_len + 1);
                return (-1);
        }

        pthread_mutex_lock (&send_lock);

        /* Check if we have enough space for this command. */
        if (command_len >= send_buffer_free)
        {
                status = http_flush_nolock (/* timeout = */ -1);
                if (status != 0)
                {
                        pthread_mutex_unlock (&send_lock);
                        return status;
                }
        }
        assert (command_len < send_buffer_free);

        /* `command_len + 1' because `command_len' does not include the
         * trailing null byte. Neither does `send_buffer_fill'. */
        memcpy (send_buffer + send_buffer_fill, command, command_len + 1);
        send_buffer_fill += command_len;
        send_buffer_free -= command_len;

        pthread_mutex_unlock (&send_lock);

        return (0);
} /* }}} int http_write_command */

static int http_write (const data_set_t *ds, const value_list_t *vl, /* {{{ */
                user_data_t __attribute__((unused)) *user_data)
{
        int status;

        status = http_write_command (ds, vl);

        return (status);
} /* }}} int http_write */

static int http_shutdown (void) /* {{{ */
{
        http_flush_nolock (/* timeout = */ -1);
        curl_easy_cleanup(curl);
        return (0);
} /* }}} int http_shutdown */

void module_register (void) /* {{{ */
{
        plugin_register_init("http", http_init);
        plugin_register_config ("http", http_config,
                        config_keys, config_keys_num);
        plugin_register_write ("http", http_write, /* user_data = */ NULL);
        plugin_register_flush ("http", http_flush, /* user_data = */ NULL);
        plugin_register_shutdown("http", http_shutdown);
} /* }}} void module_register */

/* vim: set fdm=marker sw=8 ts=8 tw=78 et : */
