/**
 * collectd - src/utils_format_stackdriver.c
 * ISC license
 *
 * Copyright (C) 2017  Florian Forster
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "utils_format_stackdriver.h"

#include "common.h"
#include "plugin.h"
#include "utils_avltree.h"
#include "utils_cache.h"
#include "utils_time.h"

#include <yajl/yajl_gen.h>
#include <yajl/yajl_parse.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif

struct sd_output_s {
  sd_resource_t *res;
  yajl_gen gen;
  c_avl_tree_t *staged;
  c_avl_tree_t *metric_descriptors;
};

struct sd_label_s {
  char *key;
  char *value;
};
typedef struct sd_label_s sd_label_t;

struct sd_resource_s {
  char *type;

  sd_label_t *labels;
  size_t labels_num;
};

static int json_string(yajl_gen gen, char const *s) /* {{{ */
{
  yajl_gen_status status =
      yajl_gen_string(gen, (unsigned char const *)s, strlen(s));
  if (status != yajl_gen_status_ok)
    return (int)status;

  return 0;
} /* }}} int json_string */

static int json_time(yajl_gen gen, cdtime_t t) {
  char buffer[64];

  size_t status = rfc3339(buffer, sizeof(buffer), t);
  if (status != 0) {
    return status;
  }

  return json_string(gen, buffer);
} /* }}} int json_time */

/* MonitoredResource
 *
 * {
 *   "type": "library.googleapis.com/book",
 *   "labels": {
 *     "/genre": "fiction",
 *     "/media": "paper"
 *     "/title": "The Old Man and the Sea"
 *   }
 * }
 */
static int format_gcm_resource(yajl_gen gen, sd_resource_t *res) /* {{{ */
{
  yajl_gen_map_open(gen);

  int status = json_string(gen, "type") || json_string(gen, res->type);
  if (status != 0)
    return status;

  if (res->labels_num != 0) {
    status = json_string(gen, "labels");
    if (status != 0)
      return status;

    yajl_gen_map_open(gen);
    for (size_t i = 0; i < res->labels_num; i++) {
      status = json_string(gen, res->labels[i].key) ||
               json_string(gen, res->labels[i].value);
      if (status != 0)
        return status;
    }
    yajl_gen_map_close(gen);
  }

  yajl_gen_map_close(gen);
  return 0;
} /* }}} int format_gcm_resource */

/* TypedValue
 *
 * {
 *   // Union field, only one of the following:
 *   "int64Value": string,
 *   "doubleValue": number,
 * }
 */
static int format_gcm_typed_value(yajl_gen gen, int ds_type,
                                  value_t v) /* {{{ */
{
  char integer[21];

  yajl_gen_map_open(gen);
  if (ds_type == DS_TYPE_GAUGE) {
    int status = json_string(gen, "doubleValue");
    if (status != 0)
      return status;

    status = (int)yajl_gen_double(gen, (double)v.gauge);
    if (status != yajl_gen_status_ok)
      return status;
  } else {
    switch (ds_type) {
    case DS_TYPE_COUNTER:
      snprintf(integer, sizeof(integer), "%llu", v.counter);
      break;
    case DS_TYPE_DERIVE:
      snprintf(integer, sizeof(integer), "%" PRIi64, v.derive);
      break;
    case DS_TYPE_ABSOLUTE:
      snprintf(integer, sizeof(integer), "%" PRIu64, v.derive);
      break;
    default:
      ERROR("format_gcm_typed_value: unknown value type %d.", ds_type);
      return EINVAL;
    }

    int status = json_string(gen, "int64Value") || json_string(gen, integer);
    if (status != 0) {
      return status;
    }
  }
  yajl_gen_map_close(gen);

  return 0;
} /* }}} int format_gcm_typed_value */

/* MetricKind
 *
 * enum(
 *   "CUMULATIVE",
 *   "GAUGE"
 * )
*/
static int format_metric_kind(yajl_gen gen, int ds_type) {
  return json_string(gen, (ds_type == DS_TYPE_GAUGE) ? "GAUGE" : "CUMULATIVE");
}

/* ValueType
 *
 * enum(
 *   "DOUBLE",
 *   "INT64"
 * )
*/
static int format_value_type(yajl_gen gen, int ds_type) {
  return json_string(gen, (ds_type == DS_TYPE_GAUGE) ? "DOUBLE" : "INT64");
}

