/**
 * collectd - src/write_prometheus.c
 * Copyright (C) 2016       Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_avltree.h"
#include "utils_complain.h"
#include "utils_time.h"

#include "prometheus.pb-c.h"

#include <microhttpd.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifndef PROMETHEUS_DEFAULT_STALENESS_DELTA
#define PROMETHEUS_DEFAULT_STALENESS_DELTA TIME_T_TO_CDTIME_T_STATIC(300)
#endif

#define VARINT_UINT32_BYTES 5

#define CONTENT_TYPE_PROTO                                                     \
  "application/vnd.google.protobuf; proto=io.prometheus.client.MetricFamily; " \
  "encoding=delimited"
#define CONTENT_TYPE_TEXT "text/plain; version=0.0.4"

static c_avl_tree_t *metrics;
static pthread_mutex_t metrics_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned short httpd_port = 9103;
static struct MHD_Daemon *httpd;

static cdtime_t staleness_delta = PROMETHEUS_DEFAULT_STALENESS_DELTA;

/* Unfortunately, protoc-c doesn't export it's implementation of varint, so we
 * need to implement our own. */
static size_t varint(uint8_t buffer[static VARINT_UINT32_BYTES],
                     uint32_t value) {
  for (size_t i = 0; i < VARINT_UINT32_BYTES; i++) {
    buffer[i] = (uint8_t)(value & 0x7f);
    value >>= 7;

    if (value == 0)
      return i + 1;

    buffer[i] |= 0x80;
  }

  return 0;
}

/* format_protobuf iterates over all metric families in "metrics" and adds them
 * to a buffer in ProtoBuf format. It prefixes each protobuf with its encoded
 * size, the so called "delimited" format. */
static void format_protobuf(ProtobufCBuffer *buffer) {
  pthread_mutex_lock(&metrics_lock);

  char *unused_name;
  Io__Prometheus__Client__MetricFamily *fam;
  c_avl_iterator_t *iter = c_avl_get_iterator(metrics);
  while (c_avl_iterator_next(iter, (void *)&unused_name, (void *)&fam) == 0) {
    /* Prometheus uses a message length prefix to determine where one
     * MetricFamily ends and the next begins. This delimiter is encoded as a
     * "varint", which is common in Protobufs. */
    uint8_t delim[VARINT_UINT32_BYTES] = {0};
    size_t delim_len = varint(
        delim,
        (uint32_t)io__prometheus__client__metric_family__get_packed_size(fam));
    buffer->append(buffer, delim_len, delim);

    io__prometheus__client__metric_family__pack_to_buffer(fam, buffer);
  }
  c_avl_iterator_destroy(iter);

  pthread_mutex_unlock(&metrics_lock);
}

static char const *escape_label_value(char *buffer, size_t buffer_size,
                                      char const *value) {
  /* shortcut for values that don't need escaping. */
  if (strpbrk(value, "\n\"\\") == NULL)
    return value;

  size_t value_len = strlen(value);
  size_t buffer_len = 0;

  for (size_t i = 0; i < value_len; i++) {
    switch (value[i]) {
    case '\n':
    case '"':
    case '\\':
      if ((buffer_size - buffer_len) < 3) {
        break;
      }
      buffer[buffer_len] = '\\';
      buffer[buffer_len + 1] = (value[i] == '\n') ? 'n' : value[i];
      buffer_len += 2;
      break;

    default:
      if ((buffer_size - buffer_len) < 2) {
        break;
      }
      buffer[buffer_len] = value[i];
      buffer_len++;
      break;
    }
  }

  assert(buffer_len < buffer_size);
  buffer[buffer_len] = 0;
  return buffer;
}

/* format_labels formats a metric's labels in Prometheus-compatible format. This
 * format looks like this:
 *
 *   key0="value0",key1="value1"
 */
