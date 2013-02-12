/**
 * collectd - src/write_tsdb.c
 * Copyright (C) 2012       Pierre-Yves Ritschard
 * Copyright (C) 2011       Scott Sanders
 * Copyright (C) 2009       Paul Sadauskas
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2007-2012  Florian octo Forster
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
 *   Doug MacEachern <dougm at hyperic.com>
 *   Paul Sadauskas <psadauskas at gmail.com>
 *   Scott Sanders <scott at jssjr.com>
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 *
 * Modified by Brett Hawn <bhawn at llnw.com> 
 * Based on the write_graphite plugin.
 **/

 /* write_tsdb plugin configuation example
  *
  * <Plugin write_tsdb>
  *   <Node>
  *     Host "localhost"
  *     Port "4242"
  *     Prefix "sys"
  *   </Node>
  * </Plugin>
  */

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include "utils_cache.h"
#include "utils_parse_option.h"

/* Folks without pthread will need to disable this plugin. */
#include <pthread.h>

#include <sys/socket.h>
#include <netdb.h>

#ifndef WT_DEFAULT_NODE
# define WT_DEFAULT_NODE "localhost"
#endif

#ifndef WT_DEFAULT_SERVICE
# define WT_DEFAULT_SERVICE "4242"
#endif

#ifndef WT_DEFAULT_ESCAPE
# define WT_DEFAULT_ESCAPE '.'
#endif

/* Ethernet - (IPv6 + TCP) = 1500 - (40 + 32) = 1428 */
#ifndef WT_SEND_BUF_SIZE
# define WT_SEND_BUF_SIZE 1428
#endif

/*
 * Private variables
 */
struct wt_callback
{
    int      sock_fd;

    char    *node;
    char    *service;
    char    *prefix;
    char    *postfix;
    char     escape_char;

    _Bool    store_rates;
    _Bool    separate_instances;
    _Bool    always_append_ds;

    char     send_buf[WT_SEND_BUF_SIZE];
    size_t   send_buf_free;
    size_t   send_buf_fill;
    cdtime_t send_buf_init_time;

    pthread_mutex_t send_lock;
};


/*
 * Functions
 */
static void wt_reset_buffer (struct wt_callback *cb)
{
    memset (cb->send_buf, 0, sizeof (cb->send_buf));
    cb->send_buf_free = sizeof (cb->send_buf);
    cb->send_buf_fill = 0;
    cb->send_buf_init_time = cdtime ();
}

static int wt_send_buffer (struct wt_callback *cb)
{
    ssize_t status = 0;

    status = swrite (cb->sock_fd, cb->send_buf, strlen (cb->send_buf));
    if (status < 0)
    {
        char errbuf[1024];
        ERROR ("write_tsdb plugin: send failed with status %zi (%s)",
                status, sstrerror (errno, errbuf, sizeof (errbuf)));


        close (cb->sock_fd);
        cb->sock_fd = -1;

        return (-1);
    }

    return (0);
}

/* NOTE: You must hold cb->send_lock when calling this function! */
static int wt_flush_nolock (cdtime_t timeout, struct wt_callback *cb)
{
    int status;

    DEBUG ("write_tsdb plugin: wt_flush_nolock: timeout = %.3f; "
            "send_buf_fill = %zu;",
            (double)timeout,
            cb->send_buf_fill);

    /* timeout == 0  => flush unconditionally */
    if (timeout > 0)
    {
        cdtime_t now;

        now = cdtime ();
        if ((cb->send_buf_init_time + timeout) > now)
            return (0);
    }

    if (cb->send_buf_fill <= 0)
    {
        cb->send_buf_init_time = cdtime ();
        return (0);
    }

    status = wt_send_buffer (cb);
    wt_reset_buffer (cb);

    return (status);
}

