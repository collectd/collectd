/**
 * collectd - src/write_tsdb.c
 * Copyright (C) 2012       Pierre-Yves Ritschard
 * Copyright (C) 2011       Scott Sanders
 * Copyright (C) 2009       Paul Sadauskas
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2007-2012  Florian octo Forster
 * Copyright (C) 2013-2014  Limelight Networks, Inc.
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
 * write_tsdb Authors:
 *   Brett Hawn <bhawn at llnw.com>
 *   Kevin Bowling <kbowling@llnw.com>
 *   Yves Mettier <ymettier@free.fr>
 **/

/* write_tsdb plugin configuation example
 * --------------------------------------
 *
 * <Plugin write_tsdb>
 *   <Node>
 *     Host "localhost"
 *     Port "4242"
 *     HostTags "status=production deviceclass=www"
 *   </Node>
 * </Plugin>
 *
 * write_tsdb meta_data
 * --------------------
 *  - tsdb_prefix : Will prefix the OpenTSDB <metric> (also prefix tsdb_id if
 * defined)
 *  - tsdb_id     : Replace the metric with this tag
 *
 *  - tsdb_tag_plugin         : When defined, tsdb_tag_* removes the related
 *  - tsdb_tag_pluginInstance : item from metric id.
 *  - tsdb_tag_type           : If it is not empty, it will be the key of an
 *  - tsdb_tag_typeInstance   : opentsdb tag (the value is the item itself)
 *  - tsdb_tag_dsname         : If it is empty, no tag is defined.
 *
 *  - tsdb_tag_add_*          : Should contain "tagv". Il will add a tag.
 *                            : The key tagk comes from the tsdb_tag_add_*
 *                            : tag. Example : tsdb_tag_add_status adds a tag
 *                            : named 'status'.
 *                            : It will be sent as is to the TSDB server.
 *
 * write_tsdb plugin filter rules example
 * --------------------------------------
 *
 * <Chain "PreCache">
 *   <Rule "opentsdb_cpu">
 *     <Match "regex">
 *       Plugin "^cpu$"
 *     </Match>
 *     <Target "set">
 *       MetaDataSet "tsdb_tag_pluginInstance" "cpu"
 *       MetaDataSet "tsdb_tag_type" ""
 *       MetaDataSet "tsdb_prefix" "sys."
 *     </Target>
 *   </Rule>
 *   <Rule "opentsdb_df">
 *     <Match "regex">
 *       Plugin "^df$"
 *     </Match>
 *     <Target "set">
 *       MetaDataSet "tsdb_tag_pluginInstance" "mount"
 *       MetaDataSet "tsdb_tag_type" ""
 *       MetaDataSet "tsdb_prefix" "sys."
 *     </Target>
 *   </Rule>
 *   <Rule "opentsdb_disk">
 *     <Match "regex">
 *       Plugin "^disk$"
 *     </Match>
 *     <Target "set">
 *       MetaDataSet "tsdb_tag_pluginInstance" "disk"
 *       MetaDataSet "tsdb_prefix" "sys."
 *     </Target>
 *   </Rule>
 *   <Rule "opentsdb_interface">
 *     <Match "regex">
 *       Plugin "^interface$"
 *     </Match>
 *     <Target "set">
 *       MetaDataSet "tsdb_tag_pluginInstance" "iface"
 *       MetaDataSet "tsdb_prefix" "sys."
 *     </Target>
 *   </Rule>
 *   <Rule "opentsdb_load">
 *     <Match "regex">
 *       Plugin "^loac$"
 *     </Match>
 *     <Target "set">
 *       MetaDataSet "tsdb_tag_type" ""
 *       MetaDataSet "tsdb_prefix" "sys."
 *     </Target>
 *   </Rule>
 *   <Rule "opentsdb_swap">
 *     <Match "regex">
 *       Plugin "^swap$"
 *     </Match>
 *     <Target "set">
 *       MetaDataSet "tsdb_prefix" "sys."
 *     </Target>
 *   </Rule>
 * </Chain>
 *
 * IMPORTANT WARNING
 * -----------------
 * OpenTSDB allows no more than 8 tags.
 * Collectd admins should be aware of this when defining filter rules and host
 * tags.
 *
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include "utils_cache.h"

#include <netdb.h>

#ifndef WT_DEFAULT_NODE
#define WT_DEFAULT_NODE "localhost"
#endif

#ifndef WT_DEFAULT_SERVICE
#define WT_DEFAULT_SERVICE "4242"
#endif

#ifndef WT_DEFAULT_ESCAPE
#define WT_DEFAULT_ESCAPE '.'
#endif

/* Ethernet - (IPv6 + TCP) = 1500 - (40 + 32) = 1428 */
#ifndef WT_SEND_BUF_SIZE
#define WT_SEND_BUF_SIZE 1428
#endif

