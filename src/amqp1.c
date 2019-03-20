/**
 * collectd - src/amqp1.c
 * Copyright(c) 2017 Red Hat Inc.
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
 *   Andy Smith <ansmith@redhat.com>
 */

#include "collectd.h"

#include "plugin.h"
#include "utils/cmds/putval.h"
#include "utils/common/common.h"
#include "utils/deq/deq.h"
#include "utils/format_graphite/format_graphite.h"
#include "utils/format_json/format_json.h"
#include "utils_random.h"

#include <proton/condition.h>
#include <proton/connection.h>
#include <proton/delivery.h>
#include <proton/link.h>
#include <proton/message.h>
#include <proton/proactor.h>
#include <proton/sasl.h>
#include <proton/session.h>
#include <proton/transport.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define BUFSIZE 8192
#define AMQP1_FORMAT_JSON 0
#define AMQP1_FORMAT_COMMAND 1
#define AMQP1_FORMAT_GRAPHITE 2

typedef struct amqp1_config_transport_s {
  DEQ_LINKS(struct amqp1_config_transport_s);
  char *name;
  char *host;
  char *port;
  char *user;
  char *password;
  char *address;
  int retry_delay;
} amqp1_config_transport_t;

typedef struct amqp1_config_instance_s {
  DEQ_LINKS(struct amqp1_config_instance_s);
  char *name;
  bool notify;
  uint8_t format;
  unsigned int graphite_flags;
  bool store_rates;
  char *prefix;
  char *postfix;
  char escape_char;
  bool pre_settle;
  char send_to[1024];
} amqp1_config_instance_t;

DEQ_DECLARE(amqp1_config_instance_t, amqp1_config_instance_list_t);

typedef struct cd_message_s {
  DEQ_LINKS(struct cd_message_s);
  pn_rwbytes_t mbuf;
  amqp1_config_instance_t *instance;
} cd_message_t;

DEQ_DECLARE(cd_message_t, cd_message_list_t);

/*
 * Globals
 */
static pn_connection_t *conn;
static pn_link_t *sender;
static pn_proactor_t *proactor;
static pthread_mutex_t send_lock;
static cd_message_list_t out_messages;
static uint64_t cd_tag = 1;
static uint64_t acknowledged;
static amqp1_config_transport_t *transport;
static bool stopping;
static bool event_thread_running;
static pthread_t event_thread_id;

/*
 * Functions
 */
static void cd_message_free(cd_message_t *cdm) {
  free(cdm->mbuf.start);
  free(cdm);
} /* }}} void cd_message_free */

static int amqp1_send_out_messages(pn_link_t *link) /* {{{ */
{
  uint64_t dtag;
  cd_message_list_t to_send;
  cd_message_t *cdm;
  int link_credit = pn_link_credit(link);
  int event_count = 0;
  pn_delivery_t *dlv;

  if (stopping) {
    return 0;
  }

  DEQ_INIT(to_send);

  pthread_mutex_lock(&send_lock);

  if (link_credit > 0) {
    dtag = cd_tag;
    cdm = DEQ_HEAD(out_messages);
    while (cdm) {
      DEQ_REMOVE_HEAD(out_messages);
      DEQ_INSERT_TAIL(to_send, cdm);
      if (DEQ_SIZE(to_send) == (size_t)link_credit)
        break;
      cdm = DEQ_HEAD(out_messages);
    }
    cd_tag += DEQ_SIZE(to_send);
  }

  pthread_mutex_unlock(&send_lock);

  /* message is already formatted and encoded */
  cdm = DEQ_HEAD(to_send);
  while (cdm) {
    DEQ_REMOVE_HEAD(to_send);
    dtag++;
    dlv = pn_delivery(link, pn_dtag((const char *)&dtag, sizeof(dtag)));
    pn_link_send(link, cdm->mbuf.start, cdm->mbuf.size);
    pn_link_advance(link);
    if (cdm->instance->pre_settle == true) {
      pn_delivery_settle(dlv);
    }
    event_count++;
    cd_message_free(cdm);
    cdm = DEQ_HEAD(to_send);
  }

  return event_count;
} /* }}} int amqp1_send_out_messages */

