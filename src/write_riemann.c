/**
 * collectd - src/write_riemann.c
 * Copyright (C) 2012,2013  Pierre-Yves Ritschard
 * Copyright (C) 2013       Florian octo Forster
 * Copyright (C) 2015,2016  Gergely Nagy
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
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 *   Florian octo Forster <octo at collectd.org>
 *   Gergely Nagy <algernon at madhouse-project.org>
 */

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils_cache.h"
#include "utils_complain.h"
#include "write_riemann_threshold.h"

#include <riemann/riemann-client.h>

#define RIEMANN_HOST "localhost"
#define RIEMANN_PORT 5555
#define RIEMANN_TTL_FACTOR 2.0
#define RIEMANN_BATCH_MAX 8192

struct riemann_host {
  c_complain_t init_complaint;
  char *name;
  char *event_service_prefix;
  pthread_mutex_t lock;
  bool batch_mode;
  bool notifications;
  bool check_thresholds;
  bool store_rates;
  bool always_append_ds;
  char *node;
  int port;
  riemann_client_type_t client_type;
  riemann_client_t *client;
  double ttl_factor;
  cdtime_t batch_init;
  int batch_max;
  int batch_timeout;
  int reference_count;
  riemann_message_t *batch_msg;
  char *tls_ca_file;
  char *tls_cert_file;
  char *tls_key_file;
  struct timeval timeout;
};

static char **riemann_tags;
static size_t riemann_tags_num;
static char **riemann_attrs;
static size_t riemann_attrs_num;

/* host->lock must be held when calling this function. */
static int wrr_connect(struct riemann_host *host) /* {{{ */
{
  char const *node;
  int port;

  if (host->client)
    return 0;

  node = (host->node != NULL) ? host->node : RIEMANN_HOST;
  port = (host->port) ? host->port : RIEMANN_PORT;

  host->client = NULL;

  host->client = riemann_client_create(
      host->client_type, node, port, RIEMANN_CLIENT_OPTION_TLS_CA_FILE,
      host->tls_ca_file, RIEMANN_CLIENT_OPTION_TLS_CERT_FILE,
      host->tls_cert_file, RIEMANN_CLIENT_OPTION_TLS_KEY_FILE,
      host->tls_key_file, RIEMANN_CLIENT_OPTION_NONE);
  if (host->client == NULL) {
    c_complain(LOG_ERR, &host->init_complaint,
               "write_riemann plugin: Unable to connect to Riemann at %s:%d",
               node, port);
    return -1;
  }
#if RCC_VERSION_NUMBER >= 0x010800
  if (host->timeout.tv_sec != 0) {
    if (riemann_client_set_timeout(host->client, &host->timeout) != 0) {
      riemann_client_free(host->client);
      host->client = NULL;
      c_complain(LOG_ERR, &host->init_complaint,
                 "write_riemann plugin: Unable to connect to Riemann at %s:%d",
                 node, port);
      return -1;
    }
  }
#endif

  set_sock_opts(riemann_client_get_fd(host->client));

  c_release(LOG_INFO, &host->init_complaint,
            "write_riemann plugin: Successfully connected to %s:%d", node,
            port);

  return 0;
} /* }}} int wrr_connect */

/* host->lock must be held when calling this function. */
static int wrr_disconnect(struct riemann_host *host) /* {{{ */
{
  if (!host->client)
    return 0;

  riemann_client_free(host->client);
  host->client = NULL;

  return 0;
} /* }}} int wrr_disconnect */

/**
 * Function to send messages to riemann.
 *
 * Acquires the host lock, disconnects on errors.
 */
static int wrr_send_nolock(struct riemann_host *host,
                           riemann_message_t *msg) /* {{{ */
{
  int status = 0;

  status = wrr_connect(host);
  if (status != 0) {
    return status;
  }

  status = riemann_client_send_message(host->client, msg);
  if (status != 0) {
    wrr_disconnect(host);
    return status;
  }

  /*
   * For TCP we need to receive message acknowledgemenent.
   */
  if (host->client_type != RIEMANN_CLIENT_UDP) {
    riemann_message_t *response;

    response = riemann_client_recv_message(host->client);

    if (response == NULL) {
      wrr_disconnect(host);
      return errno;
    }
    riemann_message_free(response);
  }

  return 0;
} /* }}} int wrr_send */

