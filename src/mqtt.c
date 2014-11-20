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
static int mqtt_reconnect_broker (struct mqtt_client_conf *conf)
{
    int status;

    if (conf->connected)
        return (0);

    pthread_mutex_lock (&conf->lock);

    status = mosquitto_reconnect (conf->mosq);

    if (status != MOSQ_ERR_SUCCESS) {
        ERROR ("mqtt_connect_broker: mosquitto_connect failed: %s",
            (status == MOSQ_ERR_ERRNO ?
                strerror(errno) : mosquitto_strerror (status)));
        pthread_mutex_unlock (&conf->lock);
        return (-1);
    }

    conf->connected = true;

    c_release (LOG_INFO,
        &conf->complaint_cantpublish,
        "mqtt plugin: successfully reconnected to broker \"%s:%d\"",
        conf->host, conf->port);

    pthread_mutex_unlock (&conf->lock);

    return (0);
} /* mqtt_reconnect_broker */

static int mqtt_publish_message (struct mqtt_client_conf *conf, char *topic,
    void const *payload, size_t payload_len)
{
    char errbuf[1024];
    int status;

    status = mosquitto_publish(conf->mosq,
        /* message id */ NULL,
        topic,
        (int) payload_len,
        payload,
        /* qos */ 0,
        /* retain */ false);

    if (status != MOSQ_ERR_SUCCESS)
    {
        c_complain (LOG_ERR,
            &conf->complaint_cantpublish,
            "plugin mqtt: mosquitto_publish failed: %s",
            status == MOSQ_ERR_ERRNO ?
            sstrerror(errno, errbuf, sizeof (errbuf)) :
                mosquitto_strerror(status));
        /*
        Mark our connection "down" regardless of the error as a safety measure;
        we will try to reconnect the next time we have to publish a message
        */
        conf->connected = false;

        return (-1);
    }

    return (0);
} /* mqtt_publish_message */

static int mqtt_format_metric_value (char *buf, size_t buf_len,
    const data_set_t *data_set, const value_list_t *vl, int ds_num)
{
    gauge_t *rates = NULL;
    gauge_t *value = NULL;
    size_t metric_value_len;
    int status = 0;

    memset (buf, 0, buf_len);

    if (data_set->ds[ds_num].type == DS_TYPE_GAUGE)
        value = &vl->values[ds_num].gauge;
    else {
        rates = uc_get_rate (data_set, vl);
        value = &rates[ds_num];
    }

    metric_value_len = ssnprintf (buf, buf_len, "%f", *value);

    if (metric_value_len >= buf_len)
        return (-ENOMEM);

    if (rates)
        sfree (rates);

    return (status);
} /* mqtt_format_metric_value */

static int mqtt_format_message_topic (char *buf, size_t buf_len,
    char const *prefix, const value_list_t *vl, const char *ds_name)
{
    size_t topic_buf_len;

    memset (buf, 0, buf_len);

    /*
        MQTT message topic format:
        [<prefix>/]<hostname>/<plugin>/<plugin instance>/<type>/<type instance>/<ds>/
    */
    topic_buf_len = (size_t) ssnprintf (buf, buf_len,
        "%s/%s/%s/%s/%s/%s/%s",
        prefix,
        vl->host,
        vl->plugin,
        vl->plugin_instance[0] != '\0' ? vl->plugin_instance : "(null)",
        vl->type,
        vl->type_instance[0] != '\0' ? vl->type_instance : "(null)",
        ds_name);

    if (topic_buf_len >= buf_len)
    {
        ERROR ("mqtt_format_message_topic: topic buffer too small: "
                "Need %zu bytes.", topic_buf_len + 1);
        return (-ENOMEM);
    }

    return (0);
} /* mqtt_format_message_topic */

