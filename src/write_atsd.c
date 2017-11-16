/**
 * collectd - src/write_atsd.c
 *
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
 * Based on the write_graphite plugin.
 **/

/* write_atsd plugin configuation example
 *
 * <Plugin write_atsd>
 *   <Node "default">
 *     AtsdUrl "atsd_url"
 *     Entity "entity"
 *     Prefix "collectd"
 *     ShortHostname false
 *   </Node>
 * </Plugin>
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_avltree.h"
#include "utils_vl_lookup.h"

#include "utils_cache.h"
#include "utils_complain.h"
#include "utils_format_atsd.h"

#include <stdbool.h>

#include <netdb.h>
#include <sys/socket.h>

#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

#ifndef WA_DEFAULT_NODE
#define WA_DEFAULT_NODE "localhost"
#endif

#ifndef WA_DEFAULT_SERVICE
#define WA_DEFAULT_SERVICE "8081"
#endif

#ifndef WA_DEFAULT_PROTOCOL
#define WA_DEFAULT_PROTOCOL "tcp"
#endif

#ifndef WA_DEFAULT_PREFIX
#define WA_DEFAULT_PREFIX "collectd"
#endif

#ifndef WA_MAX_LENGTH
#define WA_MAX_LENGTH 512
#endif

/* Ethernet - (IPv6 + TCP) = 1500 - (40 + 32) = 1428 */
#ifndef WA_SEND_BUF_SIZE
#define WA_SEND_BUF_SIZE 1428
#endif

#ifndef WA_MIN_RECONNECT_INTERVAL
#define WA_MIN_RECONNECT_INTERVAL TIME_T_TO_CDTIME_T(1)
#endif

#ifndef WA_PROPERTY_INTERVAL
#define WA_PROPERTY_INTERVAL TIME_T_TO_CDTIME_T(300)
#endif

struct wa_cache_s {
  char *name;
  int interval;
  double threshold;

  struct wa_cache_s *next;
};

struct wa_callback {
  int sock_fd;

  char *name;

  char *node;
  char *service;
  char *protocol;
  char *prefix;
  char *entity;

  _Bool short_hostname;
  _Bool store_rates;

  char send_buf[WA_SEND_BUF_SIZE];
  size_t send_buf_free;
  size_t send_buf_fill;
  cdtime_t send_buf_init_time;

  pthread_mutex_t send_lock;
  pthread_mutex_t value_cache_lock;
  pthread_mutex_t metric_cache_lock;
  c_complain_t init_complaint;
  cdtime_t last_connect_time;
  cdtime_t last_property_time;

  struct wa_cache_s *wa_caches;
  int wa_num_caches;
  c_avl_tree_t *value_cache;
  c_avl_tree_t *metric_cache;

  /* Force reconnect useful for load balanced environments */
  cdtime_t last_reconnect_time;
  cdtime_t reconnect_interval;
  _Bool reconnect_interval_reached;
};

struct atsd_key_s {
  char host[DATA_MAX_NAME_LEN];
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
  char data_source[DATA_MAX_NAME_LEN];
};
typedef struct atsd_key_s atsd_key_t;

struct atsd_value_s {
  uint64_t time;
  double value;
};
typedef struct atsd_value_s atsd_value_t;

int compare_atsd_keys(atsd_key_t *key_a, atsd_key_t *key_b) {
  int p;

#define COMPARE(l, r)                                                          \
  do {                                                                         \
    p = strcmp(l, r);                                                          \
    if (p != 0)                                                                \
      return p;                                                                \
  } while (0)

  COMPARE(key_a->host, key_b->host);
  COMPARE(key_a->plugin, key_b->plugin);
  COMPARE(key_a->type, key_b->type);
  COMPARE(key_a->plugin_instance, key_b->plugin_instance);
  COMPARE(key_a->type_instance, key_b->type_instance);
  COMPARE(key_a->data_source, key_b->data_source);

#undef COMPARE

  return p;
}

/* wa_force_reconnect_check closes cb->sock_fd when it was open for longer
 * than cb->reconnect_interval. Must hold cb->send_lock when calling. */
