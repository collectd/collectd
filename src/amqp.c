/**
 * collectd - src/amqp.c
 * Copyright (C) 2009       Sebastien Pahl
 * Copyright (C) 2010-2012  Florian Forster
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
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/cmds/putval.h"
#include "utils/common/common.h"
#include "utils/format_graphite/format_graphite.h"
#include "utils/format_json/format_json.h"
#include "utils_random.h"

#if HAVE_RABBITMQ_C_AMQP_H
#include <rabbitmq-c/amqp.h>
#if HAVE_RABBITMQ_C_FRAMING_H
#include <rabbitmq-c/framing.h>
#endif
#if HAVE_RABBITMQ_C_TCP_SOCKET_H
#include <rabbitmq-c/tcp_socket.h>
#endif
#if HAVE_RABBITMQ_C_SSL_SOCKET_H
#include <rabbitmq-c/ssl_socket.h>
#endif
#elif HAVE_AMQP_H
#include <amqp.h>
#if HAVE_AMQP_FRAMING_H
#include <amqp_framing.h>
#endif
#if HAVE_AMQP_TCP_SOCKET_H
#include <amqp_tcp_socket.h>
#endif
#if HAVE_AMQP_SSL_SOCKET_H
#include <amqp_ssl_socket.h>
#endif
#endif

/* Defines for the delivery mode. I have no idea why they're not defined by the
 * library.. */
#define CAMQP_DM_VOLATILE 1
#define CAMQP_DM_PERSISTENT 2

#define CAMQP_FORMAT_COMMAND 1
#define CAMQP_FORMAT_JSON 2
#define CAMQP_FORMAT_GRAPHITE 3

#define CAMQP_CHANNEL 1

/*
 * Data types
 */
struct camqp_config_s {
  bool publish;
  char *name;

  char **hosts;
  size_t hosts_count;
  int port;
  char *vhost;
  char *user;
  char *password;

  bool tls_enabled;
  bool tls_verify_peer;
  bool tls_verify_hostname;
  char *tls_cacert;
  char *tls_client_cert;
  char *tls_client_key;

  char *exchange;
  char *routing_key;

  /* Number of seconds to wait before connection is retried */
  int connection_retry_delay;

  /* publish only */
  uint8_t delivery_mode;
  bool store_rates;
  int format;
  /* publish & graphite format only */
  char *prefix;
  char *postfix;
  char escape_char;
  unsigned int graphite_flags;

  /* subscribe only */
  char *exchange_type;
  char *queue;
  bool queue_durable;
  bool queue_auto_delete;

  amqp_connection_state_t connection;
  pthread_mutex_t lock;
};
typedef struct camqp_config_s camqp_config_t;

/*
 * Global variables
 */
static const char *def_vhost = "/";
static const char *def_user = "guest";
static const char *def_password = "guest";
static const char *def_exchange = "amq.fanout";

static pthread_t *subscriber_threads;
static size_t subscriber_threads_num;
static bool subscriber_threads_running = true;

#define CONF(c, f) (((c)->f != NULL) ? (c)->f : def_##f)

/*
 * Functions
 */
static void camqp_close_connection(camqp_config_t *conf) /* {{{ */
{
  int sockfd;

  if ((conf == NULL) || (conf->connection == NULL))
    return;

  sockfd = amqp_get_sockfd(conf->connection);
  amqp_channel_close(conf->connection, CAMQP_CHANNEL, AMQP_REPLY_SUCCESS);
  amqp_connection_close(conf->connection, AMQP_REPLY_SUCCESS);
  amqp_destroy_connection(conf->connection);
  close(sockfd);
  conf->connection = NULL;
} /* }}} void camqp_close_connection */

static void camqp_config_free(void *ptr) /* {{{ */
{
  camqp_config_t *conf = ptr;

  if (conf == NULL)
    return;

  camqp_close_connection(conf);

  sfree(conf->name);
  strarray_free(conf->hosts, conf->hosts_count);
  sfree(conf->vhost);
  sfree(conf->user);
  sfree(conf->password);
  sfree(conf->tls_cacert);
  sfree(conf->tls_client_cert);
  sfree(conf->tls_client_key);
  sfree(conf->exchange);
  sfree(conf->exchange_type);
  sfree(conf->queue);
  sfree(conf->routing_key);
  sfree(conf->prefix);
  sfree(conf->postfix);

  sfree(conf);
} /* }}} void camqp_config_free */

static char *camqp_bytes_cstring(amqp_bytes_t *in) /* {{{ */
{
  char *ret;

  if ((in == NULL) || (in->bytes == NULL))
    return NULL;

  ret = malloc(in->len + 1);
  if (ret == NULL)
    return NULL;

  memcpy(ret, in->bytes, in->len);
  ret[in->len] = 0;

  return ret;
} /* }}} char *camqp_bytes_cstring */

