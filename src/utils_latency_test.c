/**
 * collectd - src/utils_latency_test.c
 * Copyright (C) 2015       Florian octo Forster
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

#define DBL_PRECISION 1e-6

#include "collectd.h"
#include "common.h" /* for STATIC_ARRAY_SIZE */

#include "testing.h"
#include "utils_latency.h"
#include "utils_time.h"

DEF_TEST(simple) {
  struct {
    double val;
    double min;
    double max;
    double sum;
    double avg;
  } cases[] = {
      /* val  min  max  sum   avg */
      {0.5, 0.5, 0.5, 0.5, 0.5}, {0.3, 0.3, 0.5, 0.8, 0.4},
      {0.7, 0.3, 0.7, 1.5, 0.5}, {2.5, 0.3, 2.5, 4.0, 1.0},
      {99, 0.3, 99, 103, 20.6},
      /* { -1, 0.3,  99, 103, 20.6}, see issue #1139 */
  };
  latency_counter_t *l;

  CHECK_NOT_NULL(l = latency_counter_create());

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("# case %" PRIsz ": DOUBLE_TO_CDTIME_T(%g) = %" PRIu64 "\n", i,
           cases[i].val, DOUBLE_TO_CDTIME_T(cases[i].val));
    latency_counter_add(l, DOUBLE_TO_CDTIME_T(cases[i].val));

    EXPECT_EQ_DOUBLE(cases[i].min,
                     CDTIME_T_TO_DOUBLE(latency_counter_get_min(l)));
    EXPECT_EQ_DOUBLE(cases[i].max,
                     CDTIME_T_TO_DOUBLE(latency_counter_get_max(l)));
    EXPECT_EQ_DOUBLE(cases[i].sum,
                     CDTIME_T_TO_DOUBLE(latency_counter_get_sum(l)));
    EXPECT_EQ_DOUBLE(cases[i].avg,
                     CDTIME_T_TO_DOUBLE(latency_counter_get_average(l)));
  }

  latency_counter_destroy(l);
  return 0;
}

DEF_TEST(percentile) {
  latency_counter_t *l;

  CHECK_NOT_NULL(l = latency_counter_create());

  for (size_t i = 0; i < 100; i++) {
    latency_counter_add(l, TIME_T_TO_CDTIME_T(((time_t)i) + 1));
  }

  EXPECT_EQ_DOUBLE(1.0, CDTIME_T_TO_DOUBLE(latency_counter_get_min(l)));
  EXPECT_EQ_DOUBLE(100.0, CDTIME_T_TO_DOUBLE(latency_counter_get_max(l)));
  EXPECT_EQ_DOUBLE(100.0 * 101.0 / 2.0,
                   CDTIME_T_TO_DOUBLE(latency_counter_get_sum(l)));
  EXPECT_EQ_DOUBLE(50.5, CDTIME_T_TO_DOUBLE(latency_counter_get_average(l)));

  EXPECT_EQ_DOUBLE(50.0,
                   CDTIME_T_TO_DOUBLE(latency_counter_get_percentile(l, 50.0)));
  EXPECT_EQ_DOUBLE(80.0,
                   CDTIME_T_TO_DOUBLE(latency_counter_get_percentile(l, 80.0)));
  EXPECT_EQ_DOUBLE(95.0,
                   CDTIME_T_TO_DOUBLE(latency_counter_get_percentile(l, 95.0)));
  EXPECT_EQ_DOUBLE(99.0,
                   CDTIME_T_TO_DOUBLE(latency_counter_get_percentile(l, 99.0)));

  CHECK_ZERO(latency_counter_get_percentile(l, -1.0));
  CHECK_ZERO(latency_counter_get_percentile(l, 101.0));

  latency_counter_destroy(l);
  return 0;
}

