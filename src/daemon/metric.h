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

/* label_set_t is a sorted set of labels. */
typedef struct {
  label_pair_t *ptr;
  size_t num;
} label_set_t;

/* label_set_clone copies all the laels in src into dest. If dest contains
 * any labels prior to calling label_set_clone, the associated memory is
 * leaked. */
int label_set_clone(label_set_t *dest, label_set_t src);

/* label_set_add adds a label to the label set. If a label with name already
 * exists, EEXIST is returned. The set of labels is sorted by label name. */
int label_set_add(label_set_t *labels, char const *name, char const *value);

/* label_set_reset frees all the memory referenced by the label set and
 * initializes the label set to zero. */
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
  metric_family_t *family; /* backreference for family->name and family->type */

  label_set_t label;
  label_set_t resource;

  value_t value;
  cdtime_t time; /* TODO(octo): use ms or µs instead? */
  cdtime_t interval;
  meta_data_t *meta;
} metric_t;

/* metric_identity writes the identity of the metric "m" to "buf", using the
 * OpenMetrics / Prometheus plain text exposition format.
 *
 * Example:
 *   "http_requests_total{method=\"post\",code=\"200\"}"
 */
int metric_identity(strbuf_t *buf, metric_t const *m);

/* metric_parse_identity parses "s" and returns a metric with only its identity
 * set. On error, errno is set and NULL is returned. The returned memory must
 * be freed by passing m->family to metric_family_free(). */
metric_t *metric_parse_identity(char const *s);

/* metric_label_set adds or updates a label to/in the label set.
 * If "value" is NULL or the empty string, the label is removed. Removing a
 * label that does not exist is *not* an error. */
int metric_label_set(metric_t *m, char const *name, char const *value);

/* metric_resource_attribute_update adds, updates, or deleted a resource
 * attribute. If "value" is NULL or the empty string, the attribute is removed.
 * Removing an attribute that does not exist is *not* an error. */
int metric_resource_attribute_update(metric_t *m, char const *name,
                                     char const *value);

/* metric_label_get efficiently looks up and returns the value of the "name"
 * label. If a label does not exist, NULL is returned and errno is set to
 * ENOENT. The returned pointer may not be valid after a subsequent call to
 * "metric_label_set". */
char const *metric_label_get(metric_t const *m, char const *name);

/* metric_reset frees all labels and meta data stored in the metric and resets
 * the metric to zero. */
int metric_reset(metric_t *m);

/* metric_list_t is an unordered list of metrics. */
typedef struct {
  metric_t *ptr;
  size_t num;
} metric_list_t;

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

/* metric_family_metric_append appends a new metric to the metric family. This
 * allocates memory which must be freed using metric_family_metric_reset. */
int metric_family_metric_append(metric_family_t *fam, metric_t m);

/* metric_family_append constructs a new metric_t and appends it to fam. It is
 * a convenience function that is funcitonally approximately equivalent to the
 * following code, but without modifying templ:
 *
 *   metric_t m = *templ;
 *   m.value = v;
 *   metric_label_set(&m, lname, lvalue);
 *   metric_family_metric_append(fam, m);
 */
int metric_family_append(metric_family_t *fam, char const *lname,
                         char const *lvalue, value_t v, metric_t const *templ);

/* metric_family_metric_reset frees all metrics in the metric family and
 * resets the count to zero. */
int metric_family_metric_reset(metric_family_t *fam);

/* metric_family_free frees a "metric_family_t" that was allocated with
 * metric_family_clone(). */
void metric_family_free(metric_family_t *fam);

/* metric_family_clone returns a copy of the provided metric family. On error,
 * errno is set and NULL is returned. The returned pointer must be freed with
 * metric_family_free(). */
metric_family_t *metric_family_clone(metric_family_t const *fam);

#endif