static int metric_type(char *buffer, size_t buffer_size, data_set_t const *ds,
                       value_list_t const *vl, int ds_index) {
  /* {{{ */
  char const *ds_name = ds->ds[ds_index].name;

#define GCM_PREFIX "custom.googleapis.com/collectd/"
  if ((ds_index != 0) || strcmp("value", ds_name) != 0) {
    snprintf(buffer, buffer_size, GCM_PREFIX "%s/%s_%s", vl->plugin, vl->type,
             ds_name);
  } else {
    snprintf(buffer, buffer_size, GCM_PREFIX "%s/%s", vl->plugin, vl->type);
  }

  char const *whitelist = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                          "abcdefghijklmnopqrstuvwxyz"
                          "0123456789_/";
  char *ptr = buffer + strlen(GCM_PREFIX);
  size_t ok_len;
  while ((ok_len = strspn(ptr, whitelist)) != strlen(ptr)) {
    ptr[ok_len] = '_';
    ptr += ok_len;
  }

  return 0;
} /* }}} int metric_type */

/* The metric type, including its DNS name prefix. The type is not URL-encoded.
 * All user-defined custom metric types have the DNS name custom.googleapis.com.
 * Metric types should use a natural hierarchical grouping. */
static int format_metric_type(yajl_gen gen, data_set_t const *ds,
                              value_list_t const *vl, int ds_index) {
  /* {{{ */
  char buffer[4 * DATA_MAX_NAME_LEN];
  metric_type(buffer, sizeof(buffer), ds, vl, ds_index);

  return json_string(gen, buffer);
} /* }}} int format_metric_type */

/* TimeInterval
 *
 * {
 *   "endTime": string,
 *   "startTime": string,
 * }
 */
static int format_time_interval(yajl_gen gen, int ds_type,
                                value_list_t const *vl) {
  /* {{{ */
  yajl_gen_map_open(gen);

  int status = json_string(gen, "endTime") || json_time(gen, vl->time);
  if (status != 0)
    return status;

  if (ds_type != DS_TYPE_GAUGE) {
    cdtime_t start_time = 0;
    if (uc_meta_data_get_unsigned_int(vl, "gcm:start_time", &start_time) != 0) {
      start_time = vl->time;
      uc_meta_data_add_unsigned_int(vl, "gcm:start_time", start_time);
    }

    int status = json_string(gen, "startTime") || json_time(gen, start_time);
    if (status != 0)
      return status;
  }

  yajl_gen_map_close(gen);
  return 0;
} /* }}} int format_time_interval */

/* Point
 *
 * {
 *   "interval": {
 *     object(TimeInterval)
 *   },
 *   "value": {
 *     object(TypedValue)
 *   },
 * }
 */
static int format_point(yajl_gen gen, data_set_t const *ds,
                        value_list_t const *vl, int ds_index) {
  /* {{{ */
  yajl_gen_map_open(gen);

  int ds_type = ds->ds[ds_index].type;

  int status = json_string(gen, "interval") ||
               format_time_interval(gen, ds_type, vl) ||
               json_string(gen, "value") ||
               format_gcm_typed_value(gen, ds_type, vl->values[ds_index]);
  if (status != 0)
    return status;

  yajl_gen_map_close(gen);
  return 0;
} /* }}} int format_point */

/* Metric
 *
 * {
 *   "type": string,
 *   "labels": {
 *     string: string,
 *     ...
 *   },
 * }
 */
static int format_metric(yajl_gen gen, data_set_t const *ds,
                         value_list_t const *vl, int ds_index) {
  /* {{{ */
  yajl_gen_map_open(gen);

  int status = json_string(gen, "type") ||
               format_metric_type(gen, ds, vl, ds_index) ||
               json_string(gen, "labels");
  if (status != 0) {
    return status;
  }

  yajl_gen_map_open(gen);
  status = json_string(gen, "host") || json_string(gen, vl->host) ||
           json_string(gen, "plugin_instance") ||
           json_string(gen, vl->plugin_instance) ||
           json_string(gen, "type_instance") ||
           json_string(gen, vl->type_instance);
  if (status != 0) {
    return status;
  }
  yajl_gen_map_close(gen);

  yajl_gen_map_close(gen);
  return 0;
} /* }}} int format_metric */

/* TimeSeries
 *
 * {
 *   "metric": {
 *     object(Metric)
 *   },
 *   "resource": {
 *     object(MonitoredResource)
 *   },
 *   "metricKind": enum(MetricKind),
 *   "valueType": enum(ValueType),
 *   "points": [
 *     {
 *       object(Point)
 *     }
 *   ],
 * }
 */
static int format_time_series(yajl_gen gen, data_set_t const *ds,
                              value_list_t const *vl, int ds_index,
                              sd_resource_t *res) {
  /* {{{ */
  yajl_gen_map_open(gen);

  int ds_type = ds->ds[ds_index].type;

  int status =
      json_string(gen, "metric") || format_metric(gen, ds, vl, ds_index) ||
      json_string(gen, "resource") || format_gcm_resource(gen, res) ||
      json_string(gen, "metricKind") || format_metric_kind(gen, ds_type) ||
      json_string(gen, "valueType") || format_value_type(gen, ds_type) ||
      json_string(gen, "points");
  if (status != 0)
    return status;

  yajl_gen_array_open(gen);
  if ((status = format_point(gen, ds, vl, ds_index)) != 0) {
    return status;
  }
  yajl_gen_array_close(gen);

  yajl_gen_map_close(gen);
  return 0;
} /* }}} int format_time_series */

