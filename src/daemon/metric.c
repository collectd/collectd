/**
 * collectd - src/daemon/metric.c
 * Copyright (C) 2019-2020  Google LLC
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

#include "metric.h"
#include "plugin.h"

/* Label names must match the regex `[a-zA-Z_][a-zA-Z0-9_]*`. Label names
 * beginning with __ are reserved for internal use.
 *
 * Source:
 * https://prometheus.io/docs/concepts/data_model/#metric-names-and-labels */
#define VALID_LABEL_CHARS                                                      \
  "abcdefghijklmnopqrstuvwxyz"                                                 \
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"                                                 \
  "0123456789_"
/* Metric names must match the regex `[a-zA-Z_:][a-zA-Z0-9_:]*` */
#define VALID_NAME_CHARS VALID_LABEL_CHARS ":"

int value_marshal_text(strbuf_t *buf, value_t v, metric_type_t type) {
  switch (type) {
  case METRIC_TYPE_GAUGE:
  case METRIC_TYPE_UNTYPED:
    return strbuf_printf(buf, GAUGE_FORMAT, v.gauge);
  case METRIC_TYPE_COUNTER:
    return strbuf_printf(buf, "%" PRIu64, v.counter);
  default:
    ERROR("Unknown metric value type: %d", type);
    return EINVAL;
  }
}

static int label_pair_compare(void const *a, void const *b) {
  return strcmp(((label_pair_t const *)a)->name,
                ((label_pair_t const *)b)->name);
}

label_pair_t *label_set_get(label_set_t labels, char const *name) {
  if (name == NULL) {
    errno = EINVAL;
    return NULL;
  }

  label_t label = {
      .name = name,
  };

  label_pair_t *ret = bsearch(&label, labels.ptr, labels.num,
                              sizeof(*labels.ptr), label_pair_compare);
  if (ret == NULL) {
    errno = ENOENT;
    return NULL;
  }

  return ret;
}

int label_set_add(label_set_t *labels, char const *name, char const *value) {
  if ((labels == NULL) || (name == NULL) || (value == NULL)) {
    return EINVAL;
  }

  size_t name_len = strlen(name);
  if (name_len == 0) {
    return EINVAL;
  }

  size_t valid_len = strspn(name, VALID_LABEL_CHARS);
  if ((valid_len != name_len) || isdigit((int)name[0])) {
    return EINVAL;
  }

  if (label_set_get(*labels, name) != NULL) {
    return EEXIST;
  }

  if (strlen(value) == 0) {
    return 0;
  }

  label_pair_t *tmp =
      realloc(labels->ptr, sizeof(*labels->ptr) * (labels->num + 1));
  if (tmp == NULL) {
    return errno;
  }
  labels->ptr = tmp;

  label_pair_t pair = {
      .name = strdup(name),
      .value = strdup(value),
  };
  if ((pair.name == NULL) || (pair.value == NULL)) {
    free(pair.name);
    free(pair.value);
    return ENOMEM;
  }

  labels->ptr[labels->num] = pair;
  labels->num++;

  qsort(labels->ptr, labels->num, sizeof(*labels->ptr), label_pair_compare);
  return 0;
}

static int label_set_clone(label_set_t *dest, label_set_t src) {
  if (src.num == 0) {
    return 0;
  }

  label_set_t ret = {
      .ptr = calloc(src.num, sizeof(*ret.ptr)),
      .num = src.num,
  };
  if (ret.ptr == NULL) {
    return ENOMEM;
  }

  for (size_t i = 0; i < src.num; i++) {
    ret.ptr[i].name = strdup(src.ptr[i].name);
    ret.ptr[i].value = strdup(src.ptr[i].value);
    if ((ret.ptr[i].name == NULL) || (ret.ptr[i].value == NULL)) {
      label_set_reset(&ret);
      return ENOMEM;
    }
  }

  *dest = ret;
  return 0;
}

void label_set_reset(label_set_t *labels) {
  if (labels == NULL) {
    return;
  }
  for (size_t i = 0; i < labels->num; i++) {
    free(labels->ptr[i].name);
    free(labels->ptr[i].value);
  }
  free(labels->ptr);

  labels->ptr = NULL;
  labels->num = 0;
}