static char *format_labels(char *buffer, size_t buffer_size,
                           Io__Prometheus__Client__Metric const *m) {
  /* our metrics always have at least one and at most three labels. */
  assert(m->n_label >= 1);
  assert(m->n_label <= 3);

#define LABEL_KEY_SIZE DATA_MAX_NAME_LEN
#define LABEL_VALUE_SIZE (2 * DATA_MAX_NAME_LEN - 1)
#define LABEL_BUFFER_SIZE (LABEL_KEY_SIZE + LABEL_VALUE_SIZE + 4)

  char *labels[3] = {
      (char[LABEL_BUFFER_SIZE]){0}, (char[LABEL_BUFFER_SIZE]){0},
      (char[LABEL_BUFFER_SIZE]){0},
  };

  /* N.B.: the label *names* are hard-coded by this plugin and therefore we
   * know that they are sane. */
  for (size_t i = 0; i < m->n_label; i++) {
    char value[LABEL_VALUE_SIZE];
    snprintf(labels[i], LABEL_BUFFER_SIZE, "%s=\"%s\"", m->label[i]->name,
             escape_label_value(value, sizeof(value), m->label[i]->value));
  }

  strjoin(buffer, buffer_size, labels, m->n_label, ",");
  return buffer;
}

/* format_protobuf iterates over all metric families in "metrics" and adds them
 * to a buffer in plain text format. */
static void format_text(ProtobufCBuffer *buffer) {
  pthread_mutex_lock(&metrics_lock);

  char *unused_name;
  Io__Prometheus__Client__MetricFamily *fam;
  c_avl_iterator_t *iter = c_avl_get_iterator(metrics);
  while (c_avl_iterator_next(iter, (void *)&unused_name, (void *)&fam) == 0) {
    char line[1024]; /* 4x DATA_MAX_NAME_LEN? */

    snprintf(line, sizeof(line), "# HELP %s %s\n", fam->name, fam->help);
    buffer->append(buffer, strlen(line), (uint8_t *)line);

    snprintf(line, sizeof(line), "# TYPE %s %s\n", fam->name,
             (fam->type == IO__PROMETHEUS__CLIENT__METRIC_TYPE__GAUGE)
                 ? "gauge"
                 : "counter");
    buffer->append(buffer, strlen(line), (uint8_t *)line);

    for (size_t i = 0; i < fam->n_metric; i++) {
      Io__Prometheus__Client__Metric *m = fam->metric[i];

      char labels[1024];

      char timestamp_ms[24] = "";
      if (m->has_timestamp_ms)
        snprintf(timestamp_ms, sizeof(timestamp_ms), " %" PRIi64,
                 m->timestamp_ms);

      if (fam->type == IO__PROMETHEUS__CLIENT__METRIC_TYPE__GAUGE)
        snprintf(line, sizeof(line), "%s{%s} " GAUGE_FORMAT "%s\n", fam->name,
                 format_labels(labels, sizeof(labels), m), m->gauge->value,
                 timestamp_ms);
      else /* if (fam->type == IO__PROMETHEUS__CLIENT__METRIC_TYPE__COUNTER) */
        snprintf(line, sizeof(line), "%s{%s} %.0f%s\n", fam->name,
                 format_labels(labels, sizeof(labels), m), m->counter->value,
                 timestamp_ms);

      buffer->append(buffer, strlen(line), (uint8_t *)line);
    }
  }
  c_avl_iterator_destroy(iter);

  char server[1024];
  snprintf(server, sizeof(server), "\n# collectd/write_prometheus %s at %s\n",
           PACKAGE_VERSION, hostname_g);
  buffer->append(buffer, strlen(server), (uint8_t *)server);

  pthread_mutex_unlock(&metrics_lock);
}

/* http_handler is the callback called by the microhttpd library. It essentially
 * handles all HTTP request aspects and creates an HTTP response. */
static int http_handler(void *cls, struct MHD_Connection *connection,
                        const char *url, const char *method,
                        const char *version, const char *upload_data,
                        size_t *upload_data_size, void **connection_state) {
  if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) {
    return MHD_NO;
  }

  /* On the first call for each connection, return without anything further.
   * Apparently not everything has been initialized yet or so; the docs are not
   * very specific on the issue. */
  if (*connection_state == NULL) {
    /* set to a random non-NULL pointer. */
    *connection_state = &(int){42};
    return MHD_YES;
  }

  char const *accept = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
                                                   MHD_HTTP_HEADER_ACCEPT);
  _Bool want_proto =
      (accept != NULL) &&
      (strstr(accept, "application/vnd.google.protobuf") != NULL);

  uint8_t scratch[4096] = {0};
  ProtobufCBufferSimple simple = PROTOBUF_C_BUFFER_SIMPLE_INIT(scratch);
  ProtobufCBuffer *buffer = (ProtobufCBuffer *)&simple;

  if (want_proto)
    format_protobuf(buffer);
  else
    format_text(buffer);

