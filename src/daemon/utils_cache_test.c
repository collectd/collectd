/**
 * collectd - src/daemon/utils_cache_test.c
 * Copyright (C) 2020       Barbara bkjg Kaczorowska
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
 *   Barbara bkjg Kaczorowska <bkjg at google.com>
 */

#include "collectd.h"

#include "distribution.h"
#include "testing.h"
#include "utils_cache.h"

static metric_family_t create_metric_family(char *name) {
  metric_family_t fam = {
      .name = name,
      .type = METRIC_TYPE_DISTRIBUTION,
  };

  metric_t m[] = {{
                      .family = &fam,
                      .value.distribution = distribution_new_linear(5, 23),
                      .time = cdtime_mock++,
                  },
                  {
                      .family = &fam,
                      .value.distribution = distribution_new_linear(5, 23),
                      .time = cdtime_mock++,
                  },
                  {
                      .family = &fam,
                      .value.distribution = distribution_new_linear(5, 23),
                      .time = cdtime_mock++,
                  }};

  for (int i = 0; i < (sizeof(m) / sizeof(m[0])); ++i) {
    metric_family_metric_append(&fam, m[i]);
  }

  return fam;
}

DEF_TEST(uc_update) {
  struct {
    int want_get;
    double **updates;
    int *num_updates;
    metric_family_t fam;
  } cases[] = {
      {
          .want_get = 0,

          .updates =
              (double *[]){
                  (double[]){5.432, 5.4234, 6.3424, 7.5453, 902.3525,
                             52352.523523, 523524.524524, 90342.4325, 136.352,
                             90.8},
                  (double[]){5.432, 5.4234, 6.3424, 7.5453, 902.3525,
                             52352.523523, 523524.524524, 90342.4325, 136.352,
                             90.8, 1.214, 90432.31434, 43141.1342, 41412.4314,
                             97.47, 90.78},
                  (double[]){
                      5.432,      5.4234,       6.3424,        7.5453,
                      902.3525,   52352.523523, 523524.524524, 90342.4325,
                      136.352,    90.8,         1.214,         90432.31434,
                      43141.1342, 41412.4314,   97.47,         90.78,
                      78.57,      90.467,       89.658,        879.67},
              },
          .num_updates = (int[]){10, 16, 20},
          .fam = create_metric_family("abc"),
      },
  };
  for (int i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    uc_init();
    for (int k = 0; k < cases[i].fam.metric.num; ++k) {
      for (int j = 0; j < cases[i].num_updates[k]; ++j) {
        CHECK_ZERO(
            distribution_update(cases[i].fam.metric.ptr[k].value.distribution,
                                cases[i].updates[k][j]));
      }
    }

    EXPECT_EQ_INT(cases[i].want_get, uc_update(&cases[i].fam));

    metric_family_metric_reset(&cases[i].fam);
  }
  return 0;
}

DEF_TEST(uc_get_percentile) { return 0; }

DEF_TEST(uc_get_rate) { return 0; }

int main() {
  RUN_TEST(uc_update);
  RUN_TEST(uc_get_percentile);
  RUN_TEST(uc_get_rate);

  END_TEST;
}