static int wt_callback_init (struct wt_callback *cb)
{
    struct addrinfo ai_hints;
    struct addrinfo *ai_list;
    struct addrinfo *ai_ptr;
    int status;

    const char *node = cb->node ? cb->node : WT_DEFAULT_NODE;
    const char *service = cb->service ? cb->service : WT_DEFAULT_SERVICE;

    if (cb->sock_fd > 0)
        return (0);

    memset (&ai_hints, 0, sizeof (ai_hints));
#ifdef AI_ADDRCONFIG
    ai_hints.ai_flags |= AI_ADDRCONFIG;
#endif
    ai_hints.ai_family = AF_UNSPEC;
    ai_hints.ai_socktype = SOCK_STREAM;

    ai_list = NULL;

    status = getaddrinfo (node, service, &ai_hints, &ai_list);
    if (status != 0)
    {
        ERROR ("write_tsdb plugin: getaddrinfo (%s, %s) failed: %s",
                node, service, gai_strerror (status));
        return (-1);
    }

    assert (ai_list != NULL);
    for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
    {
        cb->sock_fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype,
                ai_ptr->ai_protocol);
        if (cb->sock_fd < 0)
            continue;

        status = connect (cb->sock_fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
        if (status != 0)
        {
            close (cb->sock_fd);
            cb->sock_fd = -1;
            continue;
        }

        break;
    }

    freeaddrinfo (ai_list);

    if (cb->sock_fd < 0)
    {
        char errbuf[1024];
        ERROR ("write_tsdb plugin: Connecting to %s:%s failed. "
                "The last error was: %s", node, service,
                sstrerror (errno, errbuf, sizeof (errbuf)));
        close (cb->sock_fd);
        return (-1);
    }

    wt_reset_buffer (cb);

    return (0);
}

static void wt_callback_free (void *data)
{
    struct wt_callback *cb;

    if (data == NULL)
        return;

    cb = data;

    pthread_mutex_lock (&cb->send_lock);

    wt_flush_nolock (/* timeout = */ 0, cb);

    close(cb->sock_fd);
    cb->sock_fd = -1;

    sfree(cb->node);
    sfree(cb->service);
    sfree(cb->prefix);
    sfree(cb->postfix);

    pthread_mutex_destroy (&cb->send_lock);

    sfree(cb);
}

static int wt_flush (cdtime_t timeout,
        const char *identifier __attribute__((unused)),
        user_data_t *user_data)
{
    struct wt_callback *cb;
    int status;

    if (user_data == NULL)
        return (-EINVAL);

    cb = user_data->data;

    pthread_mutex_lock (&cb->send_lock);

    if (cb->sock_fd < 0)
    {
        status = wt_callback_init (cb);
        if (status != 0)
        {
            ERROR ("write_tsdb plugin: wt_callback_init failed.");
            pthread_mutex_unlock (&cb->send_lock);
            return (-1);
        }
    }

    status = wt_flush_nolock (timeout, cb);
    pthread_mutex_unlock (&cb->send_lock);

    return (status);
}

static int wt_format_values (char *ret, size_t ret_len,
        int ds_num, const data_set_t *ds, const value_list_t *vl,
        _Bool store_rates)
{
    size_t offset = 0;
    int status;
    gauge_t *rates = NULL;

    assert (0 == strcmp (ds->type, vl->type));

    memset (ret, 0, ret_len);

#define BUFFER_ADD(...) do { \
    status = ssnprintf (ret + offset, ret_len - offset, \
            __VA_ARGS__); \
    if (status < 1) \
    { \
        sfree (rates); \
        return (-1); \
    } \
    else if (((size_t) status) >= (ret_len - offset)) \
    { \
        sfree (rates); \
        return (-1); \
    } \
    else \
    offset += ((size_t) status); \
} while (0)

    if (ds->ds[ds_num].type == DS_TYPE_GAUGE)
        BUFFER_ADD ("%f", vl->values[ds_num].gauge);
    else if (store_rates)
    {
        if (rates == NULL)
            rates = uc_get_rate (ds, vl);
        if (rates == NULL)
        {
            WARNING ("format_values: "
                    "uc_get_rate failed.");
            return (-1);
        }
        BUFFER_ADD ("%f", rates[ds_num]);
    }
    else if (ds->ds[ds_num].type == DS_TYPE_COUNTER)
        BUFFER_ADD ("%llu", vl->values[ds_num].counter);
    else if (ds->ds[ds_num].type == DS_TYPE_DERIVE)
        BUFFER_ADD ("%"PRIi64, vl->values[ds_num].derive);
    else if (ds->ds[ds_num].type == DS_TYPE_ABSOLUTE)
        BUFFER_ADD ("%"PRIu64, vl->values[ds_num].absolute);
    else
    {
        ERROR ("format_values plugin: Unknown data source type: %i",
                ds->ds[ds_num].type);
        sfree (rates);
        return (-1);
    }

#undef BUFFER_ADD

    sfree (rates);
    return (0);
}