static bool camqp_is_error(camqp_config_t *conf) /* {{{ */
{
  amqp_rpc_reply_t r;

  r = amqp_get_rpc_reply(conf->connection);
  if (r.reply_type == AMQP_RESPONSE_NORMAL)
    return false;

  return true;
} /* }}} bool camqp_is_error */

static char *camqp_strerror(camqp_config_t *conf, /* {{{ */
                            char *buffer, size_t buffer_size) {
  amqp_rpc_reply_t r;

  r = amqp_get_rpc_reply(conf->connection);
  switch (r.reply_type) {
  case AMQP_RESPONSE_NORMAL:
    sstrncpy(buffer, "Success", buffer_size);
    break;

  case AMQP_RESPONSE_NONE:
    sstrncpy(buffer, "Missing RPC reply type", buffer_size);
    break;

  case AMQP_RESPONSE_LIBRARY_EXCEPTION:
#if HAVE_AMQP_RPC_REPLY_T_LIBRARY_ERRNO
    if (r.library_errno)
      return sstrerror(r.library_errno, buffer, buffer_size);
#else
    if (r.library_error)
      return sstrerror(r.library_error, buffer, buffer_size);
#endif
    else
      sstrncpy(buffer, "End of stream", buffer_size);
    break;

  case AMQP_RESPONSE_SERVER_EXCEPTION:
    if (r.reply.id == AMQP_CONNECTION_CLOSE_METHOD) {
      amqp_connection_close_t *m = r.reply.decoded;
      char *tmp = camqp_bytes_cstring(&m->reply_text);
      ssnprintf(buffer, buffer_size, "Server connection error %d: %s",
                m->reply_code, tmp);
      sfree(tmp);
    } else if (r.reply.id == AMQP_CHANNEL_CLOSE_METHOD) {
      amqp_channel_close_t *m = r.reply.decoded;
      char *tmp = camqp_bytes_cstring(&m->reply_text);
      ssnprintf(buffer, buffer_size, "Server channel error %d: %s",
                m->reply_code, tmp);
      sfree(tmp);
    } else {
      ssnprintf(buffer, buffer_size, "Server error method %#" PRIx32,
                r.reply.id);
    }
    break;

  default:
    ssnprintf(buffer, buffer_size, "Unknown reply type %i", (int)r.reply_type);
  }

  return buffer;
} /* }}} char *camqp_strerror */

#if HAVE_AMQP_RPC_REPLY_T_LIBRARY_ERRNO
static int camqp_create_exchange(camqp_config_t *conf) /* {{{ */
{
  amqp_exchange_declare_ok_t *ed_ret;

  if (conf->exchange_type == NULL)
    return 0;

  ed_ret = amqp_exchange_declare(
      conf->connection,
      /* channel     = */ CAMQP_CHANNEL,
      /* exchange    = */ amqp_cstring_bytes(conf->exchange),
      /* type        = */ amqp_cstring_bytes(conf->exchange_type),
      /* passive     = */ 0,
      /* durable     = */ 0,
      /* auto_delete = */ 1,
      /* arguments   = */ AMQP_EMPTY_TABLE);
  if ((ed_ret == NULL) && camqp_is_error(conf)) {
    char errbuf[1024];
    ERROR("amqp plugin: amqp_exchange_declare failed: %s",
          camqp_strerror(conf, errbuf, sizeof(errbuf)));
    camqp_close_connection(conf);
    return -1;
  }

  INFO("amqp plugin: Successfully created exchange \"%s\" "
       "with type \"%s\".",
       conf->exchange, conf->exchange_type);

  return 0;
} /* }}} int camqp_create_exchange */
#else
static int camqp_create_exchange(camqp_config_t *conf) /* {{{ */
{
  amqp_exchange_declare_ok_t *ed_ret;
  amqp_table_t argument_table;
  struct amqp_table_entry_t_ argument_table_entries[1];

  if (conf->exchange_type == NULL)
    return 0;

  /* Valid arguments: "auto_delete", "internal" */
  argument_table.num_entries = STATIC_ARRAY_SIZE(argument_table_entries);
  argument_table.entries = argument_table_entries;
  argument_table_entries[0].key = amqp_cstring_bytes("auto_delete");
  argument_table_entries[0].value.kind = AMQP_FIELD_KIND_BOOLEAN;
  argument_table_entries[0].value.value.boolean = 1;

  ed_ret = amqp_exchange_declare(
      conf->connection,
      /* channel     = */ CAMQP_CHANNEL,
      /* exchange    = */ amqp_cstring_bytes(conf->exchange),
      /* type        = */ amqp_cstring_bytes(conf->exchange_type),
      /* passive     = */ 0,
      /* durable     = */ 0,
#if defined(AMQP_VERSION) && AMQP_VERSION >= 0x00060000
      /* auto delete = */ 0,
      /* internal    = */ 0,
#endif
      /* arguments   = */ argument_table);
  if ((ed_ret == NULL) && camqp_is_error(conf)) {
    char errbuf[1024];
    ERROR("amqp plugin: amqp_exchange_declare failed: %s",
          camqp_strerror(conf, errbuf, sizeof(errbuf)));
    camqp_close_connection(conf);
    return -1;
  }

  INFO("amqp plugin: Successfully created exchange \"%s\" "
       "with type \"%s\".",
       conf->exchange, conf->exchange_type);

  return 0;
} /* }}} int camqp_create_exchange */
#endif