/* Meta data definitions about tsdb tags */
#define TSDB_TAG_PLUGIN 0
#define TSDB_TAG_PLUGININSTANCE 1
#define TSDB_TAG_TYPE 2
#define TSDB_TAG_TYPEINSTANCE 3
#define TSDB_TAG_DSNAME 4
static const char *meta_tag_metric_id[] = {
    "tsdb_tag_plugin", "tsdb_tag_pluginInstance", "tsdb_tag_type",
    "tsdb_tag_typeInstance", "tsdb_tag_dsname"};

/*
 * Private variables
 */
struct wt_callback {
  int sock_fd;

  char *node;
  char *service;
  char *host_tags;

  _Bool store_rates;
  _Bool always_append_ds;

  char send_buf[WT_SEND_BUF_SIZE];
  size_t send_buf_free;
  size_t send_buf_fill;
  cdtime_t send_buf_init_time;

  pthread_mutex_t send_lock;

  _Bool connect_failed_log_enabled;
};

/*
 * Functions
 */
static void wt_reset_buffer(struct wt_callback *cb) {
  memset(cb->send_buf, 0, sizeof(cb->send_buf));
  cb->send_buf_free = sizeof(cb->send_buf);
  cb->send_buf_fill = 0;
  cb->send_buf_init_time = cdtime();
}

static int wt_send_buffer(struct wt_callback *cb) {
  ssize_t status = 0;

  status = swrite(cb->sock_fd, cb->send_buf, strlen(cb->send_buf));
  if (status < 0) {
    char errbuf[1024];
    ERROR("write_tsdb plugin: send failed with status %zi (%s)", status,
          sstrerror(errno, errbuf, sizeof(errbuf)));

    close(cb->sock_fd);
    cb->sock_fd = -1;

    return -1;
  }

  return 0;
}

