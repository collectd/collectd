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

#include "utils/format_stackdriver/format_stackdriver.h"

#include "plugin.h"
#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils_cache.h"
#include "utils_time.h"

#include <math.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_parse.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif

#ifndef GCM_PREFIX
#define GCM_PREFIX "custom.googleapis.com/collectd/"
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
static int format_typed_value(yajl_gen gen, metric_t const *m,
                              int64_t start_value) {
  char integer[32];

  yajl_gen_map_open(gen);

  switch (m->family->type) {
  case METRIC_TYPE_GAUGE:
  case METRIC_TYPE_UNTYPED: {
    int status = json_string(gen, "doubleValue");
    if (status != 0)
      return status;

    status = (int)yajl_gen_double(gen, (double)m->value.gauge);
    if (status != yajl_gen_status_ok)
      return status;

    yajl_gen_map_close(gen);
    return 0;
  }
  case METRIC_TYPE_COUNTER: {
    /* Counter resets are handled in format_time_series(). */
    assert(m->value.counter >= (uint64_t)start_value);
    uint64_t diff = m->value.counter - (uint64_t)start_value;
    ssnprintf(integer, sizeof(integer), "%" PRIu64, diff);
    break;
  }
  default: {
    ERROR("format_typed_value: unknown value type %d.", m->family->type);
    return EINVAL;
  }
  }

  int status = json_string(gen, "int64Value") || json_string(gen, integer);
  if (status != 0) {
    return status;
  }

  yajl_gen_map_close(gen);
  return 0;
} /* }}} int format_typed_value */

/* MetricKind
 *
 * enum(
 *   "CUMULATIVE",
 *   "GAUGE"
 * )
 */
static int format_metric_kind(yajl_gen gen, metric_t const *m) {
  switch (m->family->type) {
  case METRIC_TYPE_GAUGE:
  case METRIC_TYPE_UNTYPED:
    return json_string(gen, "GAUGE");
  case METRIC_TYPE_COUNTER:
    return json_string(gen, "CUMULATIVE");
  default:
    ERROR("format_metric_kind: unknown value type %d.", m->family->type);
    return EINVAL;
  }
}

/* ValueType
 *
 * enum(
 *   "DOUBLE",
 *   "INT64"
 * )
 */
static int format_value_type(yajl_gen gen, metric_t const *m) {
  switch (m->family->type) {
  case METRIC_TYPE_GAUGE:
  case METRIC_TYPE_UNTYPED:
    return json_string(gen, "DOUBLE");
  case METRIC_TYPE_COUNTER:
    return json_string(gen, "INT64");
  default:
    ERROR("format_value_type: unknown value type %d.", m->family->type);
    return EINVAL;
  }
}

static int metric_type(strbuf_t *buf, metric_t const *m) {
  char const *name = m->family->name;
  size_t name_len = strlen(name);

  int status = strbuf_print(buf, GCM_PREFIX);

  char const *valid_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "abcdefghijklmnopqrstuvwxyz"
                            "0123456789_/";
  while (name_len != 0) {
    size_t valid_len = strspn(name, valid_chars);
    if (valid_len != 0) {
      status = status || strbuf_printn(buf, name, valid_len);
      name += valid_len;
      name_len -= valid_len;
      continue;
    }

    status = status || strbuf_print(buf, "_");
    name++;
    name_len--;
  }

  return status;
} /* }}} int metric_type */

/* The metric type, including its DNS name prefix. The type is not URL-encoded.
 * All user-defined custom metric types have the DNS name custom.googleapis.com.
 * Metric types should use a natural hierarchical grouping. */
static int format_metric_type(yajl_gen gen, metric_t const *m) {
  /* {{{ */
  strbuf_t buf = STRBUF_CREATE;

  int status = metric_type(&buf, m) || json_string(gen, buf.ptr);

  STRBUF_DESTROY(buf);
  return status;
} /* }}} int format_metric_type */

/* TimeInterval
 *
 * {
 *   "endTime": string,
 *   "startTime": string,
 * }
 */