static int camqp_setup_queue(camqp_config_t *conf) /* {{{ */
{
  amqp_queue_declare_ok_t *qd_ret;
  amqp_basic_consume_ok_t *cm_ret;

  qd_ret =
      amqp_queue_declare(conf->connection,
                         /* channel     = */ CAMQP_CHANNEL,
                         /* queue       = */
                         (conf->queue != NULL) ? amqp_cstring_bytes(conf->queue)
                                               : AMQP_EMPTY_BYTES,
                         /* passive     = */ 0,
                         /* durable     = */ conf->queue_durable,
                         /* exclusive   = */ 0,
                         /* auto_delete = */ conf->queue_auto_delete,
                         /* arguments   = */ AMQP_EMPTY_TABLE);
  if (qd_ret == NULL) {
    ERROR("amqp plugin: amqp_queue_declare failed.");
    camqp_close_connection(conf);
    return -1;
  }

  if (conf->queue == NULL) {
    conf->queue = camqp_bytes_cstring(&qd_ret->queue);
    if (conf->queue == NULL) {
      ERROR("amqp plugin: camqp_bytes_cstring failed.");
      camqp_close_connection(conf);
      return -1;
    }

    INFO("amqp plugin: Created queue \"%s\".", conf->queue);
  }
  DEBUG("amqp plugin: Successfully created queue \"%s\".", conf->queue);

  /* bind to an exchange */
  if (conf->exchange != NULL) {
    amqp_queue_bind_ok_t *qb_ret;

    assert(conf->queue != NULL);
    qb_ret = amqp_queue_bind(
        conf->connection,
        /* channel     = */ CAMQP_CHANNEL,
        /* queue       = */ amqp_cstring_bytes(conf->queue),
        /* exchange    = */ amqp_cstring_bytes(conf->exchange),
        /* routing_key = */
        (conf->routing_key != NULL) ? amqp_cstring_bytes(conf->routing_key)
                                    : AMQP_EMPTY_BYTES,
        /* arguments   = */ AMQP_EMPTY_TABLE);
    if ((qb_ret == NULL) && camqp_is_error(conf)) {
      char errbuf[1024];
      ERROR("amqp plugin: amqp_queue_bind failed: %s",
            camqp_strerror(conf, errbuf, sizeof(errbuf)));
      camqp_close_connection(conf);
      return -1;
    }

    DEBUG("amqp plugin: Successfully bound queue \"%s\" to exchange \"%s\".",
          conf->queue, conf->exchange);
  } /* if (conf->exchange != NULL) */

  cm_ret =
      amqp_basic_consume(conf->connection,
                         /* channel      = */ CAMQP_CHANNEL,
                         /* queue        = */ amqp_cstring_bytes(conf->queue),
                         /* consumer_tag = */ AMQP_EMPTY_BYTES,
                         /* no_local     = */ 0,
                         /* no_ack       = */ 1,
                         /* exclusive    = */ 0,
                         /* arguments    = */ AMQP_EMPTY_TABLE);
  if ((cm_ret == NULL) && camqp_is_error(conf)) {
    char errbuf[1024];
    ERROR("amqp plugin: amqp_basic_consume failed: %s",
          camqp_strerror(conf, errbuf, sizeof(errbuf)));
    camqp_close_connection(conf);
    return -1;
  }

  return 0;
} /* }}} int camqp_setup_queue */

