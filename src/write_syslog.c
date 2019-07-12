/**
 * collectd - src/write_syslog.c
 * Copyright (C) 2012       Pierre-Yves Ritschard
 * Copyright (C) 2011       Scott Sanders
 * Copyright (C) 2009       Paul Sadauskas
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2007-2012  Florian octo Forster
 * Copyright (C) 2013-2014  Limelight Networks, Inc.
 * Copyright (C) 2019       Shirly Radco
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
 * Based on the write_graphite plugin. Authors:
 *   Florian octo Forster <octo at collectd.org>
 *   Doug MacEachern <dougm at hyperic.com>
 *   Paul Sadauskas <psadauskas at gmail.com>
 *   Scott Sanders <scott at jssjr.com>
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 * Based on the write_tsdb plugin. Authors:
 *   Brett Hawn <bhawn at llnw.com>
 *   Kevin Bowling <kbowling@llnw.com>
 * write_syslog. Authors:
 *   Shirly Radco <sradco@redhat.com>
 **/

/* write_syslog plugin configuration example
 *
 * <Plugin write_syslog>
 *   <Node>
 *     Host "localhost"
 *     Port "44514"
 *     Prefix "collectd"
 *     MessageFormat "human"
 *     HostTags "["prefix1" "example1"="example1_v"]
 *   </Node>
 * </Plugin>
 *
 */

#include "collectd.h"
#include "utils/common/common.h"

#include "plugin.h"
#include "utils_cache.h"
#include "utils_random.h"

#include <netdb.h>

#define WS_DEFAULT_NODE "localhost"

#define WS_DEFAULT_SERVICE "44514"

#define WS_DEFAULT_FORMAT "human"

#define WS_DEFAULT_PREFIX "collectd"

#define WS_DEFAULT_ESCAPE '.'

/* Ethernet - (IPv6 + TCP) = 1500 - (40 + 32) = 1428 */
#define WS_SEND_BUF_SIZE 1428

/*
 * Private variables
 */
struct ws_callback {
  struct addrinfo *ai;
  cdtime_t ai_last_update;
  int sock_fd;

  char *node;
  char *service;
  char *host_tags;
  char *msg_format;
  char *metrics_prefix;
  bool store_rates;
  bool always_append_ds;

  char send_buf[WS_SEND_BUF_SIZE];
  size_t send_buf_free;
  size_t send_buf_fill;
  cdtime_t send_buf_init_time;

  pthread_mutex_t send_lock;

  bool connect_failed_log_enabled;
  int connect_dns_failed_attempts_remaining;
  cdtime_t next_random_ttl;
};

static cdtime_t resolve_interval;
static cdtime_t resolve_jitter;

/*
 * Functions
 */
static void ws_reset_buffer(struct ws_callback *cb) {
  memset(cb->send_buf, 0, sizeof(cb->send_buf));
  cb->send_buf_free = sizeof(cb->send_buf);
  cb->send_buf_fill = 0;
  cb->send_buf_init_time = cdtime();
}

static int ws_send_buffer(struct ws_callback *cb) {
  ssize_t status = 0;

  status = swrite(cb->sock_fd, cb->send_buf, strlen(cb->send_buf));
  if (status != 0) {
    ERROR("write_syslog plugin: send failed with status %zi (%s)", status,
          STRERRNO);

    if (cb->sock_fd > 0) {
      close(cb->sock_fd);
      cb->sock_fd = -1;
    }

    return -1;
  }

  return 0;
}

/* NOTE: You must hold cb->send_lock when calling this function! */
static int ws_flush_nolock(cdtime_t timeout, struct ws_callback *cb) {
  int status;

  DEBUG("write_syslog plugin: ws_flush_nolock: timeout = %.3f; "
        "send_buf_fill = %" PRIsz ";",
        (double)timeout, cb->send_buf_fill);

  /* timeout == 0  => flush unconditionally */
  if (timeout > 0) {
    cdtime_t now;

    now = cdtime();
    if ((cb->send_buf_init_time + timeout) > now)
      return 0;
  }

  if (cb->send_buf_fill == 0) {
    cb->send_buf_init_time = cdtime();
    return 0;
  }

  status = ws_send_buffer(cb);
  ws_reset_buffer(cb);

  return status;
}

