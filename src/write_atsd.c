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

/* write_atsd plugin configuration example
 *
 * <Plugin write_atsd>
 *      <Node "default">
 *          AtsdUrl "atsd_url"
 *          Entity "entity"
 *          Prefix "collectd."
 *          ShortHostname false
 *          <Cache "df">
 *               Interval 300
 *               Threshold 0
 *          </Cache>
 *      </Node>
 *  </Plugin>
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_avltree.c"
#include "utils_avltree.h"

#include "utils_cache.h"
#include "utils_complain.h"
#include "utils_format_atsd.h"

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
#define WA_DEFAULT_PREFIX "collectd."
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

/*
 * Private variables
 */

struct wa_cache_s {
  char *name;
  int interval;
  double threshold;
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

  char send_buf[WA_SEND_BUF_SIZE];
  size_t send_buf_free;
  size_t send_buf_fill;
  cdtime_t send_buf_init_time;

  pthread_mutex_t send_lock;
  pthread_mutex_t avl_lock;
  c_complain_t init_complaint;
  cdtime_t last_connect_time;
  cdtime_t last_property_time;

  struct wa_cache_s **wa_caches;
  int wa_num_caches;
  c_avl_tree_t *cache_tree;

  /* Force reconnect useful for load balanced environments */
  cdtime_t last_reconnect_time;
  cdtime_t reconnect_interval;
  _Bool reconnect_interval_reached;
};

struct atsd_key_s {
  char *plugin;
  char *plugin_instance;
  char *type;
  char *type_instance;
};
typedef struct atsd_key_s atsd_key_t;

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

int compare_atsd_keys(atsd_key_t *atsd_key1, atsd_key_t *atsd_key2) {
  int p = strcmp(atsd_key1->plugin, atsd_key2->plugin);
  if (p == 0) {
    p = strcmp(atsd_key1->type, atsd_key2->type);
    if (p == 0) {
      p = strcmp(atsd_key1->plugin_instance, atsd_key2->plugin_instance);
      if (p == 0) {
        return strcmp(atsd_key1->type_instance, atsd_key2->type_instance);
      }
      return p;
    }
    return p;
  }
  return p;
}