static int camqp_connect(camqp_config_t *conf) /* {{{ */
{
  static time_t last_connect_time;

  if (conf->connection != NULL)
    return 0;

  time_t now = time(NULL);
  if (now < (last_connect_time + conf->connection_retry_delay)) {
    DEBUG("amqp plugin: skipping connection retry, "
          "ConnectionRetryDelay: %d",
          conf->connection_retry_delay);
    return 1;
  } else {
    DEBUG("amqp plugin: retrying connection");
    last_connect_time = now;
  }

  conf->connection = amqp_new_connection();
  if (conf->connection == NULL) {
    ERROR("amqp plugin: amqp_new_connection failed.");
    return ENOMEM;
  }

  char *host = conf->hosts[cdrand_u() % conf->hosts_count];
  INFO("amqp plugin: Connecting to %s", host);

  amqp_socket_t *socket = NULL;
  if (conf->tls_enabled) {
    socket = amqp_ssl_socket_new(conf->connection);
    if (socket == NULL) {
      ERROR("amqp plugin: amqp_ssl_socket_new failed.");
      amqp_destroy_connection(conf->connection);
      conf->connection = NULL;
      return ENOMEM;
    }

#if AMQP_VERSION >= 0x00080000
    amqp_ssl_socket_set_verify_peer(socket, conf->tls_verify_peer);
    amqp_ssl_socket_set_verify_hostname(socket, conf->tls_verify_hostname);
#endif

    if (conf->tls_cacert) {
      int status = amqp_ssl_socket_set_cacert(socket, conf->tls_cacert);
      if (status < 0) {
        ERROR("amqp plugin: amqp_ssl_socket_set_cacert failed: %s",
              amqp_error_string2(status));
        amqp_destroy_connection(conf->connection);
        conf->connection = NULL;
        return status;
      }
    }
    if (conf->tls_client_cert && conf->tls_client_key) {
      int status = amqp_ssl_socket_set_key(socket, conf->tls_client_cert,
                                           conf->tls_client_key);
      if (status < 0) {
        ERROR("amqp plugin: amqp_ssl_socket_set_key failed: %s",
              amqp_error_string2(status));
        amqp_destroy_connection(conf->connection);
        conf->connection = NULL;
        return status;
      }
    }
  } else {
    socket = amqp_tcp_socket_new(conf->connection);
    if (!socket) {
      ERROR("amqp plugin: amqp_tcp_socket_new failed.");
      amqp_destroy_connection(conf->connection);
      conf->connection = NULL;
      return ENOMEM;
    }
  }

  int status = amqp_socket_open(socket, host, conf->port);
  if (status < 0) {
    ERROR("amqp plugin: amqp_socket_open failed: %s",
          amqp_error_string2(status));
    amqp_destroy_connection(conf->connection);
    conf->connection = NULL;
    return status;
  }

  amqp_rpc_reply_t reply =
      amqp_login(conf->connection, CONF(conf, vhost),
                 /* channel max = */ 0,
                 /* frame max   = */ 131072,
                 /* heartbeat   = */ 0,
                 /* authentication = */ AMQP_SASL_METHOD_PLAIN,
                 CONF(conf, user), CONF(conf, password));
  if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
    ERROR("amqp plugin: amqp_login (vhost = %s, user = %s) failed.",
          CONF(conf, vhost), CONF(conf, user));
    amqp_destroy_connection(conf->connection);
    conf->connection = NULL;
    return 1;
  }

  amqp_channel_open(conf->connection, /* channel = */ 1);
  /* FIXME: Is checking "reply.reply_type" really correct here? How does
   * it get set? --octo */
  if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
    ERROR("amqp plugin: amqp_channel_open failed.");
    amqp_connection_close(conf->connection, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conf->connection);
    conf->connection = NULL;
    return 1;
  }

  INFO("amqp plugin: Successfully opened connection to vhost \"%s\" "
       "on %s:%i.",
       CONF(conf, vhost), host, conf->port);

  status = camqp_create_exchange(conf);
  if (status != 0)
    return status;

  if (!conf->publish)
    return camqp_setup_queue(conf);
  return 0;
} /* }}} int camqp_connect */

static int camqp_shutdown(void) /* {{{ */
{
  DEBUG("amqp plugin: Shutting down %" PRIsz " subscriber threads.",
        subscriber_threads_num);

  subscriber_threads_running = 0;
  for (size_t i = 0; i < subscriber_threads_num; i++) {
    /* FIXME: Sending a signal is not very elegant here. Maybe find out how
     * to use a timeout in the thread and check for the variable in regular
     * intervals. */
    pthread_kill(subscriber_threads[i], SIGTERM);
    pthread_join(subscriber_threads[i], /* retval = */ NULL);
  }

  subscriber_threads_num = 0;
  sfree(subscriber_threads);

  DEBUG("amqp plugin: All subscriber threads exited.");

  return 0;
} /* }}} int camqp_shutdown */

/*
 * Subscribing code
 */