static int mqtt_format_payload (char *buf, size_t buf_len,
    const data_set_t *data_set, const value_list_t *vl, int ds_num)
{
    char metric_path[10 * DATA_MAX_NAME_LEN];
    char metric_value[512];
    size_t payload_buf_len;
    int status = 0;

    memset (buf, 0, buf_len);

    ssnprintf (metric_path, sizeof (metric_path),
        "%s.%s%s%s.%s%s%s%s%s",
        vl->host,
        vl->plugin,
        vl->plugin_instance[0] != '\0' ? "." : "",
        vl->plugin_instance[0] != '\0' ? vl->plugin_instance : "",
        vl->type,
        vl->type_instance[0] != '\0' ? "." : "",
        vl->type_instance[0] != '\0' ? vl->type_instance : "",
        strcmp(data_set->ds[ds_num].name, "value") != 0 ? "." : "",
        strcmp(data_set->ds[ds_num].name, "value") != 0 ?
            data_set->ds[ds_num].name : "");

    status = mqtt_format_metric_value (metric_value,
        sizeof (metric_value),
        data_set,
        vl,
        ds_num);

    if (status != 0)
    {
        ERROR ("mqtt_format_payload: error with mqtt_format_metric_value");
        return (status);
    }

    payload_buf_len = (size_t) ssnprintf (buf, buf_len,
        "%s %s %u",
        metric_path,
        metric_value,
        (unsigned int) CDTIME_T_TO_TIME_T (vl->time));

    if (payload_buf_len >= buf_len)
    {
        ERROR ("mqtt_format_payload: payload buffer too small: "
                "Need %zu bytes.", payload_buf_len + 1);
        return (-ENOMEM);
    }

    return (status);
} /* mqtt_format_payload */

static int mqtt_write (const data_set_t *data_set, const value_list_t *vl,
    user_data_t *user_data)
{
    struct mqtt_client_conf *conf;
    char msg_topic[MQTT_MAX_TOPIC_SIZE];
    char msg_payload[MQTT_MAX_MESSAGE_SIZE];
    int status = 0;
    int i;

    if (user_data == NULL)
        return (EINVAL);

    conf = user_data->data;

    if (!conf->connected)
    {
        status = mqtt_reconnect_broker (conf);

        if (status != 0) {
            ERROR ("plugin mqtt: unable to reconnect to broker");
            return (status);
        }
    }

    for (i = 0; i < data_set->ds_num; i++)
    {
        status = mqtt_format_message_topic (msg_topic, sizeof (msg_topic),
            conf->topic_prefix, vl, data_set->ds[i].name);
        if (status != 0)
        {
            ERROR ("plugin mqtt: error with mqtt_format_message_topic");
            return (status);
        }

        status = mqtt_format_payload (msg_payload,
            sizeof (msg_payload),
            data_set,
            vl,
            i);

        if (status != 0)
        {
            ERROR ("mqtt_write: error with mqtt_format_payload");
            return (status);
        }

        status = mqtt_publish_message (conf,
            msg_topic,
            msg_payload,
            sizeof (msg_payload));
        if (status != 0)
        {
            ERROR ("plugin mqtt: unable to publish message");
            return (status);
        }

        DEBUG ("\x1B[36m[debug]\x1B[0m\x1B[37m mqtt_write[%02X]\x1B[0m "
            "published message: topic=%s payload=%s",
            (unsigned)pthread_self(),
            msg_topic,
            msg_payload);
    }

    return (status);
} /* mqtt_write */

static int mqtt_config (oconfig_item_t *ci)
{
    struct mqtt_client_conf *conf;
    user_data_t user_data;
    char errbuf[1024];
    int status;

    DEBUG ("\x1B[36m[debug]\x1B[0m\x1B[37m mqtt_config[%02X]\x1B[0m ",
        (unsigned)pthread_self());

    conf = malloc (sizeof (*conf));
    if (conf == NULL)
    {
        ERROR ("write_mqtt plugin: malloc failed.");
        return (-1);
    }

    memset (conf, 0, sizeof (*conf));

    conf->connected = false;
    conf->host = MQTT_DEFAULT_HOST;
    conf->port = MQTT_DEFAULT_PORT;
    conf->client_id = MQTT_DEFAULT_CLIENT_ID;
    conf->topic_prefix = MQTT_DEFAULT_TOPIC_PREFIX;
    C_COMPLAIN_INIT (&conf->complaint_cantpublish);

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
    if (status != MOSQ_ERR_SUCCESS) {
        ERROR ("mqtt_config: mosquitto_connect failed: %s",
            (status == MOSQ_ERR_ERRNO ?
                sstrerror(errno, errbuf, sizeof (errbuf)) :
                mosquitto_strerror (status)));
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
