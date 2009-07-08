/**
 * collectd - src/http.c
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
        "Location", "User", "Password"
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
static char  *send_buffer_ptr;
static int    send_buffer_fill;

static pthread_mutex_t  send_lock = PTHREAD_MUTEX_INITIALIZER;

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

        return (0);
} /* }}} */

static int http_value_list_to_string (char *buffer, int buffer_len, /* {{{ */
                const data_set_t *ds, const value_list_t *vl)
{
        int offset = 0;
        int status;
        int i;
        gauge_t *rates = NULL;

        assert (0 == strcmp (ds->type, vl->type));

        memset (buffer, '\0', buffer_len);

        for (i = 0; i < ds->ds_num; i++)
        {
                if ((ds->ds[i].type != DS_TYPE_COUNTER)
                                && (ds->ds[i].type != DS_TYPE_GAUGE)
                                && (ds->ds[i].type != DS_TYPE_DERIVE)
                                && (ds->ds[i].type != DS_TYPE_ABSOLUTE))
                        return (-1);

                if (ds->ds[i].type == DS_TYPE_GAUGE) 
                {
                        status = ssnprintf (buffer + offset, buffer_len - offset,
                                        ":%lf", vl->values[i].gauge);
                } 
                else if (ds->ds[i].type == DS_TYPE_COUNTER)
                {
                        if (rates == NULL)
                                rates = uc_get_rate (ds, vl);
                        if (rates == NULL)
                        {
                                WARNING ("csv plugin: "
                                                "uc_get_rate failed.");
                                return (-1);
                        }
                        status = ssnprintf (buffer + offset,
                                        buffer_len - offset,
                                        ":%lf", rates[i]);
                }
                else if (ds->ds[i].type == DS_TYPE_DERIVE)
                {
                        status = ssnprintf (buffer + offset,
                                        buffer_len - offset,
                                        ":%"PRIi64,
                                        vl->values[i].derive);
                }
                else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
                {
                        status = ssnprintf (buffer + offset,
                                        buffer_len - offset,
                                        ":%"PRIu64,
                                        vl->values[i].absolute);
                }

                if ((status < 1) || (status >= (buffer_len - offset)))
                {
                        sfree (rates);
                        return (-1);
                }

                offset += status;
        } /* for ds->ds_num */

        sfree (rates);
        return (0);
} /* }}} int http_value_list_to_string */

static int http_config (const char *key, const char *value) /* {{{ */
{
        if (strcasecmp ("Location", key) == 0)
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

static void http_init_buffer (void)  /* {{{ */
{
        memset (send_buffer, 0, sizeof (send_buffer));
        send_buffer_ptr = send_buffer;
        send_buffer_fill = 0;
} /* }}} http_init_buffer */

static int http_send_buffer (char *buffer) /* {{{ */
{
        int status = 0;
        curl_easy_setopt (curl, CURLOPT_POSTFIELDS, buffer);
        //status = curl_easy_perform (curl);
        if (status != 0)
        {
                ERROR ("http plugin: curl_easy_perform failed with staus %i: %s",
                                status, curl_errbuf);
        }
        return (status);
} /* }}} http_send_buffer */

static int http_flush_buffer (void) /* {{{ */
{
        int status = 0;
        DEBUG ("http plugin: flushing buffer:\n%s", send_buffer);

        status = http_send_buffer (send_buffer);
        http_init_buffer ();

        return (status);
} /* }}} http_flush_buffer */

static int http_write (const data_set_t *ds, const value_list_t *vl, /* {{{ */
                user_data_t __attribute__((unused)) *user_data)
{

        char key[1024];
        char values[512];

        int status;

        if (0 != strcmp (ds->type, vl->type)) {
                ERROR ("http plugin: DS type does not match value list type");
                return -1;
        }

        status = format_name( key, sizeof(key), vl->host, vl->plugin,
                        vl->plugin_instance, vl->type, vl->type_instance );

        if (status != 0) {
                ERROR ("http plugin: error with format_name");
                return (status);
        }

        status = http_value_list_to_string (values, sizeof (values), ds, vl);

        if (status != 0) {
                ERROR ("http plugin: error with http_value_list_to_string");
                return (status);
        }


        pthread_mutex_lock (&send_lock);

        status = ssnprintf (send_buffer + send_buffer_fill, sizeof (send_buffer) - send_buffer_fill,
                        "PUTVAL %s interval=%i %u%s\n",
                        key, interval_g, vl->time, values);
        send_buffer_fill += status;

        if ((sizeof (send_buffer) - send_buffer_fill) < (sizeof(key) + sizeof(values)))
        {
                status = http_flush_buffer();
                if (status != 0)
                        return status;

        }

        pthread_mutex_unlock (&send_lock);


        return (0);

} /* }}} int http_write */

static int http_shutdown (void) /* {{{ */
{
        http_flush_buffer();
        curl_easy_cleanup(curl);
        return (0);
}

void module_register (void) /* {{{ */
{
        plugin_register_init("http", http_init);
        plugin_register_config ("http", http_config,
                        config_keys, config_keys_num);
        plugin_register_write ("http", http_write, /* user_data = */ NULL);
        plugin_register_shutdown("http", http_shutdown);
} /* }}} void module_register */

/* vim: set fdm=marker sw=8 ts=8 tw=78 : */