static int wrr_send(struct riemann_host *host, riemann_message_t *msg) {
  int status = 0;

  pthread_mutex_lock(&host->lock);
  status = wrr_send_nolock(host, msg);
  pthread_mutex_unlock(&host->lock);
  return status;
}

static riemann_message_t *wrr_notification_to_message(notification_t const *n) {
  riemann_message_t *msg;
  riemann_event_t *event;
  char service_buffer[6 * DATA_MAX_NAME_LEN];
  char const *severity;

  switch (n->severity) {
  case NOTIF_OKAY:
    severity = "ok";
    break;
  case NOTIF_WARNING:
    severity = "warning";
    break;
  case NOTIF_FAILURE:
    severity = "critical";
    break;
  default:
    severity = "unknown";
  }

  format_name(service_buffer, sizeof(service_buffer),
              /* host = */ "", n->plugin, n->plugin_instance, n->type,
              n->type_instance);

  event = riemann_event_create(
      RIEMANN_EVENT_FIELD_HOST, n->host, RIEMANN_EVENT_FIELD_TIME,
      (int64_t)CDTIME_T_TO_TIME_T(n->time), RIEMANN_EVENT_FIELD_TAGS,
      "notification", NULL, RIEMANN_EVENT_FIELD_STATE, severity,
      RIEMANN_EVENT_FIELD_SERVICE, &service_buffer[1],
      RIEMANN_EVENT_FIELD_NONE);

#if RCC_VERSION_NUMBER >= 0x010A00
  riemann_event_set(event, RIEMANN_EVENT_FIELD_TIME_MICROS,
                    (int64_t)CDTIME_T_TO_US(n->time));
#endif

  if (n->host[0] != 0)
    riemann_event_string_attribute_add(event, "host", n->host);
  if (n->plugin[0] != 0)
    riemann_event_string_attribute_add(event, "plugin", n->plugin);
  if (n->plugin_instance[0] != 0)
    riemann_event_string_attribute_add(event, "plugin_instance",
                                       n->plugin_instance);

  if (n->type[0] != 0)
    riemann_event_string_attribute_add(event, "type", n->type);
  if (n->type_instance[0] != 0)
    riemann_event_string_attribute_add(event, "type_instance",
                                       n->type_instance);

  for (size_t i = 0; i < riemann_attrs_num; i += 2)
    riemann_event_string_attribute_add(event, riemann_attrs[i],
                                       riemann_attrs[i + 1]);

  for (size_t i = 0; i < riemann_tags_num; i++)
    riemann_event_tag_add(event, riemann_tags[i]);

  if (n->message[0] != 0)
    riemann_event_string_attribute_add(event, "description", n->message);

  /* Pull in values from threshold and add extra attributes */
  for (notification_meta_t *meta = n->meta; meta != NULL; meta = meta->next) {
    if (strcasecmp("CurrentValue", meta->name) == 0 &&
        meta->type == NM_TYPE_DOUBLE) {
      riemann_event_set(event, RIEMANN_EVENT_FIELD_METRIC_D,
                        (double)meta->nm_value.nm_double,
                        RIEMANN_EVENT_FIELD_NONE);
      continue;
    }

    if (meta->type == NM_TYPE_STRING) {
      riemann_event_string_attribute_add(event, meta->name,
                                         meta->nm_value.nm_string);
      continue;
    }
  }

  msg = riemann_message_create_with_events(event, NULL);
  if (msg == NULL) {
    ERROR("write_riemann plugin: riemann_message_create_with_events() failed.");
    riemann_event_free(event);
    return NULL;
  }

  DEBUG("write_riemann plugin: Successfully created message for notification: "
        "host = \"%s\", service = \"%s\", state = \"%s\"",
        event->host, event->service, event->state);
  return msg;
}

