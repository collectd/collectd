/**
 * collectd - src/mqtt.c
 * Copyright (C) 2014       Marc Falzon
 * Copyright (C) 2014,2015  Florian octo Forster
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
 *   Marc Falzon <marc at baha dot mu>
 *   Florian octo Forster <octo at collectd.org>
 *   Jan-Piet Mens <jpmens at gmail.com>
 **/

// Reference: http://mosquitto.org/api/files/mosquitto-h.html

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_complain.h"

#include <mosquitto.h>

#define MQTT_MAX_TOPIC_SIZE 1024
#define MQTT_MAX_MESSAGE_SIZE MQTT_MAX_TOPIC_SIZE + 1024
#define MQTT_DEFAULT_HOST "localhost"
#define MQTT_DEFAULT_PORT 1883
#define MQTT_DEFAULT_TOPIC_PREFIX "collectd"
#define MQTT_DEFAULT_TOPIC "collectd/#"
#ifndef MQTT_KEEPALIVE
#define MQTT_KEEPALIVE 60
#endif
#ifndef SSL_VERIFY_PEER
#define SSL_VERIFY_PEER 1
#endif

/*
 * Data types
 */
struct mqtt_client_conf {
  _Bool publish;
  char *name;

  struct mosquitto *mosq;
  _Bool connected;

  char *host;
  int port;
  char *client_id;
  char *username;
  char *password;
  int qos;
  char *cacertificatefile;
  char *certificatefile;
  char *certificatekeyfile;
  char *tlsprotocol;
  char *ciphersuite;

  /* For publishing */
  char *topic_prefix;
  _Bool store_rates;
  _Bool retain;

  /* For subscribing */
  pthread_t thread;
  _Bool loop;
  char *topic;
  _Bool clean_session;

  c_complain_t complaint_cantpublish;
  pthread_mutex_t lock;
};
typedef struct mqtt_client_conf mqtt_client_conf_t;

static mqtt_client_conf_t **subscribers = NULL;
static size_t subscribers_num = 0;

/*
 * Functions
 */
#if LIBMOSQUITTO_MAJOR == 0
static char const *mosquitto_strerror(int code) {
  switch (code) {
  case MOSQ_ERR_SUCCESS:
    return "MOSQ_ERR_SUCCESS";
  case MOSQ_ERR_NOMEM:
    return "MOSQ_ERR_NOMEM";
  case MOSQ_ERR_PROTOCOL:
    return "MOSQ_ERR_PROTOCOL";
  case MOSQ_ERR_INVAL:
    return "MOSQ_ERR_INVAL";
  case MOSQ_ERR_NO_CONN:
    return "MOSQ_ERR_NO_CONN";
  case MOSQ_ERR_CONN_REFUSED:
    return "MOSQ_ERR_CONN_REFUSED";
  case MOSQ_ERR_NOT_FOUND:
    return "MOSQ_ERR_NOT_FOUND";
  case MOSQ_ERR_CONN_LOST:
    return "MOSQ_ERR_CONN_LOST";
  case MOSQ_ERR_SSL:
    return "MOSQ_ERR_SSL";
  case MOSQ_ERR_PAYLOAD_SIZE:
    return "MOSQ_ERR_PAYLOAD_SIZE";
  case MOSQ_ERR_NOT_SUPPORTED:
    return "MOSQ_ERR_NOT_SUPPORTED";
  case MOSQ_ERR_AUTH:
    return "MOSQ_ERR_AUTH";
  case MOSQ_ERR_ACL_DENIED:
    return "MOSQ_ERR_ACL_DENIED";
  case MOSQ_ERR_UNKNOWN:
    return "MOSQ_ERR_UNKNOWN";
  case MOSQ_ERR_ERRNO:
    return "MOSQ_ERR_ERRNO";
  }

  return "UNKNOWN ERROR CODE";
}
#else
/* provided by libmosquitto */
#endif

static void mqtt_free(mqtt_client_conf_t *conf) {
  if (conf == NULL)
    return;

  if (conf->connected)
    (void)mosquitto_disconnect(conf->mosq);
  conf->connected = 0;
  (void)mosquitto_destroy(conf->mosq);

  sfree(conf->host);
  sfree(conf->username);
  sfree(conf->password);
  sfree(conf->client_id);
  sfree(conf->topic_prefix);
  sfree(conf);
}