/* NOTE: You must hold cb->send_lock when calling this function! */
static int wt_flush_nolock(cdtime_t timeout, struct wt_callback *cb) {
  int status;

  DEBUG("write_tsdb plugin: wt_flush_nolock: timeout = %.3f; "
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

  status = wt_send_buffer(cb);
  wt_reset_buffer(cb);

  return status;
}

static int wt_callback_init(struct wt_callback *cb) {
  struct addrinfo *ai_list;
  int status;

  const char *node = cb->node ? cb->node : WT_DEFAULT_NODE;
  const char *service = cb->service ? cb->service : WT_DEFAULT_SERVICE;

  if (cb->sock_fd > 0)
    return 0;

  struct addrinfo ai_hints = {.ai_family = AF_UNSPEC,
                              .ai_flags = AI_ADDRCONFIG,
                              .ai_socktype = SOCK_STREAM};

  status = getaddrinfo(node, service, &ai_hints, &ai_list);
  if (status != 0) {
    if (cb->connect_failed_log_enabled) {
      ERROR("write_tsdb plugin: getaddrinfo (%s, %s) failed: %s", node, service,
            gai_strerror(status));
      cb->connect_failed_log_enabled = 0;
    }
    return -1;
  }

  assert(ai_list != NULL);
  for (struct addrinfo *ai_ptr = ai_list; ai_ptr != NULL;
       ai_ptr = ai_ptr->ai_next) {
    cb->sock_fd =
        socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
    if (cb->sock_fd < 0)
      continue;

    set_sock_opts(cb->sock_fd);

    status = connect(cb->sock_fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    if (status != 0) {
      close(cb->sock_fd);
      cb->sock_fd = -1;
      continue;
    }

    break;
  }

  freeaddrinfo(ai_list);

  if (cb->sock_fd < 0) {
    if (cb->connect_failed_log_enabled) {
      char errbuf[1024];
      ERROR("write_tsdb plugin: Connecting to %s:%s failed. "
            "The last error was: %s",
            node, service, sstrerror(errno, errbuf, sizeof(errbuf)));
      cb->connect_failed_log_enabled = 0;
    }
    return -1;
  }

  if (0 == cb->connect_failed_log_enabled) {
    WARNING("write_tsdb plugin: Connecting to %s:%s succeeded.", node, service);
    cb->connect_failed_log_enabled = 1;
  }
  wt_reset_buffer(cb);

  return 0;
}

static void wt_callback_free(void *data) {
  struct wt_callback *cb;

  if (data == NULL)
    return;

  cb = data;

  pthread_mutex_lock(&cb->send_lock);

  wt_flush_nolock(0, cb);

  close(cb->sock_fd);
  cb->sock_fd = -1;

  sfree(cb->node);
  sfree(cb->service);
  sfree(cb->host_tags);

  pthread_mutex_destroy(&cb->send_lock);

  sfree(cb);
}

static int wt_flush(cdtime_t timeout,
                    const char *identifier __attribute__((unused)),
                    user_data_t *user_data) {
  struct wt_callback *cb;
  int status;

  if (user_data == NULL)
    return -EINVAL;

  cb = user_data->data;

  pthread_mutex_lock(&cb->send_lock);

  if (cb->sock_fd < 0) {
    status = wt_callback_init(cb);
    if (status != 0) {
      if (cb->connect_failed_log_enabled || cb->sock_fd >= 0) {
        /* Do not log if socket is not enabled : it was logged already
         * in wt_callback_init(). */
        ERROR("write_tsdb plugin: wt_callback_init failed.");
      }
      pthread_mutex_unlock(&cb->send_lock);
      return -1;
    }
  }

  status = wt_flush_nolock(timeout, cb);
  pthread_mutex_unlock(&cb->send_lock);

  return status;
}

static int wt_format_values(char *ret, size_t ret_len, int ds_num,
                            const data_set_t *ds, const value_list_t *vl,
                            _Bool store_rates) {
  size_t offset = 0;
  int status;
  gauge_t *rates = NULL;

  assert(0 == strcmp(ds->type, vl->type));

  memset(ret, 0, ret_len);

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    status = ssnprintf(ret + offset, ret_len - offset, __VA_ARGS__);           \
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
    BUFFER_ADD("%llu", vl->values[ds_num].counter);
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

static int wt_format_tags(char *ret, int ret_len, const value_list_t *vl,
                          const struct wt_callback *cb, const char *ds_name) {
  int status;
  char *temp = NULL;
  char *ptr = ret;
  size_t remaining_len = ret_len;
  char **meta_toc;
  int i, n;
#define TSDB_META_TAG_ADD_PREFIX "tsdb_tag_add_"

#define TSDB_META_DATA_GET_STRING(tag)                                         \
  do {                                                                         \
    temp = NULL;                                                               \
    status = meta_data_get_string(vl->meta, tag, &temp);                       \
    if (status == -ENOENT) {                                                   \
      temp = NULL;                                                             \
      /* defaults to empty string */                                           \
    } else if (status < 0) {                                                   \
      sfree(temp);                                                             \
      return status;                                                           \
    }                                                                          \
  } while (0)

#define TSDB_STRING_APPEND_SPRINTF(key, value)                                 \
  do {                                                                         \
    int n;                                                                     \
    const char *k = (key);                                                     \
    const char *v = (value);                                                   \
    if (k[0] != '\0' && v[0] != '\0') {                                        \
      n = ssnprintf(ptr, remaining_len, " %s=%s", k, v);                       \
      if (n >= remaining_len) {                                                \
        ptr[0] = '\0';                                                         \
      } else {                                                                 \
        char *ptr2 = ptr + 1;                                                  \
        while (NULL != (ptr2 = strchr(ptr2, ' ')))                             \
          ptr2[0] = '_';                                                       \
        ptr += n;                                                              \
        remaining_len -= n;                                                    \
      }                                                                        \
    }                                                                          \
  } while (0)

  if (vl->meta) {
    TSDB_META_DATA_GET_STRING(meta_tag_metric_id[TSDB_TAG_PLUGIN]);
    if (temp) {
      TSDB_STRING_APPEND_SPRINTF(temp, vl->plugin);
      sfree(temp);
    }

    TSDB_META_DATA_GET_STRING(meta_tag_metric_id[TSDB_TAG_PLUGININSTANCE]);
    if (temp) {
      TSDB_STRING_APPEND_SPRINTF(temp, vl->plugin_instance);
      sfree(temp);
    }

    TSDB_META_DATA_GET_STRING(meta_tag_metric_id[TSDB_TAG_TYPE]);
    if (temp) {
      TSDB_STRING_APPEND_SPRINTF(temp, vl->type);
      sfree(temp);
    }

    TSDB_META_DATA_GET_STRING(meta_tag_metric_id[TSDB_TAG_TYPEINSTANCE]);
    if (temp) {
      TSDB_STRING_APPEND_SPRINTF(temp, vl->type_instance);
      sfree(temp);
    }

    if (ds_name) {
      TSDB_META_DATA_GET_STRING(meta_tag_metric_id[TSDB_TAG_DSNAME]);
      if (temp) {
        TSDB_STRING_APPEND_SPRINTF(temp, ds_name);
        sfree(temp);
      }
    }

    n = meta_data_toc(vl->meta, &meta_toc);
    for (i = 0; i < n; i++) {
      if (strncmp(meta_toc[i], TSDB_META_TAG_ADD_PREFIX,
                  sizeof(TSDB_META_TAG_ADD_PREFIX) - 1)) {
        free(meta_toc[i]);
        continue;
      }
      if ('\0' == meta_toc[i][sizeof(TSDB_META_TAG_ADD_PREFIX) - 1]) {
        ERROR("write_tsdb plugin: meta_data tag '%s' is unknown (host=%s, "
              "plugin=%s, type=%s)",
              temp, vl->host, vl->plugin, vl->type);
        free(meta_toc[i]);
        continue;
      }

      TSDB_META_DATA_GET_STRING(meta_toc[i]);
      if (temp && temp[0]) {
        int n;
        char *key = meta_toc[i] + sizeof(TSDB_META_TAG_ADD_PREFIX) - 1;

        n = ssnprintf(ptr, remaining_len, " %s=%s", key, temp);
        if (n >= remaining_len) {
          ptr[0] = '\0';
        } else {
          /* We do not check the tags syntax here. It should have
           * been done earlier.
           */
          ptr += n;
          remaining_len -= n;
        }
      }
      if (temp)
        sfree(temp);
      free(meta_toc[i]);
    }
    if (meta_toc)
      free(meta_toc);

  } else {
    ret[0] = '\0';
  }

#undef TSDB_META_DATA_GET_STRING
#undef TSDB_STRING_APPEND_SPRINTF

  return 0;
}

static int wt_format_name(char *ret, int ret_len, const value_list_t *vl,
                          const struct wt_callback *cb, const char *ds_name) {
  int status;
  int i;
  char *temp = NULL;
  char *prefix = NULL;
  const char *meta_prefix = "tsdb_prefix";
  char *tsdb_id = NULL;
  const char *meta_id = "tsdb_id";

  _Bool include_in_id[] = {
      /* plugin =          */ 1,
      /* plugin instance = */ (vl->plugin_instance[0] == '\0') ? 0 : 1,
      /* type =            */ 1,
      /* type instance =   */ (vl->type_instance[0] == '\0') ? 0 : 1,
      /* ds_name =         */ (ds_name == NULL) ? 0 : 1};

  if (vl->meta) {
    status = meta_data_get_string(vl->meta, meta_prefix, &temp);
    if (status == -ENOENT) {
      /* defaults to empty string */
    } else if (status < 0) {
      sfree(temp);
      return status;
    } else {
      prefix = temp;
    }

    status = meta_data_get_string(vl->meta, meta_id, &temp);
    if (status == -ENOENT) {
      /* defaults to empty string */
    } else if (status < 0) {
      sfree(temp);
      return status;
    } else {
      tsdb_id = temp;
    }

    for (i = 0; i < (sizeof(meta_tag_metric_id) / sizeof(*meta_tag_metric_id));
         i++) {
      if (meta_data_exists(vl->meta, meta_tag_metric_id[i]) == 0) {
        /* defaults to already initialized format */
      } else {
        include_in_id[i] = 0;
      }
    }
  }
  if (tsdb_id) {
    ssnprintf(ret, ret_len, "%s%s", prefix ? prefix : "", tsdb_id);
  } else {
#define TSDB_STRING_APPEND_STRING(string)                                      \
  do {                                                                         \
    const char *str = (string);                                                \
    size_t len = strlen(str);                                                  \
    if (len > (remaining_len - 1)) {                                           \
      ptr[0] = '\0';                                                           \
      return (-ENOSPC);                                                        \
    }                                                                          \
    if (len > 0) {                                                             \
      memcpy(ptr, str, len);                                                   \
      ptr += len;                                                              \
      remaining_len -= len;                                                    \
    }                                                                          \
  } while (0)

#define TSDB_STRING_APPEND_DOT                                                 \
  do {                                                                         \
    if (remaining_len > 2) {                                                   \
      ptr[0] = '.';                                                            \
      ptr++;                                                                   \
      remaining_len--;                                                         \
    } else {                                                                   \
      ptr[0] = '\0';                                                           \
      return (-ENOSPC);                                                        \
    }                                                                          \
  } while (0)

    char *ptr = ret;
    size_t remaining_len = ret_len;
    if (prefix) {
      TSDB_STRING_APPEND_STRING(prefix);
    }
    if (include_in_id[TSDB_TAG_PLUGIN]) {
      TSDB_STRING_APPEND_STRING(vl->plugin);
    }

    if (include_in_id[TSDB_TAG_PLUGININSTANCE]) {
      TSDB_STRING_APPEND_DOT;
      TSDB_STRING_APPEND_STRING(vl->plugin_instance);
    }
    if (include_in_id[TSDB_TAG_TYPE]) {
      TSDB_STRING_APPEND_DOT;
      TSDB_STRING_APPEND_STRING(vl->type);
    }
    if (include_in_id[TSDB_TAG_TYPEINSTANCE]) {
      TSDB_STRING_APPEND_DOT;
      TSDB_STRING_APPEND_STRING(vl->type_instance);
    }
    if (include_in_id[TSDB_TAG_DSNAME]) {
      TSDB_STRING_APPEND_DOT;
      TSDB_STRING_APPEND_STRING(ds_name);
    }
    ptr[0] = '\0';
#undef TSDB_STRING_APPEND_STRING
#undef TSDB_STRING_APPEND_DOT
  }

  sfree(tsdb_id);
  sfree(prefix);
  return 0;
}

static int wt_send_message(const char *key, const char *value,
                           const char *value_tags, cdtime_t time,
                           struct wt_callback *cb, const value_list_t *vl) {
  int status;
  size_t message_len;
  char *temp = NULL;
  const char *tags = "";
  char message[1024];
  const char *host_tags = cb->host_tags ? cb->host_tags : "";
  const char *meta_tsdb = "tsdb_tags";
  const char *host = vl->host;
  meta_data_t *md = vl->meta;

  const char *node = cb->node ? cb->node : WT_DEFAULT_NODE;
  const char *service = cb->service ? cb->service : WT_DEFAULT_SERVICE;

  /* skip if value is NaN */
  if (value[0] == 'n')
    return 0;

  if (md) {
    status = meta_data_get_string(md, meta_tsdb, &temp);
    if (status == -ENOENT) {
      /* defaults to empty string */
    } else if (status < 0) {
      ERROR("write_tsdb plugin (%s:%s): tags metadata get failure", node,
            service);
      sfree(temp);
      return status;
    } else {
      tags = temp;
    }
  }

  status = ssnprintf(
      message, sizeof(message), "put %s %.0f %s fqdn=%s %s %s %s\r\n", key,
      CDTIME_T_TO_DOUBLE(time), value, host, value_tags, tags, host_tags);
  sfree(temp);
  if (status < 0)
    return -1;
  message_len = (size_t)status;

  if (message_len >= sizeof(message)) {
    ERROR("write_tsdb plugin(%s:%s): message buffer too small: "
          "Need %zu bytes.",
          node, service, message_len + 1);
    return -1;
  }

  pthread_mutex_lock(&cb->send_lock);

  if (cb->sock_fd < 0) {
    status = wt_callback_init(cb);
    if (status != 0) {
      if (cb->connect_failed_log_enabled || cb->sock_fd >= 0) {
        /* Do not log if socket is not enabled : it was logged already
         * in wt_callback_init(). */
        ERROR("write_tsdb plugin (%s:%s): wt_callback_init failed.", node,
              service);
        cb->connect_failed_log_enabled = 0;
      }
      pthread_mutex_unlock(&cb->send_lock);
      return -1;
    }
  }

  if (message_len >= cb->send_buf_free) {
    status = wt_flush_nolock(0, cb);
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

  DEBUG("write_tsdb plugin: [%s]:%s buf %zu/%zu (%.1f %%) \"%s\"", node,
        service, cb->send_buf_fill, sizeof(cb->send_buf),
        100.0 * ((double)cb->send_buf_fill) / ((double)sizeof(cb->send_buf)),
        message);

  pthread_mutex_unlock(&cb->send_lock);

  return 0;
}

static int wt_write_messages(const data_set_t *ds, const value_list_t *vl,
                             struct wt_callback *cb) {
  char key[10 * DATA_MAX_NAME_LEN];
  char values[512];
  char tags[10 * DATA_MAX_NAME_LEN];

  int status;

  const char *node = cb->node ? cb->node : WT_DEFAULT_NODE;
  const char *service = cb->service ? cb->service : WT_DEFAULT_SERVICE;

  if (0 != strcmp(ds->type, vl->type)) {
    ERROR("write_tsdb plugin: DS type does not match "
          "value list type");
    return -1;
  }

  for (size_t i = 0; i < ds->ds_num; i++) {
    const char *ds_name = NULL;

    if (cb->always_append_ds || (ds->ds_num > 1))
      ds_name = ds->ds[i].name;

    /* Copy the identifier to 'key' and escape it. */
    status = wt_format_name(key, sizeof(key), vl, cb, ds_name);
    if (status != 0) {
      ERROR("write_tsdb plugin: error with format_name");
      return status;
    }

    escape_string(key, sizeof(key));
    /* Convert the values to an ASCII representation and put that into
     * 'values'. */
    status =
        wt_format_values(values, sizeof(values), i, ds, vl, cb->store_rates);
    if (status != 0) {
      ERROR("write_tsdb plugin: error with "
            "wt_format_values");
      return status;
    }

    /* Copy tags from p-pi/t-ti ds notation into tags */
    tags[0] = '\0';
    status = wt_format_tags(tags, sizeof(tags), vl, cb, ds_name);
    if (status != 0) {
      ERROR("write_tsdb plugin: error with format_tags");
      return status;
    }

    /* Send the message to tsdb */
    status = wt_send_message(key, values, tags, vl->time, cb, vl);
    if (status != 0) {
      if (cb->connect_failed_log_enabled) {
        /* Do not log if socket is not enabled : it was logged already
         * in wt_callback_init(). */
        ERROR("write_tsdb plugin (%s:%s): error with "
              "wt_send_message",
              node, service);
      }
      return status;
    }
  }

  return 0;
}

static int wt_write(const data_set_t *ds, const value_list_t *vl,
                    user_data_t *user_data) {
  struct wt_callback *cb;
  int status;

  if (user_data == NULL)
    return EINVAL;

  cb = user_data->data;

  status = wt_write_messages(ds, vl, cb);

  return status;
}

static int wt_config_tsd(oconfig_item_t *ci) {
  struct wt_callback *cb;
  char callback_name[DATA_MAX_NAME_LEN];

  cb = calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("write_tsdb plugin: calloc failed.");
    return -1;
  }
  cb->sock_fd = -1;
  cb->node = NULL;
  cb->service = NULL;
  cb->host_tags = NULL;
  cb->store_rates = 0;
  cb->connect_failed_log_enabled = 1;

  pthread_mutex_init(&cb->send_lock, NULL);

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Host", child->key) == 0)
      cf_util_get_string(child, &cb->node);
    else if (strcasecmp("Port", child->key) == 0)
      cf_util_get_service(child, &cb->service);
    else if (strcasecmp("HostTags", child->key) == 0)
      cf_util_get_string(child, &cb->host_tags);
    else if (strcasecmp("StoreRates", child->key) == 0)
      cf_util_get_boolean(child, &cb->store_rates);
    else if (strcasecmp("AlwaysAppendDS", child->key) == 0)
      cf_util_get_boolean(child, &cb->always_append_ds);
    else {
      ERROR("write_tsdb plugin: Invalid configuration "
            "option: %s.",
            child->key);
    }
  }

  ssnprintf(callback_name, sizeof(callback_name), "write_tsdb/%s/%s",
            cb->node != NULL ? cb->node : WT_DEFAULT_NODE,
            cb->service != NULL ? cb->service : WT_DEFAULT_SERVICE);

  user_data_t user_data = {.data = cb, .free_func = wt_callback_free};

  plugin_register_write(callback_name, wt_write, &user_data);

  user_data.free_func = NULL;
  plugin_register_flush(callback_name, wt_flush, &user_data);

  return 0;
}

static int wt_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Node", child->key) == 0)
      wt_config_tsd(child);
    else {
      ERROR("write_tsdb plugin: Invalid configuration "
            "option: %s.",
            child->key);
    }
  }

  return 0;
}

void module_register(void) {
  plugin_register_complex_config("write_tsdb", wt_config);
}

/* vim: set sw=4 ts=4 sts=4 tw=78 et : */