#if defined(MHD_VERSION) && MHD_VERSION >= 0x00090500
  struct MHD_Response *res = MHD_create_response_from_buffer(
      simple.len, simple.data, MHD_RESPMEM_MUST_COPY);
#else
  struct MHD_Response *res = MHD_create_response_from_data(
      simple.len, simple.data, /* must_free = */ 0, /* must_copy = */ 1);
#endif
  MHD_add_response_header(res, MHD_HTTP_HEADER_CONTENT_TYPE,
                          want_proto ? CONTENT_TYPE_PROTO : CONTENT_TYPE_TEXT);

  int status = MHD_queue_response(connection, MHD_HTTP_OK, res);

  MHD_destroy_response(res);
  PROTOBUF_C_BUFFER_SIMPLE_CLEAR(&simple);
  return status;
}

/*
 * Functions for manipulating the global state in "metrics". This is organized
 * in two tiers: the global "metrics" tree holds "metric families", which are
 * identified by a name (a string). Each metric family has one or more
 * "metrics", which are identified by a unique set of key-value-pairs. For
 * example:
 *
 * collectd_cpu_total
 *   {cpu="0",type="idle"}
 *   {cpu="0",type="user"}
 *   ...
 * collectd_memory
 *   {memory="used"}
 *   {memory="free"}
 *   ...
 * {{{ */
/* label_pair_destroy frees the memory used by a label pair. */
static void label_pair_destroy(Io__Prometheus__Client__LabelPair *msg) {
  if (msg == NULL)
    return;

  sfree(msg->name);
  sfree(msg->value);

  sfree(msg);
}

/* label_pair_clone allocates and initializes a new label pair. */
static Io__Prometheus__Client__LabelPair *
label_pair_clone(Io__Prometheus__Client__LabelPair const *orig) {
  Io__Prometheus__Client__LabelPair *copy = calloc(1, sizeof(*copy));
  if (copy == NULL)
    return NULL;
  io__prometheus__client__label_pair__init(copy);

  copy->name = strdup(orig->name);
  copy->value = strdup(orig->value);
  if ((copy->name == NULL) || (copy->value == NULL)) {
    label_pair_destroy(copy);
    return NULL;
  }

  return copy;
}

/* metric_destroy frees the memory used by a metric. */
static void metric_destroy(Io__Prometheus__Client__Metric *msg) {
  if (msg == NULL)
    return;

  for (size_t i = 0; i < msg->n_label; i++) {
    label_pair_destroy(msg->label[i]);
  }
  sfree(msg->label);

  sfree(msg->gauge);
  sfree(msg->counter);

  sfree(msg);
}

/* metric_cmp compares two metrics. It's prototype makes it easy to use with
 * qsort(3) and bsearch(3). */