static cdtime_t new_random_ttl(void) {
  if (resolve_jitter == 0)
    return 0;

  return (cdtime_t)cdrand_range(0, (long)resolve_jitter);
}

static int ws_callback_init(struct ws_callback *cb) {
  int status;
  cdtime_t now;

  const char *node = cb->node ? cb->node : WS_DEFAULT_NODE;
  const char *service = cb->service ? cb->service : WS_DEFAULT_SERVICE;

  if (cb->sock_fd > 0)
    return 0;

  now = cdtime();
  if (cb->ai) {
    /* When we are here, we still have the IP in cache.
     * If we have remaining attempts without calling the DNS, we update the
     * last_update date so we keep the info until next time.
     * If there is no more attempts, we need to flush the cache.
     */

    if ((cb->ai_last_update + resolve_interval + cb->next_random_ttl) < now) {
      cb->next_random_ttl = new_random_ttl();
      if (cb->connect_dns_failed_attempts_remaining > 0) {
        /* Warning : this is run under send_lock mutex.
         * This is why we do not use another mutex here.
         * */
        cb->ai_last_update = now;
        cb->connect_dns_failed_attempts_remaining--;
      } else {
        freeaddrinfo(cb->ai);
        cb->ai = NULL;
      }
    }
  }

  if (cb->ai == NULL) {
    if ((cb->ai_last_update + resolve_interval + cb->next_random_ttl) >= now) {
      DEBUG("write_syslog plugin: too many getaddrinfo(%s, %s) failures", node,
            service);
      return -1;
    }
    cb->ai_last_update = now;
    cb->next_random_ttl = new_random_ttl();

    struct addrinfo ai_hints = {
        .ai_family = AF_UNSPEC,
        .ai_flags = AI_ADDRCONFIG,
        .ai_socktype = SOCK_STREAM,
    };

    status = getaddrinfo(node, service, &ai_hints, &cb->ai);
    if (status != 0) {
      if (cb->ai) {
        freeaddrinfo(cb->ai);
        cb->ai = NULL;
      }
      if (cb->connect_failed_log_enabled) {
        ERROR("write_syslog plugin: getaddrinfo(%s, %s) failed: %s", node,
              service, gai_strerror(status));
        cb->connect_failed_log_enabled = 0;
      }
      return -1;
    }
  }

  assert(cb->ai != NULL);
  for (struct addrinfo *ai = cb->ai; ai != NULL; ai = ai->ai_next) {
    cb->sock_fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (cb->sock_fd < 0)
      continue;

    set_sock_opts(cb->sock_fd);

    status = connect(cb->sock_fd, ai->ai_addr, ai->ai_addrlen);
    if (status != 0) {
      close(cb->sock_fd);
      cb->sock_fd = -1;
      continue;
    }

    break;
  }

  if (cb->sock_fd < 0) {
    ERROR("write_syslog plugin: Connecting to %s:%s failed. "
          "The last error was: %s",
          node, service, STRERRNO);
    return -1;
  }

  if (cb->connect_failed_log_enabled == 0) {
    INFO("write_syslog plugin: Connecting to %s:%s succeeded.", node, service);
    cb->connect_failed_log_enabled = 1;
  }
  cb->connect_dns_failed_attempts_remaining = 1;

  ws_reset_buffer(cb);

  return 0;
}

static void ws_callback_free(void *data) {
  struct ws_callback *cb;

  if (data == NULL)
    return;

  cb = data;

  pthread_mutex_lock(&cb->send_lock);

  ws_flush_nolock(0, cb);

  close(cb->sock_fd);
  cb->sock_fd = -1;

  sfree(cb->node);
  sfree(cb->service);
  sfree(cb->host_tags);
  sfree(cb->msg_format);
  sfree(cb->metrics_prefix);

  pthread_mutex_unlock(&cb->send_lock);
  pthread_mutex_destroy(&cb->send_lock);

  sfree(cb);
}

