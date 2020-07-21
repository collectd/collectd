/**
 * collectd - src/daemon/metric.h
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

#ifndef METRIC_H
#define METRIC_H 1

#include "utils/metadata/meta_data.h"
#include "utils/strbuf/strbuf.h"
#include "utils_time.h"

#define VALUE_TYPE_GAUGE 1
#define VALUE_TYPE_DERIVE 2

typedef enum {
  METRIC_TYPE_COUNTER = 0,
  METRIC_TYPE_GAUGE = 1,
  METRIC_TYPE_UNTYPED = 2,
} metric_type_t;

typedef uint64_t counter_t;
typedef double gauge_t;
typedef int64_t derive_t;

union value_u {
  counter_t counter;
  gauge_t gauge;
  derive_t derive;
};
typedef union value_u value_t;

/* value_marshal_text prints a text representation of v to buf. */
int value_marshal_text(strbuf_t *buf, value_t v, metric_type_t type);

/*
 * Labels
 */
/* label_pair_t represents a label, i.e. a key/value pair. */
typedef struct {
  char *name;
  char *value;
} label_pair_t;

/* label_t represents a constant label, i.e. a key/value pair. It is similar to
 * label_pair_t, except that is has const fields. label_t is used in function
 * arguments to prevent the called function from modifying its argument.
 * Internally labels are stored as label_pair_t to allow modification, e.g. by
 * targets in the "filter chain". */
typedef struct {
  char const *name;
  char const *value;
} label_t;

/* label_set_t is a sorted set of labels. */
typedef struct {
  label_pair_t *ptr;
  size_t num;
} label_set_t;

/* label_set_get efficiently looks up and returns the "name" label. If a label
 * does not exist, NULL is returned and errno is set to ENOENT. */
label_pair_t *label_set_get(label_set_t labels, char const *name);

/* label_set_add adds a new label to the set of labels. If a label with "name"
 * already exists, EEXIST is returned. If "value" is the empty string, no label
 * is added and zero is returned. */
int label_set_add(label_set_t *labels, char const *name, char const *value);

/* label_set_reset frees all the labels in the label set. It does *not* free
 * the passed "label_set_t*" itself. */
void label_set_reset(label_set_t *labels);

/*
 * Metric
 */
/* forward declaration since metric_family_t and metric_t refer to each other.
 */
struct metric_family_s;
typedef struct metric_family_s metric_family_t;

/* metric_t is a metric inside a metric family. */
typedef struct {
  metric_family_t *family; /* for family->name and family->type */

  label_set_t label;
  value_t value;
  cdtime_t time; /* TODO(octo): use ms or µs instead? */
  cdtime_t interval;
  /* TODO(octo): target labels */
  meta_data_t *meta; /* TODO(octo): free in metric_list_reset() */
} metric_t;

/* metric_identity writes the identity of the metric "m" to "buf". An example
 * string is:
 *
 *   "http_requests_total{method=\"post\",code=\"200\"}"
 */
int metric_identity(strbuf_t *buf, metric_t const *m);

/* metric_parse_identity parses "s" and returns a metric with only its identity
 * set. On error, errno is set and NULL is returned. The returned memory must
 * be freed by passing m->family to metric_family_free(). */
metric_t *metric_parse_identity(char const *s);

/* metric_list_t is an unordered list of metrics. */
typedef struct {
  metric_t *ptr;
  size_t num;
} metric_list_t;

/* metric_list_add appends a metric to the metric list. */
int metric_list_add(metric_list_t *metrics, metric_t m);

/* metric_list_reset frees all the metrics in the metric list. It does *not*
 * free the passed "metric_list_t*" itself. */
void metric_list_reset(metric_list_t *metrics);

/*
 * Metric Family
 */
/* metric_family_t is a group of metrics of the same type. */
struct metric_family_s {
  char *name;
  char *help;
  metric_type_t type;

  metric_list_t metric;
};

/* metric_family_metrics_append appends a new metric to the metric family. This
 * allocates memory which must be freed using metric_family_metrics_reset. */
int metric_family_metrics_append(metric_family_t *fam, value_t v,
                                 label_t const *label, size_t label_num);

/* metric_family_free frees a "metric_family_t" that was allocated with
 * metric_family_clone(). */
void metric_family_free(metric_family_t *fam);

/* metric_family_clone returns a copy of the provided metric family. On error,
 * errno is set and NULL is returned. The returned pointer must be freed with
 * metric_family_free(). */
metric_family_t *metric_family_clone(metric_family_t const *fam);

#endif