static int format_time_interval(yajl_gen gen, metric_t const *m,
                                cdtime_t start_time) {
  /* {{{ */
  yajl_gen_map_open(gen);

  int status = json_string(gen, "endTime") || json_time(gen, m->time);
  if (status != 0)
    return status;

  if (m->family->type == METRIC_TYPE_COUNTER) {
    int status = json_string(gen, "startTime") || json_time(gen, start_time);
    if (status != 0)
      return status;
  }

  yajl_gen_map_close(gen);
  return 0;
} /* }}} int format_time_interval */

/* read_cumulative_state reads the start time and start value of cumulative
 * (i.e. METRIC_TYPE_COUNTER) metrics from the cache. If a metric is seen for
 * the first time, or reset (the current value is smaller than the cached
 * value), the start time is (re)set to vl->time. */
static int read_cumulative_state(metric_t const *m, cdtime_t *ret_start_time,
                                 int64_t *ret_start_value) {
  /* TODO(octo): Add back DERIVE here. */
  if (m->family->type != METRIC_TYPE_COUNTER) {
    return 0;
  }

  char const *start_value_key = "stackdriver:start_value";
  char const *start_time_key = "stackdriver:start_time";

  int status =
      uc_meta_data_get_signed_int(m, start_value_key, ret_start_value) ||
      uc_meta_data_get_unsigned_int(m, start_time_key, ret_start_time);
  bool is_reset = *ret_start_value > m->value.counter;
  if ((status == 0) && !is_reset) {
    return 0;
  }

  *ret_start_value = (int64_t)m->value.counter;
  *ret_start_time = m->time;

  return uc_meta_data_add_signed_int(m, start_value_key, *ret_start_value) ||
         uc_meta_data_add_unsigned_int(m, start_time_key, *ret_start_time);
} /* int read_cumulative_state */

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
static int format_point(yajl_gen gen, metric_t const *m, cdtime_t start_time,
                        int64_t start_value) {
  /* {{{ */
  yajl_gen_map_open(gen);

  int status = json_string(gen, "interval") ||
               format_time_interval(gen, m, start_time) ||
               json_string(gen, "value") ||
               format_typed_value(gen, m, start_value);
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
static int format_metric(yajl_gen gen, metric_t const *m) {
  /* {{{ */
  yajl_gen_map_open(gen);

  int status = json_string(gen, "type") || format_metric_type(gen, m);
  if (status != 0) {
    return status;
  }

  if (m->label.num != 0) {
    status = json_string(gen, "labels");
    yajl_gen_map_open(gen);

    for (size_t i = 0; i < m->label.num; i++) {
      label_pair_t *l = m->label.ptr + i;
      status =
          status || json_string(gen, l->name) || json_string(gen, l->value);
    }

    yajl_gen_map_close(gen);
    if (status != 0) {
      return status;
    }
  }

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
/* format_time_series formats a TimeSeries object. Returns EAGAIN when a
 * cumulative metric is seen for the first time and cannot be sent to
 * Stackdriver due to lack of state. */
static int format_time_series(yajl_gen gen, metric_t const *m,
                              sd_resource_t *res) {
  metric_type_t type = m->family->type;

  cdtime_t start_time = 0;
  int64_t start_value = 0;
  int status = read_cumulative_state(m, &start_time, &start_value);
  if (status != 0) {
    return status;
  }
  if (start_time == m->time) {
    /* for cumulative metrics, the interval must not be zero. */
    return EAGAIN;
  }
  if ((type == METRIC_TYPE_GAUGE) || (type == METRIC_TYPE_UNTYPED)) {
    double d = (double)m->value.gauge;
    if (isnan(d) || isinf(d)) {
      return EAGAIN;
    }
  }
  if (type == METRIC_TYPE_COUNTER) {
    uint64_t sv = (uint64_t)start_value;
    if (m->value.counter < sv) {
      return EAGAIN;
    }
  }

  yajl_gen_map_open(gen);

  status = json_string(gen, "metric") || format_metric(gen, m) ||
           json_string(gen, "resource") || format_gcm_resource(gen, res) ||
           json_string(gen, "metricKind") || format_metric_kind(gen, m) ||
           json_string(gen, "valueType") || format_value_type(gen, m) ||
           json_string(gen, "points");
  if (status != 0)
    return status;

  yajl_gen_array_open(gen);

  status = format_point(gen, m, start_time, start_value);
  if (status != 0)
    return status;

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

int sd_output_add(sd_output_t *out, metric_t const *m) {
  if ((out == NULL) || (m == NULL) || (m->family == NULL)) {
    return EINVAL;
  }

  strbuf_t mtype = STRBUF_CREATE;
  int status = metric_type(&mtype, m);
  if (status != 0) {
    STRBUF_DESTROY(mtype);
    return status;
  }

  /* first, check that the metric descriptor exists. */
  status = c_avl_get(out->metric_descriptors, mtype.ptr, NULL);
  if (status != 0) {
    STRBUF_DESTROY(mtype);
    return ENOENT;
  }
  STRBUF_DESTROY(mtype);

  strbuf_t id = STRBUF_CREATE;
  status = metric_identity(&id, m);
  if (status != 0) {
    ERROR("sd_output_add: metric_identity failed: %s", STRERROR(status));
    return status;
  }

  if (c_avl_get(out->staged, id.ptr, NULL) == 0) {
    STRBUF_DESTROY(id);
    return EEXIST;
  }

  /* EAGAIN -> first instance of a cumulative metric or NaN value */
  status = format_time_series(out->gen, m, out->res);
  if ((status != 0) && (status != EAGAIN)) {
    ERROR("sd_output_add: format_time_series failed with status %d.", status);
    STRBUF_DESTROY(id);
    return status;
  }

  if (status == 0) {
    c_avl_insert(out->staged, strdup(id.ptr), NULL);
  }
  STRBUF_DESTROY(id);

  size_t json_buffer_size = 0;
  yajl_gen_get_buf(out->gen, &(unsigned char const *){NULL}, &json_buffer_size);
  if (json_buffer_size > 65535)
    return ENOBUFS;

  return 0;
} /* }}} int sd_output_add */

int sd_output_register_metric(sd_output_t *out, metric_t const *m) {
  /* {{{ */
  strbuf_t buf = STRBUF_CREATE;
  int status = metric_type(&buf, m);
  if (status != 0) {
    STRBUF_DESTROY(buf);
    return status;
  }

  char *key = strdup(buf.ptr);
  if (key == NULL) {
    STRBUF_DESTROY(buf);
    return ENOMEM;
  }
  STRBUF_DESTROY(buf);

  status = c_avl_insert(out->metric_descriptors, key, NULL);
  if (status != 0) {
    sfree(key);
  }

  return status;
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
int sd_format_metric_descriptor(strbuf_t *buf, metric_t const *m) {
  /* {{{ */
  yajl_gen gen = yajl_gen_alloc(/* funcs = */ NULL);
  if (gen == NULL) {
    return ENOMEM;
  }

  yajl_gen_map_open(gen);

  int status = json_string(gen, "type") || format_metric_type(gen, m) ||
               json_string(gen, "metricKind") || format_metric_kind(gen, m) ||
               json_string(gen, "valueType") || format_value_type(gen, m) ||
               json_string(gen, "labels");
  if (status != 0) {
    yajl_gen_free(gen);
    return status;
  }

  yajl_gen_array_open(gen);

  for (size_t i = 0; i < m->label.num; i++) {
    label_pair_t *l = m->label.ptr + i;
    int status = format_label_descriptor(gen, l->name);
    if (status != 0) {
      yajl_gen_free(gen);
      return status;
    }
  }

  yajl_gen_array_close(gen);
  yajl_gen_map_close(gen);

  unsigned char const *tmp = NULL;
  yajl_gen_get_buf(gen, &tmp, &(size_t){0});

  status = strbuf_print(buf, (void const *)tmp);

  yajl_gen_free(gen);
  return status;
} /* }}} int sd_format_metric_descriptor */