static int ws_flush(cdtime_t timeout,
                    const char *identifier __attribute__((unused)),
                    user_data_t *user_data) {
  struct ws_callback *cb;
  int status;

  if (user_data == NULL)
    return -EINVAL;

  cb = user_data->data;

  pthread_mutex_lock(&cb->send_lock);

  if (cb->sock_fd < 0) {
    status = ws_callback_init(cb);
    if (status != 0) {
      ERROR("write_syslog plugin: ws_callback_init failed.");
      pthread_mutex_unlock(&cb->send_lock);
      return -1;
    }
  }

  status = ws_flush_nolock(timeout, cb);
  pthread_mutex_unlock(&cb->send_lock);

  return status;
}

static int ws_format_values(char *ret, size_t ret_len, int ds_num,
                            const data_set_t *ds, const value_list_t *vl,
                            bool store_rates) {
  size_t offset = 0;
  int status;
  gauge_t *rates = NULL;

  assert(strcmp(ds->type, vl->type) == 0);

  memset(ret, 0, ret_len);

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    status = snprintf(ret + offset, ret_len - offset, __VA_ARGS__);            \
    if (status < 1) {                                                          \
      sfree(rates);                                                            \
      return -1;                                                               \
    } else if (((size_t)status) >= (ret_len - offset)) {                       \
      sfree(rates);                                                            \
      return -1;                                                               \
    } else                                                                     \
      offset += ((size_t)status);                                              \
  } while (0)

  if (ds->ds[ds_num].type == DS_TYPE_GAUGE)
    BUFFER_ADD(GAUGE_FORMAT, vl->values[ds_num].gauge);
  else if (store_rates) {
    if (rates == NULL)
      rates = uc_get_rate(ds, vl);
    if (rates == NULL) {
      WARNING("format_values: "
              "uc_get_rate failed.");
      return -1;
    }
    BUFFER_ADD(GAUGE_FORMAT, rates[ds_num]);
  } else if (ds->ds[ds_num].type == DS_TYPE_COUNTER)
    BUFFER_ADD("%" PRIu64, (uint64_t)vl->values[ds_num].counter);
  else if (ds->ds[ds_num].type == DS_TYPE_DERIVE)
    BUFFER_ADD("%" PRIi64, vl->values[ds_num].derive);
  else if (ds->ds[ds_num].type == DS_TYPE_ABSOLUTE)
    BUFFER_ADD("%" PRIu64, vl->values[ds_num].absolute);
  else {
    ERROR("format_values plugin: Unknown data source type: %i",
          ds->ds[ds_num].type);
    sfree(rates);
    return -1;
  }

#undef BUFFER_ADD

  sfree(rates);
  return 0;
}

static int ws_format_name(char *ret, int ret_len, const value_list_t *vl,
                          const struct ws_callback *cb, const char *ds_name) {

  if (ds_name != NULL) {
    snprintf(ret, ret_len, "%s.%s", vl->type, ds_name);
  } else { /* ds_name == NULL */
    snprintf(ret, ret_len, "%s", vl->type);
  }

  return 0;
}