static char *strip_prefix(char *topic) {
  size_t num = 0;

  for (size_t i = 0; topic[i] != 0; i++)
    if (topic[i] == '/')
      num++;

  if (num < 2)
    return NULL;

  while (num > 2) {
    char *tmp = strchr(topic, '/');
    if (tmp == NULL)
      return NULL;
    topic = tmp + 1;
    num--;
  }

  return topic;
}

static void on_message(
#if LIBMOSQUITTO_MAJOR == 0
#else
    __attribute__((unused)) struct mosquitto *m,
#endif
    __attribute__((unused)) void *arg, const struct mosquitto_message *msg) {
  value_list_t vl = VALUE_LIST_INIT;
  data_set_t const *ds;
  char *topic;
  char *name;
  char *payload;
  int status;

  if (msg->payloadlen <= 0) {
    DEBUG("mqtt plugin: message has empty payload");
    return;
  }

  topic = strdup(msg->topic);
  name = strip_prefix(topic);

  status = parse_identifier_vl(name, &vl);
  if (status != 0) {
    ERROR("mqtt plugin: Unable to parse topic \"%s\".", topic);
    sfree(topic);
    return;
  }
  sfree(topic);

  ds = plugin_get_ds(vl.type);
  if (ds == NULL) {
    ERROR("mqtt plugin: Unknown type: \"%s\".", vl.type);
    return;
  }

  vl.values = calloc(ds->ds_num, sizeof(*vl.values));
  if (vl.values == NULL) {
    ERROR("mqtt plugin: calloc failed.");
    return;
  }
  vl.values_len = ds->ds_num;

  payload = malloc(msg->payloadlen + 1);
  if (payload == NULL) {
    ERROR("mqtt plugin: malloc for payload buffer failed.");
    sfree(vl.values);
    return;
  }
  memmove(payload, msg->payload, msg->payloadlen);
  payload[msg->payloadlen] = 0;

  DEBUG("mqtt plugin: payload = \"%s\"", payload);
  status = parse_values(payload, &vl, ds);
  if (status != 0) {
    ERROR("mqtt plugin: Unable to parse payload \"%s\".", payload);
    sfree(payload);
    sfree(vl.values);
    return;
  }
  sfree(payload);

  plugin_dispatch_values(&vl);
  sfree(vl.values);
} /* void on_message */

/* must hold conf->lock when calling. */
static int mqtt_reconnect(mqtt_client_conf_t *conf) {
  int status;

  if (conf->connected)
    return 0;

  status = mosquitto_reconnect(conf->mosq);
  if (status != MOSQ_ERR_SUCCESS) {
    ERROR("mqtt_connect_broker: mosquitto_connect failed: %s",
          (status == MOSQ_ERR_ERRNO) ? STRERRNO : mosquitto_strerror(status));
    return -1;
  }

  conf->connected = 1;

  c_release(LOG_INFO, &conf->complaint_cantpublish,
            "mqtt plugin: successfully reconnected to broker \"%s:%d\"",
            conf->host, conf->port);

  return 0;
} /* mqtt_reconnect */

