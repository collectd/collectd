/**
 * collectd - src/mqtt.c
 * Copyright (C) 2014       Marc Falzon <marc at baha dot mu>
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
 **/

// Reference: http://mosquitto.org/api/files/mosquitto-h.html


#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_cache.h"
#include "utils_complain.h"

#include <pthread.h>

#include <mosquitto.h>

#define MQTT_MAX_TOPIC_SIZE         1024
#define MQTT_MAX_MESSAGE_SIZE       MQTT_MAX_TOPIC_SIZE + 1024
#define MQTT_DEFAULT_HOST           "localhost"
#define MQTT_DEFAULT_PORT           1883
#define MQTT_DEFAULT_CLIENT_ID      "collectd"
#define MQTT_DEFAULT_TOPIC_PREFIX   "collectd"

/*
 * Data types
 */
struct mqtt_client_conf
{
    struct mosquitto    *mosq;
    bool                connected;
    char                *host;
    int                 port;
    char                *client_id;
    char                *topic_prefix;
    c_complain_t        complaint_cantpublish;
    pthread_mutex_t     lock;
};
typedef struct mqtt_client_conf mqtt_client_conf_t;

static char const *mosquitto_strerror (int code)
{
    switch (code)
    {
        case MOSQ_ERR_SUCCESS: return "MOSQ_ERR_SUCCESS";
        case MOSQ_ERR_NOMEM: return "MOSQ_ERR_NOMEM";
        case MOSQ_ERR_PROTOCOL: return "MOSQ_ERR_PROTOCOL";
        case MOSQ_ERR_INVAL: return "MOSQ_ERR_INVAL";
        case MOSQ_ERR_NO_CONN: return "MOSQ_ERR_NO_CONN";
        case MOSQ_ERR_CONN_REFUSED: return "MOSQ_ERR_CONN_REFUSED";
        case MOSQ_ERR_NOT_FOUND: return "MOSQ_ERR_NOT_FOUND";
        case MOSQ_ERR_CONN_LOST: return "MOSQ_ERR_CONN_LOST";
        case MOSQ_ERR_SSL: return "MOSQ_ERR_SSL";
        case MOSQ_ERR_PAYLOAD_SIZE: return "MOSQ_ERR_PAYLOAD_SIZE";
        case MOSQ_ERR_NOT_SUPPORTED: return "MOSQ_ERR_NOT_SUPPORTED";
        case MOSQ_ERR_AUTH: return "MOSQ_ERR_AUTH";
        case MOSQ_ERR_ACL_DENIED: return "MOSQ_ERR_ACL_DENIED";
        case MOSQ_ERR_UNKNOWN: return "MOSQ_ERR_UNKNOWN";
        case MOSQ_ERR_ERRNO: return "MOSQ_ERR_ERRNO";
    }

    return "UNKNOWN ERROR CODE";
}

/*
 * Functions
 */
/* must hold conf->lock when calling. */
static int mqtt_reconnect_broker (mqtt_client_conf_t *conf)
{
    int status;

    if (conf->connected)
        return (0);

    status = mosquitto_reconnect (conf->mosq);
    if (status != MOSQ_ERR_SUCCESS)
    {
        ERROR ("mqtt_connect_broker: mosquitto_connect failed: %s",
            (status == MOSQ_ERR_ERRNO ?
                strerror(errno) : mosquitto_strerror (status)));
        return (-1);
    }

    conf->connected = true;

    c_release (LOG_INFO,
        &conf->complaint_cantpublish,
        "mqtt plugin: successfully reconnected to broker \"%s:%d\"",
        conf->host, conf->port);

    return (0);
} /* mqtt_reconnect_broker */

static int publish (mqtt_client_conf_t *conf, char const *topic,
    void const *payload, size_t payload_len)
{
    int const qos = 0; /* TODO: Config option */
    int status;

    pthread_mutex_lock (&conf->lock);

    status = mqtt_reconnect_broker (conf);
    if (status != 0) {
        pthread_mutex_unlock (&conf->lock);
        ERROR ("mqtt plugin: unable to reconnect to broker");
        return (status);
    }

    status = mosquitto_publish(conf->mosq,
            /* message id */ NULL,
            topic,
            (uint32_t) payload_len, payload,
            /* qos */ qos,
            /* retain */ false);
    if (status != MOSQ_ERR_SUCCESS)
    {
        char errbuf[1024];
        c_complain (LOG_ERR,
                &conf->complaint_cantpublish,
                "plugin mqtt: mosquitto_publish failed: %s",
                status == MOSQ_ERR_ERRNO ?
                sstrerror(errno, errbuf, sizeof (errbuf)) :
                mosquitto_strerror(status));
        /* Mark our connection "down" regardless of the error as a safety
         * measure; we will try to reconnect the next time we have to publish a
         * message */
        conf->connected = false;

        pthread_mutex_unlock (&conf->lock);
        return (-1);
    }

    pthread_mutex_unlock (&conf->lock);
    return (0);
} /* int publish */