static void check_condition(pn_event_t *e, pn_condition_t *cond) /* {{{ */
{
  if (pn_condition_is_set(cond)) {
    ERROR("amqp1 plugin: %s: %s: %s", pn_event_type_name(pn_event_type(e)),
          pn_condition_get_name(cond), pn_condition_get_description(cond));
    pn_connection_close(pn_event_connection(e));
    conn = NULL;
  }
} /* }}} void check_condition */

static bool handle(pn_event_t *event) /* {{{ */
{

  switch (pn_event_type(event)) {

  case PN_CONNECTION_INIT: {
    conn = pn_event_connection(event);
    pn_connection_set_container(conn, transport->name);
    pn_connection_open(conn);
    pn_session_t *ssn = pn_session(conn);
    pn_session_open(ssn);
    sender = pn_sender(ssn, "cd-sender");
    pn_link_set_snd_settle_mode(sender, PN_SND_MIXED);
    pn_link_open(sender);
    break;
  }

  case PN_LINK_FLOW: {
    /* peer has given us credit, send outbound messages */
    amqp1_send_out_messages(sender);
    break;
  }

  case PN_DELIVERY: {
    /* acknowledgement from peer that a message was delivered */
    pn_delivery_t *dlv = pn_event_delivery(event);
    if (pn_delivery_remote_state(dlv) == PN_ACCEPTED) {
      pn_delivery_settle(dlv);
      acknowledged++;
    }
    break;
  }

  case PN_CONNECTION_WAKE: {
    if (!stopping) {
      amqp1_send_out_messages(sender);
    }
    break;
  }

  case PN_TRANSPORT_CLOSED: {
    check_condition(event, pn_transport_condition(pn_event_transport(event)));
    break;
  }

  case PN_CONNECTION_REMOTE_CLOSE: {
    check_condition(event,
                    pn_session_remote_condition(pn_event_session(event)));
    pn_connection_close(pn_event_connection(event));
    break;
  }

  case PN_SESSION_REMOTE_CLOSE: {
    check_condition(event,
                    pn_session_remote_condition(pn_event_session(event)));
    pn_connection_close(pn_event_connection(event));
    break;
  }

  case PN_LINK_REMOTE_CLOSE:
  case PN_LINK_REMOTE_DETACH: {
    check_condition(event, pn_link_remote_condition(pn_event_link(event)));
    pn_connection_close(pn_event_connection(event));
    break;
  }

  case PN_PROACTOR_INACTIVE: {
    return false;
  }

  default:
    break;
  }
  return true;
} /* }}} bool handle */

static void *event_thread(void __attribute__((unused)) * arg) /* {{{ */
{
  char addr[PN_MAX_ADDR];
  cd_message_t *cdm;

  /* setup proactor */
  proactor = pn_proactor();
  pn_proactor_addr(addr, sizeof(addr), transport->host, transport->port);

  while (!stopping) {
    /* make connection */
    conn = pn_connection();
    if (transport->user != NULL) {
      pn_connection_set_user(conn, transport->user);
      pn_connection_set_password(conn, transport->password);
    }
    pn_proactor_connect(proactor, conn, addr);

    bool engine_running = true;
    while (engine_running && !stopping) {
      pn_event_batch_t *events = pn_proactor_wait(proactor);
      pn_event_t *e;
      while ((e = pn_event_batch_next(events))) {
        engine_running = handle(e);
        if (!engine_running) {
          break;
        }
      }
      pn_proactor_done(proactor, events);
    }

    pn_proactor_release_connection(conn);

    DEBUG("amqp1 plugin: retrying connection");
    int delay = transport->retry_delay;
    while (delay-- > 0 && !stopping) {
      sleep(1.0);
    }
  }

  pn_proactor_disconnect(proactor, NULL);

  /* Free the remaining out_messages */
  cdm = DEQ_HEAD(out_messages);
  while (cdm) {
    DEQ_REMOVE_HEAD(out_messages);
    cd_message_free(cdm);
    cdm = DEQ_HEAD(out_messages);
  }

  event_thread_running = false;

  return NULL;
} /* }}} void event_thread */