static int metric_cmp(void const *a, void const *b) {
  Io__Prometheus__Client__Metric const *m_a =
      *((Io__Prometheus__Client__Metric **)a);
  Io__Prometheus__Client__Metric const *m_b =
      *((Io__Prometheus__Client__Metric **)b);

  if (m_a->n_label < m_b->n_label)
    return -1;
  else if (m_a->n_label > m_b->n_label)
    return 1;

  /* Prometheus does not care about the order of labels. All labels in this
   * plugin are created by METRIC_ADD_LABELS(), though, and therefore always
   * appear in the same order. We take advantage of this and simplify the check
   * by making sure all labels are the same in each position.
   *
   * We also only need to check the label values, because the label names are
   * the same for all metrics in a metric family.
   *
   * 3 labels:
   * [0] $plugin="$plugin_instance" => $plugin is the same within a family
   * [1] type="$type_instance"      => "type" is a static string
   * [2] instance="$host"           => "instance" is a static string
   *
   * 2 labels, variant 1:
   * [0] $plugin="$plugin_instance" => $plugin is the same within a family
   * [1] instance="$host"           => "instance" is a static string
   *
   * 2 labels, variant 2:
   * [0] $plugin="$type_instance"   => $plugin is the same within a family
   * [1] instance="$host"           => "instance" is a static string
   *
   * 1 label:
   * [1] instance="$host"           => "instance" is a static string
   */
  for (size_t i = 0; i < m_a->n_label; i++) {
    int status = strcmp(m_a->label[i]->value, m_b->label[i]->value);
    if (status != 0)
      return status;

#if COLLECT_DEBUG
    assert(strcmp(m_a->label[i]->name, m_b->label[i]->name) == 0);
#endif
  }

  return 0;
}

#define METRIC_INIT                                                            \
  &(Io__Prometheus__Client__Metric) {                                          \
    .label =                                                                   \
        (Io__Prometheus__Client__LabelPair *[]){                               \
            &(Io__Prometheus__Client__LabelPair){                              \
                .name = NULL,                                                  \
            },                                                                 \
            &(Io__Prometheus__Client__LabelPair){                              \
                .name = NULL,                                                  \
            },                                                                 \
            &(Io__Prometheus__Client__LabelPair){                              \
                .name = NULL,                                                  \
            },                                                                 \
        },                                                                     \
    .n_label = 0,                                                              \
  }

#define METRIC_ADD_LABELS(m, vl)                                               \
  do {                                                                         \
    if (strlen((vl)->plugin_instance) != 0) {                                  \
      (m)->label[(m)->n_label]->name = (char *)(vl)->plugin;                   \
      (m)->label[(m)->n_label]->value = (char *)(vl)->plugin_instance;         \
      (m)->n_label++;                                                          \
    }                                                                          \
                                                                               \
    if (strlen((vl)->type_instance) != 0) {                                    \
      (m)->label[(m)->n_label]->name = "type";                                 \
      if (strlen((vl)->plugin_instance) == 0)                                  \
        (m)->label[(m)->n_label]->name = (char *)(vl)->plugin;                 \
      (m)->label[(m)->n_label]->value = (char *)(vl)->type_instance;           \
      (m)->n_label++;                                                          \
    }                                                                          \
                                                                               \
    (m)->label[(m)->n_label]->name = "instance";                               \
    (m)->label[(m)->n_label]->value = (char *)(vl)->host;                      \
    (m)->n_label++;                                                            \
  } while (0)

/* metric_clone allocates and initializes a new metric based on orig. */
static Io__Prometheus__Client__Metric *
metric_clone(Io__Prometheus__Client__Metric const *orig) {
  Io__Prometheus__Client__Metric *copy = calloc(1, sizeof(*copy));
  if (copy == NULL)
    return NULL;
  io__prometheus__client__metric__init(copy);

  copy->n_label = orig->n_label;
  copy->label = calloc(copy->n_label, sizeof(*copy->label));
  if (copy->label == NULL) {
    sfree(copy);
    return NULL;
  }

  for (size_t i = 0; i < copy->n_label; i++) {
    copy->label[i] = label_pair_clone(orig->label[i]);
    if (copy->label[i] == NULL) {
      metric_destroy(copy);
      return NULL;
    }
  }

  return copy;
}