static void wa_force_reconnect_check(struct wa_callback *cb) {
  cdtime_t now;

  if (cb->reconnect_interval == 0)
    return;

  /* check if address changes if addr_timeout */
  now = cdtime();
  if ((now - cb->last_reconnect_time) < cb->reconnect_interval)
    return;

  /* here we should close connection on next */
  close(cb->sock_fd);
  cb->sock_fd = -1;
  cb->last_reconnect_time = now;
  cb->reconnect_interval_reached = 1;

  INFO("write_atsd plugin: Connection closed after %.3f seconds.",
       CDTIME_T_TO_DOUBLE(now - cb->last_reconnect_time));
}

static void wa_reset_buffer(struct wa_callback *cb) {
  memset(cb->send_buf, 0, sizeof(cb->send_buf));
  cb->send_buf_free = sizeof(cb->send_buf);
  cb->send_buf_fill = 0;
  cb->send_buf_init_time = cdtime();
}

static int wa_send_buffer(struct wa_callback *cb) {
  ssize_t status;

  if (cb->sock_fd < 0)
    return -1;

  status = swrite(cb->sock_fd, cb->send_buf, strlen(cb->send_buf));
  if (status != 0) {
    char errbuf[1024];
    ERROR("write_atsd plugin: send failed with status %zi (%s)", status,
          sstrerror(errno, errbuf, sizeof(errbuf)));

    close(cb->sock_fd);
    cb->sock_fd = -1;
    return -1;
  }

  return 0;
}

