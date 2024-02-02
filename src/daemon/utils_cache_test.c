/**
 * collectd - src/daemon/utils_cache_test.c
 * Copyright (C) 2024       Florian octo Forster
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

#include "collectd.h"

#include "daemon/utils_cache.h"
#include "testing.h"
#include "utils/common/common.h"

DEF_TEST(uc_get_rate) {
  struct {
    char const *name;
    value_t first_value;
    value_t second_value;
    cdtime_t first_time;
    cdtime_t second_time;
    metric_type_t type;

    gauge_t want;
  } cases[] = {
      {
          .name = "gauge",
          .first_value = (value_t){.gauge = 1.0},
          .second_value = (value_t){.gauge = 2.0},
          .first_time = TIME_T_TO_CDTIME_T(100),
          .second_time = TIME_T_TO_CDTIME_T(110),
          .type = METRIC_TYPE_GAUGE,
          .want = 2.0,
      },
      {
          .name = "decreasing gauge",
          .first_value = (value_t){.gauge = 100.0},
          .second_value = (value_t){.gauge = 21.5},
          .first_time = TIME_T_TO_CDTIME_T(100),
          .second_time = TIME_T_TO_CDTIME_T(110),
          .type = METRIC_TYPE_GAUGE,
          .want = 21.5,
      },
      {
          .name = "counter",
          .first_value = (value_t){.counter = 42},
          .second_value = (value_t){.counter = 102},
          .first_time = TIME_T_TO_CDTIME_T(100),
          .second_time = TIME_T_TO_CDTIME_T(110),
          .type = METRIC_TYPE_COUNTER,
          .want = (102. - 42.) / (110. - 100.),
      },
      {
          .name = "with 32bit overflow",
          .first_value = (value_t){.counter = UINT32_MAX - 23},
          .second_value = (value_t){.counter = 18},
          .first_time = TIME_T_TO_CDTIME_T(100),
          .second_time = TIME_T_TO_CDTIME_T(110),
          .type = METRIC_TYPE_COUNTER,
          .want = (23. + 18. + 1.) / (110. - 100.),
      },
      {
          .name = "with 64bit overflow",
          .first_value = (value_t){.counter = UINT64_MAX - 23},
          .second_value = (value_t){.counter = 18},
          .first_time = TIME_T_TO_CDTIME_T(100),
          .second_time = TIME_T_TO_CDTIME_T(110),
          .type = METRIC_TYPE_COUNTER,
          .want = (23. + 18. + 1.) / (110. - 100.),
      },
      {
          .name = "fpcounter",
          .first_value = (value_t){.fpcounter = 4.2},
          .second_value = (value_t){.fpcounter = 10.2},
          .first_time = TIME_T_TO_CDTIME_T(100),
          .second_time = TIME_T_TO_CDTIME_T(110),
          .type = METRIC_TYPE_FPCOUNTER,
          .want = (10.2 - 4.2) / (110 - 100),
      },
      {
          .name = "fpcounter with reset",
          .first_value = (value_t){.fpcounter = 100000.0},
          .second_value = (value_t){.fpcounter = 0.2},
          .first_time = TIME_T_TO_CDTIME_T(100),
          .second_time = TIME_T_TO_CDTIME_T(110),
          .type = METRIC_TYPE_FPCOUNTER,
          .want = NAN,
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("## Case %zu: %s\n", i, cases[i].name);

    char name[64];
    snprintf(name, sizeof(name), "unit.test%zu", i);

    metric_family_t fam = {
        .name = name,
        .type = cases[i].type,
    };
    metric_t m = {
        .family = &fam,
        .time = cases[i].first_time,
        .value = cases[i].first_value,
    };
    fam.metric = (metric_list_t){
        .ptr = &m,
        .num = 1,
    };

    // first value
    EXPECT_EQ_INT(0, uc_update(&fam));
    gauge_t got = 0;
    EXPECT_EQ_INT(0, uc_get_rate(&m, &got));
    gauge_t want = NAN;
    if (fam.type == METRIC_TYPE_GAUGE) {
      want = cases[i].first_value.gauge;
    }
    EXPECT_EQ_DOUBLE(want, got);

    // second value
    m.time = cases[i].second_time;
    m.value = cases[i].second_value;
    EXPECT_EQ_INT(0, uc_update(&fam));
    got = 0;
    EXPECT_EQ_INT(0, uc_get_rate(&m, &got));
    EXPECT_EQ_DOUBLE(cases[i].want, got);
  }

  return 0;
}

int main(void) {
  RUN_TEST(uc_get_rate);

  END_TEST;
}