/* metric_update stores the new value and timestamp in m. */
static int metric_update(Io__Prometheus__Client__Metric *m, value_t value,
                         int ds_type, cdtime_t t, cdtime_t interval) {
  if (ds_type == DS_TYPE_GAUGE) {
    sfree(m->counter);
    if (m->gauge == NULL) {
      m->gauge = calloc(1, sizeof(*m->gauge));
      if (m->gauge == NULL)
        return ENOMEM;
      io__prometheus__client__gauge__init(m->gauge);
    }

    m->gauge->value = (double)value.gauge;
    m->gauge->has_value = 1;
  } else { /* not gauge */
    sfree(m->gauge);
    if (m->counter == NULL) {
      m->counter = calloc(1, sizeof(*m->counter));
      if (m->counter == NULL)
        return ENOMEM;
      io__prometheus__client__counter__init(m->counter);
    }

    switch (ds_type) {
    case DS_TYPE_ABSOLUTE:
      m->counter->value = (double)value.absolute;
      break;
    case DS_TYPE_COUNTER:
      m->counter->value = (double)value.counter;
      break;
    default:
      m->counter->value = (double)value.derive;
      break;
    }
    m->counter->has_value = 1;
  }

  /* Prometheus has a globally configured timeout after which metrics are
   * considered stale. This causes problems when metrics have an interval
   * exceeding that limit. We emulate the behavior of "pushgateway" and *not*
   * send a timestamp value – Prometheus will fill in the current time. */
  if (interval <= staleness_delta) {
    m->timestamp_ms = CDTIME_T_TO_MS(t);
    m->has_timestamp_ms = 1;
  } else {
    static c_complain_t long_metric = C_COMPLAIN_INIT_STATIC;
    c_complain(
        LOG_NOTICE, &long_metric,
        "write_prometheus plugin: You have metrics with an interval exceeding "
        "\"StalenessDelta\" setting (%.3fs). This is suboptimal, please check "
        "the collectd.conf(5) manual page to understand what's going on.",
        CDTIME_T_TO_DOUBLE(staleness_delta));

    m->timestamp_ms = 0;
    m->has_timestamp_ms = 0;
  }

  return 0;
}

/* metric_family_add_metric adds m to the metric list of fam. */
static int metric_family_add_metric(Io__Prometheus__Client__MetricFamily *fam,
                                    Io__Prometheus__Client__Metric *m) {
  Io__Prometheus__Client__Metric **tmp =
      realloc(fam->metric, (fam->n_metric + 1) * sizeof(*fam->metric));
  if (tmp == NULL)
    return ENOMEM;
  fam->metric = tmp;

  fam->metric[fam->n_metric] = m;
  fam->n_metric++;

  /* Sort the metrics so that lookup is fast. */
  qsort(fam->metric, fam->n_metric, sizeof(*fam->metric), metric_cmp);

  return 0;
}

/* metric_family_delete_metric looks up and deletes the metric corresponding to
 * vl. */
static int
metric_family_delete_metric(Io__Prometheus__Client__MetricFamily *fam,
                            value_list_t const *vl) {
  Io__Prometheus__Client__Metric *key = METRIC_INIT;
  METRIC_ADD_LABELS(key, vl);

  size_t i;
  for (i = 0; i < fam->n_metric; i++) {
    if (metric_cmp(&key, &fam->metric[i]) == 0)
      break;
  }

  if (i >= fam->n_metric)
    return ENOENT;

  metric_destroy(fam->metric[i]);
  if ((fam->n_metric - 1) > i)
    memmove(&fam->metric[i], &fam->metric[i + 1],
            ((fam->n_metric - 1) - i) * sizeof(fam->metric[i]));
  fam->n_metric--;

  if (fam->n_metric == 0) {
    sfree(fam->metric);
    return 0;
  }

  Io__Prometheus__Client__Metric **tmp =
      realloc(fam->metric, fam->n_metric * sizeof(*fam->metric));
  if (tmp != NULL)
    fam->metric = tmp;

  return 0;
}

/* metric_family_get_metric looks up the matching metric in a metric family,
 * allocating it if necessary. */
static Io__Prometheus__Client__Metric *
metric_family_get_metric(Io__Prometheus__Client__MetricFamily *fam,
                         value_list_t const *vl) {
  Io__Prometheus__Client__Metric *key = METRIC_INIT;
  METRIC_ADD_LABELS(key, vl);

  /* Metrics are sorted in metric_family_add_metric() so that we can do a binary
   * search here. */
  Io__Prometheus__Client__Metric **m = bsearch(
      &key, fam->metric, fam->n_metric, sizeof(*fam->metric), metric_cmp);

  if (m != NULL) {
    return *m;
  }

  Io__Prometheus__Client__Metric *new_metric = metric_clone(key);
  if (new_metric == NULL)
    return NULL;

  DEBUG("write_prometheus plugin: created new metric in family");
  int status = metric_family_add_metric(fam, new_metric);
  if (status != 0) {
    metric_destroy(new_metric);
    return NULL;
  }

  return new_metric;
}