static int camqp_read_body(camqp_config_t *conf, /* {{{ */
                           size_t body_size, const char *content_type) {
  char body[body_size + 1];
  char *body_ptr;
  size_t received;
  amqp_frame_t frame;
  int status;

  memset(body, 0, sizeof(body));
  body_ptr = &body[0];
  received = 0;

  while (received < body_size) {
    status = amqp_simple_wait_frame(conf->connection, &frame);
    if (status < 0) {
      status = (-1) * status;
      ERROR("amqp plugin: amqp_simple_wait_frame failed: %s", STRERROR(status));
      camqp_close_connection(conf);
      return status;
    }

    if (frame.frame_type != AMQP_FRAME_BODY) {
      NOTICE("amqp plugin: Unexpected frame type: %#" PRIx8, frame.frame_type);
      return -1;
    }

    if ((body_size - received) < frame.payload.body_fragment.len) {
      WARNING("amqp plugin: Body is larger than indicated by header.");
      return -1;
    }

    memcpy(body_ptr, frame.payload.body_fragment.bytes,
           frame.payload.body_fragment.len);
    body_ptr += frame.payload.body_fragment.len;
    received += frame.payload.body_fragment.len;
  } /* while (received < body_size) */

  if (strcasecmp("text/collectd", content_type) == 0) {
    status = cmd_handle_putval(stderr, body);
    if (status != 0)
      ERROR("amqp plugin: cmd_handle_putval failed with status %i.", status);
    return status;
  } else if (strcasecmp("application/json", content_type) == 0) {
    ERROR("amqp plugin: camqp_read_body: Parsing JSON data has not "
          "been implemented yet. FIXME!");
    return 0;
  } else {
    ERROR("amqp plugin: camqp_read_body: Unknown content type \"%s\".",
          content_type);
    return EINVAL;
  }

  /* not reached */
  return 0;
} /* }}} int camqp_read_body */

static int camqp_read_header(camqp_config_t *conf) /* {{{ */
{
  int status;
  amqp_frame_t frame;
  amqp_basic_properties_t *properties;
  char *content_type;

  status = amqp_simple_wait_frame(conf->connection, &frame);
  if (status < 0) {
    status = (-1) * status;
    ERROR("amqp plugin: amqp_simple_wait_frame failed: %s", STRERROR(status));
    camqp_close_connection(conf);
    return status;
  }

  if (frame.frame_type != AMQP_FRAME_HEADER) {
    NOTICE("amqp plugin: Unexpected frame type: %#" PRIx8, frame.frame_type);
    return -1;
  }

  properties = frame.payload.properties.decoded;
  content_type = camqp_bytes_cstring(&properties->content_type);
  if (content_type == NULL) {
    ERROR("amqp plugin: Unable to determine content type.");
    return -1;
  }

  status = camqp_read_body(conf, (size_t)frame.payload.properties.body_size,
                           content_type);

  sfree(content_type);
  return status;
} /* }}} int camqp_read_header */

static void *camqp_subscribe_thread(void *user_data) /* {{{ */
{
  camqp_config_t *conf = user_data;
  int status;

  cdtime_t interval = plugin_get_interval();

  while (subscriber_threads_running) {
    amqp_frame_t frame;

    status = camqp_connect(conf);
    if (status != 0) {
      ERROR("amqp plugin: camqp_connect failed. "
            "Will sleep for %.3f seconds.",
            CDTIME_T_TO_DOUBLE(interval));
      nanosleep(&CDTIME_T_TO_TIMESPEC(interval), /* remaining = */ NULL);
      continue;
    }

    status = amqp_simple_wait_frame(conf->connection, &frame);
    if (status < 0) {
      ERROR("amqp plugin: amqp_simple_wait_frame failed. "
            "Will sleep for %.3f seconds.",
            CDTIME_T_TO_DOUBLE(interval));
      camqp_close_connection(conf);
      nanosleep(&CDTIME_T_TO_TIMESPEC(interval), /* remaining = */ NULL);
      continue;
    }

    if (frame.frame_type != AMQP_FRAME_METHOD) {
      DEBUG("amqp plugin: Unexpected frame type: %#" PRIx8, frame.frame_type);
      continue;
    }

    if (frame.payload.method.id != AMQP_BASIC_DELIVER_METHOD) {
      DEBUG("amqp plugin: Unexpected method id: %#" PRIx32,
            frame.payload.method.id);
      continue;
    }

    camqp_read_header(conf);

    amqp_maybe_release_buffers(conf->connection);
  } /* while (subscriber_threads_running) */

  camqp_config_free(conf);
  pthread_exit(NULL);
  return NULL;
} /* }}} void *camqp_subscribe_thread */

static int camqp_subscribe_init(camqp_config_t *conf) /* {{{ */
{
  int status;
  pthread_t *tmp;

  tmp = realloc(subscriber_threads,
                sizeof(*subscriber_threads) * (subscriber_threads_num + 1));
  if (tmp == NULL) {
    ERROR("amqp plugin: realloc failed.");
    sfree(subscriber_threads);
    return ENOMEM;
  }
  subscriber_threads = tmp;
  tmp = subscriber_threads + subscriber_threads_num;
  memset(tmp, 0, sizeof(*tmp));

  status =
      plugin_thread_create(tmp, camqp_subscribe_thread, conf, "amqp subscribe");
  if (status != 0) {
    ERROR("amqp plugin: pthread_create failed: %s", STRERROR(status));
    return status;
  }

  subscriber_threads_num++;

  return 0;
} /* }}} int camqp_subscribe_init */

/*
 * Publishing code
 */
