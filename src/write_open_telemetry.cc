/**
 * collectd - src/write_open_telemetry.c
 * Copyright (C) 2023       Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

/* write_open_telemetry plugin configuration example
 *
 * <Plugin write_open_telemetry>
 *   <Node "name">
 *     Host "localhost"
 *     Port "8080"
 *     Path "/v1/metrics"
 *   </Node>
 * </Plugin>
 */

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include "utils/avltree/avltree.h"
#include "utils/format_open_telemetry/format_open_telemetry.h"
#include "utils/strbuf/strbuf.h"
#include "utils_complain.h"

#include <netdb.h>

#ifndef OT_DEFAULT_HOST
#define OT_DEFAULT_HOST "localhost"
#endif

#ifndef OT_DEFAULT_PORT
#define OT_DEFAULT_PORT "8080"
#endif

#ifndef OT_DEFAULT_PATH
#define OT_DEFAULT_PATH "/v1/metrics"
#endif

#ifndef OT_DEFAULT_LOG_SEND_ERRORS
#define OT_DEFAULT_LOG_SEND_ERRORS true
#endif

#ifndef OT_DEFAULT_ESCAPE
#define OT_DEFAULT_ESCAPE '_'
#endif

/* Ethernet - (IPv6 + TCP) = 1500 - (40 + 32) = 1428 */
#ifndef OT_SEND_BUF_SIZE
#define OT_SEND_BUF_SIZE 1428
#endif

#ifndef OT_MIN_RECONNECT_INTERVAL
#define OT_MIN_RECONNECT_INTERVAL TIME_T_TO_CDTIME_T(1)
#endif

/*
 * Private variables
 */
typedef struct {
  char *name;
  int reference_count;

  char *host;
  char *port;
  char *path;

  c_avl_tree_t *cached_metrics;         // char* metric_identity() -> NULL
  c_avl_tree_t *cached_metric_families; // char* fam->name -> metric_family_t*

  pthread_mutex_t mu;
} ot_callback_t;

static int ot_send_buffer(ot_callback_t *cb) {
  size_t families_num = (size_t)c_avl_size(cb->cached_metric_families);
  metric_family_t *families[families_num];

  memset(families, 0, sizeof(families));

  c_avl_iterator_t *iter = c_avl_get_iterator(cb->cached_metric_families);
  for (size_t i = 0; i < families_num; i++) {
    metric_family_t *fam = NULL;
    int status = c_avl_iterator_next(iter, /* key = */ NULL, (void **)&fam);
    if (status != 0) {
      ERROR("write_open_telemetry plugin: found %zu metric families, want %zu",
            i, families_num);
      return -1;
    }

    families[i] = fam;
  }

  DEBUG("write_open_telemetry plugin: TODO(octo): send %zu metric families",
        families_num);
  return 0;
}

static void ot_reset_buffer(ot_callback_t *cb) {
  void *dummy = NULL;
  metric_family_t *fam = NULL;
  while (c_avl_pick(cb->cached_metric_families, &dummy, (void **)&fam) == 0) {
    metric_family_free(fam);
  }

  char *id = NULL;
  while (c_avl_pick(cb->cached_metrics, (void **)&id, NULL) == 0) {
    sfree(id);
  }
}

/* NOTE: You must hold cb->send_lock when calling this function! */
static int ot_flush_nolock(cdtime_t timeout, ot_callback_t *cb) {
  DEBUG("write_open_telemetry plugin: ot_flush_nolock: timeout = %.3f; "
        "send_buf_fill = %" PRIsz ";",
        (double)timeout, cb->send_buf_fill);

#if 0
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
#endif

  int status = ot_send_buffer(cb);
  ot_reset_buffer(cb);

  return status;
}

static void ot_callback_decref(void *data) {
  ot_callback_t *cb = (ot_callback_t *)data;
  if (cb == NULL)
    return;

  pthread_mutex_lock(&cb->mu);
  cb->reference_count--;
  if (cb->reference_count > 0) {
    pthread_mutex_unlock(&cb->mu);
    return;
  }

  ot_flush_nolock(/* timeout = */ 0, cb);

  c_avl_destroy(cb->cached_metrics);
  c_avl_destroy(cb->cached_metric_families);

  sfree(cb->host);
  sfree(cb->port);
  sfree(cb->path);

  pthread_mutex_unlock(&cb->mu);
  pthread_mutex_destroy(&cb->mu);

  sfree(cb);
}

static int ot_flush(cdtime_t timeout,
                    const char *identifier __attribute__((unused)),
                    user_data_t *user_data) {
  if (user_data == NULL)
    return -EINVAL;

  ot_callback_t *cb = (ot_callback_t *)user_data->data;

  pthread_mutex_lock(&cb->mu);
  int status = ot_flush_nolock(timeout, cb);
  pthread_mutex_unlock(&cb->mu);

  return status;
}

static bool ot_metric_is_cached(ot_callback_t *cb, metric_t const *m) {
  strbuf_t id = STRBUF_CREATE;
  metric_identity(&id, m);

  int status = c_avl_get(cb->cached_metrics, id.ptr, NULL);
  STRBUF_DESTROY(id);
  return status == 0;
}