/* NOTE: You must hold cb->send_lock when calling this function! */
static int wa_flush_nolock(cdtime_t timeout, struct wa_callback *cb) {
  int status;

  DEBUG("write_atsd plugin: wa_flush_nolock: timeout = %.3f; "
        "send_buf_fill = %zu;",
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

  status = wa_send_buffer(cb);
  wa_reset_buffer(cb);

  return (status);
}

static int wa_callback_init(struct wa_callback *cb) {
  struct addrinfo *ai_list;
  cdtime_t now;
  int status;

  char connerr[1024] = "";

  if (cb->sock_fd > 0)
    return 0;

  /* Don't try to reconnect too often. By default, one reconnection attempt
   * is made per second. */
  now = cdtime();
  if ((now - cb->last_connect_time) < WA_MIN_RECONNECT_INTERVAL)
    return (EAGAIN);
  cb->last_connect_time = now;
  cb->last_property_time = now;

  struct addrinfo ai_hints = {.ai_family = AF_UNSPEC,
                              .ai_flags = AI_ADDRCONFIG};

  if (0 == strcasecmp("tcp", cb->protocol))
    ai_hints.ai_socktype = SOCK_STREAM;
  else
    ai_hints.ai_socktype = SOCK_DGRAM;

  status = getaddrinfo(cb->node, cb->service, &ai_hints, &ai_list);
  if (status != 0) {
    ERROR("write_atsd plugin: getaddrinfo (%s, %s, %s) failed: %s", cb->node,
          cb->service, cb->protocol, gai_strerror(status));
    return -1;
  }

  assert(ai_list != NULL);
  for (struct addrinfo *ai_ptr = ai_list; ai_ptr != NULL;
       ai_ptr = ai_ptr->ai_next) {
    cb->sock_fd =
        socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
    if (cb->sock_fd < 0) {
      char errbuf[1024];
      snprintf(connerr, sizeof(connerr), "failed to open socket: %s",
               sstrerror(errno, errbuf, sizeof(errbuf)));
      continue;
    }

    set_sock_opts(cb->sock_fd);

    status = connect(cb->sock_fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    if (status != 0) {
      char errbuf[1024];
      snprintf(connerr, sizeof(connerr), "failed to connect to remote "
                                         "host: %s",
               sstrerror(errno, errbuf, sizeof(errbuf)));
      close(cb->sock_fd);
      cb->sock_fd = -1;
      continue;
    }
    break;
  }

  freeaddrinfo(ai_list);

  if (cb->sock_fd < 0) {
    if (connerr[0] == '\0')
      /* this should not happen but try to get a message anyway */
      sstrerror(errno, connerr, sizeof(connerr));
    c_complain(LOG_ERR, &cb->init_complaint,
               "write_atsd plugin: Connecting to %s:%s via %s failed. "
               "The last error was: %s",
               cb->node, cb->service, cb->protocol, connerr);
    return -1;
  } else {
    c_release(LOG_INFO, &cb->init_complaint,
              "write_atsd plugin: Successfully connected to %s:%s via %s.",
              cb->node, cb->service, cb->protocol);
  }

  /* wa_force_reconnect_check does not flush the buffer before closing a
   * sending socket, so only call wa_reset_buffer() if the socket was closed
   * for a different reason (tracked in cb->reconnect_interval_reached). */
  if (!cb->reconnect_interval_reached || (cb->send_buf_free == 0))
    wa_reset_buffer(cb);
  else
    cb->reconnect_interval_reached = 0;

  return 0;
}

static void wa_callback_free(void *data) {
  struct wa_callback *cb;
  atsd_key_t *atsd_stored_key = NULL;
  atsd_value_t *atsd_stored_value = NULL;
  struct wa_cache_s *cache, *next_cache;
  void *empty;

  if (data == NULL)
    return;

  cb = data;

  pthread_mutex_lock(&cb->send_lock);

  wa_flush_nolock(/* timeout = */ 0, cb);

  if (cb->sock_fd >= 0) {
    close(cb->sock_fd);
    cb->sock_fd = -1;
  }

  sfree(cb->name);
  sfree(cb->node);
  sfree(cb->protocol);
  sfree(cb->service);
  sfree(cb->prefix);

  while (c_avl_pick(cb->value_cache, (void *)&atsd_stored_key,
                    (void *)&atsd_stored_value) == 0) {
    sfree(atsd_stored_key);
    sfree(atsd_stored_value);
  }
  c_avl_destroy(cb->value_cache);

  while (c_avl_pick(cb->metric_cache, (void *)&atsd_stored_key, &empty) == 0) {
    sfree(atsd_stored_key);
  }
  c_avl_destroy(cb->metric_cache);

  cache = cb->wa_caches;
  while (cache != NULL) {
    next_cache = cache->next;
    sfree(cache->name);
    sfree(cache);
    cache = next_cache;
  }

  sfree(cb->entity);

  pthread_mutex_destroy(&cb->send_lock);

  sfree(cb);
}

static int wa_send_message(char const *message, struct wa_callback *cb) {
  int status;
  size_t message_len;

  message_len = strlen(message);

  pthread_mutex_lock(&cb->send_lock);

  wa_force_reconnect_check(cb);

  if (cb->sock_fd < 0) {
    status = wa_callback_init(cb);
    if (status != 0) {
      /* An error message has already been printed. */
      pthread_mutex_unlock(&cb->send_lock);
      return -1;
    }
  }

  status = wa_flush_nolock(/* timeout = */ 0, cb);
  if (status != 0) {
    pthread_mutex_unlock(&cb->send_lock);
    return (status);
  }

  /* Assert that we have enough space for this message. */
  assert(message_len < cb->send_buf_free);

  /* `message_len + 1' because `message_len' does not include the
   * trailing null byte. Neither does `send_buffer_fill'. */
  memcpy(cb->send_buf + cb->send_buf_fill, message, message_len + 1);
  cb->send_buf_fill += message_len;
  cb->send_buf_free -= message_len;

  DEBUG("write_atsd plugin: [%s]:%s (%s) buf %zu/%zu (%.1f %%) \"%s\"",
        cb->node, cb->service, cb->protocol, cb->send_buf_fill,
        sizeof(cb->send_buf),
        100.0 * ((double)cb->send_buf_fill) / ((double)sizeof(cb->send_buf)),
        message);

  pthread_mutex_unlock(&cb->send_lock);

  return 0;
}

static int wa_update_property(const value_list_t *vl, const char *entity,
                              struct wa_callback *cb) {
  int ret;
  struct utsname buf;
  cdtime_t now;
  char command[1024];
  char escape_buffer[6 * DATA_MAX_NAME_LEN];
  size_t written = 0;

  now = cdtime();
  if ((now - cb->last_property_time) > WA_PROPERTY_INTERVAL) {
    cb->last_property_time = now;

    written += snprintf(
        command, sizeof(command),
        "property e:\"%s\" ms:%" PRIu64 " t:collectd-atsd v:host=\"%s\"",
        escape_atsd_string(escape_buffer, entity, sizeof escape_buffer),
        CDTIME_T_TO_MS(vl->time),
        escape_atsd_string(escape_buffer, vl->host, sizeof escape_buffer));

    ret = uname(&buf);
    if (!ret) {
      escape_atsd_string(buf.sysname, buf.sysname, sizeof buf.sysname);
      escape_atsd_string(buf.sysname, buf.nodename, sizeof buf.nodename);
      escape_atsd_string(buf.sysname, buf.release, sizeof buf.release);
      escape_atsd_string(buf.sysname, buf.version, sizeof buf.version);
      escape_atsd_string(buf.sysname, buf.machine, sizeof buf.machine);
      written += snprintf(command + written, sizeof(command) - written,
                          " v:OperatingSystem=\"%s\""
                          " v:Node=\"%s\""
                          " v:Kernel_Release_Version=\"%s\""
                          " v:OS_Version=\"%s\""
                          " v:Hardware=\"%s\"",
                          buf.sysname, buf.nodename, buf.release, buf.version,
                          buf.machine);
    }

    snprintf(command + written, sizeof(command) - written, "\n");
    return wa_send_message(command, cb);
  }
  return 0;
}

static int check_cache_value(atsd_key_t *ak, atsd_value_t *av,
                             struct wa_callback *cb, _Bool *update_series,
                             _Bool *update_metrics) {

  atsd_key_t *atsd_stored_key;
  atsd_value_t *atsd_stored_value;
  struct wa_cache_s *cache;
  double stored_value, cur_value;
  double diff;
  int status;

  *update_series = true;
  *update_metrics = false;

#define COPY_KEY(dst, src)                                                     \
  do {                                                                         \
    strncpy(dst->host, src->host, sizeof(dst->host));                          \
    strncpy(dst->plugin, src->plugin, sizeof(dst->plugin));                    \
    strncpy(dst->plugin_instance, src->plugin_instance,                        \
            sizeof(dst->plugin_instance));                                     \
    strncpy(dst->type, src->type, sizeof(dst->type));                          \
    strncpy(dst->type_instance, src->type_instance,                            \
            sizeof(dst->type_instance));                                       \
    strncpy(dst->data_source, src->data_source, sizeof(dst->data_source));     \
  } while (0)

  pthread_mutex_lock(&cb->metric_cache_lock);
  status = c_avl_get(cb->metric_cache, ak, NULL);
  pthread_mutex_unlock(&cb->metric_cache_lock);

  if (status != 0) {
    atsd_stored_key = (atsd_key_t *)malloc(sizeof(atsd_key_t));
    if (atsd_stored_key == NULL) {
      ERROR("write_atsd plugin: malloc failed.");
      return -1;
    }
    COPY_KEY(atsd_stored_key, ak);

    pthread_mutex_lock(&cb->metric_cache_lock);
    status = c_avl_insert(cb->metric_cache, atsd_stored_key, NULL);
    pthread_mutex_unlock(&cb->metric_cache_lock);

    *update_metrics = true;
  }

  for (cache = cb->wa_caches; cache != NULL; cache = cache->next)
    if (strcasecmp(ak->plugin, cache->name) == 0)
      break;
  if (cache == NULL)
    return 0;

  pthread_mutex_lock(&cb->value_cache_lock);
  status = c_avl_get(cb->value_cache, ak, (void *)&atsd_stored_value);
  pthread_mutex_unlock(&cb->value_cache_lock);

  if (status == 0) /* metric in tree */ {
    assert(atsd_stored_value->time < av->time);

    cur_value = av->value;
    stored_value = atsd_stored_value->value;
    diff = fabs(cur_value - stored_value);

    if ((av->time - atsd_stored_value->time >= cache->interval * 1000) ||
        (diff > (cache->threshold) * stored_value / 100)) {
      atsd_stored_value->value = av->value;
      atsd_stored_value->time = av->time;
    } else {
      *update_series = false;
    }
  } else {
    atsd_stored_key = (atsd_key_t *)malloc(sizeof(atsd_key_t));
    if (atsd_stored_key == NULL) {
      ERROR("write_atsd plugin: malloc failed.");
      return -1;
    }
    COPY_KEY(atsd_stored_key, ak);

    atsd_stored_value = (atsd_value_t *)malloc(sizeof(atsd_value_t));
    if (atsd_stored_value == NULL) {
      ERROR("write_atsd plugin: malloc failed.");
      free(atsd_stored_key);
      return -1;
    }

    atsd_stored_value->value = av->value;
    atsd_stored_value->time = av->time;

    pthread_mutex_lock(&cb->value_cache_lock);
    status = c_avl_insert(cb->value_cache, atsd_stored_key, atsd_stored_value);
    pthread_mutex_unlock(&cb->value_cache_lock);

    if (status != 0) {
      ERROR("utils_vl_lookup: c_avl_insert(\"%s\") failed with status %i.",
            ak->plugin, status);
      return -1;
    }
  }
#undef COPY_KEY

  return 0;
}

static int wa_write_messages(const data_set_t *ds, const value_list_t *vl,
                             struct wa_callback *cb) {
  int status;
  size_t i;
  gauge_t *rates;
  atsd_key_t cache_key;
  atsd_value_t cache_value;
  format_info_t format;

  _Bool update_metrics;
  _Bool update_series;

  char commands[1024];
  char entity[WA_MAX_LENGTH];

  status = 0;
  rates = NULL;

  if (cb->store_rates) {
    rates = uc_get_rate(ds, vl);
    if (rates == NULL) {
      ERROR("wa_write_messages: error with uc_get_rate");
      return -1;
    }
  }

  status = format_entity(entity, sizeof(entity), cb->entity, vl->host,
                         cb->short_hostname);
  if (status != 0)
    goto end;

  status = wa_update_property(vl, entity, cb);
  if (status != 0)
    goto end;

  format.buffer = commands;
  format.buffer_len = sizeof(commands);
  format.entity = entity;
  format.prefix = cb->prefix;
  format.vl = vl;
  format.ds = ds;
  format.rates = rates;

  for (i = 0; i < ds->ds_num; i++) {
    if (rates != NULL && isnan(rates[i]))
      continue;

    format.index = i;

    strncpy(cache_key.host, vl->host, DATA_MAX_NAME_LEN);
    strncpy(cache_key.plugin, vl->plugin, DATA_MAX_NAME_LEN);
    strncpy(cache_key.plugin_instance, vl->plugin_instance, DATA_MAX_NAME_LEN);
    strncpy(cache_key.type, vl->type, DATA_MAX_NAME_LEN);
    strncpy(cache_key.type_instance, vl->type_instance, DATA_MAX_NAME_LEN);
    strncpy(cache_key.data_source, ds->ds[i].name, DATA_MAX_NAME_LEN);

    cache_value.value = get_value(&format);
    cache_value.time = CDTIME_T_TO_MS(vl->time);

    status = check_cache_value(&cache_key, &cache_value, cb, &update_series,
                               &update_metrics);
    if (status != 0) {
      goto end;
    }

    if (update_series) {
      format_atsd_command(&format, update_metrics);
      status = wa_send_message(commands, cb);
      if (status != 0)
        goto end;
    }
  }

end:
  sfree(rates);
  return 0;
}

/* int wa_write_messages */

static int wa_write(const data_set_t *ds, const value_list_t *vl,
                    user_data_t *user_data) {
  if (user_data == NULL)
    return -1;

  return wa_write_messages(ds, vl, user_data->data);
}

static int wa_config_cache(struct wa_callback *cb, oconfig_item_t *child) {

  int q, status;
  struct wa_cache_s *wc;

  wc = malloc(sizeof(struct wa_cache_s));

  if (wc == NULL) {
    ERROR("write_atsd plugin: malloc failed.");
    return -1;
  }

  memset(wc, 0, sizeof(struct wa_cache_s));
  wc->next = cb->wa_caches;
  cb->wa_caches = wc;

  status = cf_util_get_string(child, &wc->name);
  if (status != 0)
    return status;

  for (q = 0; q < child->children_num; q++) {
    oconfig_item_t *grandchild = child->children + q;

    if (strcasecmp("Interval", grandchild->key) == 0)
      cf_util_get_int(grandchild, &wc->interval);
    else if (strcasecmp("Threshold", grandchild->key) == 0)
      cf_util_get_double(grandchild, &wc->threshold);
    else {
      ERROR("write_atsd plugin: Invalid configuration "
            "option: %s.",
            grandchild->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  return 0;
}

static int wa_config_node(oconfig_item_t *ci) {
  struct wa_callback *cb;
  char callback_name[DATA_MAX_NAME_LEN];
  int status = 0;

  cb = calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("write_atsd plugin: calloc failed.");
    return -1;
  }

  cb->sock_fd = -1;
  cb->name = NULL;
  cb->node = strdup(WA_DEFAULT_NODE);
  cb->service = strdup(WA_DEFAULT_SERVICE);
  cb->protocol = strdup(WA_DEFAULT_PROTOCOL);
  cb->prefix = strdup(WA_DEFAULT_PREFIX);
  cb->entity = NULL;
  cb->short_hostname = false;
  cb->store_rates = true;
  cb->wa_num_caches = 0;
  cb->wa_caches = NULL;
  cb->value_cache = NULL;
  cb->value_cache = c_avl_create((void *)compare_atsd_keys);
  cb->metric_cache = NULL;
  cb->metric_cache = c_avl_create((void *)compare_atsd_keys);

  pthread_mutex_init(&cb->send_lock, /* attr = */ NULL);
  pthread_mutex_init(&cb->value_cache_lock, /* attr = */ NULL);
  pthread_mutex_init(&cb->metric_cache_lock, /* attr = */ NULL);

  C_COMPLAIN_INIT(&cb->init_complaint);

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("atsdurl", child->key) == 0) {
      int s = 0;
      for (int q = 0; q < strlen(child->values[0].value.string); q++)
        if (child->values[0].value.string[q] == ':')
          s++;
      if (s > 2) {
        ERROR("write_atsd plugin: failed to parse atsdurl (%s)",
              child->values[0].value.string);
        status = -1;
      } else {
        int args =
            sscanf(child->values[0].value.string, "%99[^:]://%99[^:]:%99[^\n]",
                   cb->protocol, cb->node, cb->service);
        if (args == 3) {
          if (strcasecmp("UDP", cb->protocol) != 0 &&
              strcasecmp("TCP", cb->protocol) != 0) {
            ERROR("write_atsd plugin: Unknown protocol (%s)", cb->protocol);
            status = -1;
          }
        } else if (args == 2) {
          if (strlen(cb->protocol) == 0) {
            ERROR("write_atsd plugin: No protocol given (%s)",
                  child->values[0].value.string);
            status = -1;
          } else if (strlen(cb->node) == 0) {
            ERROR("write_atsd plugin: No hostname given (%s)",
                  child->values[0].value.string);
            status = -1;
          } else {
            if (strcasecmp("TCP", cb->protocol) == 0) {
              sfree(cb->service);
              cb->service = strdup("8081");
            } else if (strcasecmp("UDP", cb->protocol) == 0) {
              sfree(cb->service);
              cb->service = strdup("8082");
            } else {
              ERROR("write_atsd plugin: Unknown protocol (%s)", cb->protocol);
              status = -1;
            }
          }
        } else {
          ERROR("write_atsd plugin: failed to parse atsdurl (%s)",
                child->values[0].value.string);
          status = -1;
        }
      }
    } else if (strcasecmp("Prefix", child->key) == 0)
      cf_util_get_string(child, &cb->prefix);
    else if (strcasecmp("Entity", child->key) == 0)
      cf_util_get_string(child, &cb->entity);
    else if (strcasecmp("ShortHostname", child->key) == 0)
      cf_util_get_boolean(child, &cb->short_hostname);
    else if (strcasecmp("StoreRates", child->key) == 0)
      cf_util_get_boolean(child, &cb->store_rates);
    else if (strcasecmp("Cache", child->key) == 0)
      wa_config_cache(cb, child);
    else {
      ERROR("write_atsd plugin: Invalid configuration "
            "option: %s.",
            child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status != 0) {
    wa_callback_free(cb);
    return (status);
  }

  if (cb->name == NULL)
    snprintf(callback_name, sizeof(callback_name), "write_atsd/%s/%s/%s",
             cb->node != NULL ? cb->node : WA_DEFAULT_NODE,
             cb->service != NULL ? cb->service : WA_DEFAULT_SERVICE,
             cb->protocol != NULL ? cb->protocol : WA_DEFAULT_PROTOCOL);
  else
    snprintf(callback_name, sizeof(callback_name), "write_atsd/%s", cb->name);

  plugin_register_write(callback_name, wa_write,
                        &(user_data_t){
                            .data = cb, .free_func = wa_callback_free,
                        });

  return 0;
}

static int wa_complex_config(oconfig_item_t *ci) {
  int i;

  for (i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Node", child->key) == 0) {
      wa_config_node(child);
    } else {
      ERROR("write_atsd plugin: Invalid configuration "
            "option: %s.",
            child->key);
    }
  }
  return 0;
}

void module_register(void) {
  plugin_register_complex_config("write_atsd", wa_complex_config);
}