/* XXX: You must hold "conf->lock" when calling this function! */
static int camqp_write_locked(camqp_config_t *conf, /* {{{ */
                              const char *buffer, const char *routing_key) {
  int status;

  status = camqp_connect(conf);
  if (status != 0)
    return status;

  amqp_basic_properties_t props = {._flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
                                             AMQP_BASIC_DELIVERY_MODE_FLAG |
                                             AMQP_BASIC_APP_ID_FLAG,
                                   .delivery_mode = conf->delivery_mode,
                                   .app_id = amqp_cstring_bytes("collectd")};

  if (conf->format == CAMQP_FORMAT_COMMAND)
    props.content_type = amqp_cstring_bytes("text/collectd");
  else if (conf->format == CAMQP_FORMAT_JSON)
    props.content_type = amqp_cstring_bytes("application/json");
  else if (conf->format == CAMQP_FORMAT_GRAPHITE)
    props.content_type = amqp_cstring_bytes("text/graphite");
  else
    assert(23 == 42);

  status = amqp_basic_publish(
      conf->connection,
      /* channel = */ 1, amqp_cstring_bytes(CONF(conf, exchange)),
      amqp_cstring_bytes(routing_key),
      /* mandatory = */ 0,
      /* immediate = */ 0, &props, amqp_cstring_bytes(buffer));
  if (status != 0) {
    ERROR("amqp plugin: amqp_basic_publish failed with status %i.", status);
    camqp_close_connection(conf);
  }

  return status;
} /* }}} int camqp_write_locked */

static int camqp_write(const data_set_t *ds, const value_list_t *vl, /* {{{ */
                       user_data_t *user_data) {
  camqp_config_t *conf = user_data->data;
  char routing_key[6 * DATA_MAX_NAME_LEN];
  char buffer[8192];
  int status;

  if ((ds == NULL) || (vl == NULL) || (conf == NULL))
    return EINVAL;

  if (conf->routing_key != NULL) {
    sstrncpy(routing_key, conf->routing_key, sizeof(routing_key));
  } else {
    ssnprintf(routing_key, sizeof(routing_key), "collectd/%s/%s/%s/%s/%s",
              vl->host, vl->plugin, vl->plugin_instance, vl->type,
              vl->type_instance);

    /* Switch slashes (the only character forbidden by collectd) and dots
     * (the separation character used by AMQP). */
    for (size_t i = 0; routing_key[i] != 0; i++) {
      if (routing_key[i] == '.')
        routing_key[i] = '/';
      else if (routing_key[i] == '/')
        routing_key[i] = '.';
    }
  }

  if (conf->format == CAMQP_FORMAT_COMMAND) {
    status = cmd_create_putval(buffer, sizeof(buffer), ds, vl);
    if (status != 0) {
      ERROR("amqp plugin: cmd_create_putval failed with status %i.", status);
      return status;
    }
  } else if (conf->format == CAMQP_FORMAT_JSON) {
    size_t bfree = sizeof(buffer);
    size_t bfill = 0;

    format_json_initialize(buffer, &bfill, &bfree);
    format_json_value_list(buffer, &bfill, &bfree, ds, vl, conf->store_rates);
    format_json_finalize(buffer, &bfill, &bfree);
  } else if (conf->format == CAMQP_FORMAT_GRAPHITE) {
    status =
        format_graphite(buffer, sizeof(buffer), ds, vl, conf->prefix,
                        conf->postfix, conf->escape_char, conf->graphite_flags);
    if (status != 0) {
      ERROR("amqp plugin: format_graphite failed with status %i.", status);
      return status;
    }
  } else {
    ERROR("amqp plugin: Invalid format (%i).", conf->format);
    return -1;
  }

  pthread_mutex_lock(&conf->lock);
  status = camqp_write_locked(conf, buffer, routing_key);
  pthread_mutex_unlock(&conf->lock);

  return status;
} /* }}} int camqp_write */

/*
 * Config handling
 */
static int camqp_config_set_format(oconfig_item_t *ci, /* {{{ */
                                   camqp_config_t *conf) {
  char *string;
  int status;

  string = NULL;
  status = cf_util_get_string(ci, &string);
  if (status != 0)
    return status;

  assert(string != NULL);
  if (strcasecmp("Command", string) == 0)
    conf->format = CAMQP_FORMAT_COMMAND;
  else if (strcasecmp("JSON", string) == 0)
    conf->format = CAMQP_FORMAT_JSON;
  else if (strcasecmp("Graphite", string) == 0)
    conf->format = CAMQP_FORMAT_GRAPHITE;
  else {
    WARNING("amqp plugin: Invalid format string: %s", string);
  }

  free(string);

  return 0;
} /* }}} int config_set_string */