static int encqueue(cd_message_t *cdm,
                    amqp1_config_instance_t *instance) /* {{{ */
{
  /* encode message */
  pn_message_t *message = pn_message();
  pn_message_set_address(message, instance->send_to);
  pn_data_t *body = pn_message_body(message);
  pn_data_clear(body);
  pn_data_put_binary(body, pn_bytes(cdm->mbuf.size, cdm->mbuf.start));
  pn_data_exit(body);

  /* put_binary copies and stores so ok to use mbuf */
  cdm->mbuf.size = BUFSIZE;

  int status;
  char *start;
  while ((status = pn_message_encode(message, cdm->mbuf.start,
                                     &cdm->mbuf.size)) == PN_OVERFLOW) {
    DEBUG("amqp1 plugin: increasing message buffer size %zu", cdm->mbuf.size);
    cdm->mbuf.size *= 2;
    start = realloc(cdm->mbuf.start, cdm->mbuf.size);
    if (start == NULL) {
      status = -1;
      break;
    } else {
      cdm->mbuf.start = start;
    }
  }

  if (status != 0) {
    ERROR("amqp1 plugin: error encoding message: %s",
          pn_error_text(pn_message_error(message)));
    pn_message_free(message);
    return -1;
  }

  pthread_mutex_lock(&send_lock);
  DEQ_INSERT_TAIL(out_messages, cdm);
  pthread_mutex_unlock(&send_lock);

  pn_message_free(message);

  /* activate the sender */
  if (conn) {
    pn_connection_wake(conn);
  }

  return 0;
} /* }}} int encqueue */

static int amqp1_notify(notification_t const *n,
                        user_data_t *user_data) /* {{{ */
{
  int status = 0;
  size_t bfree = BUFSIZE;
  size_t bfill = 0;
  size_t bufsize = BUFSIZE;

  if (n == NULL || user_data == NULL)
    return EINVAL;

  amqp1_config_instance_t *instance = user_data->data;

  if (instance->notify != true) {
    ERROR("amqp1 plugin: write notification failed");
  }

  cd_message_t *cdm = malloc(sizeof(*cdm));
  if (cdm == NULL) {
    ERROR("amqp1 plugin: notify failed");
    return -1;
  }

  DEQ_ITEM_INIT(cdm);
  char *start = malloc(bufsize);
  if (start == NULL) {
    ERROR("amqp1 plugin: malloc failed");
    free(cdm);
    return -1;
  }
  cdm->mbuf.size = bufsize;
  cdm->mbuf.start = start;
  cdm->instance = instance;

  switch (instance->format) {
  case AMQP1_FORMAT_JSON:
    format_json_initialize(cdm->mbuf.start, &bfill, &bfree);
    status = format_json_notification(cdm->mbuf.start, bufsize, n);
    if (status != 0) {
      ERROR("amqp1 plugin: formatting notification failed");
      cd_message_free(cdm);
      return status;
    }
    cdm->mbuf.size = strlen(cdm->mbuf.start);
    if (cdm->mbuf.size >= BUFSIZE) {
      ERROR("amqp1 plugin: notify format json failed");
      cd_message_free(cdm);
      return -1;
    }
    break;
  default:
    ERROR("amqp1 plugin: Invalid notify format (%i).", instance->format);
    cd_message_free(cdm);
    return -1;
  }

  /* encode message and place on outbound queue */
  status = encqueue(cdm, instance);
  if (status != 0) {
    ERROR("amqp1 plugin: notify enqueue failed");
    cd_message_free(cdm);
  }
  return status;

} /* }}} int amqp1_notify */