static int format_topic (char *buf, size_t buf_len,
    data_set_t const *ds, value_list_t const *vl,
    mqtt_client_conf_t *conf)
{
    char name[MQTT_MAX_TOPIC_SIZE];
    int status;

    if ((conf->topic_prefix == NULL) || (conf->topic_prefix[0] == 0))
        return (FORMAT_VL (buf, buf_len, vl));

    status = FORMAT_VL (name, sizeof (name), vl);
    if (status != 0)
        return (status);

    status = ssnprintf (buf, buf_len, "%s/%s", conf->topic_prefix, name);
    if ((status < 0) || (((size_t) status) >= buf_len))
        return (ENOMEM);

    return (0);
} /* int format_topic */

static int mqtt_write (const data_set_t *ds, const value_list_t *vl,
    user_data_t *user_data)
{
    mqtt_client_conf_t *conf;
    char topic[MQTT_MAX_TOPIC_SIZE];
    char payload[MQTT_MAX_MESSAGE_SIZE];
    int status = 0;
    _Bool const store_rates = 0; /* TODO: Config option */

    if ((user_data == NULL) || (user_data->data == NULL))
        return (EINVAL);
    conf = user_data->data;

    status = format_topic (topic, sizeof (topic), ds, vl, conf);
    {
        ERROR ("mqtt plugin: format_topic failed with status %d.", status);
        return (status);
    }

    status = format_values (payload, sizeof (payload),
            ds, vl, store_rates);
    if (status != 0)
    {
        ERROR ("mqtt plugin: format_values failed with status %d.", status);
        return (status);
    }

    status = publish (conf, topic, payload, sizeof (payload));
    if (status != 0)
    {
        ERROR ("mqtt plugin: publish failed: %s", mosquitto_strerror (status));
        return (status);
    }

    return (status);
} /* mqtt_write */

/*
 * <Plugin mqtt>
 *   Host "example.com"
 *   Port 1883
 *   Prefix "collectd"
 *   ClientId "collectd"
 * </Plugin>
 */
static int mqtt_config (oconfig_item_t *ci)
{
    mqtt_client_conf_t *conf;
    user_data_t user_data;
    int status;
    int i;

    conf = calloc (1, sizeof (*conf));
    if (conf == NULL)
    {
        ERROR ("mqtt plugin: malloc failed.");
        return (-1);
    }

    conf->connected = false;
    conf->host = strdup (MQTT_DEFAULT_HOST);
    conf->port = MQTT_DEFAULT_PORT;
    conf->client_id = strdup (MQTT_DEFAULT_CLIENT_ID);
    conf->topic_prefix = strdup (MQTT_DEFAULT_TOPIC_PREFIX);
    C_COMPLAIN_INIT (&conf->complaint_cantpublish);

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;
        if (strcasecmp ("Host", child->key) == 0)
            cf_util_get_string (child, &conf->host);
        else if (strcasecmp ("Port", child->key) == 0)
        {
            int tmp = cf_util_get_port_number (child);
            if (tmp < 0)
            {
                ERROR ("mqtt plugin: Invalid port number.");
                continue;
            }
            conf->port = tmp;
        }
        else if (strcasecmp ("Prefix", child->key) == 0)
            cf_util_get_string (child, &conf->topic_prefix);
        else if (strcasecmp ("ClientId", child->key) == 0)
            cf_util_get_string (child, &conf->client_id);
        else
            ERROR ("mqtt plugin: Unknown config option: %s", child->key);
    }

    memset (&user_data, 0, sizeof (user_data));
    user_data.data = conf;

    conf->mosq = mosquitto_new (conf->client_id, /* user data = */ conf);
    if (conf->mosq == NULL)
    {
        ERROR ("mqtt plugin: mosquitto_new failed");
        free (conf);
        return (-1);
    }

    status = mosquitto_connect (conf->mosq, conf->host, conf->port,
            /* keepalive = */ 10, /* clean session = */ 1);
    if (status != MOSQ_ERR_SUCCESS)
    {
        char errbuf[1024];
        ERROR ("mqtt plugin: mosquitto_connect failed: %s",
                (status == MOSQ_ERR_ERRNO)
                ? sstrerror (errno, errbuf, sizeof (errbuf))
                : mosquitto_strerror (status));
        free (conf);
        return (-1);
    }

    DEBUG ("mqtt plugin: successfully connected to broker \"%s:%d\"",
        conf->host, conf->port);

    conf->connected = true;

    plugin_register_write ("mqtt", mqtt_write, &user_data);

    return (0);
} /* mqtt_config */

static int mqtt_init (void)
{
    mosquitto_lib_init();

    return (0);
} /* mqtt_init */

void module_register (void)
{
    plugin_register_complex_config ("mqtt", mqtt_config);
    plugin_register_init ("mqtt", mqtt_init);
} /* void module_register */

/* vim: set sw=4 sts=4 et fdm=marker : */
