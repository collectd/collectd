/**
 * collectd - src/utils_format_json.c
 * Copyright (C) 2009-2015  Florian octo Forster
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
 *   Manoj Srivastava <srivasta at google.com>
 **/

#include "collectd.h"

#include "utils/format_json/format_json.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils_cache.h"

#include <yajl/yajl_common.h>
#include <yajl/yajl_gen.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif
#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
#define HAVE_YAJL_V2 1
#endif

static int json_add_string(yajl_gen g, char const *str) /* {{{ */
{
  if (str == NULL)
    return (int)yajl_gen_null(g);

  return (int)yajl_gen_string(g, (const unsigned char *)str,
                              (unsigned int)strlen(str));
} /* }}} int json_add_string */

#define JSON_ADD(g, str)                                                       \
  do {                                                                         \
    yajl_gen_status status = json_add_string(g, str);                          \
    if (status != yajl_gen_status_ok) {                                        \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define JSON_ADDF(g, format, ...)                                              \
  do {                                                                         \
    char *str = ssnprintf_alloc(format, __VA_ARGS__);                          \
    yajl_gen_status status = json_add_string(g, str);                          \
    free(str);                                                                 \
    if (status != yajl_gen_status_ok) {                                        \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define CHECK_SUCCESS(cmd)                                                     \
  do {                                                                         \
    yajl_gen_status s = (cmd);                                                 \
    if (s != yajl_gen_status_ok) {                                             \
      return (int)s;                                                           \
    }                                                                          \
  } while (0)

static int format_json_meta(yajl_gen g, notification_meta_t *meta) /* {{{ */
{
  if (meta == NULL)
    return 0;

  JSON_ADD(g, meta->name);
  switch (meta->type) {
  case NM_TYPE_STRING:
    JSON_ADD(g, meta->nm_value.nm_string);
    break;
  case NM_TYPE_SIGNED_INT:
    JSON_ADDF(g, "%" PRIi64, meta->nm_value.nm_signed_int);
    break;
  case NM_TYPE_UNSIGNED_INT:
    JSON_ADDF(g, "%" PRIu64, meta->nm_value.nm_unsigned_int);
    break;
  case NM_TYPE_DOUBLE:
    JSON_ADDF(g, JSON_GAUGE_FORMAT, meta->nm_value.nm_double);
    break;
  case NM_TYPE_BOOLEAN:
    JSON_ADD(g, meta->nm_value.nm_boolean ? "true" : "false");
    break;
  default:
    ERROR("format_json_meta: unknown meta data type %d (name \"%s\")",
          meta->type, meta->name);
    CHECK_SUCCESS(yajl_gen_null(g));
  }

  return format_json_meta(g, meta->next);
} /* }}} int format_json_meta */

static int format_time(yajl_gen g, cdtime_t t) /* {{{ */
{
  char buffer[RFC3339NANO_SIZE] = "";

  if (rfc3339nano(buffer, sizeof(buffer), t) != 0)
    return -1;

  JSON_ADD(g, buffer);
  return 0;
} /* }}} int format_time */

/* TODO(octo): format_metric should export the interval, too. */
/* TODO(octo): Decide whether format_metric should export meta data. */
static int format_metric(yajl_gen g, metric_t const *m) {
  CHECK_SUCCESS(yajl_gen_map_open(g)); /* BEGIN metric */

  if (m->label.num != 0) {
    JSON_ADD(g, "labels");
    CHECK_SUCCESS(yajl_gen_map_open(g)); /* BEGIN labels */

    for (size_t i = 0; i < m->label.num; i++) {
      label_pair_t *l = m->label.ptr + i;
      JSON_ADD(g, l->name);
      JSON_ADD(g, l->value);
    }

    CHECK_SUCCESS(yajl_gen_map_close(g)); /* END labels */
  }

  if (m->time != 0) {
    JSON_ADD(g, "timestamp_ms");
    JSON_ADDF(g, "%" PRIu64, CDTIME_T_TO_MS(m->time));
  }

  strbuf_t buf = STRBUF_CREATE;
  int status = value_marshal_text(&buf, m->value, m->family->type);
  if (status != 0) {
    STRBUF_DESTROY(buf);
    return status;
  }
  JSON_ADD(g, "value");
  JSON_ADD(g, buf.ptr);
  STRBUF_DESTROY(buf);

  CHECK_SUCCESS(yajl_gen_map_close(g)); /* END metric */

  return 0;
}

/* json_metric_family that all metrics in ml have the same name and value_type.
 *
 * Example:
     [
       {
         "name": "roshi_select_call_count",
         "help": "How many select calls have been made.",
         "type": "COUNTER",
         "metrics": [
           {
             "value": "1063110"
           }
         ]
       }
     ]
 */
static int json_metric_family(yajl_gen g, metric_family_t const *fam) {
  CHECK_SUCCESS(yajl_gen_map_open(g)); /* BEGIN metric family */

  JSON_ADD(g, "name");
  JSON_ADD(g, fam->name);

  char const *type = NULL;
  switch (fam->type) {
  /* TODO(octo): handle store_rates. */
  case METRIC_TYPE_GAUGE:
    type = "GAUGE";
    break;
  case METRIC_TYPE_COUNTER:
    type = "COUNTER";
    break;
  case METRIC_TYPE_UNTYPED:
    type = "UNTYPED";
    break;
  default:
    ERROR("format_json_metric: Unknown value type: %d", fam->type);
    return EINVAL;
  }
  JSON_ADD(g, "type");
  JSON_ADD(g, type);

  JSON_ADD(g, "metrics");
  CHECK_SUCCESS(yajl_gen_array_open(g));
  for (size_t i = 0; i < fam->metric.num; i++) {
    metric_t *m = fam->metric.ptr + i;
    int status = format_metric(g, m);
    if (status != 0) {
      return status;
    }
  }
  CHECK_SUCCESS(yajl_gen_array_close(g));

  CHECK_SUCCESS(yajl_gen_map_close(g)); /* END metric family */

  return 0;
}

int format_json_metric_family(strbuf_t *buf, metric_family_t const *fam,
                              bool store_rates) {
  if ((buf == NULL) || (fam == NULL))
    return EINVAL;

#if HAVE_YAJL_V2
  yajl_gen g = yajl_gen_alloc(NULL);
  if (g == NULL)
    return -1;
#if COLLECT_DEBUG
  yajl_gen_config(g, yajl_gen_validate_utf8, 1);
#endif

#else /* !HAVE_YAJL_V2 */
  yajl_gen_config conf = {0};
  yajl_gen g = yajl_gen_alloc(&conf, NULL);
  if (g == NULL)
    return -1;
#endif

  yajl_gen_array_open(g);

  int status = json_metric_family(g, fam);
  if (status != 0) {
    yajl_gen_clear(g);
    yajl_gen_free(g);
    return status;
  }

  yajl_gen_array_close(g);

  /* copy to output buffer */
  unsigned char const *out = NULL;
#if HAVE_YAJL_V2
  size_t out_len = 0;
#else
  unsigned int out_len = 0;
#endif
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

  /* If the buffer is not empty, append by converting the closing ']' of "buf"
   * to a comma and skip the opening '[' of "out". */
  if (buf->pos != 0) {
    assert(buf->ptr[buf->pos - 1] == ']');
    buf->ptr[buf->pos - 1] = ',';

    assert(out[0] == '[');
    out++;
  }

  status = strbuf_print(buf, (void *)out);

  yajl_gen_clear(g);
  yajl_gen_free(g);
  return status;
} /* }}} format_json_metric_family */

static int format_alert(yajl_gen g, notification_t const *n) /* {{{ */
{
  CHECK_SUCCESS(yajl_gen_array_open(g)); /* BEGIN array */
  CHECK_SUCCESS(yajl_gen_map_open(g));   /* BEGIN alert */

  /*
   * labels
   */
  JSON_ADD(g, "labels");
  CHECK_SUCCESS(yajl_gen_map_open(g)); /* BEGIN labels */

  JSON_ADD(g, "alertname");
  if (strncmp(n->plugin, n->type, strlen(n->plugin)) == 0)
    JSON_ADDF(g, "collectd_%s", n->type);
  else
    JSON_ADDF(g, "collectd_%s_%s", n->plugin, n->type);

  JSON_ADD(g, "instance");
  JSON_ADD(g, n->host);

  /* mangling of plugin instance and type instance into labels is copied from
   * the Prometheus collectd exporter. */
  if (strlen(n->plugin_instance) > 0) {
    JSON_ADD(g, n->plugin);
    JSON_ADD(g, n->plugin_instance);
  }
  if (strlen(n->type_instance) > 0) {
    if (strlen(n->plugin_instance) > 0)
      JSON_ADD(g, "type");
    else
      JSON_ADD(g, n->plugin);
    JSON_ADD(g, n->type_instance);
  }

  JSON_ADD(g, "severity");
  JSON_ADD(g, (n->severity == NOTIF_FAILURE)
                  ? "FAILURE"
                  : (n->severity == NOTIF_WARNING)
                        ? "WARNING"
                        : (n->severity == NOTIF_OKAY) ? "OKAY" : "UNKNOWN");

  JSON_ADD(g, "service");
  JSON_ADD(g, "collectd");

  CHECK_SUCCESS(yajl_gen_map_close(g)); /* END labels */

  /*
   * annotations
   */
  JSON_ADD(g, "annotations");
  CHECK_SUCCESS(yajl_gen_map_open(g)); /* BEGIN annotations */

  JSON_ADD(g, "summary");
  JSON_ADD(g, n->message);

  if (format_json_meta(g, n->meta) != 0) {
    return -1;
  }

  CHECK_SUCCESS(yajl_gen_map_close(g)); /* END annotations */

  JSON_ADD(g, "startsAt");
  if (format_time(g, n->time) != 0) {
    return -1;
  }

  CHECK_SUCCESS(yajl_gen_map_close(g));   /* END alert */
  CHECK_SUCCESS(yajl_gen_array_close(g)); /* END array */

  return 0;
} /* }}} format_alert */

/*
 * Format (prometheus/alertmanager v1):
 *
 * [{
 *   "labels": {
 *     "alertname": "collectd_cpu",
 *     "instance":  "host.example.com",
 *     "severity":  "FAILURE",
 *     "service":   "collectd",
 *     "cpu":       "0",
 *     "type":      "wait"
 *   },
 *   "annotations": {
 *     "summary": "...",
 *     // meta
 *   },
 *   "startsAt": <rfc3339 time>,
 *   "endsAt": <rfc3339 time>, // not used
 * }]
 */
int format_json_notification(char *buffer, size_t buffer_size, /* {{{ */
                             notification_t const *n) {
  yajl_gen g;
  unsigned char const *out;
#if HAVE_YAJL_V2
  size_t unused_out_len;
#else
  unsigned int unused_out_len;
#endif

  if ((buffer == NULL) || (n == NULL))
    return EINVAL;

#if HAVE_YAJL_V2
  g = yajl_gen_alloc(NULL);
  if (g == NULL)
    return -1;
#if COLLECT_DEBUG
  yajl_gen_config(g, yajl_gen_beautify, 1);
  yajl_gen_config(g, yajl_gen_validate_utf8, 1);
#endif

#else /* !HAVE_YAJL_V2 */
  yajl_gen_config conf = {0};
#if COLLECT_DEBUG
  conf.beautify = 1;
  conf.indentString = "  ";
#endif
  g = yajl_gen_alloc(&conf, NULL);
  if (g == NULL)
    return -1;
#endif

  if (format_alert(g, n) != 0) {
    yajl_gen_clear(g);
    yajl_gen_free(g);
    return -1;
  }

  /* copy to output buffer */
  if (yajl_gen_get_buf(g, &out, &unused_out_len) != yajl_gen_status_ok) {
    yajl_gen_clear(g);
    yajl_gen_free(g);
    return -1;
  }
  sstrncpy(buffer, (void *)out, buffer_size);

  yajl_gen_clear(g);
  yajl_gen_free(g);
  return 0;
} /* }}} format_json_notification */
