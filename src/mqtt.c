/**
 * collectd - src/mqtt.c
 * Copyright (C) 2014       Tim Loh
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
 * Based on the AMQP plugin.
 *
 * Authors:
 *   Tim Loh <tim.loh at outlook.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_cmd_putval.h"
#include "utils_format_json.h"
#include "utils_format_graphite.h"

#include <pthread.h>

#include "MQTTClient.h"

#define CMQTT_FORMAT_COMMAND    1
#define CMQTT_FORMAT_JSON       2
#define CMQTT_FORMAT_GRAPHITE   3

/*
 * Data types
 */
struct cmqtt_config_s
{
    _Bool   publish;
    char   *name;

    char   *host;
    int     port;
    char   *client_id;
    int     persistence_type;
    int     keep_alive_interval;
    char   *topic;
    int     qos;
    int     wait_timeout;
    int     disconnect_delay;
    char   *user;
    char   *password;

    /* publish only */
    _Bool   retained;
    _Bool   store_rates;
    int     format;

    /* publish & graphite format only */
    char   *prefix;
    char   *postfix;
    char    escape_char;
    unsigned int graphite_flags;

    /* subscribe only */

    MQTTClient connection;
    pthread_mutex_t lock;
};
typedef struct cmqtt_config_s cmqtt_config_t;

/*
 * Global variables
 */
static const char *def_host       = "localhost";
//static const char *def_user       = "guest";
//static const char *def_password   = "guest";

static pthread_t  *subscriber_threads     = NULL;
static size_t      subscriber_threads_num = 0;
static _Bool       subscriber_threads_running = 1;

#define CONF(c,f) (((c)->f != NULL) ? (c)->f : def_##f)

/*
 * Functions
 */
static void cmqtt_close_connection (cmqtt_config_t *conf) /* {{{ */
{
    if ((conf == NULL) || (conf->connection == NULL))
        return;

    MQTTClient_disconnect(conf->connection, conf->disconnect_delay);
    MQTTClient_destroy(&conf->connection);

    conf->connection = NULL;
} /* }}} void cmqtt_close_connection */

static void cmqtt_config_free (void *ptr) /* {{{ */
{
    cmqtt_config_t *conf = ptr;

    if (conf == NULL)
        return;

    cmqtt_close_connection (conf);

    sfree (conf->name);
    sfree (conf->host);
    sfree (conf->client_id);
    sfree (conf->topic);
    sfree (conf->user);
    sfree (conf->password);
    sfree (conf->prefix);
    sfree (conf->postfix);

    sfree (conf);
} /* }}} void cmqtt_config_free */

static int cmqtt_connect (cmqtt_config_t *conf) /* {{{ */
{
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    //MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
    char server_uri[270];
    int status;

    /* WARNING: Verify MQTTCLIENT_SUCCESS == 0 for every new version of Paho MQTT C Client Library */
    if (conf->connection != NULL)
        return (0);

    ssnprintf (server_uri, sizeof (server_uri), "tcp://%s:%d", conf->host, conf->port);

    status = MQTTClient_create(&conf->connection, server_uri, conf->client_id, conf->persistence_type, NULL);
    if (status != MQTTCLIENT_SUCCESS)
    {
        ERROR ("mqtt plugin: MQTTClient_create failed.");
        return (status);
    }

    conn_opts.keepAliveInterval = conf->keep_alive_interval;
    //conn_opts.reliable = 0;
    conn_opts.cleansession = 1;
    conn_opts.username = conf->user;
    conn_opts.password = conf->password;
    //ssl_opts.enableServerCertAuth = 0;
    //conn_opts.ssl = &ssl_opts;

    status = MQTTClient_connect(conf->connection, &conn_opts);
    if (status != MQTTCLIENT_SUCCESS)
    {
        ERROR ("mqtt plugin: MQTTClient_connect failed.");
        conf->connection = NULL;
        return (status);
    }

    if (!conf->publish)
    {
        status = MQTTClient_subscribe(conf->connection, conf->topic, conf->qos);
        if (status != MQTTCLIENT_SUCCESS)
        {
            ERROR ("mqtt plugin: MQTTClient_subscribe failed.");
            conf->connection = NULL;
            return (status);
        }
    }

    INFO ("mqtt plugin: Successfully opened connection to client \"%s\" "
            "on %s:%i.", conf->client_id, CONF(conf, host), conf->port);

    return (0);
} /* }}} int cmqtt_connect */

static int cmqtt_shutdown (void) /* {{{ */
{
    return (0);
} /* }}} int cmqtt_shutdown */