static int camqp_config_connection(oconfig_item_t *ci, /* {{{ */
                                   bool publish) {
  camqp_config_t *conf;
  int status;

  conf = calloc(1, sizeof(*conf));
  if (conf == NULL) {
    ERROR("amqp plugin: calloc failed.");
    return ENOMEM;
  }

  /* Initialize "conf" {{{ */
  conf->publish = publish;
  conf->name = NULL;
  conf->format = CAMQP_FORMAT_COMMAND;
  conf->hosts = NULL;
  conf->hosts_count = 0;
  conf->port = 5672;
  conf->vhost = NULL;
  conf->user = NULL;
  conf->password = NULL;
  conf->tls_enabled = false;
  conf->tls_verify_peer = true;
  conf->tls_verify_hostname = true;
  conf->tls_cacert = NULL;
  conf->tls_client_cert = NULL;
  conf->tls_client_key = NULL;
  conf->exchange = NULL;
  conf->routing_key = NULL;
  conf->connection_retry_delay = 0;

  /* publish only */
  conf->delivery_mode = CAMQP_DM_VOLATILE;
  conf->store_rates = false;
  conf->graphite_flags = 0;
  /* publish & graphite only */
  conf->prefix = NULL;
  conf->postfix = NULL;
  conf->escape_char = '_';
  /* subscribe only */
  conf->exchange_type = NULL;
  conf->queue = NULL;
  conf->queue_durable = false;
  conf->queue_auto_delete = true;
  /* general */
  conf->connection = NULL;
  pthread_mutex_init(&conf->lock, /* attr = */ NULL);
  /* }}} */

  status = cf_util_get_string(ci, &conf->name);
  if (status != 0) {
    sfree(conf);
    return status;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Host", child->key) == 0) {
      for (int j = 0; j < child->values_num; j++) {
        if (child->values[j].type != OCONFIG_TYPE_STRING) {
          status = EINVAL;
          ERROR("amqp plugin: Host arguments must be strings");
          break;
        }

        status = strarray_add(&conf->hosts, &conf->hosts_count,
                              child->values[j].value.string);
        if (status) {
          ERROR("amqp plugin: Host configuration failed: %d", status);
          break;
        }
      }
    } else if (strcasecmp("Port", child->key) == 0) {
      status = cf_util_get_port_number(child);
      if (status > 0) {
        conf->port = status;
        status = 0;
      }
    } else if (strcasecmp("VHost", child->key) == 0)
      status = cf_util_get_string(child, &conf->vhost);
    else if (strcasecmp("User", child->key) == 0)
      status = cf_util_get_string(child, &conf->user);
    else if (strcasecmp("Password", child->key) == 0)
      status = cf_util_get_string(child, &conf->password);
    else if (strcasecmp("TLSEnabled", child->key) == 0)
      status = cf_util_get_boolean(child, &conf->tls_enabled);
    else if (strcasecmp("TLSVerifyPeer", child->key) == 0)
      status = cf_util_get_boolean(child, &conf->tls_verify_peer);
    else if (strcasecmp("TLSVerifyHostName", child->key) == 0)
      status = cf_util_get_boolean(child, &conf->tls_verify_hostname);
    else if (strcasecmp("TLSCACert", child->key) == 0)
      status = cf_util_get_string(child, &conf->tls_cacert);
    else if (strcasecmp("TLSClientCert", child->key) == 0)
      status = cf_util_get_string(child, &conf->tls_client_cert);
    else if (strcasecmp("TLSClientKey", child->key) == 0)
      status = cf_util_get_string(child, &conf->tls_client_key);
    else if (strcasecmp("Exchange", child->key) == 0)
      status = cf_util_get_string(child, &conf->exchange);
    else if (strcasecmp("ExchangeType", child->key) == 0)
      status = cf_util_get_string(child, &conf->exchange_type);
    else if ((strcasecmp("Queue", child->key) == 0) && !publish)
      status = cf_util_get_string(child, &conf->queue);
    else if ((strcasecmp("QueueDurable", child->key) == 0) && !publish)
      status = cf_util_get_boolean(child, &conf->queue_durable);
    else if ((strcasecmp("QueueAutoDelete", child->key) == 0) && !publish)
      status = cf_util_get_boolean(child, &conf->queue_auto_delete);
    else if (strcasecmp("RoutingKey", child->key) == 0)
      status = cf_util_get_string(child, &conf->routing_key);
    else if ((strcasecmp("Persistent", child->key) == 0) && publish) {
      bool tmp = 0;
      status = cf_util_get_boolean(child, &tmp);
      if (tmp)
        conf->delivery_mode = CAMQP_DM_PERSISTENT;
      else
        conf->delivery_mode = CAMQP_DM_VOLATILE;
    } else if ((strcasecmp("StoreRates", child->key) == 0) && publish) {
      status = cf_util_get_boolean(child, &conf->store_rates);
      (void)cf_util_get_flag(child, &conf->graphite_flags,
                             GRAPHITE_STORE_RATES);
    } else if ((strcasecmp("Format", child->key) == 0) && publish)
      status = camqp_config_set_format(child, conf);
    else if ((strcasecmp("GraphiteSeparateInstances", child->key) == 0) &&
             publish)
      status = cf_util_get_flag(child, &conf->graphite_flags,
                                GRAPHITE_SEPARATE_INSTANCES);
    else if ((strcasecmp("GraphiteAlwaysAppendDS", child->key) == 0) && publish)
      status = cf_util_get_flag(child, &conf->graphite_flags,
                                GRAPHITE_ALWAYS_APPEND_DS);
    else if ((strcasecmp("GraphitePreserveSeparator", child->key) == 0) &&
             publish)
      status = cf_util_get_flag(child, &conf->graphite_flags,
                                GRAPHITE_PRESERVE_SEPARATOR);
    else if ((strcasecmp("GraphitePrefix", child->key) == 0) && publish)
      status = cf_util_get_string(child, &conf->prefix);
    else if ((strcasecmp("GraphitePostfix", child->key) == 0) && publish)
      status = cf_util_get_string(child, &conf->postfix);
    else if ((strcasecmp("GraphiteEscapeChar", child->key) == 0) && publish) {
      char *tmp_buff = NULL;
      status = cf_util_get_string(child, &tmp_buff);
      if (strlen(tmp_buff) > 1)
        WARNING("amqp plugin: The option \"GraphiteEscapeChar\" handles "
                "only one character. Others will be ignored.");
      conf->escape_char = tmp_buff[0];
      sfree(tmp_buff);
    } else if (strcasecmp("ConnectionRetryDelay", child->key) == 0)
      status = cf_util_get_int(child, &conf->connection_retry_delay);
    else
      WARNING("amqp plugin: Ignoring unknown "
              "configuration option \"%s\".",
              child->key);

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status == 0 && conf->hosts_count == 0) {
    status = strarray_add(&conf->hosts, &conf->hosts_count, "localhost");
    if (status)
      ERROR("amqp plugin: Host configuration failed: %d", status);
  }

  if ((status == 0) && (conf->exchange == NULL)) {
    if (conf->exchange_type != NULL)
      WARNING("amqp plugin: The option \"ExchangeType\" was given "
              "without the \"Exchange\" option. It will be ignored.");

    if (!publish && (conf->routing_key != NULL))
      WARNING("amqp plugin: The option \"RoutingKey\" was given "
              "without the \"Exchange\" option. It will be ignored.");
  }

