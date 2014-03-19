/**
 * collectd - src/write_redis_pubsub.c
 * Copyright (C) 2009       Paul Sadauskas
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2007-2009  Florian octo Forster
 * Copyright (C) 2013-2014  Pavel Kirpichyov
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
 *   Pavel Kirpichyov <pavel.kirpichyov@gmail.com>
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

#include <hiredis/hiredis.h>

/*
 * Private variables
 */
struct wh_callback_s
{
	char *node;
	char *host;
	char *channel;
	int port;
	int timeout;
	int store_rates;

	redisContext *c;
	redisReply *reply;

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

        format_json_initialize (cb->send_buffer,
                                &cb->send_buffer_fill,
                                &cb->send_buffer_free);
} /* }}} wh_reset_buffer */

static int wh_send_buffer (wh_callback_t *cb) /* {{{ */
{
        int status = 0;
	const char *message = cb->send_buffer;
	cb->reply = redisCommand(cb->c, "PUBLISH %s %s", cb->channel, message);
	freeReplyObject(cb->reply);
        return (status);
} /* }}} wh_send_buffer */

static int wh_callback_init (wh_callback_t *cb) /* {{{ */
{
        if (cb->c != NULL) {
                return (0);
        }

	cb->c = redisConnect(cb->host, cb->port);
    	if (cb->c == NULL || cb->c->err) {
        	if (cb->c) {
            		ERROR("Connection error: %s\n", cb->c->errstr);
            		redisFree(cb->c);
        	} else {
            		ERROR("Connection error: can't allocate redis context\n");
        	}
        	return (-1);
    	}

        wh_reset_buffer (cb);

        return (0);
} /* }}} int wh_callback_init */

static int wh_flush_nolock (cdtime_t timeout, wh_callback_t *cb) /* {{{ */
{
        int status;

        DEBUG ("write_redis_pubsub plugin: wh_flush_nolock: timeout = %.3f; "
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
                ERROR ("write_redis_pubsub: wh_flush_nolock: "
                       "format_json_finalize failed.");
                wh_reset_buffer (cb);
                return (status);
        }

        status = wh_send_buffer (cb);
        wh_reset_buffer (cb);

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

        if (cb->c == NULL)
        {
                status = wh_callback_init (cb);
                if (status != 0)
                {
                        ERROR ("write_redis_pubsub plugin: wh_callback_init failed.");
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

        redisFree(cb->c);
	cb->c = NULL;

        sfree (cb->node);
        sfree (cb->host);
        sfree (cb->channel);
        sfree (cb);
} /* }}} void wh_callback_free */

static int wh_write_json (const data_set_t *ds, const value_list_t *vl, /* {{{ */
                wh_callback_t *cb)
{
        int status;

        pthread_mutex_lock (&cb->send_lock);
        if (cb->c == NULL)
        {
                status = wh_callback_init (cb);
                if (status != 0)
                {
                        ERROR ("write_redis_pubsub plugin: wh_callback_init failed.");
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

        DEBUG ("write_redis_pubsub plugin: <%s> buffer %zu/%zu (%g%%)",
                        cb->host,
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
	status = wh_write_json (ds, vl, cb);
        return (status);
} /* }}} int wh_write */

static int config_set_string (char **ret_string, /* {{{ */
                oconfig_item_t *ci)
{
        char *string;

        if ((ci->values_num != 1)
                        || (ci->values[0].type != OCONFIG_TYPE_STRING))
        {
                WARNING ("write_redis_pubsub plugin: The `%s' config option "
                                "needs exactly one string argument.", ci->key);
                return (-1);
        }

        string = strdup (ci->values[0].value.string);
        if (string == NULL)
        {
                ERROR ("write_redis_pubsub plugin: strdup failed.");
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
                WARNING ("write_redis_pubsub plugin: The `%s' config option "
                                "needs exactly one boolean argument.", ci->key);
                return (-1);
        }

        *dest = ci->values[0].value.boolean ? 1 : 0;

        return (0);
} /* }}} int config_set_boolean */

static int config_set_number (int *dest, oconfig_item_t *ci) /* {{{ */
{
        if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
        {
                WARNING ("write_redis_pubsub plugin: The `%s' config option "
                                "needs exactly one number argument.", ci->key);
                return (-1);
        }

        *dest = (int)ci->values[0].value.number;

        return (0);
} /* }}} int config_set_number */

static int wh_config_node (oconfig_item_t *ci) /* {{{ */
{
        wh_callback_t *cb;
        user_data_t user_data;
        int i;

        cb = malloc (sizeof (*cb));
        if (cb == NULL)
        {
                ERROR ("write_redis_pubsub plugin: malloc failed.");
                return (-1);
        }
        memset (cb, 0, sizeof (*cb));
        cb->node = NULL;
        cb->host = NULL;
        cb->channel = NULL;

        pthread_mutex_init (&cb->send_lock, /* attr = */ NULL);

        config_set_string (&cb->node, ci);
        if (cb->node == NULL)
                return (-1);

        for (i = 0; i < ci->children_num; i++)
        {
                oconfig_item_t *child = ci->children + i;

                if (strcasecmp ("Host", child->key) == 0)
                        config_set_string (&cb->host, child);
                else if (strcasecmp ("Port", child->key) == 0)
                        config_set_number (&cb->port, child);
                else if (strcasecmp ("Timeout", child->key) == 0)
                        config_set_number (&cb->timeout, child);
                else if (strcasecmp ("Channel", child->key) == 0)
                        config_set_string (&cb->channel, child);
				else if (strcasecmp ("StoreRates", child->key) == 0)
				        config_set_boolean (&cb->store_rates, child);
                else
                {
                        ERROR ("write_redis_pubsub plugin: Invalid configuration "
                                        "option: %s.", child->key);
                }
        }

        DEBUG ("write_redis_pubsub: Registering write callback with HOST %s",
                        cb->host);

        memset (&user_data, 0, sizeof (user_data));
        user_data.data = cb;
        user_data.free_func = NULL;
        plugin_register_flush ("write_redis_pubsub", wh_flush, &user_data);

        user_data.free_func = wh_callback_free;
        plugin_register_write ("write_redis_pubsub", wh_write, &user_data);

        return (0);
} /* }}} int wh_config_node */

static int wh_config (oconfig_item_t *ci) /* {{{ */
{
        int i;

        for (i = 0; i < ci->children_num; i++)
        {
                oconfig_item_t *child = ci->children + i;

                if (strcasecmp ("Node", child->key) == 0)
                        wh_config_node (child);
                else
                {
                        ERROR ("write_redis_pubsub plugin: Invalid configuration "
                                        "option: %s.", child->key);
                }
        }

        return (0);
} /* }}} int wh_config */

void module_register (void) /* {{{ */
{
        plugin_register_complex_config ("write_redis_pubsub", wh_config);
} /* }}} void module_register */