static riemann_event_t *
wrr_value_to_event(struct riemann_host const *host, /* {{{ */
                   data_set_t const *ds, value_list_t const *vl, size_t index,
                   gauge_t const *rates, int status) {
  riemann_event_t *event;
  char name_buffer[5 * DATA_MAX_NAME_LEN];
  char service_buffer[6 * DATA_MAX_NAME_LEN];
  size_t i;

  event = riemann_event_new();
  if (event == NULL) {
    ERROR("write_riemann plugin: riemann_event_new() failed.");
    return NULL;
  }

  format_name(name_buffer, sizeof(name_buffer),
              /* host = */ "", vl->plugin, vl->plugin_instance, vl->type,
              vl->type_instance);
  if (host->always_append_ds || (ds->ds_num > 1)) {
    if (host->event_service_prefix == NULL)
      ssnprintf(service_buffer, sizeof(service_buffer), "%s/%s",
                &name_buffer[1], ds->ds[index].name);
    else
      ssnprintf(service_buffer, sizeof(service_buffer), "%s%s/%s",
                host->event_service_prefix, &name_buffer[1],
                ds->ds[index].name);
  } else {
    if (host->event_service_prefix == NULL)
      sstrncpy(service_buffer, &name_buffer[1], sizeof(service_buffer));
    else
      ssnprintf(service_buffer, sizeof(service_buffer), "%s%s",
                host->event_service_prefix, &name_buffer[1]);
  }

  riemann_event_set(
      event, RIEMANN_EVENT_FIELD_HOST, vl->host, RIEMANN_EVENT_FIELD_TIME,
      (int64_t)CDTIME_T_TO_TIME_T(vl->time), RIEMANN_EVENT_FIELD_TTL,
      (float)CDTIME_T_TO_DOUBLE(vl->interval) * host->ttl_factor,
      RIEMANN_EVENT_FIELD_STRING_ATTRIBUTES, "plugin", vl->plugin, "type",
      vl->type, "ds_name", ds->ds[index].name, NULL,
      RIEMANN_EVENT_FIELD_SERVICE, service_buffer, RIEMANN_EVENT_FIELD_NONE);

#if RCC_VERSION_NUMBER >= 0x010A00
  riemann_event_set(event, RIEMANN_EVENT_FIELD_TIME_MICROS,
                    (int64_t)CDTIME_T_TO_US(vl->time));
#endif

  if (host->check_thresholds) {
    const char *state = NULL;

    switch (status) {
    case STATE_OKAY:
      state = "ok";
      break;
    case STATE_ERROR:
      state = "critical";
      break;
    case STATE_WARNING:
      state = "warning";
      break;
    case STATE_MISSING:
      state = "unknown";
      break;
    }
    if (state)
      riemann_event_set(event, RIEMANN_EVENT_FIELD_STATE, state,
                        RIEMANN_EVENT_FIELD_NONE);
  }

  if (vl->plugin_instance[0] != 0)
    riemann_event_string_attribute_add(event, "plugin_instance",
                                       vl->plugin_instance);
  if (vl->type_instance[0] != 0)
    riemann_event_string_attribute_add(event, "type_instance",
                                       vl->type_instance);

  if ((ds->ds[index].type != DS_TYPE_GAUGE) && (rates != NULL)) {
    char ds_type[DATA_MAX_NAME_LEN];

    ssnprintf(ds_type, sizeof(ds_type), "%s:rate",
              DS_TYPE_TO_STRING(ds->ds[index].type));
    riemann_event_string_attribute_add(event, "ds_type", ds_type);
  } else {
    riemann_event_string_attribute_add(event, "ds_type",
                                       DS_TYPE_TO_STRING(ds->ds[index].type));
  }

  {
    char ds_index[DATA_MAX_NAME_LEN];

    ssnprintf(ds_index, sizeof(ds_index), "%" PRIsz, index);
    riemann_event_string_attribute_add(event, "ds_index", ds_index);
  }

  for (i = 0; i < riemann_attrs_num; i += 2)
    riemann_event_string_attribute_add(event, riemann_attrs[i],
                                       riemann_attrs[i + 1]);

  for (i = 0; i < riemann_tags_num; i++)
    riemann_event_tag_add(event, riemann_tags[i]);

  if (ds->ds[index].type == DS_TYPE_GAUGE) {
    riemann_event_set(event, RIEMANN_EVENT_FIELD_METRIC_D,
                      (double)vl->values[index].gauge,
                      RIEMANN_EVENT_FIELD_NONE);
  } else if (rates != NULL) {
    riemann_event_set(event, RIEMANN_EVENT_FIELD_METRIC_D, (double)rates[index],
                      RIEMANN_EVENT_FIELD_NONE);
  } else {
    int64_t metric;

    if (ds->ds[index].type == DS_TYPE_DERIVE)
      metric = (int64_t)vl->values[index].derive;
    else if (ds->ds[index].type == DS_TYPE_ABSOLUTE)
      metric = (int64_t)vl->values[index].absolute;
    else
      metric = (int64_t)vl->values[index].counter;

    riemann_event_set(event, RIEMANN_EVENT_FIELD_METRIC_S64, (int64_t)metric,
                      RIEMANN_EVENT_FIELD_NONE);
  }

  if (vl->meta) {
    char **toc;
    int n = meta_data_toc(vl->meta, &toc);

    for (int i = 0; i < n; i++) {
      char *key = toc[i];
      char *value;

      if (0 == meta_data_as_string(vl->meta, key, &value)) {
        riemann_event_string_attribute_add(event, key, value);
        free(value);
      }
      free(toc[i]);
    }

    free(toc);
  }

  DEBUG("write_riemann plugin: Successfully created message for metric: "
        "host = \"%s\", service = \"%s\"",
        event->host, event->service);
  return event;
} /* }}} riemann_event_t *wrr_value_to_event */

