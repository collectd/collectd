/**
 * collectd - src/write_prometheus_test.c
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

#include "collectd.h"

#include "daemon/metric.h"
#include "testing.h"
#include "utils/common/common.h"

void format_metric_family(strbuf_t *buf, metric_family_t const *prom_fam);

DEF_TEST(format_metric_family) {
  struct {
    char const *name;
    metric_family_t fam;
    char const *want;
  } cases[] = {
      {
          .name = "metrics is empty",
          .fam =
              {
                  .name = "unit.test",
              },
          .want = NULL,
      },
      {
          .name = "metric without labels",
          .fam =
              {
                  .name = "unit.test",
                  .type = METRIC_TYPE_COUNTER,
                  .metric =
                      {
                          .ptr =
                              &(metric_t){
                                  .value =
                                      (value_t){
                                          .counter = 42,
                                      },
                              },
                          .num = 1,
                      },
              },
          .want = "# HELP unit_test\n"
                  "# TYPE unit_test counter\n"
                  "unit_test 42\n",
      },
      {
          .name = "metric with one label",
          .fam =
              {
                  .name = "unittest",
                  .type = METRIC_TYPE_COUNTER,
                  .metric =
                      {
                          .ptr =
                              &(metric_t){
                                  .label =
                                      {
                                          .ptr =
                                              &(label_pair_t){
                                                  .name = "foo",
                                                  .value = "bar",
                                              },
                                          .num = 1,
                                      },
                                  .value =
                                      (value_t){
                                          .counter = 42,
                                      },
                              },
                          .num = 1,
                      },
              },
          .want = "# HELP unittest\n"
                  "# TYPE unittest counter\n"
                  "unittest{foo=\"bar\"} 42\n",
      },
      {
          .name = "invalid characters are replaced",
          .fam =
              {
                  .name = "unit.test",
                  .type = METRIC_TYPE_COUNTER,
                  .metric =
                      {
                          .ptr =
                              &(metric_t){
                                  .label =
                                      {
                                          .ptr =
                                              &(label_pair_t){
                                                  .name = "metric.name",
                                                  .value = "unit.test",
                                              },
                                          .num = 1,
                                      },
                                  .value =
                                      (value_t){
                                          .counter = 42,
                                      },
                              },
                          .num = 1,
                      },
              },
          .want = "# HELP unit_test\n"
                  "# TYPE unit_test counter\n"
                  "unit_test{metric_name=\"unit.test\"} 42\n",
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("# Case %zu: %s\n", i, cases[i].name);
    strbuf_t got = STRBUF_CREATE;

    metric_family_t *fam = &cases[i].fam;
    for (size_t j = 0; j < fam->metric.num; j++) {
      fam->metric.ptr[j].family = fam;
    }

    format_metric_family(&got, &cases[i].fam);
    EXPECT_EQ_STR(cases[i].want, got.ptr);

    STRBUF_DESTROY(got);
  }

  return 0;
}

int main(void) {
  RUN_TEST(format_metric_family);

  END_TEST;
}
