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
    ERROR("Unknown metric value type: %d", (int)type);
    return EINVAL;
  }
}

static int label_pair_compare(void const *a, void const *b) {
  return strcmp(((label_pair_t const *)a)->name,
                ((label_pair_t const *)b)->name);
}

static label_pair_t *label_set_read(label_set_t labels, char const *name) {
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

static int label_set_create(label_set_t *labels, char const *name,
                            char const *value) {
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

  if (label_set_read(*labels, name) != NULL) {
    return EEXIST;
  }
  errno = 0;

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

static int label_set_delete(label_set_t *labels, label_pair_t *elem) {
  if ((labels == NULL) || (elem == NULL)) {
    return EINVAL;
  }

  if ((elem < labels->ptr) || (elem > labels->ptr + (labels->num - 1))) {
    return ERANGE;
  }

  size_t index = elem - labels->ptr;
  assert(labels->ptr + index == elem);

  free(elem->name);
  free(elem->value);

  if (index != (labels->num - 1)) {
    memmove(labels->ptr + index, labels->ptr + (index + 1),
            labels->num - (index + 1));
  }
  labels->num--;

  if (labels->num == 0) {
    free(labels->ptr);
    labels->ptr = NULL;
  }

  return 0;
}

static void label_set_reset(label_set_t *labels) {
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

int metric_reset(metric_t *m) {
  if (m == NULL) {
    return EINVAL;
  }

  label_set_reset(&m->label);
  meta_data_destroy(m->meta);

  if (m->family != NULL && m->family->type == METRIC_TYPE_DISTRIBUTION) {
    distribution_destroy(m->value.distribution);
  }

  memset(m, 0, sizeof(*m));

  return 0;
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

int metric_label_set(metric_t *m, char const *name, char const *value) {
  if ((m == NULL) || (name == NULL)) {
    return EINVAL;
  }

  label_pair_t *label = label_set_read(m->label, name);
  if ((label == NULL) && (errno != ENOENT)) {
    return errno;
  }
  errno = 0;

  if (label == NULL) {
    if ((value == NULL) || strlen(value) == 0) {
      return 0;
    }
    return label_set_create(&m->label, name, value);
  }

  if ((value == NULL) || strlen(value) == 0) {
    return label_set_delete(&m->label, label);
  }

  char *new_value = strdup(value);
  if (new_value == NULL) {
    return errno;
  }

  free(label->value);
  label->value = new_value;

  return 0;
}

char const *metric_label_get(metric_t const *m, char const *name) {
  if ((m == NULL) || (name == NULL)) {
    errno = EINVAL;
    return NULL;
  }

  label_pair_t *set = label_set_read(m->label, name);
  if (set == NULL) {
    return NULL;
  }

  return set->value;
}

static int metric_list_add(metric_list_t *metrics, metric_t m) {
  if (metrics == NULL) {
    return EINVAL;
  }

  metric_t *tmp =
      realloc(metrics->ptr, sizeof(*metrics->ptr) * (metrics->num + 1));
  if (tmp == NULL) {
    return errno;
  }
  metrics->ptr = tmp;

  metric_t copy = {
      .family = m.family,
      .value = m.value,
      .time = m.time,
      .interval = m.interval,
      .meta = meta_data_clone(m.meta),
  };
  int status = label_set_clone(&copy.label, m.label);
  if (((m.meta != NULL) && (copy.meta == NULL)) || (status != 0)) {
    label_set_reset(&copy.label);
    meta_data_destroy(copy.meta);
    return status;
  }

  metrics->ptr[metrics->num] = copy;
  metrics->num++;

  return 0;
}

static void metric_list_reset(metric_list_t *metrics) {
  if (metrics == NULL) {
    return;
  }

  for (size_t i = 0; i < metrics->num; i++) {
    metric_reset(metrics->ptr + i);
  }
  free(metrics->ptr);

  metrics->ptr = NULL;
  metrics->num = 0;
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
        .time = src.ptr[i].time,
        .interval = src.ptr[i].interval,
    };

    if (src.ptr[i].family->type == METRIC_TYPE_DISTRIBUTION) {
      ret.ptr[i].value.distribution =
          distribution_clone(src.ptr[i].value.distribution);
    } else {
      ret.ptr[i].value = src.ptr[i].value;
    }

    int status = label_set_clone(&ret.ptr[i].label, src.ptr[i].label);
    if (status != 0) {
      metric_list_reset(&ret);
      return status;
    }
  }

  *dest = ret;
  return 0;
}

int metric_family_metric_append(metric_family_t *fam, metric_t m) {
  if (fam == NULL) {
    return EINVAL;
  }

  m.family = fam;
  return metric_list_add(&fam->metric, m);
}

int metric_family_append(metric_family_t *fam, char const *lname,
                         char const *lvalue, value_t v, metric_t const *templ) {
  if ((fam == NULL) || ((lname == NULL) != (lvalue == NULL))) {
    return EINVAL;
  }

  metric_t m = {
      .family = fam,
      .value = v,
  };
  if (templ != NULL) {
    int status = label_set_clone(&m.label, templ->label);
    if (status != 0) {
      return status;
    }

    m.time = templ->time;
    m.interval = templ->interval;
    m.meta = meta_data_clone(templ->meta);
  }

  if (lname != NULL) {
    int status = metric_label_set(&m, lname, lvalue);
    if (status != 0) {
      return status;
    }
  }

  int status = metric_family_metric_append(fam, m);
  metric_reset(&m);
  return status;
}

int metric_family_metric_reset(metric_family_t *fam) {
  if (fam == NULL) {
    return EINVAL;
  }

  metric_list_reset(&fam->metric);
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

/* parse_label_value reads a label value, unescapes it and prints it to buf. On
 * success, inout is updated to point to the character just *after* the label
 * value, i.e. the character *following* the ending quotes - either a comma or
 * closing curlies. */
static int parse_label_value(strbuf_t *buf, char const **inout) {
  char const *ptr = *inout;

  if (ptr[0] != '"') {
    return EINVAL;
  }
  ptr++;

  while (ptr[0] != '"') {
    size_t valid_len = strcspn(ptr, "\\\"\n");
    if (valid_len != 0) {
      strbuf_printn(buf, ptr, valid_len);
      ptr += valid_len;
      continue;
    }

    if ((ptr[0] == 0) || (ptr[0] == '\n')) {
      return EINVAL;
    }

    assert(ptr[0] == '\\');
    if (ptr[1] == 0) {
      return EINVAL;
    }

    char tmp[2] = {ptr[1], 0};
    if (tmp[0] == 'n') {
      tmp[0] = '\n';
    } else if (tmp[0] == 'r') {
      tmp[0] = '\r';
    } else if (tmp[0] == 't') {
      tmp[0] = '\t';
    }

    strbuf_print(buf, tmp);

    ptr += 2;
  }

  assert(ptr[0] == '"');
  ptr++;
  *inout = ptr;
  return 0;
}

/* metric_family_unmarshal_identity parses the metric identity and updates
 * "inout" to point to the first character following the identity. With valid
 * input, this means that "inout" will then point either to a '\0' (null byte)
 * or a ' ' (space). */
static int metric_family_unmarshal_identity(metric_family_t *fam,
                                            char const **inout) {
  if ((fam == NULL) || (inout == NULL) || (*inout == NULL)) {
    return EINVAL;
  }

  char const *ptr = *inout;
  size_t name_len = strspn(ptr, VALID_NAME_CHARS);
  if (name_len == 0) {
    return EINVAL;
  }

  char name[name_len + 1];
  strncpy(name, ptr, name_len);
  name[name_len] = 0;
  ptr += name_len;

  fam->name = strdup(name);
  if (fam->name == NULL) {
    return ENOMEM;
  }

  /* metric name without labels */
  if ((ptr[0] == 0) || (ptr[0] == ' ')) {
    *inout = ptr;
    return 0;
  }

  if (ptr[0] != '{') {
    return EINVAL;
  }

  metric_t *m = fam->metric.ptr;
  int ret = 0;
  while ((ptr[0] == '{') || (ptr[0] == ',')) {
    ptr++;

    size_t key_len = strspn(ptr, VALID_LABEL_CHARS);
    if (key_len == 0) {
      ret = EINVAL;
      break;
    }
    char key[key_len + 1];
    strncpy(key, ptr, key_len);
    key[key_len] = 0;
    ptr += key_len;

    if (ptr[0] != '=') {
      ret = EINVAL;
      break;
    }
    ptr++;

    strbuf_t value = STRBUF_CREATE;
    int status = parse_label_value(&value, &ptr);
    if (status != 0) {
      ret = status;
      STRBUF_DESTROY(value);
      break;
    }

    /* one metric is added to the family by metric_family_unmarshal_text. */
    assert(fam->metric.num >= 1);

    status = metric_label_set(m, key, value.ptr);
    STRBUF_DESTROY(value);
    if (status != 0) {
      ret = status;
      break;
    }
  }

  if (ret != 0) {
    return ret;
  }

  if ((ptr[0] != '}') || ((ptr[1] != 0) && (ptr[1] != ' '))) {
    return EINVAL;
  }

  *inout = &ptr[1];
  return 0;
}

metric_t *metric_parse_identity(char const *buf) {
  if (buf == NULL) {
    errno = EINVAL;
    return NULL;
  }

  metric_family_t *fam = calloc(1, sizeof(*fam));
  if (fam == NULL) {
    return NULL;
  }
  fam->type = METRIC_TYPE_UNTYPED;

  int status = metric_list_add(&fam->metric, (metric_t){.family = fam});
  if (status != 0) {
    metric_family_free(fam);
    errno = status;
    return NULL;
  }

  status = metric_family_unmarshal_identity(fam, &buf);
  if (status != 0) {
    metric_family_free(fam);
    errno = status;
    return NULL;
  }

  if (buf[0] != 0) {
    metric_family_free(fam);
    errno = EINVAL;
    return NULL;
  }

  return fam->metric.ptr;
}