/* must hold conf->lock when calling. */
static int mqtt_connect(mqtt_client_conf_t *conf) {
  char const *client_id;
  int status;

  if (conf->mosq != NULL)
    return mqtt_reconnect(conf);

  if (conf->client_id)
    client_id = conf->client_id;
  else
    client_id = hostname_g;

#if LIBMOSQUITTO_MAJOR == 0
  conf->mosq = mosquitto_new(client_id, /* user data = */ conf);
#else
  conf->mosq =
      mosquitto_new(client_id, conf->clean_session, /* user data = */ conf);
#endif
  if (conf->mosq == NULL) {
    ERROR("mqtt plugin: mosquitto_new failed");
    return -1;
  }

#if LIBMOSQUITTO_MAJOR != 0
  if (conf->cacertificatefile) {
    status = mosquitto_tls_set(conf->mosq, conf->cacertificatefile, NULL,
                               conf->certificatefile, conf->certificatekeyfile,
                               /* pw_callback */ NULL);
    if (status != MOSQ_ERR_SUCCESS) {
      ERROR("mqtt plugin: cannot mosquitto_tls_set: %s",
            mosquitto_strerror(status));
      mosquitto_destroy(conf->mosq);
      conf->mosq = NULL;
      return -1;
    }

    status = mosquitto_tls_opts_set(conf->mosq, SSL_VERIFY_PEER,
                                    conf->tlsprotocol, conf->ciphersuite);
    if (status != MOSQ_ERR_SUCCESS) {
      ERROR("mqtt plugin: cannot mosquitto_tls_opts_set: %s",
            mosquitto_strerror(status));
      mosquitto_destroy(conf->mosq);
      conf->mosq = NULL;
      return -1;
    }

    status = mosquitto_tls_insecure_set(conf->mosq, false);
    if (status != MOSQ_ERR_SUCCESS) {
      ERROR("mqtt plugin: cannot mosquitto_tls_insecure_set: %s",
            mosquitto_strerror(status));
      mosquitto_destroy(conf->mosq);
      conf->mosq = NULL;
      return -1;
    }
  }
#endif

  if (conf->username && conf->password) {
    status =
        mosquitto_username_pw_set(conf->mosq, conf->username, conf->password);
    if (status != MOSQ_ERR_SUCCESS) {
      ERROR("mqtt plugin: mosquitto_username_pw_set failed: %s",
            (status == MOSQ_ERR_ERRNO) ? STRERRNO : mosquitto_strerror(status));

      mosquitto_destroy(conf->mosq);
      conf->mosq = NULL;
      return -1;
    }
  }

#if LIBMOSQUITTO_MAJOR == 0
  status = mosquitto_connect(conf->mosq, conf->host, conf->port,
                             /* keepalive = */ MQTT_KEEPALIVE,
                             /* clean session = */ conf->clean_session);
#else
  status =
      mosquitto_connect(conf->mosq, conf->host, conf->port, MQTT_KEEPALIVE);
#endif
  if (status != MOSQ_ERR_SUCCESS) {
    ERROR("mqtt plugin: mosquitto_connect failed: %s",
          (status == MOSQ_ERR_ERRNO) ? STRERRNO : mosquitto_strerror(status));

    mosquitto_destroy(conf->mosq);
    conf->mosq = NULL;
    return -1;
  }

  if (!conf->publish) {
    mosquitto_message_callback_set(conf->mosq, on_message);

    status =
        mosquitto_subscribe(conf->mosq,
                            /* message_id = */ NULL, conf->topic, conf->qos);
    if (status != MOSQ_ERR_SUCCESS) {
      ERROR("mqtt plugin: Subscribing to \"%s\" failed: %s", conf->topic,
            mosquitto_strerror(status));

      mosquitto_disconnect(conf->mosq);
      mosquitto_destroy(conf->mosq);
      conf->mosq = NULL;
      return -1;
    }
  }

  conf->connected = 1;
  return 0;
} /* mqtt_connect */

static void *subscribers_thread(void *arg) {
  mqtt_client_conf_t *conf = arg;
  int status;

  conf->loop = 1;

  while (conf->loop) {
    status = mqtt_connect(conf);
    if (status != 0) {
      sleep(1);
      continue;
    }

/* The documentation says "0" would map to the default (1000ms), but
 * that does not work on some versions. */
#if LIBMOSQUITTO_MAJOR == 0
    status = mosquitto_loop(conf->mosq, /* timeout = */ 1000 /* ms */);
#else
    status = mosquitto_loop(conf->mosq,
                            /* timeout[ms] = */ 1000,
                            /* max_packets = */ 100);
#endif
    if (status == MOSQ_ERR_CONN_LOST) {
      conf->connected = 0;
      continue;
    } else if (status != MOSQ_ERR_SUCCESS) {
      ERROR("mqtt plugin: mosquitto_loop failed: %s",
            mosquitto_strerror(status));
      mosquitto_destroy(conf->mosq);
      conf->mosq = NULL;
      conf->connected = 0;
      continue;
    }

    DEBUG("mqtt plugin: mosquitto_loop succeeded.");
  } /* while (conf->loop) */

  pthread_exit(0);
} /* void *subscribers_thread */