/* metric_family_update looks up the matching metric in a metric family,
 * allocating it if necessary, and updates the metric to the latest value. */
static int metric_family_update(Io__Prometheus__Client__MetricFamily *fam,
                                data_set_t const *ds, value_list_t const *vl,
                                size_t ds_index) {
  Io__Prometheus__Client__Metric *m = metric_family_get_metric(fam, vl);
  if (m == NULL)
    return -1;

  return metric_update(m, vl->values[ds_index], ds->ds[ds_index].type, vl->time,
                       vl->interval);
}

/* metric_family_destroy frees the memory used by a metric family. */
static void metric_family_destroy(Io__Prometheus__Client__MetricFamily *msg) {
  if (msg == NULL)
    return;

  sfree(msg->name);
  sfree(msg->help);

  for (size_t i = 0; i < msg->n_metric; i++) {
    metric_destroy(msg->metric[i]);
  }
  sfree(msg->metric);

  sfree(msg);
}

/* metric_family_create allocates and initializes a new metric family. */
static Io__Prometheus__Client__MetricFamily *
metric_family_create(char *name, data_set_t const *ds, value_list_t const *vl,
                     size_t ds_index) {
  Io__Prometheus__Client__MetricFamily *msg = calloc(1, sizeof(*msg));
  if (msg == NULL)
    return NULL;
  io__prometheus__client__metric_family__init(msg);

  msg->name = name;

  char help[1024];
  snprintf(
      help, sizeof(help),
      "write_prometheus plugin: '%s' Type: '%s', Dstype: '%s', Dsname: '%s'",
      vl->plugin, vl->type, DS_TYPE_TO_STRING(ds->ds[ds_index].type),
      ds->ds[ds_index].name);
  msg->help = strdup(help);

  msg->type = (ds->ds[ds_index].type == DS_TYPE_GAUGE)
                  ? IO__PROMETHEUS__CLIENT__METRIC_TYPE__GAUGE
                  : IO__PROMETHEUS__CLIENT__METRIC_TYPE__COUNTER;
  msg->has_type = 1;

  return msg;
}

/* metric_family_name creates a metric family's name from a data source. This is
 * done in the same way as done by the "collectd_exporter" for best possible
 * compatibility. In essence, the plugin, type and data source name go in the
 * metric family name, while hostname, plugin instance and type instance go into
 * the labels of a metric. */
static char *metric_family_name(data_set_t const *ds, value_list_t const *vl,
                                size_t ds_index) {
  char const *fields[5] = {"collectd"};
  size_t fields_num = 1;

  if (strcmp(vl->plugin, vl->type) != 0) {
    fields[fields_num] = vl->plugin;
    fields_num++;
  }
  fields[fields_num] = vl->type;
  fields_num++;

  if (strcmp("value", ds->ds[ds_index].name) != 0) {
    fields[fields_num] = ds->ds[ds_index].name;
    fields_num++;
  }

  /* Prometheus best practices:
   * cumulative metrics should have a "total" suffix. */
  if ((ds->ds[ds_index].type == DS_TYPE_COUNTER) ||
      (ds->ds[ds_index].type == DS_TYPE_DERIVE)) {
    fields[fields_num] = "total";
    fields_num++;
  }

  char name[5 * DATA_MAX_NAME_LEN];
  strjoin(name, sizeof(name), (char **)fields, fields_num, "_");
  return strdup(name);
}

/* metric_family_get looks up the matching metric family, allocating it if
 * necessary. */