static int ws_send_message(const char *key, const char *value, cdtime_t time,
                           struct ws_callback *cb, const char *plugin,
                           const char *plugin_instance,
                           const char *type_instance, const char *type,
                           const char *ds_name, cdtime_t interval,
                           const char *host) {
  int status;
  size_t message_len;
  char message[1024];
  char rfc3339_timestamp[64];
  const char *host_tags = cb->host_tags ? cb->host_tags : "";
  const char *host_tags_json_prefix = "";
  const char *metrics_prefix =
      cb->metrics_prefix ? cb->metrics_prefix : WS_DEFAULT_PREFIX;
  const char *msg_format = cb->msg_format ? cb->msg_format : WS_DEFAULT_FORMAT;
  int pid;

  pid = getpid();

  rfc3339_local(rfc3339_timestamp, sizeof(rfc3339_timestamp), time);

  /* skip if value is NaN */
  if (value[0] == 'n')
    return 0;

  if (strcasecmp("JSON", msg_format) == 0) {
    if (cb->host_tags) {
      host_tags_json_prefix = ",";
    }
    status = snprintf(
        /* The metric key-values are are part of the syslog msg, in json
           format */
        message, sizeof(message),
        "<166>1 %s %s collectd %d - - {\"time\":%.0f, \"%s\":{ \"%s\":{ "
        "\"%s\":%s }, "
        "\"plugin\":\"%s\", \"plugin_instance\":\"%s\", "
        "\"type_instance\":\"%s\","
        " \"type\":\"%s\", \"interval\":%.0f }, \"hostname\":\"%s\" %s "
        "%s}\n",
        rfc3339_timestamp, host, pid, CDTIME_T_TO_DOUBLE(time), metrics_prefix,
        plugin, key, value, plugin, plugin_instance, type_instance, type,
        CDTIME_T_TO_DOUBLE(interval), host, host_tags_json_prefix, host_tags);
  } else {
    status = snprintf(
        /* The metric key-values are part of the syslog structrude data,
         * MessageFormat = "human" */
        message, sizeof(message),
        "<166>1 %s %s collectd %d - [%s value=\"%s\""
        " plugin=\"%s\" plugin_instance=\"%s\""
        " type_instance=\"%s\" type=\"%s\""
        " ds_name=\"%s\" interval=\"%.0f\"] %s %s.%s=\"%s\"\n",
        rfc3339_timestamp, host, pid, metrics_prefix, value, plugin,
        plugin_instance, type_instance, type, ds_name,
        CDTIME_T_TO_DOUBLE(interval), host_tags, plugin, key, value);
  }
  if (status < 0)
    return -1;
  message_len = (size_t)status;

  if (message_len >= sizeof(message)) {
    ERROR("write_syslog plugin: message buffer too small: "
          "Need %" PRIsz " bytes.",
          message_len + 1);
    return -1;
  }

  pthread_mutex_lock(&cb->send_lock);

  if (cb->sock_fd < 0) {
    status = ws_callback_init(cb);
    if (status != 0) {
      ERROR("write_syslog plugin: ws_callback_init failed.");
      pthread_mutex_unlock(&cb->send_lock);
      return -1;
    }
  }

  if (message_len >= cb->send_buf_free) {
    status = ws_flush_nolock(0, cb);
    if (status != 0) {
      pthread_mutex_unlock(&cb->send_lock);
      return status;
    }
  }

  /* Assert that we have enough space for this message. */
  assert(message_len < cb->send_buf_free);

  /* `message_len + 1' because `message_len' does not include the
   * trailing null byte. Neither does `send_buffer_fill'. */
  memcpy(cb->send_buf + cb->send_buf_fill, message, message_len + 1);
  cb->send_buf_fill += message_len;
  cb->send_buf_free -= message_len;

  DEBUG("write_syslog plugin: [%s]:%s buf %" PRIsz "/%" PRIsz
        " (%.1f %%) \"%s\"",
        cb->node, cb->service, cb->send_buf_fill, sizeof(cb->send_buf),
        100.0 * ((double)cb->send_buf_fill) / ((double)sizeof(cb->send_buf)),
        message);

  pthread_mutex_unlock(&cb->send_lock);

  return 0;
}

