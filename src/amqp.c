/**
 * collectd - src/amqp.c
 * Copyright (C) 2009  Sebastien Pahl
 * Copyright (C) 2010  Florian Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Sebastien Pahl <sebastien.pahl at dotcloud.com>
 *   Florian Forster <octo at verplant.org>
 **/

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <pthread.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_format_json.h"

#include <amqp.h>
#include <amqp_framing.h>

/* Defines for the delivery mode. I have no idea why they're not defined by the
 * library.. */
#define CAMQP_DM_VOLATILE   1
#define CAMQP_DM_PERSISTENT 2

#define CAMQP_CHANNEL 1

/*
 * Data types
 */
struct camqp_config_s
{
    _Bool   publish;
    char   *name;

    char   *host;
    int     port;
    char   *vhost;
    char   *user;
    char   *password;

    char   *exchange;
    char   *exchange_type;
    char   *queue;
    char   *routingkey;
    uint8_t delivery_mode;

    _Bool   store_rates;

    amqp_connection_state_t connection;
    pthread_mutex_t lock;
};
typedef struct camqp_config_s camqp_config_t;

/*
 * Global variables
 */
static const char *def_host       = "localhost";
static const char *def_vhost      = "/";
static const char *def_user       = "guest";
static const char *def_password   = "guest";
static const char *def_exchange   = "amq.fanout";
static const char *def_routingkey = "collectd";

#define CONF(c,f) (((c)->f != NULL) ? (c)->f : def_##f)

/*
 * Functions
 */
static void camqp_close_connection (camqp_config_t *conf) /* {{{ */
{
    int sockfd;

    if ((conf == NULL) || (conf->connection == NULL))
        return;

    sockfd = amqp_get_sockfd (conf->connection);
    amqp_channel_close (conf->connection, CAMQP_CHANNEL, AMQP_REPLY_SUCCESS);
    amqp_connection_close (conf->connection, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection (conf->connection);
    close (sockfd);
    conf->connection = NULL;
} /* }}} void camqp_close_connection */

static void camqp_config_free (void *ptr) /* {{{ */
{
    camqp_config_t *conf = ptr;

    if (conf == NULL)
        return;

    camqp_close_connection (conf);

    sfree (conf->name);
    sfree (conf->host);
    sfree (conf->vhost);
    sfree (conf->user);
    sfree (conf->password);
    sfree (conf->exchange);
    sfree (conf->exchange_type);
    sfree (conf->queue);
    sfree (conf->routingkey);

    sfree (conf);
} /* }}} void camqp_config_free */

static int amqp_connect (camqp_config_t *conf) /* {{{ */
{
    amqp_rpc_reply_t reply;
    int sockfd;
    int status;

    if (conf->connection != NULL)
        return (0);

    conf->connection = amqp_new_connection ();
    if (conf->connection == NULL)
    {
        ERROR ("amqp plugin: amqp_new_connection failed.");
        return (ENOMEM);
    }

    sockfd = amqp_open_socket (CONF(conf, host), conf->port);
    if (sockfd < 0)
    {
        char errbuf[1024];
        status = (-1) * sockfd;
        ERROR ("amqp plugin: amqp_open_socket failed: %s",
                sstrerror (status, errbuf, sizeof (errbuf)));
        amqp_destroy_connection (conf->connection);
        conf->connection = NULL;
        return (status);
    }
    amqp_set_sockfd (conf->connection, sockfd);

    reply = amqp_login (conf->connection, CONF(conf, vhost),
            /* channel max = */      0,
            /* frame max   = */ 131072,
            /* heartbeat   = */      0,
            /* authentication = */ AMQP_SASL_METHOD_PLAIN,
            CONF(conf, user), CONF(conf, password));
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        ERROR ("amqp plugin: amqp_login (vhost = %s, user = %s) failed.",
                CONF(conf, vhost), CONF(conf, user));
        amqp_destroy_connection (conf->connection);
        close (sockfd);
        conf->connection = NULL;
        return (1);
    }

    amqp_channel_open (conf->connection, /* channel = */ 1);
    /* FIXME: Is checking "reply.reply_type" really correct here? How does
     * it get set? --octo */
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        ERROR ("amqp plugin: amqp_channel_open failed.");
        amqp_connection_close (conf->connection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection (conf->connection);
        close(sockfd);
        conf->connection = NULL;
        return (1);
    }

    INFO ("amqp plugin: Successfully opened connection to vhost \"%s\" "
            "on %s:%i.", CONF(conf, vhost), CONF(conf, host), conf->port);
    return (0);
} /* }}} int amqp_connect */

static int amqp_write_locked (camqp_config_t *conf, /* {{{ */
        const char *buffer)
{
    amqp_basic_properties_t props;
    int status;

    status = amqp_connect (conf);
    if (status != 0)
        return (status);

    memset (&props, 0, sizeof (props));
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG
        | AMQP_BASIC_DELIVERY_MODE_FLAG
        | AMQP_BASIC_APP_ID_FLAG;
    props.content_type = amqp_cstring_bytes("application/json");
    props.delivery_mode = conf->delivery_mode;
    props.app_id = amqp_cstring_bytes("collectd");

    status = amqp_basic_publish(conf->connection,
                /* channel = */ 1,
                amqp_cstring_bytes(CONF(conf, exchange)),
                amqp_cstring_bytes(CONF(conf, routingkey)),
                /* mandatory = */ 0,
                /* immediate = */ 0,
                &props,
                amqp_cstring_bytes(buffer));
    if (status != 0)
    {
        ERROR ("amqp plugin: amqp_basic_publish failed with status %i.",
                status);
        camqp_close_connection (conf);
    }

    return (status);
} /* }}} int amqp_write_locked */