struct atsd_value_s {
  uint64_t time;
  char *value;
};
typedef struct atsd_value_s atsd_value_t;

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
      snprintf(connerr, sizeof(connerr),
               "failed to connect to remote "
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

  int i;
  struct wa_callback *cb;

  atsd_key_t *atsd_stored_key = NULL;
  atsd_value_t *atsd_stored_value = NULL;

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

  while (c_avl_pick(cb->cache_tree, (void *)&atsd_stored_key,
                    (void *)&atsd_stored_value) == 0) {

    sfree(atsd_stored_key->plugin);
    sfree(atsd_stored_key->plugin_instance);
    sfree(atsd_stored_key->type);
    sfree(atsd_stored_key->type_instance);
    sfree(atsd_stored_key);

    sfree(atsd_stored_value->value);
    sfree(atsd_stored_value);
  }

  c_avl_destroy(cb->cache_tree);

  for (i = 0; i < cb->wa_num_caches; i++) {
    sfree(cb->wa_caches[i]->name);
  }
  sfree(cb->wa_caches);

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

static int check_cache_value(atsd_key_t *ak, atsd_value_t *av,
                             struct wa_callback *cb) {

  atsd_key_t *atsd_stored_key = NULL;
  atsd_value_t *atsd_stored_value = NULL;
  double stored_value, cur_value;
  double cur_threshold;

  int status, q;
  int same_value = 1;
  int interested = 0;

  for (q = 0; q < cb->wa_num_caches; q++) {

    if (strcasecmp(ak->plugin, cb->wa_caches[q]->name) == 0) {
      interested = 1;
      status = c_avl_get(cb->cache_tree, ak, (void *)&atsd_stored_value);

      if (status == 0) /* metric in tree */ {
        cur_value = atof(av->value);
        stored_value = atof(atsd_stored_value->value);
        cur_threshold = fabs(cur_value - stored_value);

        if ((av->time - atsd_stored_value->time >=
             cb->wa_caches[q]->interval * 1000) ||
            (cur_threshold >
             (cb->wa_caches[q]->threshold) * stored_value / 100)) {

          status =
              c_avl_remove(cb->cache_tree, ak, (void *)&atsd_stored_key, NULL);

          sfree(atsd_stored_key->plugin);
          sfree(atsd_stored_key->plugin_instance);
          sfree(atsd_stored_key->type);
          sfree(atsd_stored_key->type_instance);
          sfree(atsd_stored_key);

          sfree(atsd_stored_value->value);
          sfree(atsd_stored_value);

          if (status != 0) {
            ERROR(
                "utils_vl_lookup: c_avl_remove(\"%s\") failed with status %i.",
                ak->plugin, status);
            return -1;
          }

          status = c_avl_insert(cb->cache_tree, ak, av);
          if (status != 0) {
            ERROR(
                "utils_vl_lookup: c_avl_insert(\"%s\") failed with status %i.",
                ak->plugin, status);
            return -1;
          }
        } else {
          same_value = 0;

          sfree(ak->plugin);
          sfree(ak->plugin_instance);
          sfree(ak->type);
          sfree(ak->type_instance);
          sfree(ak);

          sfree(av->value);
          sfree(av);
        }
      } else {
        status = c_avl_insert(cb->cache_tree, ak, av);
        if (status != 0) {
          ERROR("utils_vl_lookup: c_avl_insert(\"%s\") failed with status %i.",
                ak->plugin, status);
          return -1;
        }
      }
      break;
    }
  }
  if (interested == 0) {
    sfree(ak->plugin);
    sfree(ak->plugin_instance);
    sfree(ak->type);
    sfree(ak->type_instance);
    sfree(ak);

    sfree(av->value);
    sfree(av);
  }
  return same_value;
}

static int wa_write_messages(const data_set_t *ds, const value_list_t *vl,
                             struct wa_callback *cb) {

  char plugin_instance[100];
  int status;
  if (0 != strcmp(ds->type, vl->type)) {
    ERROR("write_atsd plugin: DS type does not match "
          "value list type");
    return -1;
  }

  int i;
  gauge_t *rates;

  rates = uc_get_rate(ds, vl);
  if (rates == NULL) {
    return -1;
  }

  if (ds->ds_num != vl->values_len) {
    ERROR("plugin_dispatch_values: ds->type = %s: "
          "(ds->ds_num = %zu) != "
          "(vl->values_len = %zu)",
          ds->type, ds->ds_num, vl->values_len);
  }

  char sendline[1024];
  char metric_name[512];
  cdtime_t now;

  char entity[WA_MAX_LENGTH];
  int ent_len = sizeof(entity);

  check_entity(entity, ent_len, cb->entity, vl->host, cb->short_hostname);

  now = cdtime();
  if ((now - cb->last_property_time) > WA_PROPERTY_INTERVAL) {
    cb->last_property_time = now;

    int ret;
    struct utsname buf;
    ret = uname(&buf);
    if (!ret) {

      snprintf(sendline, sizeof(sendline),
               "property e:%s ms:%" PRIu64
               " t:collectd-atsd v:host=%s v:OperatingSystem=\"%s\" "
               "v:Node=\"%s\" v:Kernel_Release_Version=\"%s\" "
               "v:OS_Version=\"%s\" v:Hardware=\"%s\"\n",
               entity, CDTIME_T_TO_MS(vl->time), vl->host, buf.sysname,
               buf.nodename, buf.release, buf.version, buf.machine);
    } else {
      snprintf(sendline, sizeof(sendline),
               "property e:%s ms:%" PRIu64 " t:collectd-atsd v:host=%s\n",
               entity, CDTIME_T_TO_MS(vl->time), vl->host);
    }

    status = wa_send_message(sendline, cb);
    if (status != 0) {
      sfree(rates);
      return (status);
    }
  }

  char tmp[512];
  char tv[50];
  char ret[128];
  size_t ret_len = sizeof(ret);

  char *str_location;
  char *c;

  memset(ret, 0, ret_len);

  atsd_key_t *ak = NULL;
  atsd_value_t *av = NULL;

  int same_value;

  for (i = 0; i < ds->ds_num; i++) {

    if (isnan(rates[i]))
      continue;

    status = format_value(ret, ret_len, i, ds, vl, rates);
    if (status != 0) {
      sfree(rates);
      return (status);
    }

    same_value = 1;
    sstrncpy(metric_name, cb->prefix, sizeof(metric_name));

/*
 *  Normalize read plugin signatures according to the ATSD scheme of data
 *  to prepare series commands from Network API.
 *  https://github.com/axibase/atsd/tree/master/api/network
 *
 *	In general use prefix with plugin as metric name.
 *	For some plugins calculate additional statistics,
 *	use types in metric name.
 *	Build tags from plugin and type instances with small changes.
 */
    if (strcasecmp(vl->plugin, "cpu") == 0) {
      strlcat(metric_name, "cpu.", sizeof(metric_name));

      if (strcasecmp(vl->type_instance, "idle") == 0) {

        sstrncpy(tmp, metric_name, sizeof(tmp));
        strlcat(tmp, "busy", sizeof(metric_name));

        snprintf(tv, sizeof(tv), "%.2g", (100.0 - atof(ret)));

        if (cb->wa_num_caches > 0) {

          ak = (atsd_key_t *)calloc(1, sizeof(*ak));

          if (ak == NULL) {
            ERROR("write_atsd plugin: calloc failed.");
            return -1;
          }

          ak->plugin = strdup(vl->plugin);
          ak->plugin_instance = strdup(vl->plugin_instance);
          ak->type = strdup(vl->type);
          ak->type_instance = strdup("busy");

          av = (atsd_value_t *)calloc(1, sizeof(*av));
          if (av == NULL) {
            ERROR("write_atsd plugin: calloc failed.");
            return -1;
          }
          av->value = strdup(tv);
          av->time = CDTIME_T_TO_MS(vl->time);

          pthread_mutex_lock(&cb->avl_lock);
          same_value = check_cache_value(ak, av, cb);
          pthread_mutex_unlock(&cb->avl_lock);
        }
        if (same_value > 0) {
          if (vl->plugin_instance[0] != '\0') {
            snprintf(sendline, sizeof(sendline),
                     "series e:%s ms:%" PRIu64 " m:%s=%s t:instance=%s\n",
                     entity, CDTIME_T_TO_MS(vl->time), tmp, tv,
                     vl->plugin_instance);
          } else {
            snprintf(sendline, sizeof(sendline),
                     "series e:%s ms:%" PRIu64 " m:%s=%s\n", entity,
                     CDTIME_T_TO_MS(vl->time), tmp, tv);
          }
          status = wa_send_message(sendline, cb);
          if (status != 0) {
            sfree(rates);
            return (status);
          }
        };
      }
      strlcat(metric_name, vl->type_instance, sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "entropy") == 0) {
      strlcat(metric_name, "entropy", sizeof(metric_name));
      strlcat(metric_name, ".available", sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "memory") == 0) {
      strlcat(metric_name, "memory.", sizeof(metric_name));
      strlcat(metric_name, vl->type_instance, sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "swap") == 0 &&
               (strcasecmp(vl->type, "swap") == 0)) {
      strlcat(metric_name, "memory.swap_", sizeof(metric_name));
      strlcat(metric_name, vl->type_instance, sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "swap") == 0 &&
               (strcasecmp(vl->type, "swap_io") == 0)) {
      strlcat(metric_name, "io.swap_", sizeof(metric_name));
      strlcat(metric_name, vl->type_instance, sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "processes") == 0 &&
               (strcasecmp(vl->type, "ps_state") == 0)) {
      strlcat(metric_name, "processes.", sizeof(metric_name));
      strlcat(metric_name, vl->type_instance, sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "processes") == 0 &&
               (strcasecmp(vl->type, "fork_rate") == 0)) {
      strlcat(metric_name, "processes.", sizeof(metric_name));
      strlcat(metric_name, vl->type, sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "contextswitch") == 0 &&
               (strcasecmp(vl->type, "fork_rate") == 0)) {
      strlcat(metric_name, "contextswitches", sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "interface") == 0) {
      strlcat(metric_name, "interface.", sizeof(metric_name));
      strlcat(metric_name, vl->type, sizeof(metric_name));
      if (strcasecmp(ds->ds[i].name, "rx") == 0) {
        strlcat(metric_name, ".received", sizeof(metric_name));
      } else if (strcasecmp(ds->ds[i].name, "tx") == 0) {
        strlcat(metric_name, ".sent", sizeof(metric_name));
      }
    } else if (strcasecmp(vl->plugin, "df") == 0) {
      strlcat(metric_name, "df.", sizeof(metric_name));
      sstrncpy(plugin_instance, "/", sizeof(plugin_instance));

      if (strcasecmp(vl->plugin_instance, "root") != 0) {
        strlcat(plugin_instance, vl->plugin_instance, sizeof(plugin_instance));

        for (c = plugin_instance; *c; c++) {
          if (*c == '-')
            *c = '/';
        }
      }

      if (strcasecmp(vl->type, "df_inodes") == 0) {
        strlcat(metric_name, "inodes.", sizeof(metric_name));
        strlcat(metric_name, vl->type_instance, sizeof(metric_name));
      } else if (strcasecmp(vl->type, "df_complex") == 0) {
        strlcat(metric_name, "space.", sizeof(metric_name));
        strlcat(metric_name, vl->type_instance, sizeof(metric_name));
      } else if (strcasecmp(vl->type, "percent_bytes") == 0) {
        strlcat(metric_name, "space.", sizeof(metric_name));
        if (strcasecmp(vl->type_instance, "free") == 0) {
          sstrncpy(tmp, metric_name, sizeof(tmp));
          strlcat(tmp, "used-reserved.percent", sizeof(metric_name));

          snprintf(tv, sizeof(tv), "%.2g", (100.0 - atof(ret)));

          if (cb->wa_num_caches > 0) {
            ak = (atsd_key_t *)calloc(1, sizeof(*ak));
            if (ak == NULL) {
              ERROR("write_atsd plugin: calloc failed.");
              return -1;
            }

            ak->plugin = strdup(vl->plugin);
            ak->plugin_instance = strdup(vl->plugin_instance);
            ak->type = strdup(vl->type);
            ak->type_instance = strdup("used-reserved.percent");

            av = (atsd_value_t *)calloc(1, sizeof(*av));
            if (av == NULL) {
              ERROR("write_atsd plugin: calloc failed.");
              return -1;
            }
            av->value = strdup(tv);
            av->time = CDTIME_T_TO_MS(vl->time);

            pthread_mutex_lock(&cb->avl_lock);
            same_value = check_cache_value(ak, av, cb);
            pthread_mutex_unlock(&cb->avl_lock);
          }
          if (same_value > 0) {
            snprintf(sendline, sizeof(sendline),
                     "series e:%s ms:%" PRIu64 " m:%s=%s t:instance=%s\n",
                     entity, CDTIME_T_TO_MS(vl->time), tmp, tv,
                     plugin_instance);
            status = wa_send_message(sendline, cb);
            if (status != 0) {
              sfree(rates);
              return (status);
            }
          };
        }

        strlcat(metric_name, vl->type_instance, sizeof(metric_name));
        strlcat(metric_name, ".percent", sizeof(metric_name));
      } else if (strcasecmp(vl->type, "percent_inodes") == 0) {
        strlcat(metric_name, "inodes.", sizeof(metric_name));
        strlcat(metric_name, vl->type_instance, sizeof(metric_name));
        strlcat(metric_name, ".percent", sizeof(metric_name));
      } else
        ERROR("df plugin in write_atsd: unexpected value->type = %s: ",
              vl->type);

      if (cb->wa_num_caches > 0) {
        ak = (atsd_key_t *)calloc(1, sizeof(*ak));
        if (ak == NULL) {
          ERROR("write_atsd plugin: calloc failed.");
          return -1;
        }

        ak->plugin = strdup(vl->plugin);
        ak->plugin_instance = strdup(vl->plugin_instance);
        ak->type = strdup(vl->type);
        ak->type_instance = strdup(vl->type_instance);

        av = (atsd_value_t *)calloc(1, sizeof(*av));
        if (av == NULL) {
          ERROR("write_atsd plugin: calloc failed.");
          return -1;
        }
        av->value = strdup(ret);
        av->time = CDTIME_T_TO_MS(vl->time);

        pthread_mutex_lock(&cb->avl_lock);
        same_value = check_cache_value(ak, av, cb);
        pthread_mutex_unlock(&cb->avl_lock);
      }
      if (same_value > 0) {
        snprintf(sendline, sizeof(sendline),
                 "series e:%s ms:%" PRIu64 " m:%s=%s t:instance=%s\n", entity,
                 CDTIME_T_TO_MS(vl->time), metric_name, ret, plugin_instance);
        status = wa_send_message(sendline, cb);
        if (status != 0) {
          sfree(rates);
          return (status);
        }
      };

      continue;

    } else if (strcasecmp(vl->plugin, "users") == 0) {
      strlcat(metric_name, "users.logged_in", sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "postgresql") == 0) {
      strlcat(metric_name, "db.", sizeof(metric_name));
      strlcat(metric_name, vl->type, sizeof(metric_name));
      strlcat(metric_name, ".", sizeof(metric_name));
      strlcat(metric_name, vl->type_instance, sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "mongodb") == 0) {
      strlcat(metric_name, "db.", sizeof(metric_name));
      strlcat(metric_name, vl->plugin, sizeof(metric_name));
      strlcat(metric_name, ".", sizeof(metric_name));
      strlcat(metric_name, vl->type_instance, sizeof(metric_name));
    } else if (strcasecmp(vl->plugin, "load") == 0) {

      strlcat(metric_name, "load", sizeof(metric_name));
      strlcat(metric_name, ".loadavg", sizeof(metric_name));

      if (strcasecmp(ds->ds[i].name, "shortterm") == 0) {
        strlcat(metric_name, ".1m", sizeof(metric_name));
      } else if (strcasecmp(ds->ds[i].name, "midterm") == 0) {
        strlcat(metric_name, ".5m", sizeof(metric_name));
      } else if (strcasecmp(ds->ds[i].name, "longterm") == 0) {
        strlcat(metric_name, ".15m", sizeof(metric_name));
      }
    } else if (strcasecmp(vl->plugin, "aggregation") == 0) {

      sstrncpy(tmp, vl->type, sizeof(tmp));
      strlcat(tmp, "-", sizeof(tmp));

      sstrncpy(plugin_instance, vl->plugin_instance, sizeof(plugin_instance));
      str_location = strstr(plugin_instance, tmp);

      sstrncpy(plugin_instance, str_location + strlen(tmp),
               sizeof(plugin_instance));
      strlcat(metric_name, vl->type, sizeof(metric_name));
      strlcat(metric_name, ".", sizeof(metric_name));
      strlcat(metric_name, vl->plugin, sizeof(metric_name));

      if (strcasecmp(vl->type_instance, "idle") == 0 &&
          strcasecmp(str_location, "average") == 0) {

        sstrncpy(tmp, metric_name, sizeof(tmp));
        strlcat(tmp, ".busy", sizeof(metric_name));

        snprintf(tv, sizeof(tv), "%.2g", (100.0 - atof(ret)));

        strlcat(tmp, ".", sizeof(tmp));
        strlcat(tmp, str_location, sizeof(tmp));

        if (cb->wa_num_caches > 0) {
          ak = (atsd_key_t *)calloc(1, sizeof(*ak));
          if (ak == NULL) {
            ERROR("write_atsd plugin: calloc failed.");
            return -1;
          }

          ak->plugin = strdup(vl->plugin);
          ak->plugin_instance = strdup(vl->plugin_instance);
          ak->type = strdup(vl->type);
          ak->type_instance = strdup("busy");

          av = (atsd_value_t *)calloc(1, sizeof(*av));
          if (av == NULL) {
            ERROR("write_atsd plugin: calloc failed.");
            return -1;
          }
          av->value = strdup(tv);
          av->time = CDTIME_T_TO_MS(vl->time);

          pthread_mutex_lock(&cb->avl_lock);
          same_value = check_cache_value(ak, av, cb);
          pthread_mutex_unlock(&cb->avl_lock);
        }
        if (same_value > 0) {
          snprintf(sendline, sizeof(sendline),
                   "series e:%s ms:%" PRIu64 " m:%s=%s\n", entity,
                   CDTIME_T_TO_MS(vl->time), tmp, tv);
          status = wa_send_message(sendline, cb);
          if (status != 0) {
            sfree(rates);
            return (status);
          }
        };
      }

      strlcat(metric_name, ".", sizeof(metric_name));
      strlcat(metric_name, vl->type_instance, sizeof(metric_name));

      strlcat(metric_name, ".", sizeof(metric_name));
      strlcat(metric_name, str_location, sizeof(metric_name));

      if (cb->wa_num_caches > 0) {
        ak = (atsd_key_t *)calloc(1, sizeof(*ak));
        if (ak == NULL) {
          ERROR("write_atsd plugin: calloc failed.");
          return -1;
        }

        ak->plugin = strdup(vl->plugin);
        ak->plugin_instance = strdup(vl->plugin_instance);
        ak->type = strdup(vl->type);
        ak->type_instance = strdup(vl->type_instance);

        av = (atsd_value_t *)calloc(1, sizeof(*av));
        if (av == NULL) {
          ERROR("write_atsd plugin: calloc failed.");
          return -1;
        }
        av->value = strdup(ret);
        av->time = CDTIME_T_TO_MS(vl->time);

        pthread_mutex_lock(&cb->avl_lock);
        same_value = check_cache_value(ak, av, cb);
        pthread_mutex_unlock(&cb->avl_lock);
      }
      if (same_value > 0) {
        snprintf(sendline, sizeof(sendline),
                 "series e:%s ms:%" PRIu64 " m:%s=%s\n", entity,
                 CDTIME_T_TO_MS(vl->time), metric_name, ret);
        status = wa_send_message(sendline, cb);
        if (status != 0) {
          sfree(rates);
          return (status);
        }
      };
      continue;
    } else if (strcasecmp(vl->plugin, "exec") == 0) {
      strlcat(metric_name, vl->plugin_instance, sizeof(metric_name));
      if (cb->wa_num_caches > 0) {
        ak = (atsd_key_t *)calloc(1, sizeof(*ak));
        if (ak == NULL) {
          ERROR("write_atsd plugin: calloc failed.");
          return -1;
        }

        ak->plugin = strdup(vl->plugin);
        ak->plugin_instance = strdup(vl->plugin_instance);
        ak->type = strdup(vl->type);
        ak->type_instance = strdup(vl->type_instance);

        av = (atsd_value_t *)calloc(1, sizeof(*av));
        if (av == NULL) {
          ERROR("write_atsd plugin: calloc failed.");
          return -1;
        }
        av->value = strdup(ret);
        av->time = CDTIME_T_TO_MS(vl->time);

        pthread_mutex_lock(&cb->avl_lock);
        same_value = check_cache_value(ak, av, cb);
        pthread_mutex_unlock(&cb->avl_lock);
      }
      if (same_value > 0) {
        snprintf(sendline, sizeof(sendline),
                 "series e:%s ms:%" PRIu64 " m:%s=%s", entity,
                 CDTIME_T_TO_MS(vl->time), metric_name, ret);

        char *type_instance = strdup(vl->type_instance);
        char *saveptr = NULL;
        char *key_value = strtok_r(type_instance, ";", &saveptr);
        memset(tmp, '\0', sizeof(tmp));

        while (key_value != NULL) {
          if (strstr(key_value, "=") != NULL) {
            strlcat(tmp, " t:", sizeof(tmp));
            strlcat(tmp, key_value, sizeof(tmp));
          } else {
            snprintf(tmp, sizeof(tmp), " t:instance=\"%s\"", vl->type_instance);
            sfree(type_instance);
            sfree(key_value);
            break;
          }
          key_value = strtok_r(NULL, ";", &saveptr);
        }

        sfree(type_instance);
        sfree(key_value);

        strlcat(sendline, tmp, sizeof(sendline));
        strlcat(sendline, "\n", sizeof(sendline));
        status = wa_send_message(sendline, cb);
        if (status != 0) {
          sfree(rates);
          return (status);
        }
      };
      continue;
    } else {
      strlcat(metric_name, vl->plugin, sizeof(metric_name));
      if (vl->type[0] != '\0') {
        strlcat(metric_name, ".", sizeof(metric_name));
        strlcat(metric_name, vl->type, sizeof(metric_name));
      }
      if (vl->type_instance[0] != '\0') {
        strlcat(metric_name, ".", sizeof(metric_name));
        strlcat(metric_name, vl->type_instance, sizeof(metric_name));
      }
      if (strcasecmp(ds->ds[i].name, "value") != 0) {
        strlcat(metric_name, ".", sizeof(metric_name));
        strlcat(metric_name, ds->ds[i].name, sizeof(metric_name));
      }
    }

    if (cb->wa_num_caches > 0) {
      ak = (atsd_key_t *)calloc(1, sizeof(*ak));
      if (ak == NULL) {
        ERROR("write_atsd plugin: calloc failed.");
        return -1;
      }

      ak->plugin = strdup(vl->plugin);
      ak->plugin_instance = strdup(vl->plugin_instance);
      ak->type = strdup(vl->type);
      ak->type_instance = strdup(vl->type_instance);

      av = (atsd_value_t *)calloc(1, sizeof(*av));
      if (av == NULL) {
        ERROR("write_atsd plugin: calloc failed.");
        return -1;
      }
      av->value = strdup(ret);
      av->time = CDTIME_T_TO_MS(vl->time);

      pthread_mutex_lock(&cb->avl_lock);
      same_value = check_cache_value(ak, av, cb);
      pthread_mutex_unlock(&cb->avl_lock);
    }
    if (same_value > 0) {
      if (vl->plugin_instance[0] != '\0') {
        snprintf(sendline, sizeof(sendline),
                 "series e:%s ms:%" PRIu64 " m:%s=%s t:instance=%s\n", entity,
                 CDTIME_T_TO_MS(vl->time), metric_name, ret,
                 vl->plugin_instance);
      } else {
        snprintf(sendline, sizeof(sendline),
                 "series e:%s ms:%" PRIu64 " m:%s=%s\n", entity,
                 CDTIME_T_TO_MS(vl->time), metric_name, ret);
      }
      status = wa_send_message(sendline, cb);
      if (status != 0) {
        sfree(rates);
        return (status);
      }
    };
  }
  sfree(rates);
  return 0;
}

/* int wa_write_messages */

static int wa_write(const data_set_t *ds, const value_list_t *vl,
                    user_data_t *user_data) {
  struct wa_callback *cb;
  int status;

  if (user_data == NULL)
    return (EINVAL);

  cb = user_data->data;

  status = wa_write_messages(ds, vl, cb);

  return (status);
}

static int wa_config_cache(struct wa_callback *cb, oconfig_item_t *child) {

  int q, status;
  struct wa_cache_s *nc, wc;
  struct wa_cache_s **tmp;
  memset(&wc, 0, sizeof(struct wa_cache_s));

  status = cf_util_get_string(child, &wc.name);
  if (status != 0)
    return status;

  for (q = 0; q < child->children_num; q++) {
    oconfig_item_t *grandchild = child->children + q;

    if (strcasecmp("Interval", grandchild->key) == 0)
      cf_util_get_int(grandchild, &wc.interval);
    else if (strcasecmp("Threshold", grandchild->key) == 0)
      cf_util_get_double(grandchild, &wc.threshold);
    else {
      ERROR("write_atsd plugin: Invalid configuration "
            "option: %s.",
            grandchild->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  tmp = realloc(cb->wa_caches,
                (cb->wa_num_caches + 1) * sizeof(*(cb->wa_caches)));
  if (tmp == NULL) {
    return ENOMEM;
  }
  cb->wa_caches = tmp;

  nc = malloc(sizeof(*nc));
  if (!nc) {
    return ENOMEM;
  }

  memcpy(nc, &wc, sizeof(*nc));
  cb->wa_caches[cb->wa_num_caches++] = nc;

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
  cb->short_hostname = 0;
  cb->wa_num_caches = 0;
  cb->wa_caches = NULL;
  cb->cache_tree = NULL;
  cb->cache_tree = c_avl_create((void *)compare_atsd_keys);

  if (cb->cache_tree == NULL) {
    ERROR("utils_vl_lookup: c_avl_create failed.");
    return -1;
  }

  pthread_mutex_init(&cb->send_lock, /* attr = */ NULL);
  pthread_mutex_init(&cb->avl_lock, /* attr = */ NULL);
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
    else if (strcasecmp("Cache", child->key) == 0) {
      wa_config_cache(cb, child);
    } else {
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