static Io__Prometheus__Client__MetricFamily *
metric_family_get(data_set_t const *ds, value_list_t const *vl, size_t ds_index,
                  _Bool allocate) {
  char *name = metric_family_name(ds, vl, ds_index);
  if (name == NULL) {
    ERROR("write_prometheus plugin: Allocating metric family name failed.");
    return NULL;
  }

  Io__Prometheus__Client__MetricFamily *fam = NULL;
  if (c_avl_get(metrics, name, (void *)&fam) == 0) {
    sfree(name);
    assert(fam != NULL);
    return fam;
  }

  if (!allocate) {
    sfree(name);
    return NULL;
  }

  fam = metric_family_create(name, ds, vl, ds_index);
  if (fam == NULL) {
    ERROR("write_prometheus plugin: Allocating metric family failed.");
    sfree(name);
    return NULL;
  }

  /* If successful, "name" is owned by "fam", i.e. don't free it here. */
  DEBUG("write_prometheus plugin: metric family \"%s\" has been created.",
        name);
  name = NULL;

  int status = c_avl_insert(metrics, fam->name, fam);
  if (status != 0) {
    ERROR("write_prometheus plugin: Adding \"%s\" failed.", name);
    metric_family_destroy(fam);
    return NULL;
  }

  return fam;
}
/* }}} */

static void prom_logger(__attribute__((unused)) void *arg, char const *fmt,
                        va_list ap) {
  /* {{{ */
  char errbuf[1024];
  vsnprintf(errbuf, sizeof(errbuf), fmt, ap);

  ERROR("write_prometheus plugin: %s", errbuf);
} /* }}} prom_logger */

#if MHD_VERSION >= 0x00090000
static int prom_open_socket(int addrfamily) {
  /* {{{ */
  char service[NI_MAXSERV];
  snprintf(service, sizeof(service), "%hu", httpd_port);

  struct addrinfo *res;
  int status = getaddrinfo(NULL, service,
                           &(struct addrinfo){
                               .ai_flags = AI_PASSIVE | AI_ADDRCONFIG,
                               .ai_family = addrfamily,
                               .ai_socktype = SOCK_STREAM,
                           },
                           &res);
  if (status != 0) {
    return -1;
  }

  int fd = -1;
  for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, 0);
    if (fd == -1)
      continue;

    int tmp = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp)) != 0) {
      char errbuf[1024];
      WARNING("write_prometheus: setsockopt(SO_REUSEADDR) failed: %s",
              sstrerror(errno, errbuf, sizeof(errbuf)));
      close(fd);
      fd = -1;
      continue;
    }

    if (bind(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
      close(fd);
      fd = -1;
      continue;
    }

    if (listen(fd, /* backlog = */ 16) != 0) {
      close(fd);
      fd = -1;
      continue;
    }

    break;
  }

  freeaddrinfo(res);

  return fd;
} /* }}} int prom_open_socket */

static struct MHD_Daemon *prom_start_daemon() {
  /* {{{ */
  int fd = prom_open_socket(PF_INET6);
  if (fd == -1)
    fd = prom_open_socket(PF_INET);
  if (fd == -1) {
    ERROR("write_prometheus plugin: Opening a listening socket failed.");
    return NULL;
  }

  struct MHD_Daemon *d = MHD_start_daemon(
      MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG, httpd_port,
      /* MHD_AcceptPolicyCallback = */ NULL,
      /* MHD_AcceptPolicyCallback arg = */ NULL, http_handler, NULL,
      MHD_OPTION_LISTEN_SOCKET, fd, MHD_OPTION_EXTERNAL_LOGGER, prom_logger,
      NULL, MHD_OPTION_END);
  if (d == NULL) {
    ERROR("write_prometheus plugin: MHD_start_daemon() failed.");
    close(fd);
    return NULL;
  }

  return d;
} /* }}} struct MHD_Daemon *prom_start_daemon */
#else /* if MHD_VERSION < 0x00090000 */
static struct MHD_Daemon *prom_start_daemon() {
  /* {{{ */
  struct MHD_Daemon *d = MHD_start_daemon(
      MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG, httpd_port,
      /* MHD_AcceptPolicyCallback = */ NULL,
      /* MHD_AcceptPolicyCallback arg = */ NULL, http_handler, NULL,
      MHD_OPTION_EXTERNAL_LOGGER, prom_logger, NULL, MHD_OPTION_END);
  if (d == NULL) {
    ERROR("write_prometheus plugin: MHD_start_daemon() failed.");
    return NULL;
  }

