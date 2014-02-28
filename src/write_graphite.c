/**
 * collectd - src/write_graphite.c
 * Copyright (C) 2012       Pierre-Yves Ritschard
 * Copyright (C) 2011       Scott Sanders
 * Copyright (C) 2009       Paul Sadauskas
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2007-2013  Florian octo Forster
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
 * Based on the write_http plugin.
 **/

 /* write_graphite plugin configuation example
  *
  * <Plugin write_graphite>
  *   <Carbon>
  *     Host "localhost"
  *     Port "2003"
  *     Protocol "udp"
  *     LogSendErrors true
  *     Prefix "collectd"
  *   </Carbon>
  * </Plugin>
  */

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include "utils_cache.h"
#include "utils_complain.h"
#include "utils_parse_option.h"
#include "utils_format_graphite.h"

/* Folks without pthread will need to disable this plugin. */
#include <pthread.h>

#include <sys/socket.h>
#include <netdb.h>

#ifndef WG_DEFAULT_NODE
# define WG_DEFAULT_NODE "localhost"
#endif

#ifndef WG_DEFAULT_SERVICE
# define WG_DEFAULT_SERVICE "2003"
#endif

#ifndef WG_DEFAULT_PROTOCOL
# define WG_DEFAULT_PROTOCOL "tcp"
#endif

#ifndef WG_DEFAULT_LOG_SEND_ERRORS
# define WG_DEFAULT_LOG_SEND_ERRORS 1
#endif

#ifndef WG_DEFAULT_ESCAPE
# define WG_DEFAULT_ESCAPE '_'
#endif

/* Ethernet - (IPv6 + TCP) = 1500 - (40 + 32) = 1428 */
#ifndef WG_SEND_BUF_SIZE
# define WG_SEND_BUF_SIZE 1428
#endif

/*
 * Private variables
 */
struct wg_callback
{
    int      sock_fd;

    char    *name;

    char    *node;
    char    *service;
    char    *protocol;
    _Bool   log_send_errors;
    char    *prefix;
    char    *postfix;
    char     escape_char;

    unsigned int format_flags;

    char     send_buf[WG_SEND_BUF_SIZE];
    size_t   send_buf_free;
    size_t   send_buf_fill;
    cdtime_t send_buf_init_time;

    pthread_mutex_t send_lock;
    c_complain_t init_complaint;
};


/*
 * Functions
 */
static void wg_reset_buffer (struct wg_callback *cb)
{
    memset (cb->send_buf, 0, sizeof (cb->send_buf));
    cb->send_buf_free = sizeof (cb->send_buf);
    cb->send_buf_fill = 0;
    cb->send_buf_init_time = cdtime ();
}

static int wg_send_buffer (struct wg_callback *cb)
{
    ssize_t status = 0;

    status = swrite (cb->sock_fd, cb->send_buf, strlen (cb->send_buf));
    if (status < 0)
    {
        const char *protocol = cb->protocol ? cb->protocol : WG_DEFAULT_PROTOCOL;

        if (cb->log_send_errors)
        {
            char errbuf[1024];
            ERROR ("write_graphite plugin: send to %s:%s (%s) failed with status %zi (%s)",
                    cb->node, cb->service, protocol,
                    status, sstrerror (errno, errbuf, sizeof (errbuf)));
        }

        close (cb->sock_fd);
        cb->sock_fd = -1;

        return (-1);
    }

    return (0);
}