/* Request body
 *
 * {
 *   "timeSeries": [
 *     {
 *       object(TimeSeries)
 *     }
 *   ],
 * }
 */
static int sd_output_initialize(sd_output_t *out) /* {{{ */
{
  yajl_gen_map_open(out->gen);

  int status = json_string(out->gen, "timeSeries");
  if (status != 0) {
    return status;
  }

  yajl_gen_array_open(out->gen);
  return 0;
} /* }}} int sd_output_initialize */

static int sd_output_finalize(sd_output_t *out) /* {{{ */
{
  yajl_gen_array_close(out->gen);
  yajl_gen_map_close(out->gen);

  return 0;
} /* }}} int sd_output_finalize */

static void sd_output_reset_staged(sd_output_t *out) /* {{{ */
{
  void *key = NULL;

  while (c_avl_pick(out->staged, &key, &(void *){NULL}) == 0)
    sfree(key);
} /* }}} void sd_output_reset_staged */

sd_output_t *sd_output_create(sd_resource_t *res) /* {{{ */
{
  sd_output_t *out = calloc(1, sizeof(*out));
  if (out == NULL)
    return NULL;

  out->res = res;

  out->gen = yajl_gen_alloc(/* funcs = */ NULL);
  if (out->gen == NULL) {
    sd_output_destroy(out);
    return NULL;
  }

  out->staged = c_avl_create((void *)strcmp);
  if (out->staged == NULL) {
    sd_output_destroy(out);
    return NULL;
  }

  out->metric_descriptors = c_avl_create((void *)strcmp);
  if (out->metric_descriptors == NULL) {
    sd_output_destroy(out);
    return NULL;
  }

  sd_output_initialize(out);

  return out;
} /* }}} sd_output_t *sd_output_create */

void sd_output_destroy(sd_output_t *out) /* {{{ */
{
  if (out == NULL)
    return;

  if (out->metric_descriptors != NULL) {
    void *key = NULL;
    while (c_avl_pick(out->metric_descriptors, &key, &(void *){NULL}) == 0) {
      sfree(key);
    }
    c_avl_destroy(out->metric_descriptors);
    out->metric_descriptors = NULL;
  }

  if (out->staged != NULL) {
    sd_output_reset_staged(out);
    c_avl_destroy(out->staged);
    out->staged = NULL;
  }

  if (out->gen != NULL) {
    yajl_gen_free(out->gen);
    out->gen = NULL;
  }

  if (out->res != NULL) {
    sd_resource_destroy(out->res);
    out->res = NULL;
  }

  sfree(out);
} /* }}} void sd_output_destroy */

int sd_output_add(sd_output_t *out, data_set_t const *ds,
                  value_list_t const *vl) /* {{{ */
{
  /* first, check that we have all appropriate metric descriptors. */
  for (size_t i = 0; i < ds->ds_num; i++) {
    char buffer[4 * DATA_MAX_NAME_LEN];
    metric_type(buffer, sizeof(buffer), ds, vl, i);

    if (c_avl_get(out->metric_descriptors, buffer, NULL) != 0) {
      return ENOENT;
    }
  }

  char key[6 * DATA_MAX_NAME_LEN];
  int status = FORMAT_VL(key, sizeof(key), vl);
  if (status != 0) {
    ERROR("sd_output_add: FORMAT_VL failed with status %d.", status);
    return status;
  }

  if (c_avl_get(out->staged, key, NULL) == 0) {
    return EEXIST;
  }

  for (size_t i = 0; i < ds->ds_num; i++) {
    int status = format_time_series(out->gen, ds, vl, i, out->res);
    if (status != 0) {
      ERROR("sd_output_add: format_time_series failed with status %d.", status);
      return status;
    }
  }

  c_avl_insert(out->staged, strdup(key), NULL);

  size_t json_buffer_size = 0;
  yajl_gen_get_buf(out->gen, &(unsigned char const *){NULL}, &json_buffer_size);
  if (json_buffer_size > 65535)
    return ENOBUFS;

  return 0;
} /* }}} int sd_output_add */

int sd_output_register_metric(sd_output_t *out, data_set_t const *ds,
                              value_list_t const *vl) {
  /* {{{ */
  for (size_t i = 0; i < ds->ds_num; i++) {
    char buffer[4 * DATA_MAX_NAME_LEN];
    metric_type(buffer, sizeof(buffer), ds, vl, i);

    char *key = strdup(buffer);
    int status = c_avl_insert(out->metric_descriptors, key, NULL);
    if (status != 0) {
      sfree(key);
      return status;
    }
  }

  return 0;
} /* }}} int sd_output_register_metric */