static int publish(mqtt_client_conf_t *conf, char const *topic,
                   void const *payload, size_t payload_len) {
  int status;

  pthread_mutex_lock(&conf->lock);

  status = mqtt_connect(conf);
  if (status != 0) {
    pthread_mutex_unlock(&conf->lock);
    ERROR("mqtt plugin: unable to reconnect to broker");
    return status;
  }

  status = mosquitto_publish(conf->mosq, /* message_id */ NULL, topic,
#if LIBMOSQUITTO_MAJOR == 0
                             (uint32_t)payload_len, payload,
#else
                             (int)payload_len, payload,
#endif
                             conf->qos, conf->retain);
  if (status != MOSQ_ERR_SUCCESS) {
    c_complain(LOG_ERR, &conf->complaint_cantpublish,
               "mqtt plugin: mosquitto_publish failed: %s",
               (status == MOSQ_ERR_ERRNO) ? STRERRNO
                                          : mosquitto_strerror(status));
    /* Mark our connection "down" regardless of the error as a safety
     * measure; we will try to reconnect the next time we have to publish a
     * message */
    conf->connected = 0;
    mosquitto_disconnect(conf->mosq);

    pthread_mutex_unlock(&conf->lock);
    return -1;
  }

  pthread_mutex_unlock(&conf->lock);
  return 0;
} /* int publish */

static int format_topic(char *buf, size_t buf_len, data_set_t const *ds,
                        value_list_t const *vl, mqtt_client_conf_t *conf) {
  char name[MQTT_MAX_TOPIC_SIZE];
  int status;
  char *c;

  if ((conf->topic_prefix == NULL) || (conf->topic_prefix[0] == 0))
    return FORMAT_VL(buf, buf_len, vl);

  status = FORMAT_VL(name, sizeof(name), vl);
  if (status != 0)
    return status;

  status = snprintf(buf, buf_len, "%s/%s", conf->topic_prefix, name);
  if ((status < 0) || (((size_t)status) >= buf_len))
    return ENOMEM;

  while ((c = strchr(buf, '#')) || (c = strchr(buf, '+'))) {
    *c = '_';
  }

  return 0;
} /* int format_topic */

static int mqtt_write(const data_set_t *ds, const value_list_t *vl,
                      user_data_t *user_data) {
  mqtt_client_conf_t *conf;
  char topic[MQTT_MAX_TOPIC_SIZE];
  char payload[MQTT_MAX_MESSAGE_SIZE];
  int status = 0;

  if ((user_data == NULL) || (user_data->data == NULL))
    return EINVAL;
  conf = user_data->data;

  status = format_topic(topic, sizeof(topic), ds, vl, conf);
  if (status != 0) {
    ERROR("mqtt plugin: format_topic failed with status %d.", status);
    return status;
  }

  status = format_values(payload, sizeof(payload), ds, vl, conf->store_rates);
  if (status != 0) {
    ERROR("mqtt plugin: format_values failed with status %d.", status);
    return status;
  }

  status = publish(conf, topic, payload, strlen(payload) + 1);
  if (status != 0) {
    ERROR("mqtt plugin: publish failed: %s", mosquitto_strerror(status));
    return status;
  }

  return status;
} /* mqtt_write */

/*
 * <Publish "name">
 *   Host "example.com"
 *   Port 1883
 *   ClientId "collectd"
 *   User "guest"
 *   Password "secret"
 *   Prefix "collectd"
 *   StoreRates true
 *   Retain false
 *   QoS 0
 *   CACert "ca.pem"                      Enables TLS if set
 *   CertificateFile "client-cert.pem"	  optional
 *   CertificateKeyFile "client-key.pem"  optional
 *   TLSProtocol "tlsv1.2"                optional
 * </Publish>
 */
