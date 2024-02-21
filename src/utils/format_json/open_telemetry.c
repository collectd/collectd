/**
 * collectd - src/utils_format_json.c
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

#include "collectd.h"

#include "utils/format_json/format_json.h"

#include <yajl/yajl_gen.h>

static void log_yajl_gen_error(int line, const char *fname, int status) {
  if (strncmp(fname, "yajl_gen_", strlen("yajl_gen_")) != 0) {
    return;
  }
  ERROR("open_telemetry.c:%d: %s failed with status %d", line, fname, status);
}

#define CHECK(f)                                                               \
  do {                                                                         \
    int status = (f);                                                          \
    if (status != 0) {                                                         \
      log_yajl_gen_error(__LINE__, #f, status);                                \
      return status;                                                           \
    }                                                                          \
  } while (0)

static int json_add_string(yajl_gen g, char const *str) /* {{{ */
{
  if (str == NULL) {
    CHECK(yajl_gen_null(g));
    return 0;
  }

  int status = yajl_gen_string(g, (unsigned char const *)str, strlen(str));
  if (status != yajl_gen_status_ok) {
    ERROR("format_json: yajl_gen_string(\"%s\") failed with status %d", str,
          status);
    return status;
  }
  return 0;
} /* }}} int json_add_string */

static int key_value(yajl_gen g, label_pair_t label) {
  CHECK(yajl_gen_map_open(g)); /* BEGIN KeyValue */

  CHECK(json_add_string(g, "key"));
  CHECK(json_add_string(g, label.name));

  CHECK(json_add_string(g, "value"));
  CHECK(yajl_gen_map_open(g)); /* BEGIN AnyValue */
  CHECK(json_add_string(g, "stringValue"));
  CHECK(json_add_string(g, label.value));
  CHECK(yajl_gen_map_close(g)); /* END AnyValue */

  CHECK(yajl_gen_map_close(g)); /* END KeyValue */
  return 0;
}

static int number_data_point(yajl_gen g, metric_t const *m) {
  CHECK(yajl_gen_map_open(g)); /* BEGIN NumberDataPoint */

  CHECK(json_add_string(g, "attributes"));
  CHECK(yajl_gen_array_open(g));
  for (size_t i = 0; i < m->label.num; i++) {
    CHECK(key_value(g, m->label.ptr[i]));
  }
  CHECK(yajl_gen_array_close(g));

  CHECK(json_add_string(g, "timeUnixNano"));
  CHECK(yajl_gen_integer(g, CDTIME_T_TO_NS(m->time)));

  switch (m->family->type) {
  case METRIC_TYPE_GAUGE:
    CHECK(json_add_string(g, "asDouble"));
    CHECK(yajl_gen_double(g, m->value.gauge));
    break;
  case METRIC_TYPE_COUNTER:
    CHECK(json_add_string(g, "asInt"));
    CHECK(yajl_gen_integer(g, (long long int)m->value.counter));
    break;
  case METRIC_TYPE_FPCOUNTER:
    CHECK(json_add_string(g, "asDouble"));
    CHECK(yajl_gen_double(g, m->value.fpcounter));
    break;
  case METRIC_TYPE_UP_DOWN:
    CHECK(json_add_string(g, "asInt"));
    CHECK(yajl_gen_integer(g, (long long int)m->value.up_down));
    break;
  case METRIC_TYPE_UP_DOWN_COUNTER_FP:
    CHECK(json_add_string(g, "asDouble"));
    CHECK(yajl_gen_integer(g, m->value.up_down_counter_fp));
    break;
  case METRIC_TYPE_UNTYPED:
    ERROR("format_json_open_telemetry: Unexpected metric type: %d",
          m->family->type);
    return EINVAL;
  }

  CHECK(yajl_gen_map_close(g)); /* END NumberDataPoint */
  return 0;
}

static int gauge(yajl_gen g, metric_family_t const *fam) {
  CHECK(yajl_gen_map_open(g)); /* BEGIN Gauge */

  CHECK(json_add_string(g, "dataPoints"));
  CHECK(yajl_gen_array_open(g));
  for (size_t i = 0; i < fam->metric.num; i++) {
    CHECK(number_data_point(g, fam->metric.ptr + i));
  }
  CHECK(yajl_gen_array_close(g));

  CHECK(yajl_gen_map_close(g)); /* END Gauge */
  return 0;
}

static int sum(yajl_gen g, metric_family_t const *fam) {
  CHECK(yajl_gen_map_open(g)); /* BEGIN Sum */

  CHECK(json_add_string(g, "dataPoints"));
  CHECK(yajl_gen_array_open(g));
  for (size_t i = 0; i < fam->metric.num; i++) {
    CHECK(number_data_point(g, fam->metric.ptr + i));
  }
  CHECK(yajl_gen_array_close(g));

  char const *aggregation_temporality_cumulative = "2";
  CHECK(json_add_string(g, "aggregationTemporality"));
  CHECK(json_add_string(g, aggregation_temporality_cumulative));

  CHECK(json_add_string(g, "isMonotonic"));
  CHECK(yajl_gen_bool(g, true));

  CHECK(yajl_gen_map_close(g)); /* END Sum */
  return 0;
}