int metric_identity(strbuf_t *buf, metric_t const *m) {
  if ((buf == NULL) || (m == NULL) || (m->family == NULL)) {
    return EINVAL;
  }

  int status = strbuf_print(buf, m->family->name);
  if (m->label.num == 0) {
    return status;
  }

  status = status || strbuf_print(buf, "{");
  for (size_t i = 0; i < m->label.num; i++) {
    if (i != 0) {
      status = status || strbuf_print(buf, ",");
    }

    status = status || strbuf_print(buf, m->label.ptr[i].name);
    status = status || strbuf_print(buf, "=\"");
    status = status || strbuf_print_escaped(buf, m->label.ptr[i].value,
                                            "\\\"\n\r\t", '\\');
    status = status || strbuf_print(buf, "\"");
  }

  return status || strbuf_print(buf, "}");
}

int metric_list_add(metric_list_t *metrics, metric_t m) {
  if (metrics == NULL) {
    return EINVAL;
  }

  metric_t *tmp =
      realloc(metrics->ptr, sizeof(*metrics->ptr) * (metrics->num + 1));
  if (tmp == NULL) {
    return errno;
  }
  metrics->ptr = tmp;

  metrics->ptr[metrics->num] = m;
  metrics->num++;

  return 0;
}

static int metric_list_clone(metric_list_t *dest, metric_list_t src,
                             metric_family_t *fam) {
  if (src.num == 0) {
    return 0;
  }

  metric_list_t ret = {
      .ptr = calloc(src.num, sizeof(*ret.ptr)),
      .num = src.num,
  };
  if (ret.ptr == NULL) {
    return ENOMEM;
  }

  for (size_t i = 0; i < src.num; i++) {
    ret.ptr[i] = (metric_t){
        .family = fam,
        .value = src.ptr[i].value,
        .time = src.ptr[i].time,
        .interval = src.ptr[i].interval,
    };

    int status = label_set_clone(&ret.ptr[i].label, src.ptr[i].label);
    if (status != 0) {
      metric_list_reset(&ret);
      return status;
    }
  }

  *dest = ret;
  return 0;
}

void metric_list_reset(metric_list_t *metrics) {
  if (metrics == NULL) {
    return;
  }

  for (size_t i = 0; i < metrics->num; i++) {
    label_set_reset(&metrics->ptr[i].label);
  }
  free(metrics->ptr);

  metrics->ptr = NULL;
  metrics->num = 0;
}

int metric_family_metrics_append(metric_family_t *fam, value_t v,
                                 label_t const *label, size_t label_num) {
  if ((fam == NULL) || ((label_num != 0) && (label == NULL))) {
    return EINVAL;
  }

  metric_t m = {
      .family = fam,
      .value = v,
  };

  for (size_t i = 0; i < label_num; i++) {
    int status = label_set_add(&m.label, label[i].name, label[i].value);
    if (status != 0) {
      label_set_reset(&m.label);
      return status;
    }
  }

  int status = metric_list_add(&fam->metric, m);
  if (status != 0) {
    label_set_reset(&m.label);
    return status;
  }

  return 0;
}

void metric_family_free(metric_family_t *fam) {
  if (fam == NULL) {
    return;
  }

  free(fam->name);
  free(fam->help);
  metric_list_reset(&fam->metric);
  free(fam);
}

metric_family_t *metric_family_clone(metric_family_t const *fam) {
  if (fam == NULL) {
    errno = EINVAL;
    return NULL;
  }

  metric_family_t *ret = calloc(1, sizeof(*ret));
  if (ret == NULL) {
    return NULL;
  }

  ret->name = strdup(fam->name);
  if (fam->help != NULL) {
    ret->help = strdup(fam->help);
  }
  ret->type = fam->type;

  int status = metric_list_clone(&ret->metric, fam->metric, ret);
  if (status != 0) {
    metric_family_free(ret);
    errno = status;
    return NULL;
  }

  return ret;
}