  return d;
} /* }}} struct MHD_Daemon *prom_start_daemon */
#endif

/*
 * collectd callbacks
 */
static int prom_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Port", child->key) == 0) {
      int status = cf_util_get_port_number(child);
      if (status > 0)
        httpd_port = (unsigned short)status;
    } else if (strcasecmp("StalenessDelta", child->key) == 0) {
      cf_util_get_cdtime(child, &staleness_delta);
    } else {
      WARNING("write_prometheus plugin: Ignoring unknown configuration option "
              "\"%s\".",
              child->key);
    }
  }

  return 0;
}

static int prom_init() {
  if (metrics == NULL) {
    metrics = c_avl_create((void *)strcmp);
    if (metrics == NULL) {
      ERROR("write_prometheus plugin: c_avl_create() failed.");
      return -1;
    }
  }

  if (httpd == NULL) {
    httpd = prom_start_daemon();
    if (httpd == NULL) {
      ERROR("write_prometheus plugin: MHD_start_daemon() failed.");
      return -1;
    }
    DEBUG("write_prometheus plugin: Successfully started microhttpd %s",
          MHD_get_version());
  }

  return 0;
}

static int prom_write(data_set_t const *ds, value_list_t const *vl,
                      __attribute__((unused)) user_data_t *ud) {
  pthread_mutex_lock(&metrics_lock);

  for (size_t i = 0; i < ds->ds_num; i++) {
    Io__Prometheus__Client__MetricFamily *fam =
        metric_family_get(ds, vl, i, /* allocate = */ 1);
    if (fam == NULL)
      continue;

    int status = metric_family_update(fam, ds, vl, i);
    if (status != 0) {
      ERROR("write_prometheus plugin: Updating metric \"%s\" failed with "
            "status %d",
            fam->name, status);
      continue;
    }
  }

  pthread_mutex_unlock(&metrics_lock);
  return 0;
}

static int prom_missing(value_list_t const *vl,
                        __attribute__((unused)) user_data_t *ud) {
  data_set_t const *ds = plugin_get_ds(vl->type);
  if (ds == NULL)
    return ENOENT;

  pthread_mutex_lock(&metrics_lock);

  for (size_t i = 0; i < ds->ds_num; i++) {
    Io__Prometheus__Client__MetricFamily *fam =
        metric_family_get(ds, vl, i, /* allocate = */ 0);
    if (fam == NULL)
      continue;

    int status = metric_family_delete_metric(fam, vl);
    if (status != 0) {
      ERROR("write_prometheus plugin: Deleting a metric in family \"%s\" "
            "failed with status %d",
            fam->name, status);

      continue;
    }

    if (fam->n_metric == 0) {
      int status = c_avl_remove(metrics, fam->name, NULL, NULL);
      if (status != 0) {
        ERROR("write_prometheus plugin: Deleting metric family \"%s\" failed "
              "with status %d",
              fam->name, status);
        continue;
      }
      metric_family_destroy(fam);
    }
  }

  pthread_mutex_unlock(&metrics_lock);
  return 0;
}

static int prom_shutdown() {
  if (httpd != NULL) {
    MHD_stop_daemon(httpd);
    httpd = NULL;
  }

  pthread_mutex_lock(&metrics_lock);
  if (metrics != NULL) {
    char *name;
    Io__Prometheus__Client__MetricFamily *fam;
    while (c_avl_pick(metrics, (void *)&name, (void *)&fam) == 0) {
      assert(name == fam->name);
      name = NULL;

      metric_family_destroy(fam);
    }
    c_avl_destroy(metrics);
    metrics = NULL;
  }
  pthread_mutex_unlock(&metrics_lock);

  return 0;
}

void module_register() {
  plugin_register_complex_config("write_prometheus", prom_config);
  plugin_register_init("write_prometheus", prom_init);
  plugin_register_write("write_prometheus", prom_write,
                        /* user data = */ NULL);
  plugin_register_missing("write_prometheus", prom_missing,
                          /* user data = */ NULL);
  plugin_register_shutdown("write_prometheus", prom_shutdown);
}