static bool ot_need_flush(ot_callback_t *cb, metric_family_t const *fam) {
  int status = c_avl_get(cb->cached_metric_families, fam->name, NULL);
  if (status != 0) {
    return false;
  }

  /* if any of the metrics are already cached, we should flush before adding
   * this metric family. */
  for (size_t i = 0; i < fam->metric.num; i++) {
    bool ok = ot_metric_is_cached(cb, fam->metric.ptr + i);
    if (ok) {
      return true;
    }
  }

  return false;
}

static int ot_mark_metric_cached(ot_callback_t *cb, metric_t const *m) {
  strbuf_t buf = STRBUF_CREATE;
  int status = metric_identity(&buf, m);
  if (status != 0) {
    ERROR("write_open_telemetry plugin: metric_identity failed: %d", status);
    STRBUF_DESTROY(buf);
    return status;
  }

  char *id = strdup(buf.ptr);
  if (id == NULL) {
    STRBUF_DESTROY(buf);
    return errno;
  }

  status = c_avl_insert(cb->cached_metrics, id, /* value = */ NULL);
  if (status != 0) {
    ERROR("write_open_telemetry plugin: c_avl_insert(\"%s\") failed: %d",
          buf.ptr, status);
    STRBUF_DESTROY(buf);
    return status;
  }

  STRBUF_DESTROY(buf);
  return 0;
}

static metric_family_t *ot_cached_metric_family(ot_callback_t *cb,
                                                metric_family_t const *fam) {
  metric_family_t *ret = NULL;
  int status = c_avl_get(cb->cached_metric_families, fam->name, (void **)&ret);
  if (status == 0) {
    return ret;
  }

  ret = metric_family_clone_shallow(fam);
  c_avl_insert(cb->cached_metric_families, ret->name, ret);
  return ret;
}

static int ot_write(metric_family_t const *fam, user_data_t *user_data) {
  if ((fam == NULL) || (user_data == NULL)) {
    return EINVAL;
  }

  ot_callback_t *cb = (ot_callback_t *)user_data->data;
  pthread_mutex_lock(&cb->mu);

  if (ot_need_flush(cb, fam)) {
    cdtime_t timeout = 0;
    ot_flush_nolock(timeout, cb);
  }

  metric_family_t *cache = ot_cached_metric_family(cb, fam);
  size_t offset = cache->metric.num;

  int status = metric_family_append_list(cache, fam->metric);
  if (status != 0) {
    ERROR("write_open_telemetry plugin: metric_list_append_list failed: %d",
          status);
    return status;
  }

  for (size_t i = offset; i < cache->metric.num; i++) {
    ot_mark_metric_cached(cb, &cache->metric.ptr[i]);
  }

  return 0;
}

static int ot_config_node(oconfig_item_t *ci) {
  ot_callback_t *cb = (ot_callback_t *)calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("write_open_telemetry plugin: calloc failed.");
    return -1;
  }

  cb->reference_count = 1;
  cf_util_get_string(ci, &cb->name);
  cb->host = strdup(OT_DEFAULT_HOST);
  cb->port = strdup(OT_DEFAULT_PORT);
  cb->path = strdup(OT_DEFAULT_PATH);

  cb->cached_metrics =
      c_avl_create((int (*)(const void *, const void *))strcmp);
  cb->cached_metric_families =
      c_avl_create((int (*)(const void *, const void *))strcmp);

  pthread_mutex_init(&cb->mu, /* attr = */ NULL);

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    int status = 0;
    if (strcasecmp("Host", child->key) == 0)
      status = cf_util_get_string(child, &cb->host);
    else if (strcasecmp("Port", child->key) == 0)
      status = cf_util_get_service(child, &cb->port);
    else if (strcasecmp("Protocol", child->key) == 0)
      status = cf_util_get_string(child, &cb->path);
    else {
      ERROR("write_open_telemetry plugin: Invalid configuration "
            "option: %s.",
            child->key);
      status = -1;
    }

    if (status != 0) {
      ot_callback_decref(cb);
      return status;
    }
  }

  char callback_name[DATA_MAX_NAME_LEN];
  ssnprintf(callback_name, sizeof(callback_name), "write_open_telemetry/%s",
            cb->name);

  user_data_t user_data = {
      .data = cb,
      .free_func = ot_callback_decref,
  };

  cb->reference_count++;
  plugin_register_write(callback_name, ot_write, &user_data);

  cb->reference_count++;
  plugin_register_flush(callback_name, ot_flush, &user_data);

  ot_callback_decref(cb);
  return 0;
}

static int ot_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Node", child->key) == 0)
      ot_config_node(child);
    else {
      ERROR("write_open_telemetry plugin: Invalid configuration "
            "option: %s.",
            child->key);
    }
  }

  return 0;
}

void module_register(void) {
  plugin_register_complex_config("write_open_telemetry", ot_config);
}