static riemann_message_t *
wrr_value_list_to_message(struct riemann_host const *host, /* {{{ */
                          data_set_t const *ds, value_list_t const *vl,
                          int *statuses) {
  riemann_message_t *msg;
  size_t i;
  gauge_t *rates = NULL;

  /* Initialize the Msg structure. */
  msg = riemann_message_new();
  if (msg == NULL) {
    ERROR("write_riemann plugin: riemann_message_new failed.");
    return NULL;
  }

  if (host->store_rates) {
    rates = uc_get_rate(ds, vl);
    if (rates == NULL) {
      ERROR("write_riemann plugin: uc_get_rate failed.");
      riemann_message_free(msg);
      return NULL;
    }
  }

  for (i = 0; i < vl->values_len; i++) {
    riemann_event_t *event;

    event = wrr_value_to_event(host, ds, vl, (int)i, rates, statuses[i]);
    if (event == NULL) {
      riemann_message_free(msg);
      sfree(rates);
      return NULL;
    }
    riemann_message_append_events(msg, event, NULL);
  }

  sfree(rates);
  return msg;
} /* }}} riemann_message_t *wrr_value_list_to_message */

/*
 * Always call while holding host->lock !
 */
static int wrr_batch_flush_nolock(cdtime_t timeout, struct riemann_host *host) {
  cdtime_t now;
  int status = 0;

  now = cdtime();
  if (timeout > 0) {
    if ((host->batch_init + timeout) > now) {
      return status;
    }
  }
  wrr_send_nolock(host, host->batch_msg);
  riemann_message_free(host->batch_msg);

  host->batch_init = now;
  host->batch_msg = NULL;
  return status;
}

static int wrr_batch_flush(cdtime_t timeout,
                           const char *identifier __attribute__((unused)),
                           user_data_t *user_data) {
  struct riemann_host *host;
  int status;

  if (user_data == NULL)
    return -EINVAL;

  host = user_data->data;
  pthread_mutex_lock(&host->lock);
  status = wrr_batch_flush_nolock(timeout, host);
  if (status != 0)
    c_complain(
        LOG_ERR, &host->init_complaint,
        "write_riemann plugin: riemann_client_send failed with status %i",
        status);
  else
    c_release(LOG_DEBUG, &host->init_complaint,
              "write_riemann plugin: batch sent.");

  pthread_mutex_unlock(&host->lock);
  return status;
}