static void *cmqtt_subscribe_thread (void *user_data) /* {{{ */
{
    cmqtt_config_t *conf = user_data;
    int status;

    cdtime_t interval = plugin_get_interval ();

    while (subscriber_threads_running)
    {
        char *topicName = NULL;
        int topicLen;
        MQTTClient_message *message = NULL;

        status = cmqtt_connect (conf);
        if (status != 0)
        {
            struct timespec ts_interval;
            ERROR ("mqtt plugin: cmqtt_connect failed. "
                    "Will sleep for %.3f seconds.",
                    CDTIME_T_TO_DOUBLE (interval));
            CDTIME_T_TO_TIMESPEC (interval, &ts_interval);
            nanosleep (&ts_interval, /* remaining = */ NULL);
            continue;
        }

        status = MQTTClient_receive(conf->connection, &topicName, &topicLen, &message, conf->wait_timeout);
        if (message)
        {
            int rc = handle_putval (stderr, (char*)message->payload);
            if (rc != 0)
            {
                ERROR ("mqtt plugin: handle_putval failed with status %i.",
                       rc);
            }
            MQTTClient_freeMessage(&message);
            message = NULL;
        }
        if (topicName) {
            MQTTClient_free(topicName);
            topicName = NULL;
        }
        if (status != MQTTCLIENT_SUCCESS && status != MQTTCLIENT_TOPICNAME_TRUNCATED)
        {
            struct timespec ts_interval;
            ERROR ("mqtt plugin: MQTTClient_receive failed. "
                    "Will sleep for %.3f seconds.",
                    CDTIME_T_TO_DOUBLE (interval));
            cmqtt_close_connection (conf);
            CDTIME_T_TO_TIMESPEC (interval, &ts_interval);
            nanosleep (&ts_interval, /* remaining = */ NULL);
            continue;
        }
    } /* while (subscriber_threads_running) */

    cmqtt_config_free (conf);
    pthread_exit (NULL);
    return (NULL);
} /* }}} void *cmqtt_subscribe_thread */

static int cmqtt_subscribe_init (cmqtt_config_t *conf) /* {{{ */
{
    int status;
    pthread_t *tmp;

    tmp = realloc (subscriber_threads,
            sizeof (*subscriber_threads) * (subscriber_threads_num + 1));
    if (tmp == NULL)
    {
        ERROR ("mqtt plugin: realloc failed.");
        cmqtt_config_free (conf);
        return (ENOMEM);
    }
    subscriber_threads = tmp;
    tmp = subscriber_threads + subscriber_threads_num;
    memset (tmp, 0, sizeof (*tmp));

    status = plugin_thread_create (tmp, /* attr = */ NULL,
                                   cmqtt_subscribe_thread, conf);
    if (status != 0)
    {
        char errbuf[1024];
        ERROR ("mqtt plugin: pthread_create failed: %s",
               sstrerror (status, errbuf, sizeof (errbuf)));
        cmqtt_config_free (conf);
        return (status);
    }

    subscriber_threads_num++;

    return (0);
} /* }}} int cmqtt_subscribe_init */

/*
 * Publishing code
 */
/* XXX: You must hold "conf->lock" when calling this function! */
static int cmqtt_write_locked (cmqtt_config_t *conf, /* {{{ */
        const char *buffer, const char *topic)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int status;

    status = cmqtt_connect (conf);
    if (status != 0)
        return (status);

    pubmsg.payload = (char *)buffer;
    pubmsg.payloadlen = strlen(buffer);
    pubmsg.qos = conf->qos;
    pubmsg.retained = conf->retained;
    status = MQTTClient_publishMessage(conf->connection, conf->topic, &pubmsg, &token);
    if (status != MQTTCLIENT_SUCCESS)
    {
        ERROR ("mqtt plugin: MQTTClient_publishMessage failed with status %i.",
                status);
        cmqtt_close_connection (conf);
        return (status);
    }
    status = MQTTClient_waitForCompletion(conf->connection, token, conf->wait_timeout);
    if (status != MQTTCLIENT_SUCCESS)
    {
        ERROR ("mqtt plugin: MQTTClient_waitForCompletion failed with status %i.",
                status);
        cmqtt_close_connection (conf);
    }

    return (status);
} /* }}} int cmqtt_write_locked */

