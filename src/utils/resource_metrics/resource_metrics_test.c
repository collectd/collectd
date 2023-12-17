/**
 * collectd - resource_metrics_test.c
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
 */

#include "testing.h"
#include "utils/resource_metrics/resource_metrics.h"

static metric_family_t *make_metric_family(int resource, int family, int metric,
                                           int time) {
  metric_family_t *fam = calloc(1, sizeof(*fam));

  char name[64] = {0};
  snprintf(name, sizeof(name), "family%d", family);
  fam->name = strdup(name);

  snprintf(name, sizeof(name), "resource%d", resource);
  metric_family_resource_attribute_update(fam, "service.name", name);

  snprintf(name, sizeof(name), "metric%d", metric);
  metric_family_append(fam, "metric.name", name, (value_t){.gauge = 42}, NULL);

  fam->metric.ptr[0].time = (cdtime_t)time;

  return fam;
}

static size_t count_metrics(resource_metrics_set_t set) {
  size_t sum = 0;
  for (size_t i = 0; i < set.num; i++) {
    resource_metrics_t const *rm = set.ptr + i;
    for (size_t j = 0; j < rm->families_num; j++) {
      sum += rm->families[j]->metric.num;
    }
  }
  return sum;
}

DEF_TEST(resource_metrics_add) {
  resource_metrics_set_t set = {0};
  metric_family_t *fam;

  CHECK_NOT_NULL(fam = make_metric_family(1, 1, 1, 1));
  CHECK_ZERO(resource_metrics_add(&set, fam));
  EXPECT_EQ_INT(1, set.num);
  EXPECT_EQ_INT(1, count_metrics(set));
  /* adding the same familiy twice should return EEXIST. */
  EXPECT_EQ_INT(EEXIST, resource_metrics_add(&set, fam));
  EXPECT_EQ_INT(1, set.num);
  EXPECT_EQ_INT(1, count_metrics(set));
  metric_family_free(fam);

  /* adding the same metric (but with a different resource attribute) should
   * succeed. */
  CHECK_NOT_NULL(fam = make_metric_family(2, 1, 1, 1));
  CHECK_ZERO(resource_metrics_add(&set, fam));
  EXPECT_EQ_INT(2, set.num);
  EXPECT_EQ_INT(2, count_metrics(set));
  /* adding the same familiy twice should return EEXIST. */
  EXPECT_EQ_INT(EEXIST, resource_metrics_add(&set, fam));
  EXPECT_EQ_INT(2, set.num);
  EXPECT_EQ_INT(2, count_metrics(set));
  metric_family_free(fam);

  /* adding a different metric family to an existing resource should work. */
  CHECK_NOT_NULL(fam = make_metric_family(1, 2, 1, 1));
  CHECK_ZERO(resource_metrics_add(&set, fam));
  /* reuses existing resource */
  EXPECT_EQ_INT(2, set.num);
  EXPECT_EQ_INT(3, count_metrics(set));
  /* adding the same familiy twice should return EEXIST. */
  EXPECT_EQ_INT(EEXIST, resource_metrics_add(&set, fam));
  EXPECT_EQ_INT(2, set.num);
  EXPECT_EQ_INT(3, count_metrics(set));
  metric_family_free(fam);

  /* adding a different metric to an existing metric family should work. */
  CHECK_NOT_NULL(fam = make_metric_family(1, 1, 2, 1));
  CHECK_ZERO(resource_metrics_add(&set, fam));
  /* reuses existing resource */
  EXPECT_EQ_INT(2, set.num);
  EXPECT_EQ_INT(4, count_metrics(set));
  /* adding the same familiy twice should return EEXIST. */
  EXPECT_EQ_INT(EEXIST, resource_metrics_add(&set, fam));
  EXPECT_EQ_INT(2, set.num);
  EXPECT_EQ_INT(4, count_metrics(set));
  metric_family_free(fam);

  /* adding a the same metric with a different time stamp should work. */
  CHECK_NOT_NULL(fam = make_metric_family(1, 1, 1, 2));
  CHECK_ZERO(resource_metrics_add(&set, fam));
  /* reuses existing resource */
  EXPECT_EQ_INT(2, set.num);
  EXPECT_EQ_INT(5, count_metrics(set));
  /* adding the same metric twice should return EEXIST. */
  EXPECT_EQ_INT(EEXIST, resource_metrics_add(&set, fam));
  EXPECT_EQ_INT(2, set.num);
  EXPECT_EQ_INT(5, count_metrics(set));
  metric_family_free(fam);

  resource_metrics_reset(&set);

  /* adding 1000 distinct metrics. */
  size_t want_metrics_count = 0;
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      for (int k = 0; k < 10; k++) {
        /* add metrics in "random" order */
        CHECK_NOT_NULL(fam = make_metric_family((i * 7) % 10, (j * 7) % 10,
                                                (k * 7) % 10, 1));
        EXPECT_EQ_INT(0, resource_metrics_add(&set, fam));
        want_metrics_count++;
        EXPECT_EQ_INT(want_metrics_count, count_metrics(set));
        metric_family_free(fam);
      }
    }
    EXPECT_EQ_INT(i + 1, set.num);
  }

  resource_metrics_reset(&set);
  return 0;
}

int main(void) {
  RUN_TEST(resource_metrics_add);

  END_TEST;
}