static int wrr_batch_add_value_list(struct riemann_host *host, /* {{{ */
                                    data_set_t const *ds,
                                    value_list_t const *vl, int *statuses) {
  riemann_message_t *msg;
  size_t len;
  int ret;
  cdtime_t timeout;

  msg = wrr_value_list_to_message(host, ds, vl, statuses);
  if (msg == NULL)
    return -1;

  pthread_mutex_lock(&host->lock);

  if (host->batch_msg == NULL) {
    host->batch_msg = msg;
  } else {
    int status;

    status = riemann_message_append_events_n(host->batch_msg, msg->n_events,
                                             msg->events);
    msg->n_events = 0;
    msg->events = NULL;

    riemann_message_free(msg);

    if (status != 0) {
      pthread_mutex_unlock(&host->lock);
      ERROR("write_riemann plugin: out of memory");
      return -1;
    }
  }

  len = riemann_message_get_packed_size(host->batch_msg);
  ret = 0;
  if ((host->batch_max < 0) || (((size_t)host->batch_max) <= len)) {
    ret = wrr_batch_flush_nolock(0, host);
  } else {
    if (host->batch_timeout > 0) {
      timeout = TIME_T_TO_CDTIME_T((time_t)host->batch_timeout);
      ret = wrr_batch_flush_nolock(timeout, host);
    }
  }

  pthread_mutex_unlock(&host->lock);
  return ret;
} /* }}} riemann_message_t *wrr_batch_add_value_list */

static int wrr_notification(const notification_t *n, user_data_t *ud) /* {{{ */
{
  int status;
  struct riemann_host *host = ud->data;
  riemann_message_t *msg;

  if (!host->notifications)
    return 0;

  /*
   * Never batch for notifications, send them ASAP
   */
  msg = wrr_notification_to_message(n);
  if (msg == NULL)
    return -1;

  status = wrr_send(host, msg);
  if (status != 0)
    c_complain(
        LOG_ERR, &host->init_complaint,
        "write_riemann plugin: riemann_client_send failed with status %i",
        status);
  else
    c_release(LOG_DEBUG, &host->init_complaint,
              "write_riemann plugin: riemann_client_send succeeded");

  riemann_message_free(msg);
  return status;
} /* }}} int wrr_notification */

static int wrr_write(const data_set_t *ds, /* {{{ */
                     const value_list_t *vl, user_data_t *ud) {
  int status = 0;
  int statuses[vl->values_len];
  struct riemann_host *host = ud->data;
  riemann_message_t *msg;

  if (host->check_thresholds) {
    status = write_riemann_threshold_check(ds, vl, statuses);
    if (status != 0)
      return status;
  } else {
    memset(statuses, 0, sizeof(statuses));
  }

  if (host->client_type != RIEMANN_CLIENT_UDP && host->batch_mode) {
    wrr_batch_add_value_list(host, ds, vl, statuses);
  } else {
    msg = wrr_value_list_to_message(host, ds, vl, statuses);
    if (msg == NULL)
      return -1;

    status = wrr_send(host, msg);

    riemann_message_free(msg);
  }
  return status;
} /* }}} int wrr_write */

static void wrr_free(void *p) /* {{{ */
{
  struct riemann_host *host = p;

  if (host == NULL)
    return;

  pthread_mutex_lock(&host->lock);

  host->reference_count--;
  if (host->reference_count > 0) {
    pthread_mutex_unlock(&host->lock);
    return;
  }

  wrr_disconnect(host);

  pthread_mutex_lock(&host->lock);
  pthread_mutex_destroy(&host->lock);
  sfree(host);
} /* }}} void wrr_free */