static int amqp1_write(const data_set_t *ds, const value_list_t *vl, /* {{{ */
                       user_data_t *user_data) {
  int status = 0;
  size_t bfree = BUFSIZE;
  size_t bfill = 0;
  size_t bufsize = BUFSIZE;

  if (ds == NULL || vl == NULL || transport == NULL || user_data == NULL)
    return EINVAL;

  amqp1_config_instance_t *instance = user_data->data;

  if (instance->notify != false) {
    ERROR("amqp1 plugin: write failed");
  }

  cd_message_t *cdm = malloc(sizeof(*cdm));
  if (cdm == NULL) {
    ERROR("amqp1 plugin: malloc failed.");
    return -1;
  }
  DEQ_ITEM_INIT(cdm);
  char *start = malloc(bufsize);
  if (start == NULL) {
    ERROR("amqp1 plugin: malloc failed.");
    free(cdm);
    return -1;
  }
  cdm->mbuf.size = bufsize;
  cdm->mbuf.start = start;
  cdm->instance = instance;

  switch (instance->format) {
  case AMQP1_FORMAT_COMMAND:
    status = cmd_create_putval((char *)cdm->mbuf.start, bufsize, ds, vl);
    if (status != 0) {
      ERROR("amqp1 plugin: cmd_create_putval failed with status %i.", status);
      cd_message_free(cdm);
      return status;
    }
    cdm->mbuf.size = strlen(cdm->mbuf.start);
    if (cdm->mbuf.size >= BUFSIZE) {
      ERROR("amqp1 plugin: format cmd failed");
      cd_message_free(cdm);
      return -1;
    }
    break;
  case AMQP1_FORMAT_JSON:
    format_json_initialize((char *)cdm->mbuf.start, &bfill, &bfree);
    format_json_value_list((char *)cdm->mbuf.start, &bfill, &bfree, ds, vl,
                           instance->store_rates);
    status = format_json_finalize((char *)cdm->mbuf.start, &bfill, &bfree);
    if (status != 0) {
      ERROR("amqp1 plugin: format_json_finalize failed with status %i.",
            status);
      cd_message_free(cdm);
      return status;
    }
    cdm->mbuf.size = strlen(cdm->mbuf.start);
    if (cdm->mbuf.size >= BUFSIZE) {
      ERROR("amqp1 plugin: format json failed");
      cd_message_free(cdm);
      return -1;
    }
    break;
  case AMQP1_FORMAT_GRAPHITE:
    status = format_graphite((char *)cdm->mbuf.start, bufsize, ds, vl,
                             instance->prefix, instance->postfix,
                             instance->escape_char, instance->graphite_flags);
    if (status != 0) {
      ERROR("amqp1 plugin: format_graphite failed with status %i.", status);
      cd_message_free(cdm);
      return status;
    }
    cdm->mbuf.size = strlen(cdm->mbuf.start);
    if (cdm->mbuf.size >= BUFSIZE) {
      ERROR("amqp1 plugin: format graphite failed");
      cd_message_free(cdm);
      return -1;
    }
    break;
  default:
    ERROR("amqp1 plugin: Invalid write format (%i).", instance->format);
    cd_message_free(cdm);
    return -1;
  }

  /* encode message and place on outbound queue */
  status = encqueue(cdm, instance);
  if (status != 0) {
    ERROR("amqp1 plugin: write enqueue failed");
    cd_message_free(cdm);
  }
  return status;

} /* }}} int amqp1_write */

static void amqp1_config_transport_free(void *ptr) /* {{{ */
{
  amqp1_config_transport_t *transport = ptr;

  if (transport == NULL)
    return;

  sfree(transport->name);
  sfree(transport->host);
  sfree(transport->port);
  sfree(transport->user);
  sfree(transport->password);
  sfree(transport->address);

  sfree(transport);
} /* }}} void amqp1_config_transport_free */