static int cmqtt_write (const data_set_t *ds, const value_list_t *vl, /* {{{ */
        user_data_t *user_data)
{
    cmqtt_config_t *conf = user_data->data;
    char topic[6 * DATA_MAX_NAME_LEN];
    char buffer[8192];
    int status;

    if ((ds == NULL) || (vl == NULL) || (conf == NULL))
        return (EINVAL);

    memset (buffer, 0, sizeof (buffer));

    if (conf->topic != NULL)
    {
        sstrncpy (topic, conf->topic, sizeof (topic));
    }
    else
    {
        ssnprintf (topic, sizeof (topic), "collectd/%s/%s/%s/%s/%s",
                vl->host,
                vl->plugin, vl->plugin_instance,
                vl->type, vl->type_instance);
    }

    if (conf->format == CMQTT_FORMAT_COMMAND)
    {
        status = create_putval (buffer, sizeof (buffer), ds, vl);
        if (status != 0)
        {
            ERROR ("mqtt plugin: create_putval failed with status %i.",
                    status);
            return (status);
        }
    }
    else if (conf->format == CMQTT_FORMAT_JSON)
    {
        size_t bfree = sizeof (buffer);
        size_t bfill = 0;

        format_json_initialize (buffer, &bfill, &bfree);
        format_json_value_list (buffer, &bfill, &bfree, ds, vl, conf->store_rates);
        format_json_finalize (buffer, &bfill, &bfree);
    }
    else if (conf->format == CMQTT_FORMAT_GRAPHITE)
    {
        status = format_graphite (buffer, sizeof (buffer), ds, vl,
                    conf->prefix, conf->postfix, conf->escape_char,
                    conf->graphite_flags);
        if (status != 0)
        {
            ERROR ("mqtt plugin: format_graphite failed with status %i.",
                    status);
            return (status);
        }
    }
    else
    {
        ERROR ("mqtt plugin: Invalid format (%i).", conf->format);
        return (-1);
    }

    pthread_mutex_lock (&conf->lock);
    status = cmqtt_write_locked (conf, buffer, topic);
    pthread_mutex_unlock (&conf->lock);

    return (status);
} /* }}} int cmqtt_write */

/*
 * Config handling
 */
static int cmqtt_config_set_format (oconfig_item_t *ci, /* {{{ */
        cmqtt_config_t *conf)
{
    char *string;
    int status;

    string = NULL;
    status = cf_util_get_string (ci, &string);
    if (status != 0)
        return (status);

    assert (string != NULL);
    if (strcasecmp ("Command", string) == 0)
        conf->format = CMQTT_FORMAT_COMMAND;
    else if (strcasecmp ("JSON", string) == 0)
        conf->format = CMQTT_FORMAT_JSON;
    else if (strcasecmp ("Graphite", string) == 0)
        conf->format = CMQTT_FORMAT_GRAPHITE;
    else
    {
        WARNING ("mqtt plugin: Invalid format string: %s",
                string);
    }

    free (string);

    return (0);
} /* }}} int cmqtt_config_set_format */

static int cmqtt_config_set_persistence_type (oconfig_item_t *ci, /* {{{ */
        cmqtt_config_t *conf)
{
    char *string;
    int status;

    string = NULL;
    status = cf_util_get_string (ci, &string);
    if (status != 0)
        return (status);

    assert (string != NULL);
    if (strcasecmp ("Default", string) == 0)
        conf->persistence_type = MQTTCLIENT_PERSISTENCE_DEFAULT;
    else if (strcasecmp ("None", string) == 0)
        conf->persistence_type = MQTTCLIENT_PERSISTENCE_NONE;
    else
    {
        WARNING ("mqtt plugin: Invalid persistence type string: %s",
                string);
    }

    free (string);

    return (0);
} /* }}} int cmqtt_config_set_persistence_type */