static int wrr_config_node(oconfig_item_t *ci) /* {{{ */
{
  struct riemann_host *host = NULL;
  int status = 0;
  int i;
  oconfig_item_t *child;
  char callback_name[DATA_MAX_NAME_LEN];

  if ((host = calloc(1, sizeof(*host))) == NULL) {
    ERROR("write_riemann plugin: calloc failed.");
    return ENOMEM;
  }
  pthread_mutex_init(&host->lock, NULL);
  C_COMPLAIN_INIT(&host->init_complaint);
  host->reference_count = 1;
  host->node = NULL;
  host->port = 0;
  host->notifications = true;
  host->check_thresholds = false;
  host->store_rates = true;
  host->always_append_ds = false;
  host->batch_mode = true;
  host->batch_max = RIEMANN_BATCH_MAX; /* typical MSS */
  host->batch_init = cdtime();
  host->batch_timeout = 0;
  host->ttl_factor = RIEMANN_TTL_FACTOR;
  host->client = NULL;
  host->client_type = RIEMANN_CLIENT_TCP;
  host->timeout.tv_sec = 0;
  host->timeout.tv_usec = 0;

  status = cf_util_get_string(ci, &host->name);
  if (status != 0) {
    WARNING("write_riemann plugin: Required host name is missing.");
    wrr_free(host);
    return -1;
  }

  for (i = 0; i < ci->children_num; i++) {
    /*
     * The code here could be simplified but makes room
     * for easy adding of new options later on.
     */
    child = &ci->children[i];
    status = 0;

    if (strcasecmp("Host", child->key) == 0) {
      status = cf_util_get_string(child, &host->node);
      if (status != 0)
        break;
    } else if (strcasecmp("Notifications", child->key) == 0) {
      status = cf_util_get_boolean(child, &host->notifications);
      if (status != 0)
        break;
    } else if (strcasecmp("EventServicePrefix", child->key) == 0) {
      status = cf_util_get_string(child, &host->event_service_prefix);
      if (status != 0)
        break;
    } else if (strcasecmp("CheckThresholds", child->key) == 0) {
      status = cf_util_get_boolean(child, &host->check_thresholds);
      if (status != 0)
        break;
    } else if (strcasecmp("Batch", child->key) == 0) {
      status = cf_util_get_boolean(child, &host->batch_mode);
      if (status != 0)
        break;
    } else if (strcasecmp("BatchMaxSize", child->key) == 0) {
      status = cf_util_get_int(child, &host->batch_max);
      if (status != 0)
        break;
    } else if (strcasecmp("BatchFlushTimeout", child->key) == 0) {
      status = cf_util_get_int(child, &host->batch_timeout);
      if (status != 0)
        break;
    } else if (strcasecmp("Timeout", child->key) == 0) {
#if RCC_VERSION_NUMBER >= 0x010800
      status = cf_util_get_int(child, (int *)&host->timeout.tv_sec);
      if (status != 0)
        break;
#else
      WARNING("write_riemann plugin: The Timeout option is not supported. "
              "Please upgrade the Riemann client to at least 1.8.0.");
#endif
    } else if (strcasecmp("Port", child->key) == 0) {
      host->port = cf_util_get_port_number(child);
      if (host->port == -1) {
        break;
      }
    } else if (strcasecmp("Protocol", child->key) == 0) {
      char tmp[16];
      status = cf_util_get_string_buffer(child, tmp, sizeof(tmp));
      if (status != 0)
        break;

      if (strcasecmp("UDP", tmp) == 0)
        host->client_type = RIEMANN_CLIENT_UDP;
      else if (strcasecmp("TCP", tmp) == 0)
        host->client_type = RIEMANN_CLIENT_TCP;
      else if (strcasecmp("TLS", tmp) == 0)
        host->client_type = RIEMANN_CLIENT_TLS;
      else
        WARNING("write_riemann plugin: The value "
                "\"%s\" is not valid for the "
                "\"Protocol\" option. Use "
                "either \"UDP\", \"TCP\" or \"TLS\".",
                tmp);
    } else if (strcasecmp("TLSCAFile", child->key) == 0) {
      status = cf_util_get_string(child, &host->tls_ca_file);
      if (status != 0)
        break;
    } else if (strcasecmp("TLSCertFile", child->key) == 0) {
      status = cf_util_get_string(child, &host->tls_cert_file);
      if (status != 0)
        break;
    } else if (strcasecmp("TLSKeyFile", child->key) == 0) {
      status = cf_util_get_string(child, &host->tls_key_file);
      if (status != 0)
        break;
    } else if (strcasecmp("StoreRates", child->key) == 0) {
      status = cf_util_get_boolean(child, &host->store_rates);
      if (status != 0)
        break;
    } else if (strcasecmp("AlwaysAppendDS", child->key) == 0) {
      status = cf_util_get_boolean(child, &host->always_append_ds);
      if (status != 0)
        break;
    } else if (strcasecmp("TTLFactor", child->key) == 0) {
      double tmp = NAN;
      status = cf_util_get_double(child, &tmp);
      if (status != 0)
        break;
      if (tmp >= 2.0) {
        host->ttl_factor = tmp;
      } else if (tmp >= 1.0) {
        NOTICE("write_riemann plugin: The configured "
               "TTLFactor is very small "
               "(%.1f). A value of 2.0 or "
               "greater is recommended.",
               tmp);
        host->ttl_factor = tmp;
      } else if (tmp > 0.0) {
        WARNING("write_riemann plugin: The configured "
                "TTLFactor is too small to be "
                "useful (%.1f). I'll use it "
                "since the user knows best, "
                "but under protest.",
                tmp);
        host->ttl_factor = tmp;
      } else { /* zero, negative and NAN */
        ERROR("write_riemann plugin: The configured "
              "TTLFactor is invalid (%.1f).",
              tmp);
      }
    } else {
      WARNING("write_riemann plugin: ignoring unknown config "
              "option: \"%s\"",
              child->key);
    }
  }
  if (status != 0) {
    wrr_free(host);
    return status;
  }

  ssnprintf(callback_name, sizeof(callback_name), "write_riemann/%s",
            host->name);

  user_data_t ud = {.data = host, .free_func = wrr_free};

  pthread_mutex_lock(&host->lock);

  status = plugin_register_write(callback_name, wrr_write, &ud);

  if (host->client_type != RIEMANN_CLIENT_UDP && host->batch_mode) {
    ud.free_func = NULL;
    plugin_register_flush(callback_name, wrr_batch_flush, &ud);
  }
  if (status != 0)
    WARNING("write_riemann plugin: plugin_register_write (\"%s\") "
            "failed with status %i.",
            callback_name, status);
  else /* success */
    host->reference_count++;

  status = plugin_register_notification(callback_name, wrr_notification, &ud);
  if (status != 0)
    WARNING("write_riemann plugin: plugin_register_notification (\"%s\") "
            "failed with status %i.",
            callback_name, status);
  else /* success */
    host->reference_count++;

  if (host->reference_count <= 1) {
    /* Both callbacks failed => free memory.
     * We need to unlock here, because riemann_free() will lock.
     * This is not a race condition, because we're the only one
     * holding a reference. */
    pthread_mutex_unlock(&host->lock);
    wrr_free(host);
    return -1;
  }

  host->reference_count--;
  pthread_mutex_unlock(&host->lock);

  return status;
} /* }}} int wrr_config_node */