static void amqp1_config_instance_free(void *ptr) /* {{{ */
{
  amqp1_config_instance_t *instance = ptr;

  if (instance == NULL)
    return;

  sfree(instance->name);
  sfree(instance->prefix);
  sfree(instance->postfix);

  sfree(instance);
} /* }}} void amqp1_config_instance_free */

static int amqp1_config_instance(oconfig_item_t *ci) /* {{{ */
{
  amqp1_config_instance_t *instance = calloc(1, sizeof(*instance));
  if (instance == NULL) {
    ERROR("amqp1 plugin: calloc failed.");
    return ENOMEM;
  }

  int status = cf_util_get_string(ci, &instance->name);
  if (status != 0) {
    sfree(instance);
    return status;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("PreSettle", child->key) == 0)
      status = cf_util_get_boolean(child, &instance->pre_settle);
    else if (strcasecmp("Notify", child->key) == 0)
      status = cf_util_get_boolean(child, &instance->notify);
    else if (strcasecmp("Format", child->key) == 0) {
      char *key = NULL;
      status = cf_util_get_string(child, &key);
      if (status != 0)
        return status;
      assert(key != NULL);
      if (strcasecmp(key, "Command") == 0) {
        instance->format = AMQP1_FORMAT_COMMAND;
      } else if (strcasecmp(key, "Graphite") == 0) {
        instance->format = AMQP1_FORMAT_GRAPHITE;
      } else if (strcasecmp(key, "JSON") == 0) {
        instance->format = AMQP1_FORMAT_JSON;
      } else {
        WARNING("amqp1 plugin: Invalid format string: %s", key);
      }
      sfree(key);
    } else if (strcasecmp("StoreRates", child->key) == 0)
      status = cf_util_get_boolean(child, &instance->store_rates);
    else if (strcasecmp("GraphiteSeparateInstances", child->key) == 0)
      status = cf_util_get_flag(child, &instance->graphite_flags,
                                GRAPHITE_SEPARATE_INSTANCES);
    else if (strcasecmp("GraphiteAlwaysAppendDS", child->key) == 0)
      status = cf_util_get_flag(child, &instance->graphite_flags,
                                GRAPHITE_ALWAYS_APPEND_DS);
    else if (strcasecmp("GraphitePreserveSeparator", child->key) == 0)
      status = cf_util_get_flag(child, &instance->graphite_flags,
                                GRAPHITE_PRESERVE_SEPARATOR);
    else if (strcasecmp("GraphitePrefix", child->key) == 0)
      status = cf_util_get_string(child, &instance->prefix);
    else if (strcasecmp("GraphitePostfix", child->key) == 0)
      status = cf_util_get_string(child, &instance->postfix);
    else if (strcasecmp("GraphiteEscapeChar", child->key) == 0) {
      char *tmp_buff = NULL;
      status = cf_util_get_string(child, &tmp_buff);
      if (status == 0) {
        if (strlen(tmp_buff) > 1)
          WARNING("amqp1 plugin: The option \"GraphiteEscapeChar\" handles "
                  "only one character. Others will be ignored.");
        instance->escape_char = tmp_buff[0];
      }
      sfree(tmp_buff);
    } else
      WARNING("amqp1 plugin: Ignoring unknown "
              "instance configuration option "
              "\"%s\".",
              child->key);
    if (status != 0)
      break;
  }

  if (status != 0) {
    amqp1_config_instance_free(instance);
    return status;
  } else {
    char tpname[DATA_MAX_NAME_LEN];
    status = snprintf(tpname, sizeof(tpname), "amqp1/%s", instance->name);
    if ((status < 0) || (size_t)status >= sizeof(tpname)) {
      ERROR("amqp1 plugin: Instance name would have been truncated.");
      return -1;
    }
    status = snprintf(instance->send_to, sizeof(instance->send_to), "/%s/%s",
                      transport->address, instance->name);
    if ((status < 0) || (size_t)status >= sizeof(instance->send_to)) {
      ERROR("amqp1 plugin: send_to address would have been truncated.");
      return -1;
    }
    if (instance->notify) {
      status = plugin_register_notification(
          tpname, amqp1_notify,
          &(user_data_t){
              .data = instance, .free_func = amqp1_config_instance_free,
          });
    } else {
      status = plugin_register_write(
          tpname, amqp1_write,
          &(user_data_t){
              .data = instance, .free_func = amqp1_config_instance_free,
          });
    }

    if (status != 0) {
      amqp1_config_instance_free(instance);
    }
  }

  return status;
} /* }}} int amqp1_config_instance */