static int cmqtt_config_connection (oconfig_item_t *ci, /* {{{ */
        _Bool publish)
{
    cmqtt_config_t *conf;
    int status;
    int i;

    conf = malloc (sizeof (*conf));
    if (conf == NULL)
    {
        ERROR ("mqtt plugin: malloc failed.");
        return (ENOMEM);
    }

    /* Initialize "conf" {{{ */
    memset (conf, 0, sizeof (*conf));
    conf->publish = publish;
    conf->name = NULL;
    conf->host = NULL;
    conf->port = 1883;
    conf->client_id = NULL;
    conf->persistence_type = MQTTCLIENT_PERSISTENCE_NONE;
    conf->keep_alive_interval = 20;
    conf->topic = NULL;
    conf->qos = 1;
    conf->wait_timeout = 10000;
    conf->disconnect_delay = 10000;
    conf->user = NULL;
    conf->password = NULL;

    /* publish only */
    conf->retained = 0;
    conf->store_rates = 0;
    conf->format = CMQTT_FORMAT_COMMAND;

    /* publish & graphite only */
    conf->prefix = NULL;
    conf->postfix = NULL;
    conf->escape_char = '_';
    conf->graphite_flags = 0;

    /* subscribe only */

    /* general */
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
            status = cf_util_get_string (child, &conf->host);
        else if (strcasecmp ("Port", child->key) == 0)
        {
            status = cf_util_get_port_number (child);
            if (status > 0)
            {
                conf->port = status;
                status = 0;
            }
        }
        else if (strcasecmp ("ClientID", child->key) == 0)
            status = cf_util_get_string (child, &conf->client_id);
        else if (strcasecmp ("PersistenceType", child->key) == 0)
            status = cmqtt_config_set_persistence_type (child, conf);
        else if (strcasecmp ("KeepAliveInterval", child->key) == 0)
            status = cf_util_get_int (child, &conf->keep_alive_interval);
        else if (strcasecmp ("Topic", child->key) == 0)
            status = cf_util_get_string (child, &conf->topic);
        else if ((strcasecmp ("QoS", child->key) == 0))
            status = cf_util_get_int (child, &conf->qos);
        else if ((strcasecmp ("Retained", child->key) == 0) && publish)
            status = cf_util_get_boolean (child, &conf->retained);
        else if (strcasecmp ("WaitTimeout", child->key) == 0)
            status = cf_util_get_int (child, &conf->wait_timeout);
        else if (strcasecmp ("DisconnectDelay", child->key) == 0)
            status = cf_util_get_int (child, &conf->disconnect_delay);
        else if (strcasecmp ("User", child->key) == 0)
            status = cf_util_get_string (child, &conf->user);
        else if (strcasecmp ("Password", child->key) == 0)
            status = cf_util_get_string (child, &conf->password);
        else if ((strcasecmp ("StoreRates", child->key) == 0) && publish)
        {
            status = cf_util_get_boolean (child, &conf->store_rates);
            (void) cf_util_get_flag (child, &conf->graphite_flags,
                    GRAPHITE_STORE_RATES);
        }
        else if ((strcasecmp ("Format", child->key) == 0) && publish)
            status = cmqtt_config_set_format (child, conf);
        else if ((strcasecmp ("GraphiteSeparateInstances", child->key) == 0) && publish)
            status = cf_util_get_flag (child, &conf->graphite_flags,
                    GRAPHITE_SEPARATE_INSTANCES);
        else if ((strcasecmp ("GraphiteAlwaysAppendDS", child->key) == 0) && publish)
            status = cf_util_get_flag (child, &conf->graphite_flags,
                    GRAPHITE_ALWAYS_APPEND_DS);
        else if ((strcasecmp ("GraphitePrefix", child->key) == 0) && publish)
            status = cf_util_get_string (child, &conf->prefix);
        else if ((strcasecmp ("GraphitePostfix", child->key) == 0) && publish)
            status = cf_util_get_string (child, &conf->postfix);
        else if ((strcasecmp ("GraphiteEscapeChar", child->key) == 0) && publish)
        {
            char *tmp_buff = NULL;
            status = cf_util_get_string (child, &tmp_buff);
            if (strlen (tmp_buff) > 1)
                WARNING ("mqtt plugin: The option \"GraphiteEscapeChar\" handles "
                        "only one character. Others will be ignored.");
            conf->escape_char = tmp_buff[0];
            sfree (tmp_buff);
        }
        else
            WARNING ("mqtt plugin: Ignoring unknown "
                    "configuration option \"%s\".", child->key);

        if (status != 0)
            break;
    } /* for (i = 0; i < ci->children_num; i++) */

    if (status != 0)
    {
        cmqtt_config_free (conf);
        return (status);
    }

    if (publish)
    {
        char cbname[128];
        user_data_t ud = { conf, cmqtt_config_free };

        ssnprintf (cbname, sizeof (cbname), "mqtt/%s", conf->name);

        status = plugin_register_write (cbname, cmqtt_write, &ud);
        if (status != 0)
        {
            cmqtt_config_free (conf);
            return (status);
        }
    }
    else
    {
        status = cmqtt_subscribe_init (conf);
        if (status != 0)
        {
            cmqtt_config_free (conf);
            return (status);
        }
    }

    return (0);
} /* }}} int cmqtt_config_connection */

static int cmqtt_config (oconfig_item_t *ci) /* {{{ */
{
    int i;

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp ("Publish", child->key) == 0)
            cmqtt_config_connection (child, /* publish = */ 1);
        else if (strcasecmp ("Subscribe", child->key) == 0)
            cmqtt_config_connection (child, /* publish = */ 0);
        else
            WARNING ("mqtt plugin: Ignoring unknown config option \"%s\".",
                    child->key);
    } /* for (ci->children_num) */

    return (0);
} /* }}} int cmqtt_config */

void module_register (void)
{
    plugin_register_complex_config ("mqtt", cmqtt_config);
    plugin_register_shutdown ("mqtt", cmqtt_shutdown);
} /* void module_register */

/* vim: set sw=4 sts=4 et fdm=marker : */