static int amqp_write (const data_set_t *ds, const value_list_t *vl, /* {{{ */
        user_data_t *user_data)
{
    camqp_config_t *conf = user_data->data;
    char buffer[4096];
    size_t bfree;
    size_t bfill;
    int status;

    if ((ds == NULL) || (vl == NULL) || (conf == NULL))
        return (EINVAL);

    memset (buffer, 0, sizeof (buffer));
    bfree = sizeof (buffer);
    bfill = 0;

    format_json_initialize (buffer, &bfill, &bfree);
    format_json_value_list (buffer, &bfill, &bfree, ds, vl, conf->store_rates);
    format_json_finalize (buffer, &bfill, &bfree);

    pthread_mutex_lock (&conf->lock);
    status = amqp_write_locked (conf, buffer);
    pthread_mutex_unlock (&conf->lock);

    return (status);
} /* }}} int amqp_write */

static int camqp_config_connection (oconfig_item_t *ci, /* {{{ */
        _Bool publish)
{
    camqp_config_t *conf;
    int status;
    int i;

    conf = malloc (sizeof (*conf));
    if (conf == NULL)
    {
        ERROR ("amqp plugin: malloc failed.");
        return (ENOMEM);
    }

    /* Initialize "conf" {{{ */
    memset (conf, 0, sizeof (*conf));
    conf->publish = publish;
    conf->name = NULL;
    conf->host = NULL;
    conf->port = 5672;
    conf->vhost = NULL;
    conf->user = NULL;
    conf->password = NULL;
    conf->exchange = NULL;
    conf->exchange_type = NULL;
    conf->queue = NULL;
    conf->routingkey = NULL;
    conf->delivery_mode = CAMQP_DM_VOLATILE;
    conf->store_rates = 0;
    conf->connection = NULL;
    pthread_mutex_init (&conf->lock, /* attr = */ NULL);
    /* }}} */

    status = cf_util_get_string (ci, &conf->name);
    if (status != 0)
    {
        sfree (conf);
        return (status);
    }

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp ("Host", child->key) == 0)
            status = cf_util_get_string (ci, &conf->host);
        else if (strcasecmp ("Port", child->key) == 0)
        {
            status = cf_util_get_port_number (child);
            if (status > 0)
            {
                conf->port = status;
                status = 0;
            }
        }
        else if (strcasecmp ("VHost", child->key) == 0)
            status = cf_util_get_string (ci, &conf->vhost);
        else if (strcasecmp ("User", child->key) == 0)
            status = cf_util_get_string (ci, &conf->user);
        else if (strcasecmp ("Password", child->key) == 0)
            status = cf_util_get_string (ci, &conf->password);
        else if (strcasecmp ("Exchange", child->key) == 0)
            status = cf_util_get_string (ci, &conf->exchange);
        else if ((strcasecmp ("ExchangeType", child->key) == 0) && !publish)
            status = cf_util_get_string (ci, &conf->exchange_type);
        else if ((strcasecmp ("Queue", child->key) == 0) && !publish)
            status = cf_util_get_string (ci, &conf->queue);
        else if (strcasecmp ("RoutingKey", child->key) == 0)
            status = cf_util_get_string (ci, &conf->routingkey);
        else if (strcasecmp ("Persistent", child->key) == 0)
        {
            _Bool tmp = 0;
            status = cf_util_get_boolean (ci, &tmp);
            if (tmp)
                conf->delivery_mode = CAMQP_DM_PERSISTENT;
            else
                conf->delivery_mode = CAMQP_DM_VOLATILE;
        }
        else if (strcasecmp ("StoreRates", child->key) == 0)
            status = cf_util_get_boolean (ci, &conf->store_rates);
        else
            WARNING ("amqp plugin: Ignoring unknown "
                    "configuration option \"%s\".", child->key);

        if (status != 0)
            break;
    } /* for (i = 0; i < ci->children_num; i++) */

    if ((status == 0) && !publish && (conf->exchange == NULL))
    {
        if (conf->routingkey != NULL)
            WARNING ("amqp plugin: The option \"RoutingKey\" was given "
                    "without the \"Exchange\" option. It will be ignored.");

        if (conf->exchange_type != NULL)
            WARNING ("amqp plugin: The option \"ExchangeType\" was given "
                    "without the \"Exchange\" option. It will be ignored.");
    }

    if (status != 0)
    {
        camqp_config_free (conf);
        return (status);
    }

    if (publish)
    {
        char cbname[128];
        user_data_t ud = { conf, camqp_config_free };

        ssnprintf (cbname, sizeof (cbname), "amqp/%s", conf->name);

        status = plugin_register_write (cbname, amqp_write, &ud);
        if (status != 0)
        {
            camqp_config_free (conf);
            return (status);
        }
    }

    return (0);
} /* }}} int camqp_config_connection */

static int camqp_config (oconfig_item_t *ci) /* {{{ */
{
    int i;

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp ("Publish", child->key) == 0)
            camqp_config_connection (child, /* publish = */ 1);
        else
            WARNING ("amqp plugin: Ignoring unknown config option \"%s\".",
                    child->key);
    } /* for (ci->children_num) */

    return (0);
} /* }}} int camqp_config */

static int shutdown (void) /* {{{ */
{
    /* FIXME: Set a global shutdown variable here. */
    return (0);
} /* }}} int shutdown */

void module_register (void)
{
    plugin_register_complex_config ("amqp", camqp_config);
    plugin_register_shutdown ("amqp", shutdown);
} /* void module_register */

/* vim: set sw=4 sts=4 et fdm=marker : */