static int ws_write_messages(const data_set_t *ds, const value_list_t *vl,
                             struct ws_callback *cb) {
  char key[10 * DATA_MAX_NAME_LEN];
  char values[512];

  int status;

  if (0 != strcmp(ds->type, vl->type)) {
    ERROR("write_syslog plugin: DS type does not match "
          "value list type");
    return -1;
  }

  for (size_t i = 0; i < ds->ds_num; i++) {
    const char *ds_name = NULL;

    if (cb->always_append_ds || (ds->ds_num > 1))
      ds_name = ds->ds[i].name;

    /* Copy the identifier to 'key' and escape it. */
    status = ws_format_name(key, sizeof(key), vl, cb, ds_name);
    if (status != 0) {
      ERROR("write_syslog plugin: error with format_name");
      return status;
    }

    escape_string(key, sizeof(key));
    /* Convert the values to an ASCII representation and put that into
     * 'values'. */
    status =
        ws_format_values(values, sizeof(values), i, ds, vl, cb->store_rates);
    if (status != 0) {
      ERROR("write_syslog plugin: error with "
            "ws_format_values");
      return status;
    }

    /* Send the message to tcp */
    status = ws_send_message(key, values, vl->time, cb, vl->plugin,
                             vl->plugin_instance, vl->type_instance, vl->type,
                             ds_name, vl->interval, vl->host);
    if (status != 0) {
      ERROR("write_syslog plugin: error with "
            "ws_send_message");
      return status;
    }
  }

  return 0;
}

static int ws_write(const data_set_t *ds, const value_list_t *vl,
                    user_data_t *user_data) {
  struct ws_callback *cb;
  int status;

  if (user_data == NULL)
    return EINVAL;

  cb = user_data->data;

  status = ws_write_messages(ds, vl, cb);

  return status;
}

static int ws_config_tsd(oconfig_item_t *ci) {
  struct ws_callback *cb;
  char callback_name[DATA_MAX_NAME_LEN];

  cb = calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("write_syslog plugin: calloc failed.");
    return -1;
  }
  cb->sock_fd = -1;
  cb->connect_failed_log_enabled = 1;
  cb->next_random_ttl = new_random_ttl();

  pthread_mutex_init(&cb->send_lock, NULL);

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Host", child->key) == 0)
      cf_util_get_string(child, &cb->node);
    else if (strcasecmp("Port", child->key) == 0)
      cf_util_get_service(child, &cb->service);
    else if (strcasecmp("MessageFormat", child->key) == 0)
      cf_util_get_string(child, &cb->msg_format);
    else if (strcasecmp("HostTags", child->key) == 0)
      cf_util_get_string(child, &cb->host_tags);
    else if (strcasecmp("StoreRates", child->key) == 0)
      cf_util_get_boolean(child, &cb->store_rates);
    else if (strcasecmp("AlwaysAppendDS", child->key) == 0)
      cf_util_get_boolean(child, &cb->always_append_ds);
    else if (strcasecmp("Prefix", child->key) == 0)
      cf_util_get_string(child, &cb->metrics_prefix);
    else {
      ERROR("write_syslog plugin: Invalid configuration "
            "option: %s.",
            child->key);
      return -1;
    }
  }

  snprintf(callback_name, sizeof(callback_name), "write_syslog/%s/%s",
           cb->node != NULL ? cb->node : WS_DEFAULT_NODE,
           cb->service != NULL ? cb->service : WS_DEFAULT_SERVICE);

  user_data_t user_data = {.data = cb, .free_func = ws_callback_free};

  plugin_register_write(callback_name, ws_write, &user_data);

  user_data.free_func = NULL;
  plugin_register_flush(callback_name, ws_flush, &user_data);

  return 0;
}

static int ws_config(oconfig_item_t *ci) {
  if ((resolve_interval == 0) && (resolve_jitter == 0))
    resolve_interval = resolve_jitter = plugin_get_interval();

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Node", child->key) == 0) {
      if (ws_config_tsd(child) < 0)
        return -1;
    } else if (strcasecmp("ResolveInterval", child->key) == 0)
      cf_util_get_cdtime(child, &resolve_interval);
    else if (strcasecmp("ResolveJitter", child->key) == 0)
      cf_util_get_cdtime(child, &resolve_jitter);
    else {
      ERROR("write_syslog plugin: Invalid configuration "
            "option: %s.",
            child->key);
      return -1;
    }
  }

  return 0;
}

void module_register(void) {
  plugin_register_complex_config("write_syslog", ws_config);
}