char *sd_output_reset(sd_output_t *out) /* {{{ */
{
  sd_output_finalize(out);

  unsigned char const *json_buffer = NULL;
  yajl_gen_get_buf(out->gen, &json_buffer, &(size_t){0});
  char *ret = strdup((void const *)json_buffer);

  sd_output_reset_staged(out);

  yajl_gen_free(out->gen);
  out->gen = yajl_gen_alloc(/* funcs = */ NULL);

  sd_output_initialize(out);

  return ret;
} /* }}} char *sd_output_reset */

sd_resource_t *sd_resource_create(char const *type) /* {{{ */
{
  sd_resource_t *res = malloc(sizeof(*res));
  if (res == NULL)
    return NULL;
  memset(res, 0, sizeof(*res));

  res->type = strdup(type);
  if (res->type == NULL) {
    sfree(res);
    return NULL;
  }

  res->labels = NULL;
  res->labels_num = 0;

  return res;
} /* }}} sd_resource_t *sd_resource_create */

void sd_resource_destroy(sd_resource_t *res) /* {{{ */
{
  if (res == NULL)
    return;

  for (size_t i = 0; i < res->labels_num; i++) {
    sfree(res->labels[i].key);
    sfree(res->labels[i].value);
  }
  sfree(res->labels);
  sfree(res->type);
  sfree(res);
} /* }}} void sd_resource_destroy */

int sd_resource_add_label(sd_resource_t *res, char const *key,
                          char const *value) /* {{{ */
{
  if ((res == NULL) || (key == NULL) || (value == NULL))
    return EINVAL;

  sd_label_t *l =
      realloc(res->labels, sizeof(*res->labels) * (res->labels_num + 1));
  if (l == NULL)
    return ENOMEM;

  res->labels = l;
  l = res->labels + res->labels_num;

  l->key = strdup(key);
  l->value = strdup(value);
  if ((l->key == NULL) || (l->value == NULL)) {
    sfree(l->key);
    sfree(l->value);
    return ENOMEM;
  }

  res->labels_num++;
  return 0;
} /* }}} int sd_resource_add_label */

/* LabelDescriptor
 *
 * {
 *   "key": string,
 *   "valueType": enum(ValueType),
 *   "description": string,
 * }
 */
static int format_label_descriptor(yajl_gen gen, char const *key) {
  /* {{{ */
  yajl_gen_map_open(gen);

  int status = json_string(gen, "key") || json_string(gen, key) ||
               json_string(gen, "valueType") || json_string(gen, "STRING");
  if (status != 0) {
    return status;
  }

  yajl_gen_map_close(gen);
  return 0;
} /* }}} int format_label_descriptor */

/* MetricDescriptor
 *
 * {
 *   "name": string,
 *   "type": string,
 *   "labels": [
 *     {
 *       object(LabelDescriptor)
 *     }
 *   ],
 *   "metricKind": enum(MetricKind),
 *   "valueType": enum(ValueType),
 *   "unit": string,
 *   "description": string,
 *   "displayName": string,
 * }
 */
int sd_format_metric_descriptor(char *buffer, size_t buffer_size,
                                data_set_t const *ds, value_list_t const *vl,
                                int ds_index) {
  /* {{{ */
  yajl_gen gen = yajl_gen_alloc(/* funcs = */ NULL);
  if (gen == NULL) {
    return ENOMEM;
  }

  int ds_type = ds->ds[ds_index].type;

  yajl_gen_map_open(gen);

  int status =
      json_string(gen, "type") || format_metric_type(gen, ds, vl, ds_index) ||
      json_string(gen, "metricKind") || format_metric_kind(gen, ds_type) ||
      json_string(gen, "valueType") || format_value_type(gen, ds_type) ||
      json_string(gen, "labels");
  if (status != 0) {
    yajl_gen_free(gen);
    return status;
  }

  char const *labels[] = {"host", "plugin_instance", "type_instance"};
  yajl_gen_array_open(gen);

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(labels); i++) {
    int status = format_label_descriptor(gen, labels[i]);
    if (status != 0) {
      yajl_gen_free(gen);
      return status;
    }
  }

  yajl_gen_array_close(gen);
  yajl_gen_map_close(gen);

  unsigned char const *tmp = NULL;
  yajl_gen_get_buf(gen, &tmp, &(size_t){0});
  sstrncpy(buffer, (void const *)tmp, buffer_size);

  yajl_gen_free(gen);
  return 0;
} /* }}} int sd_format_metric_descriptor */