static int metric(yajl_gen g, metric_family_t const *fam) {
  CHECK(yajl_gen_map_open(g)); /* BEGIN Metric */

  CHECK(json_add_string(g, "name"));
  CHECK(json_add_string(g, fam->name));

  if (fam->unit != NULL) {
    CHECK(json_add_string(g, "unit"));
    CHECK(json_add_string(g, fam->unit));
  }

  if (fam->help != NULL) {
    CHECK(json_add_string(g, "description"));
    CHECK(json_add_string(g, fam->help));
  }

  switch (fam->type) {
  case METRIC_TYPE_GAUGE:
    CHECK(json_add_string(g, "gauge"));
    CHECK(gauge(g, fam));
    break;
  case METRIC_TYPE_COUNTER:
  case METRIC_TYPE_FPCOUNTER:
  case METRIC_TYPE_UP_DOWN:
  case METRIC_TYPE_UP_DOWN_COUNTER_FP:
    CHECK(json_add_string(g, "sum"));
    CHECK(sum(g, fam));
    break;
  case METRIC_TYPE_UNTYPED:
    ERROR("format_json_open_telemetry: Unexpected metric type: %d", fam->type);
    return EINVAL;
  }

  CHECK(yajl_gen_map_close(g)); /* END Metric */
  return 0;
}

static int instrumentation_scope(yajl_gen g) {
  CHECK(yajl_gen_map_open(g)); /* BEGIN InstrumentationScope */

  CHECK(json_add_string(g, "name"));
  CHECK(json_add_string(g, PACKAGE_NAME));

  CHECK(json_add_string(g, "version"));
  CHECK(json_add_string(g, PACKAGE_VERSION));

  CHECK(yajl_gen_map_close(g)); /* END InstrumentationScope */
  return 0;
}

static int scope_metrics(yajl_gen g, resource_metrics_t const *rm) {
  CHECK(yajl_gen_map_open(g)); /* BEGIN ScopeMetrics */

  CHECK(json_add_string(g, "scope"));
  CHECK(instrumentation_scope(g));

  CHECK(json_add_string(g, "metrics"));
  CHECK(yajl_gen_array_open(g));
  for (size_t i = 0; i < rm->families_num; i++) {
    CHECK(metric(g, rm->families[i]));
  }
  CHECK(yajl_gen_array_close(g));

  CHECK(yajl_gen_map_close(g)); /* END ScopeMetrics */
  return 0;
}

static int resource(yajl_gen g, label_set_t res) {
  CHECK(yajl_gen_map_open(g)); /* BEGIN Resource */

  CHECK(json_add_string(g, "attributes"));
  CHECK(yajl_gen_array_open(g));
  for (size_t i = 0; i < res.num; i++) {
    CHECK(key_value(g, res.ptr[i]));
  }
  CHECK(yajl_gen_array_close(g));

  CHECK(yajl_gen_map_close(g)); /* END Resource */
  return 0;
}

static int add_resource_metric(yajl_gen g, resource_metrics_t const *rm) {
  CHECK(yajl_gen_map_open(g)); /* BEGIN ResourceMetrics */

  if (rm->resource.num > 0) {
    CHECK(json_add_string(g, "resource"));
    CHECK(resource(g, rm->resource));
  }

  CHECK(json_add_string(g, "scopeMetrics"));
  CHECK(yajl_gen_array_open(g));
  CHECK(scope_metrics(g, rm));
  CHECK(yajl_gen_array_close(g));

  CHECK(yajl_gen_map_close(g)); /* END ResourceMetrics */
  return 0;
}

int format_json_open_telemetry(strbuf_t *buf,
                               resource_metrics_set_t const *set) {
  if (buf->pos != 0) {
    ERROR("format_json_open_telemetry: buffer is not empty.");
    return EINVAL;
  }

  yajl_gen g = yajl_gen_alloc(NULL);
  if (g == NULL) {
    ERROR("format_json_open_telemetry: yajl_gen_alloc() failed.");
    return ENOMEM;
  }
#if COLLECT_DEBUG
  yajl_gen_config(g, yajl_gen_validate_utf8, 1);
#endif

  CHECK(yajl_gen_map_open(g)); /* BEGIN ExportMetricsServiceRequest */
  CHECK(json_add_string(g, "resourceMetrics"));
  CHECK(yajl_gen_array_open(g));

  unsigned char const *out = NULL;
  for (size_t i = 0; i < set->num; i++) {
    resource_metrics_t const *rm = set->ptr + i;
    add_resource_metric(g, rm);
  }

  CHECK(yajl_gen_array_close(g));
  CHECK(yajl_gen_map_close(g)); /* END ExportMetricsServiceRequest */

  size_t out_len = 0;

  if (yajl_gen_get_buf(g, &out, &out_len) != yajl_gen_status_ok) {
    yajl_gen_clear(g);
    yajl_gen_free(g);
    return -1;
  }

  if (buf->fixed) {
    size_t avail = (buf->size == 0) ? 0 : buf->size - (buf->pos + 1);
    if (avail < out_len) {
      yajl_gen_clear(g);
      yajl_gen_free(g);
      return ENOBUFS;
    }
  }

  int status = strbuf_print(buf, (void *)out);
  yajl_gen_clear(g);
  yajl_gen_free(g);
  return status;
}