#if !defined(AMQP_VERSION) || AMQP_VERSION < 0x00040000
  if (status == 0 && conf->tls_enabled) {
    ERROR("amqp plugin: TLSEnabled is set but not supported. "
          "rebuild collectd with rabbitmq-c >= 0.4");
    status = 1;
  }
#endif
#if !defined(AMQP_VERSION) || AMQP_VERSION < 0x00080000
  if (status == 0 && (!conf->tls_verify_peer || !conf->tls_verify_hostname)) {
    ERROR("amqp plugin: disabling TLSVerify* is not supported. "
          "rebuild collectd with rabbitmq-c >= 0.8");
    status = 1;
  }
#endif
  if (status == 0 &&
      (conf->tls_client_cert != NULL || conf->tls_client_key != NULL)) {
    if (conf->tls_client_cert == NULL || conf->tls_client_key == NULL) {
      ERROR("amqp plugin: only one of TLSClientCert/TLSClientKey is "
            "configured. need both or neither.");
      status = 1;
    }
  }

  if (status != 0) {
    camqp_config_free(conf);
    return status;
  }

  if (conf->exchange != NULL) {
    DEBUG("amqp plugin: camqp_config_connection: exchange = %s;",
          conf->exchange);
  }

  if (publish) {
    char cbname[128];
    ssnprintf(cbname, sizeof(cbname), "amqp/%s", conf->name);

    status = plugin_register_write(cbname, camqp_write,
                                   &(user_data_t){
                                       .data = conf,
                                       .free_func = camqp_config_free,
                                   });
    if (status != 0) {
      camqp_config_free(conf);
      return status;
    }
  } else {
    status = camqp_subscribe_init(conf);
    if (status != 0) {
      camqp_config_free(conf);
      return status;
    }
  }

  return 0;
} /* }}} int camqp_config_connection */

static int camqp_config(oconfig_item_t *ci) /* {{{ */
{
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Publish", child->key) == 0)
      camqp_config_connection(child, /* publish = */ true);
    else if (strcasecmp("Subscribe", child->key) == 0)
      camqp_config_connection(child, /* publish = */ false);
    else
      WARNING("amqp plugin: Ignoring unknown config option \"%s\".",
              child->key);
  } /* for (ci->children_num) */

  return 0;
} /* }}} int camqp_config */

void module_register(void) {
  plugin_register_complex_config("amqp", camqp_config);
  plugin_register_shutdown("amqp", camqp_shutdown);
} /* void module_register */