static int mqtt_config_publisher(oconfig_item_t *ci) {
  mqtt_client_conf_t *conf;
  char cb_name[1024];
  int status;

  conf = calloc(1, sizeof(*conf));
  if (conf == NULL) {
    ERROR("mqtt plugin: calloc failed.");
    return -1;
  }
  conf->publish = 1;

  conf->name = NULL;
  status = cf_util_get_string(ci, &conf->name);
  if (status != 0) {
    mqtt_free(conf);
    return status;
  }

  conf->host = strdup(MQTT_DEFAULT_HOST);
  conf->port = MQTT_DEFAULT_PORT;
  conf->client_id = NULL;
  conf->qos = 0;
  conf->topic_prefix = strdup(MQTT_DEFAULT_TOPIC_PREFIX);
  conf->store_rates = 1;

  status = pthread_mutex_init(&conf->lock, NULL);
  if (status != 0) {
    mqtt_free(conf);
    return status;
  }

  C_COMPLAIN_INIT(&conf->complaint_cantpublish);

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Host", child->key) == 0)
      cf_util_get_string(child, &conf->host);
    else if (strcasecmp("Port", child->key) == 0) {
      int tmp = cf_util_get_port_number(child);
      if (tmp < 0)
        ERROR("mqtt plugin: Invalid port number.");
      else
        conf->port = tmp;
    } else if (strcasecmp("ClientId", child->key) == 0)
      cf_util_get_string(child, &conf->client_id);
    else if (strcasecmp("User", child->key) == 0)
      cf_util_get_string(child, &conf->username);
    else if (strcasecmp("Password", child->key) == 0)
      cf_util_get_string(child, &conf->password);
    else if (strcasecmp("QoS", child->key) == 0) {
      int tmp = -1;
      status = cf_util_get_int(child, &tmp);
      if ((status != 0) || (tmp < 0) || (tmp > 2))
        ERROR("mqtt plugin: Not a valid QoS setting.");
      else
        conf->qos = tmp;
    } else if (strcasecmp("Prefix", child->key) == 0)
      cf_util_get_string(child, &conf->topic_prefix);
    else if (strcasecmp("StoreRates", child->key) == 0)
      cf_util_get_boolean(child, &conf->store_rates);
    else if (strcasecmp("Retain", child->key) == 0)
      cf_util_get_boolean(child, &conf->retain);
    else if (strcasecmp("CACert", child->key) == 0)
      cf_util_get_string(child, &conf->cacertificatefile);
    else if (strcasecmp("CertificateFile", child->key) == 0)
      cf_util_get_string(child, &conf->certificatefile);
    else if (strcasecmp("CertificateKeyFile", child->key) == 0)
      cf_util_get_string(child, &conf->certificatekeyfile);
    else if (strcasecmp("TLSProtocol", child->key) == 0)
      cf_util_get_string(child, &conf->tlsprotocol);
    else if (strcasecmp("CipherSuite", child->key) == 0)
      cf_util_get_string(child, &conf->ciphersuite);
    else
      ERROR("mqtt plugin: Unknown config option: %s", child->key);
  }

  snprintf(cb_name, sizeof(cb_name), "mqtt/%s", conf->name);
  plugin_register_write(cb_name, mqtt_write,
                        &(user_data_t){
                            .data = conf,
                        });
  return 0;
} /* mqtt_config_publisher */

/*
 * <Subscribe "name">
 *   Host "example.com"
 *   Port 1883
 *   ClientId "collectd"
 *   User "guest"
 *   Password "secret"
 *   Topic "collectd/#"
 *   CACert "ca.pem"                      Enables TLS if set
 *   CertificateFile "client-cert.pem"	  optional
 *   CertificateKeyFile "client-key.pem"  optional
 *   TLSProtocol "tlsv1.2"                optional
 * </Subscribe>
 */