/* NOTE: You must hold cb->send_lock when calling this function! */
static int wg_flush_nolock (cdtime_t timeout, struct wg_callback *cb)
{
    int status;

    DEBUG ("write_graphite plugin: wg_flush_nolock: timeout = %.3f; "
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

    status = wg_send_buffer (cb);
    wg_reset_buffer (cb);

    return (status);
}

static int wg_callback_init (struct wg_callback *cb)
{
    struct addrinfo ai_hints;
    struct addrinfo *ai_list;
    struct addrinfo *ai_ptr;
    int status;

    const char *node = cb->node ? cb->node : WG_DEFAULT_NODE;
    const char *service = cb->service ? cb->service : WG_DEFAULT_SERVICE;
    const char *protocol = cb->protocol ? cb->protocol : WG_DEFAULT_PROTOCOL;

    if (cb->sock_fd > 0)
        return (0);

    memset (&ai_hints, 0, sizeof (ai_hints));
#ifdef AI_ADDRCONFIG
    ai_hints.ai_flags |= AI_ADDRCONFIG;
#endif
    ai_hints.ai_family = AF_UNSPEC;

    if (0 == strcasecmp ("tcp", protocol))
        ai_hints.ai_socktype = SOCK_STREAM;
    else
        ai_hints.ai_socktype = SOCK_DGRAM;

    ai_list = NULL;

    status = getaddrinfo (node, service, &ai_hints, &ai_list);
    if (status != 0)
    {
        ERROR ("write_graphite plugin: getaddrinfo (%s, %s, %s) failed: %s",
                node, service, protocol, gai_strerror (status));
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
        c_complain (LOG_ERR, &cb->init_complaint,
                "write_graphite plugin: Connecting to %s:%s via %s failed. "
                "The last error was: %s", node, service, protocol,
                sstrerror (errno, errbuf, sizeof (errbuf)));
        return (-1);
    }
    else
    {
        c_release (LOG_INFO, &cb->init_complaint,
                "write_graphite plugin: Successfully connected to %s:%s via %s.",
                node, service, protocol);
    }

    wg_reset_buffer (cb);

    return (0);
}

static void wg_callback_free (void *data)
{
    struct wg_callback *cb;

    if (data == NULL)
        return;

    cb = data;

    pthread_mutex_lock (&cb->send_lock);

    wg_flush_nolock (/* timeout = */ 0, cb);

    if (cb->sock_fd >= 0)
    {
        close (cb->sock_fd);
        cb->sock_fd = -1;
    }

    sfree(cb->name);
    sfree(cb->node);
    sfree(cb->protocol);
    sfree(cb->service);
    sfree(cb->prefix);
    sfree(cb->postfix);

    pthread_mutex_destroy (&cb->send_lock);

    sfree(cb);
}

static int wg_flush (cdtime_t timeout,
        const char *identifier __attribute__((unused)),
        user_data_t *user_data)
{
    struct wg_callback *cb;
    int status;

    if (user_data == NULL)
        return (-EINVAL);

    cb = user_data->data;

    pthread_mutex_lock (&cb->send_lock);

    if (cb->sock_fd < 0)
    {
        status = wg_callback_init (cb);
        if (status != 0)
        {
            /* An error message has already been printed. */
            pthread_mutex_unlock (&cb->send_lock);
            return (-1);
        }
    }

    status = wg_flush_nolock (timeout, cb);
    pthread_mutex_unlock (&cb->send_lock);

    return (status);
}

static int wg_send_message (char const *message, struct wg_callback *cb)
{
    int status;
    size_t message_len;

    message_len = strlen (message);

    pthread_mutex_lock (&cb->send_lock);

    if (cb->sock_fd < 0)
    {
        status = wg_callback_init (cb);
        if (status != 0)
        {
            /* An error message has already been printed. */
            pthread_mutex_unlock (&cb->send_lock);
            return (-1);
        }
    }

    if (message_len >= cb->send_buf_free)
    {
        status = wg_flush_nolock (/* timeout = */ 0, cb);
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

    DEBUG ("write_graphite plugin: [%s]:%s (%s) buf %zu/%zu (%.1f %%) \"%s\"",
            cb->node,
            cb->service,
            cb->protocol,
            cb->send_buf_fill, sizeof (cb->send_buf),
            100.0 * ((double) cb->send_buf_fill) / ((double) sizeof (cb->send_buf)),
            message);

    pthread_mutex_unlock (&cb->send_lock);

    return (0);
}

static int wg_write_messages (const data_set_t *ds, const value_list_t *vl,
        struct wg_callback *cb)
{
    char buffer[WG_SEND_BUF_SIZE];
    int status;

    if (0 != strcmp (ds->type, vl->type))
    {
        ERROR ("write_graphite plugin: DS type does not match "
                "value list type");
        return -1;
    }

    memset (buffer, 0, sizeof (buffer));
    status = format_graphite (buffer, sizeof (buffer), ds, vl,
            cb->prefix, cb->postfix, cb->escape_char, cb->format_flags);
    if (status != 0) /* error message has been printed already. */
        return (status);

    /* Send the message to graphite */
    status = wg_send_message (buffer, cb);
    if (status != 0) /* error message has been printed already. */
        return (status);

    return (0);
} /* int wg_write_messages */

static int wg_write (const data_set_t *ds, const value_list_t *vl,
        user_data_t *user_data)
{
    struct wg_callback *cb;
    int status;

    if (user_data == NULL)
        return (EINVAL);

    cb = user_data->data;

    status = wg_write_messages (ds, vl, cb);

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
        ERROR ("write_graphite plugin: Cannot use an empty string for the "
                "\"EscapeCharacter\" option.");
        return (-1);
    }

    if (buffer[1] != 0)
    {
        WARNING ("write_graphite plugin: Only the first character of the "
                "\"EscapeCharacter\" option ('%c') will be used.",
                (int) buffer[0]);
    }

    *dest = buffer[0];

    return (0);
}

static int wg_config_node (oconfig_item_t *ci)
{
    struct wg_callback *cb;
    user_data_t user_data;
    char callback_name[DATA_MAX_NAME_LEN];
    int i;
    int status = 0;

    cb = malloc (sizeof (*cb));
    if (cb == NULL)
    {
        ERROR ("write_graphite plugin: malloc failed.");
        return (-1);
    }
    memset (cb, 0, sizeof (*cb));
    cb->sock_fd = -1;
    cb->name = NULL;
    cb->node = NULL;
    cb->service = NULL;
    cb->protocol = NULL;
    cb->log_send_errors = WG_DEFAULT_LOG_SEND_ERRORS;
    cb->prefix = NULL;
    cb->postfix = NULL;
    cb->escape_char = WG_DEFAULT_ESCAPE;
    cb->format_flags = GRAPHITE_STORE_RATES;

    /* FIXME: Legacy configuration syntax. */
    if (strcasecmp ("Carbon", ci->key) != 0)
    {
        status = cf_util_get_string (ci, &cb->name);
        if (status != 0)
        {
            wg_callback_free (cb);
            return (status);
        }
    }

    pthread_mutex_init (&cb->send_lock, /* attr = */ NULL);
    C_COMPLAIN_INIT (&cb->init_complaint);

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp ("Host", child->key) == 0)
            cf_util_get_string (child, &cb->node);
        else if (strcasecmp ("Port", child->key) == 0)
            cf_util_get_service (child, &cb->service);
        else if (strcasecmp ("Protocol", child->key) == 0)
        {
            cf_util_get_string (child, &cb->protocol);

            if (strcasecmp ("UDP", cb->protocol) != 0 &&
                strcasecmp ("TCP", cb->protocol) != 0)
            {
                ERROR ("write_graphite plugin: Unknown protocol (%s)",
                        cb->protocol);
                status = -1;
            }
        }
        else if (strcasecmp ("LogSendErrors", child->key) == 0)
            cf_util_get_boolean (child, &cb->log_send_errors);
        else if (strcasecmp ("Prefix", child->key) == 0)
            cf_util_get_string (child, &cb->prefix);
        else if (strcasecmp ("Postfix", child->key) == 0)
            cf_util_get_string (child, &cb->postfix);
        else if (strcasecmp ("StoreRates", child->key) == 0)
            cf_util_get_flag (child, &cb->format_flags,
                    GRAPHITE_STORE_RATES);
        else if (strcasecmp ("SeparateInstances", child->key) == 0)
            cf_util_get_flag (child, &cb->format_flags,
                    GRAPHITE_SEPARATE_INSTANCES);
        else if (strcasecmp ("AlwaysAppendDS", child->key) == 0)
            cf_util_get_flag (child, &cb->format_flags,
                    GRAPHITE_ALWAYS_APPEND_DS);
        else if (strcasecmp ("EscapeCharacter", child->key) == 0)
            config_set_char (&cb->escape_char, child);
        else
        {
            ERROR ("write_graphite plugin: Invalid configuration "
                        "option: %s.", child->key);
            status = -1;
        }

        if (status != 0)
            break;
    }

    if (status != 0)
    {
        wg_callback_free (cb);
        return (status);
    }

    /* FIXME: Legacy configuration syntax. */
    if (cb->name == NULL)
        ssnprintf (callback_name, sizeof (callback_name), "write_graphite/%s/%s/%s",
                cb->node != NULL ? cb->node : WG_DEFAULT_NODE,
                cb->service != NULL ? cb->service : WG_DEFAULT_SERVICE,
                cb->protocol != NULL ? cb->protocol : WG_DEFAULT_PROTOCOL);
    else
        ssnprintf (callback_name, sizeof (callback_name), "write_graphite/%s",
                cb->name);

    memset (&user_data, 0, sizeof (user_data));
    user_data.data = cb;
    user_data.free_func = wg_callback_free;
    plugin_register_write (callback_name, wg_write, &user_data);

    user_data.free_func = NULL;
    plugin_register_flush (callback_name, wg_flush, &user_data);

    return (0);
}

static int wg_config (oconfig_item_t *ci)
{
    int i;

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp ("Node", child->key) == 0)
            wg_config_node (child);
        /* FIXME: Remove this legacy mode in version 6. */
        else if (strcasecmp ("Carbon", child->key) == 0)
            wg_config_node (child);
        else
        {
            ERROR ("write_graphite plugin: Invalid configuration "
                    "option: %s.", child->key);
        }
    }

    return (0);
}

void module_register (void)
{
    plugin_register_complex_config ("write_graphite", wg_config);
}

/* vim: set sw=4 ts=4 sts=4 tw=78 et : */