static int wrr_config(oconfig_item_t *ci) /* {{{ */
{
  int i;
  oconfig_item_t *child;
  int status;

  for (i = 0; i < ci->children_num; i++) {
    child = &ci->children[i];

    if (strcasecmp("Node", child->key) == 0) {
      wrr_config_node(child);
    } else if (strcasecmp(child->key, "attribute") == 0) {
      char *key = NULL;
      char *val = NULL;

      if (child->values_num != 2) {
        WARNING("riemann attributes need both a key and a value.");
        return -1;
      }
      if (child->values[0].type != OCONFIG_TYPE_STRING ||
          child->values[1].type != OCONFIG_TYPE_STRING) {
        WARNING("riemann attribute needs string arguments.");
        return -1;
      }
      if ((key = strdup(child->values[0].value.string)) == NULL) {
        WARNING("cannot allocate memory for attribute key.");
        return -1;
      }
      if ((val = strdup(child->values[1].value.string)) == NULL) {
        WARNING("cannot allocate memory for attribute value.");
        sfree(key);
        return -1;
      }
      strarray_add(&riemann_attrs, &riemann_attrs_num, key);
      strarray_add(&riemann_attrs, &riemann_attrs_num, val);
      DEBUG("write_riemann: got attr: %s => %s", key, val);
      sfree(key);
      sfree(val);
    } else if (strcasecmp(child->key, "tag") == 0) {
      char *tmp = NULL;
      status = cf_util_get_string(child, &tmp);
      if (status != 0)
        continue;

      strarray_add(&riemann_tags, &riemann_tags_num, tmp);
      DEBUG("write_riemann plugin: Got tag: %s", tmp);
      sfree(tmp);
    } else {
      WARNING("write_riemann plugin: Ignoring unknown "
              "configuration option \"%s\" at top level.",
              child->key);
    }
  }
  return 0;
} /* }}} int wrr_config */

void module_register(void) {
  plugin_register_complex_config("write_riemann", wrr_config);
}