static int wt_format_name (char *ret, int ret_len,
        const value_list_t *vl,
        const struct wt_callback *cb,
        const char *ds_name)
{
    char *prefix;
    char *postfix;

    prefix = cb->prefix;
    if (prefix == NULL)
        prefix = "";

    postfix = cb->postfix;
    if (postfix == NULL)
        postfix = "";

    if (ds_name != NULL) {
		if (vl->plugin_instance[0] == '\0') {
			ssnprintf(ret, ret_len, "%s.%s.%s",
				prefix, vl->plugin, ds_name);
		} else if (vl->type_instance == '\0') {
			ssnprintf(ret, ret_len, "%s.%s.%s.%s.%s",
				prefix, vl->plugin, vl->plugin_instance, vl->type_instance, ds_name);
		} else {
			ssnprintf(ret, ret_len, "%s.%s.%s.%s.%s",
				prefix, vl->plugin, vl->plugin_instance, vl->type, ds_name);
		}
	} else if (vl->plugin_instance[0] == '\0') {
		if (vl->type_instance[0] == '\0') 
			ssnprintf(ret, ret_len, "%s.%s.%s",
				prefix, vl->plugin, vl->type);
		else 
			ssnprintf(ret, ret_len, "%s.%s.%s",
				prefix, vl->plugin, vl->type_instance);
    } else if (vl->type_instance[0] == '\0') {
		ssnprintf(ret, ret_len, "%s.%s.%s.%s",
			prefix, vl->plugin, vl->plugin_instance, vl->type);
    } else {
		ssnprintf(ret, ret_len, "%s.%s.%s.%s",
			prefix, vl->plugin, vl->plugin_instance, vl->type_instance);
    }

    return (0);
}

static int wt_send_message (const char* key, const char* value,
        cdtime_t time, struct wt_callback *cb, const char* host)
{
    int status;
    size_t message_len;
    char message[1024];

    /* skip if value is NaN */
    if (value[0] == 'n')
      return (0);

    message_len = (size_t) ssnprintf (message, sizeof (message),
            "put %s %u %s fqdn=%s\r\n",
            key,
            (unsigned int) CDTIME_T_TO_TIME_T (time),
            value,
	    host);
    if (message_len >= sizeof (message)) {
        ERROR ("write_tsdb plugin: message buffer too small: "
                "Need %zu bytes.", message_len + 1);
        return (-1);
    }

    pthread_mutex_lock (&cb->send_lock);

    if (cb->sock_fd < 0)
    {
        status = wt_callback_init (cb);
        if (status != 0)
        {
            ERROR ("write_tsdb plugin: wt_callback_init failed.");
            pthread_mutex_unlock (&cb->send_lock);
            return (-1);
        }
    }

    if (message_len >= cb->send_buf_free)
    {
        status = wt_flush_nolock (/* timeout = */ 0, cb);
        if (status != 0)
        {
            pthread_mutex_unlock (&cb->send_lock);
            return (status);
        }
    }

    /* Assert that we have enough space for this message. */
    assert (message_len < cb->send_buf_free);

    /* `message_len + 1' because `message_len' does not include the
     * trailing null byte. Neither does `send_buffer_fill'. */
    memcpy (cb->send_buf + cb->send_buf_fill,
            message, message_len + 1);
    cb->send_buf_fill += message_len;
    cb->send_buf_free -= message_len;

    DEBUG ("write_tsdb plugin: [%s]:%s buf %zu/%zu (%.1f %%) \"%s\"",
            cb->node,
            cb->service,
            cb->send_buf_fill, sizeof (cb->send_buf),
            100.0 * ((double) cb->send_buf_fill) / ((double) sizeof (cb->send_buf)),
            message);

    pthread_mutex_unlock (&cb->send_lock);

    return (0);
}

static int wt_write_messages (const data_set_t *ds, const value_list_t *vl,
        struct wt_callback *cb)
{
    char key[10*DATA_MAX_NAME_LEN];
    char values[512];

    int status, i;

    if (0 != strcmp (ds->type, vl->type))
    {
        ERROR ("write_tsdb plugin: DS type does not match "
                "value list type");
        return -1;
    }

