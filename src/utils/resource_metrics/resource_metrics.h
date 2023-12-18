/**
 * collectd - src/utils/resource_metrics/resource_metrics.h
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

#ifndef UTILS_RESOURCE_METRICS_H
#define UTILS_RESOURCE_METRICS_H 1

#include "collectd.h"
#include "daemon/metric.h"

typedef struct {
  label_set_t resource;

  metric_family_t **families;
  size_t families_num;
} resource_metrics_t;

/* resource_metrics_set_t is a set of metric families, grouped by resource
 * attributes. Because the resource attributes are kept track of in
 * resource_metrics_t, the metric_family_t.resource field is cleared and cannot
 * be used. */
typedef struct {
  resource_metrics_t *ptr;
  size_t num;
} resource_metrics_set_t;

/* resource_metrics_add copies a metric family to the resource metrics set.
 * Identical metrics are skipped and not added to the set. Metrics are
 * identical, if their resource attributes, metric family name, metric labels,
 * and time stamp are equal.
 * Returns the number of metrics that were skipped or -1 on error. That means
 * that zero indicates complete success, a positive number indicates partial
 * success, and a negative number indicates an error condition. The number of
 * skipped entries may be equal to the total number of metrics provided; this is
 * not indicated as an error. */
int resource_metrics_add(resource_metrics_set_t *rm,
                         metric_family_t const *fam);

/* resource_metrics_reset frees all the memory held inside the set. set itself
 * is not freed and can be reused afterwards. */
void resource_metrics_reset(resource_metrics_set_t *set);

#endif