DEF_TEST(get_rate) {
  /* We re-declare the struct here so we can inspect its content. */
  struct {
    cdtime_t start_time;
    cdtime_t sum;
    size_t num;
    cdtime_t min;
    cdtime_t max;
    cdtime_t bin_width;
    int histogram[HISTOGRAM_NUM_BINS];
  } * peek;
  latency_counter_t *l;

  CHECK_NOT_NULL(l = latency_counter_create());
  peek = (void *)l;

  for (time_t i = 1; i <= 125; i++) {
    latency_counter_add(l, TIME_T_TO_CDTIME_T(i));
  }

  /* We expect a bucket width of 125ms. */
  EXPECT_EQ_UINT64(DOUBLE_TO_CDTIME_T(0.125), peek->bin_width);

  struct {
    size_t index;
    int want;
  } bucket_cases[] = {
      {0, 0},  /* (0.000-0.125] */
      {1, 0},  /* (0.125-0.250] */
      {2, 0},  /* (0.250-0.375] */
      {3, 0},  /* (0.375-0.500] */
      {4, 0},  /* (0.500-0.625] */
      {5, 0},  /* (0.625-0.750] */
      {6, 0},  /* (0.750-0.875] */
      {7, 1},  /* (0.875-1.000] */
      {8, 0},  /* (1.000-1.125] */
      {9, 0},  /* (1.125-1.250] */
      {10, 0}, /* (1.250-1.375] */
      {11, 0}, /* (1.375-1.500] */
      {12, 0}, /* (1.500-1.625] */
      {13, 0}, /* (1.625-1.750] */
      {14, 0}, /* (1.750-1.875] */
      {15, 1}, /* (1.875-2.000] */
      {16, 0}, /* (2.000-2.125] */
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(bucket_cases); i++) {
    size_t index = bucket_cases[i].index;
    EXPECT_EQ_INT(bucket_cases[i].want, peek->histogram[index]);
  }

  struct {
    cdtime_t lower_bound;
    cdtime_t upper_bound;
    double want;
  } cases[] = {
      {
          // bucket 6 is zero
          DOUBLE_TO_CDTIME_T_STATIC(0.750), DOUBLE_TO_CDTIME_T_STATIC(0.875),
          0.00,
      },
      {
          // bucket 7 contains the t=1 update
          DOUBLE_TO_CDTIME_T_STATIC(0.875), DOUBLE_TO_CDTIME_T_STATIC(1.000),
          1.00,
      },
      {
          // range: bucket 7 - bucket 15; contains the t=1 and t=2 updates
          DOUBLE_TO_CDTIME_T_STATIC(0.875), DOUBLE_TO_CDTIME_T_STATIC(2.000),
          2.00,
      },
      {
          // lower bucket is only partially applied
          DOUBLE_TO_CDTIME_T_STATIC(0.875 + (0.125 / 4)),
          DOUBLE_TO_CDTIME_T_STATIC(2.000), 1.75,
      },
      {
          // upper bucket is only partially applied
          DOUBLE_TO_CDTIME_T_STATIC(0.875),
          DOUBLE_TO_CDTIME_T_STATIC(2.000 - (0.125 / 4)), 1.75,
      },
      {
          // both buckets are only partially applied
          DOUBLE_TO_CDTIME_T_STATIC(0.875 + (0.125 / 4)),
          DOUBLE_TO_CDTIME_T_STATIC(2.000 - (0.125 / 4)), 1.50,
      },
      {
          // lower bound is unspecified
          0, DOUBLE_TO_CDTIME_T_STATIC(2.000), 2.00,
      },
      {
          // upper bound is unspecified
          DOUBLE_TO_CDTIME_T_STATIC(125.000 - 0.125), 0, 1.00,
      },
      {
          // overflow test: upper >> longest latency
          DOUBLE_TO_CDTIME_T_STATIC(1.000), DOUBLE_TO_CDTIME_T_STATIC(999999),
          124.00,
      },
      {
          // overflow test: lower > longest latency
          DOUBLE_TO_CDTIME_T_STATIC(130), 0, 0.00,
      },
      {
          // lower > upper => error
          DOUBLE_TO_CDTIME_T_STATIC(10), DOUBLE_TO_CDTIME_T_STATIC(9), NAN,
      },
      {
          // lower == upper => zero
          DOUBLE_TO_CDTIME_T_STATIC(9), DOUBLE_TO_CDTIME_T_STATIC(9), 0.00,
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    cdtime_t now = peek->start_time + TIME_T_TO_CDTIME_T(1);
    EXPECT_EQ_DOUBLE(cases[i].want,
                     latency_counter_get_rate(l, cases[i].lower_bound,
                                              cases[i].upper_bound, now));
  }

  latency_counter_destroy(l);
  return 0;
}

int main(void) {
  RUN_TEST(simple);
  RUN_TEST(percentile);
  RUN_TEST(get_rate);

  END_TEST;
}