    for (i = 0; i < ds->ds_num; i++)
    {
        const char *ds_name = NULL;

        if (cb->always_append_ds || (ds->ds_num > 1))
            ds_name = ds->ds[i].name;

        /* Copy the identifier to `key' and escape it. */
        status = wt_format_name (key, sizeof (key), vl, cb, ds_name);
        if (status != 0)
        {
            ERROR ("write_tsdb plugin: error with format_name");
            return (status);
        }

        escape_string (key, sizeof (key));
        /* Convert the values to an ASCII representation and put that into
         * `values'. */
        status = wt_format_values (values, sizeof (values), i, ds, vl,
                    cb->store_rates);
        if (status != 0)
        {
            ERROR ("write_tsdb plugin: error with "
                    "wt_format_values");
            return (status);
        }

        /* Send the message to tsdb */
        status = wt_send_message (key, values, vl->time, cb, vl->host);
        if (status != 0)
        {
            ERROR ("write_tsdb plugin: error with "
                    "wt_send_message");
            return (status);
        }
    }

    return (0);
}

static int wt_write (const data_set_t *ds, const value_list_t *vl,
        user_data_t *user_data)
{
    struct wt_callback *cb;
    int status;

    if (user_data == NULL)
        return (EINVAL);

    cb = user_data->data;

    status = wt_write_messages (ds, vl, cb);

    return (status);
}

static int config_set_char (char *dest,
        oconfig_item_t *ci)
{
    char buffer[4];
    int status;

    memset (buffer, 0, sizeof (buffer));

    status = cf_util_get_string_buffer (ci, buffer, sizeof (buffer));
    if (status != 0)
        return (status);

    if (buffer[0] == 0)
    {
        ERROR ("write_tsdb plugin: Cannot use an empty string for the "
                "\"EscapeCharacter\" option.");
        return (-1);
    }

    if (buffer[1] != 0)
    {
        WARNING ("write_tsdb plugin: Only the first character of the "
                "\"EscapeCharacter\" option ('%c') will be used.",
                (int) buffer[0]);
    }

    *dest = buffer[0];

    return (0);
}

static int wt_config_tsd (oconfig_item_t *ci)
{
    struct wt_callback *cb;
    user_data_t user_data;
    char callback_name[DATA_MAX_NAME_LEN];
    int i;

    cb = malloc (sizeof (*cb));
    if (cb == NULL)
    {
        ERROR ("write_tsdb plugin: malloc failed.");
        return (-1);
    }
    memset (cb, 0, sizeof (*cb));
    cb->sock_fd = -1;
    cb->node = NULL;
    cb->service = NULL;
    cb->prefix = NULL;
    cb->postfix = NULL;
    cb->escape_char = WT_DEFAULT_ESCAPE;
    cb->store_rates = 1;

    pthread_mutex_init (&cb->send_lock, /* attr = */ NULL);

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp ("Host", child->key) == 0)
            cf_util_get_string (child, &cb->node);
        else if (strcasecmp ("Port", child->key) == 0)
            cf_util_get_service (child, &cb->service);
        else if (strcasecmp ("Prefix", child->key) == 0)
            cf_util_get_string (child, &cb->prefix);
        else if (strcasecmp ("Postfix", child->key) == 0)
            cf_util_get_string (child, &cb->postfix);
        else if (strcasecmp ("StoreRates", child->key) == 0)
            cf_util_get_boolean (child, &cb->store_rates);
        else if (strcasecmp ("SeparateInstances", child->key) == 0)
            cf_util_get_boolean (child, &cb->separate_instances);
        else if (strcasecmp ("AlwaysAppendDS", child->key) == 0)
            cf_util_get_boolean (child, &cb->always_append_ds);
        else if (strcasecmp ("EscapeCharacter", child->key) == 0)
            config_set_char (&cb->escape_char, child);
        else
        {
            ERROR ("write_tsdb plugin: Invalid configuration "
                        "option: %s.", child->key);
        }
    }

    ssnprintf (callback_name, sizeof (callback_name), "write_tsdb/%s/%s",
            cb->node != NULL ? cb->node : WT_DEFAULT_NODE,
            cb->service != NULL ? cb->service : WT_DEFAULT_SERVICE);

    memset (&user_data, 0, sizeof (user_data));
    user_data.data = cb;
    user_data.free_func = wt_callback_free;
    plugin_register_write (callback_name, wt_write, &user_data);

    user_data.free_func = NULL;
    plugin_register_flush (callback_name, wt_flush, &user_data);

    return (0);
}

static int wt_config (oconfig_item_t *ci)
{
    int i;

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp ("Node", child->key) == 0)
            wt_config_tsd (child);
        else
        {
            ERROR ("write_tsdb plugin: Invalid configuration "
                    "option: %s.", child->key);
        }
    }

    return (0);
}

void module_register (void)
{
    plugin_register_complex_config ("write_tsdb", wt_config);
}

/* vim: set sw=4 ts=4 sts=4 tw=78 et : */