static int amqp1_config_transport(oconfig_item_t *ci) /* {{{ */
{
  transport = calloc(1, sizeof(*transport));
  if (transport == NULL) {
    ERROR("amqp1 plugin: calloc failed.");
    return ENOMEM;
  }

  /* Initialize transport configuration {{{ */
  transport->retry_delay = 1;

  int status = cf_util_get_string(ci, &transport->name);
  if (status != 0) {
    sfree(transport);
    return status;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Host", child->key) == 0)
      status = cf_util_get_string(child, &transport->host);
    else if (strcasecmp("Port", child->key) == 0)
      status = cf_util_get_string(child, &transport->port);
    else if (strcasecmp("User", child->key) == 0)
      status = cf_util_get_string(child, &transport->user);
    else if (strcasecmp("Password", child->key) == 0)
      status = cf_util_get_string(child, &transport->password);
    else if (strcasecmp("Address", child->key) == 0)
      status = cf_util_get_string(child, &transport->address);
    else if (strcasecmp("RetryDelay", child->key) == 0)
      status = cf_util_get_int(child, &transport->retry_delay);
    else if (strcasecmp("Instance", child->key) == 0)
      amqp1_config_instance(child);
    else
      WARNING("amqp1 plugin: Ignoring unknown "
              "transport configuration option "
              "\"%s\".",
              child->key);

    if (status != 0)
      break;
  }

  if (status != 0) {
    amqp1_config_transport_free(transport);
  }
  return status;
} /* }}} int amqp1_config_transport */

static int amqp1_config(oconfig_item_t *ci) /* {{{ */
{

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Transport", child->key) == 0)
      amqp1_config_transport(child);
    else
      WARNING("amqp1 plugin: Ignoring unknown config option \"%s\".",
              child->key);
  }

  return 0;
} /* }}} int amqp1_config */

static int amqp1_init(void) /* {{{ */
{
  if (transport == NULL) {
    ERROR("amqp1: init failed, no transport configured");
    return -1;
  }

  if (proactor == NULL) {
    pthread_mutex_init(&send_lock, /* attr = */ NULL);
    /* start_thread */
    int status =
        plugin_thread_create(&event_thread_id, NULL /* no attributes */,
                             event_thread, NULL /* no argument */, "handle");
    if (status != 0) {
      ERROR("amqp1 plugin: pthread_create failed: %s", STRERRNO);
    } else {
      event_thread_running = true;
    }
  }
  return 0;
} /* }}} int amqp1_init */

static int amqp1_shutdown(void) /* {{{ */
{
  stopping = true;

  /* Stop the proactor thread */
  if (event_thread_running) {
    DEBUG("amqp1 plugin: Shutting down proactor thread.");
    pn_connection_wake(conn);
  }
  pthread_join(event_thread_id, NULL /* no return value */);
  memset(&event_thread_id, 0, sizeof(event_thread_id));

  DEBUG("amqp1 plugin: proactor thread exited.");

  if (transport) {
    amqp1_config_transport_free(transport);
  }

  return 0;
} /* }}} int amqp1_shutdown */

void module_register(void) {
  plugin_register_complex_config("amqp1", amqp1_config);
  plugin_register_init("amqp1", amqp1_init);
  plugin_register_shutdown("amqp1", amqp1_shutdown);
} /* void module_register */