static int mqtt_config_subscriber(oconfig_item_t *ci) {
  mqtt_client_conf_t **tmp;
  mqtt_client_conf_t *conf;
  int status;

  conf = calloc(1, sizeof(*conf));
  if (conf == NULL) {
    ERROR("mqtt plugin: calloc failed.");
    return -1;
  }
  conf->publish = 0;

  conf->name = NULL;
  status = cf_util_get_string(ci, &conf->name);
  if (status != 0) {
    mqtt_free(conf);
    return status;
  }

  conf->host = strdup(MQTT_DEFAULT_HOST);
  conf->port = MQTT_DEFAULT_PORT;
  conf->client_id = NULL;
  conf->qos = 2;
  conf->topic = strdup(MQTT_DEFAULT_TOPIC);
  conf->clean_session = 1;

  status = pthread_mutex_init(&conf->lock, NULL);
  if (status != 0) {
    mqtt_free(conf);
    return status;
  }

  C_COMPLAIN_INIT(&conf->complaint_cantpublish);

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Host", child->key) == 0)
      cf_util_get_string(child, &conf->host);
    else if (strcasecmp("Port", child->key) == 0) {
      status = cf_util_get_port_number(child);
      if (status < 0)
        ERROR("mqtt plugin: Invalid port number.");
      else
        conf->port = status;
    } else if (strcasecmp("ClientId", child->key) == 0)
      cf_util_get_string(child, &conf->client_id);
    else if (strcasecmp("User", child->key) == 0)
      cf_util_get_string(child, &conf->username);
    else if (strcasecmp("Password", child->key) == 0)
      cf_util_get_string(child, &conf->password);
    else if (strcasecmp("QoS", child->key) == 0) {
      int qos = -1;
      status = cf_util_get_int(child, &qos);
      if ((status != 0) || (qos < 0) || (qos > 2))
        ERROR("mqtt plugin: Not a valid QoS setting.");
      else
        conf->qos = qos;
    } else if (strcasecmp("Topic", child->key) == 0)
      cf_util_get_string(child, &conf->topic);
    else if (strcasecmp("CleanSession", child->key) == 0)
      cf_util_get_boolean(child, &conf->clean_session);
    else if (strcasecmp("CACert", child->key) == 0)
      cf_util_get_string(child, &conf->cacertificatefile);
    else if (strcasecmp("CertificateFile", child->key) == 0)
      cf_util_get_string(child, &conf->certificatefile);
    else if (strcasecmp("CertificateKeyFile", child->key) == 0)
      cf_util_get_string(child, &conf->certificatekeyfile);
    else if (strcasecmp("TLSProtocol", child->key) == 0)
      cf_util_get_string(child, &conf->tlsprotocol);
    else if (strcasecmp("CipherSuite", child->key) == 0)
      cf_util_get_string(child, &conf->ciphersuite);
    else
      ERROR("mqtt plugin: Unknown config option: %s", child->key);
  }

  tmp = realloc(subscribers, sizeof(*subscribers) * (subscribers_num + 1));
  if (tmp == NULL) {
    ERROR("mqtt plugin: realloc failed.");
    mqtt_free(conf);
    return -1;
  }
  subscribers = tmp;
  subscribers[subscribers_num] = conf;
  subscribers_num++;

  return 0;
} /* mqtt_config_subscriber */

/*
 * <Plugin mqtt>
 *   <Publish "name">
 *     # ...
 *   </Publish>
 *   <Subscribe "name">
 *     # ...
 *   </Subscribe>
 * </Plugin>
 */
static int mqtt_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Publish", child->key) == 0)
      mqtt_config_publisher(child);
    else if (strcasecmp("Subscribe", child->key) == 0)
      mqtt_config_subscriber(child);
    else
      ERROR("mqtt plugin: Unknown config option: %s", child->key);
  }

  return 0;
} /* int mqtt_config */

static int mqtt_init(void) {
  mosquitto_lib_init();

  for (size_t i = 0; i < subscribers_num; i++) {
    int status;

    if (subscribers[i]->loop)
      continue;

    status = plugin_thread_create(&subscribers[i]->thread,
                                  /* attrs = */ NULL,
                                  /* func  = */ subscribers_thread,
                                  /* args  = */ subscribers[i],
                                  /* name  = */ "mqtt");
    if (status != 0) {
      ERROR("mqtt plugin: pthread_create failed: %s", STRERRNO);
      continue;
    }
  }

  return 0;
} /* mqtt_init */

void module_register(void) {
  plugin_register_complex_config("mqtt", mqtt_config);
  plugin_register_init("mqtt", mqtt_init);
} /* void module_register */
