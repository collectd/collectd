/**
 * collectd - src/daemon/distribution_test.c
 * Copyright (C) 2020       Google LLC
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
 *   Svetlana Shmidt <sshmidt at google.com>
 */

#include "collectd.h"

#include "distribution.h"
#include "testing.h"

static double *linear_upper_bounds(size_t num, double size) {
  double *linear_upper_bounds = calloc(num, sizeof(*linear_upper_bounds));
  for (size_t i = 0; i + 1 < num; i++)
    linear_upper_bounds[i] = (i + 1) * size;
  linear_upper_bounds[num - 1] = INFINITY;
  return linear_upper_bounds;
}

static double *exponential_upper_bounds(size_t num, double base,
                                        double factor) {
  double *exponential_upper_bounds =
      calloc(num, sizeof(*exponential_upper_bounds));
  exponential_upper_bounds[0] = factor;
  for (size_t i = 1; i + 1 < num; i++)
    exponential_upper_bounds[i] =
        factor * pow(base, i); // exponential_upper_bounds[i - 1] * base;
  exponential_upper_bounds[num - 1] = INFINITY;
  return exponential_upper_bounds;
}

DEF_TEST(distribution_new_linear) {
  struct {
    size_t num_buckets;
    double size;
    double *want_get;
    int want_err;
  } cases[] = {
      {
          .num_buckets = 0,
          .size = 5,
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .num_buckets = 3,
          .size = -5,
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .num_buckets = 5,
          .size = 0,
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .num_buckets = 3,
          .size = 2.5,
          .want_get = linear_upper_bounds(3, 2.5),
      },
      {
          .num_buckets = 5,
          .size = 5.75,
          .want_get = linear_upper_bounds(5, 5.75),
      },
      {
          .num_buckets = 151,
          .size = 0.7,
          .want_get = linear_upper_bounds(151, 0.7),
      },
      {
          .num_buckets = 111,
          .size = 1074,
          .want_get = linear_upper_bounds(111, 1074),
      },
      {
          .num_buckets = 77,
          .size = 1.0 / 3.0,
          .want_get = linear_upper_bounds(77, 1.0 / 3.0),
      },
      {
          .num_buckets = 1,
          .size = 100,
          .want_get = linear_upper_bounds(1, 100),
      },
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    printf("## Case %zu:\n", i);
    if (cases[i].want_err != 0) {
      EXPECT_EQ_PTR(
          cases[i].want_get,
          distribution_new_linear(cases[i].num_buckets, cases[i].size));
      EXPECT_EQ_INT(cases[i].want_err, errno);
      continue;
    }
    distribution_t *d;
    CHECK_NOT_NULL(
        d = distribution_new_linear(cases[i].num_buckets, cases[i].size));
    buckets_array_t buckets_array = get_buckets(d);
    for (size_t j = 0; j < cases[i].num_buckets; j++) {
      EXPECT_EQ_DOUBLE(cases[i].want_get[j], buckets_array.buckets[j].maximum);
    }
    destroy_buckets_array(buckets_array);
    distribution_destroy(d);
  }
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    free(cases[i].want_get);
  }
  return 0;
}

DEF_TEST(distribution_new_exponential) {
  struct {
    size_t num_buckets;
    double factor;
    double base;
    double *want_get;
    int want_err;
  } cases[] = {
      {
          .num_buckets = 0,
          .factor = 5,
          .base = 8,
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .num_buckets = 5,
          .factor = 0.2,
          .base = -1,
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .num_buckets = 8,
          .factor = 100,
          .base = 0.5,
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .num_buckets = 100,
          .factor = 5.87,
          .base = 1,
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .num_buckets = 6,
          .factor = 0,
          .base = 9.005,
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .num_buckets = 16,
          .factor = -153,
          .base = 1.41,
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .num_buckets = 1,
          .factor = 10,
          .base = 1.05,
          .want_get = exponential_upper_bounds(1, 10, 1.05),
      },
      {
          .num_buckets = 63,
          .factor = 1,
          .base = 2,
          .want_get = exponential_upper_bounds(63, 2, 1),
      },
      {
          .num_buckets = 600,
          .factor = 0.55,
          .base = 1.055,
          .want_get = exponential_upper_bounds(600, 1.055, 0.55),
      },
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    printf("## Case %zu:\n", i);
    if (cases[i].want_err != 0) {
      EXPECT_EQ_PTR(cases[i].want_get,
                    distribution_new_exponential(
                        cases[i].num_buckets, cases[i].base, cases[i].factor));
      EXPECT_EQ_INT(cases[i].want_err, errno);
      continue;
    }
    distribution_t *d;
    CHECK_NOT_NULL(d = distribution_new_exponential(
                       cases[i].num_buckets, cases[i].base, cases[i].factor));
    buckets_array_t buckets_array = get_buckets(d);
    for (size_t j = 0; j < cases[i].num_buckets; j++) {
      EXPECT_EQ_DOUBLE(cases[i].want_get[j], buckets_array.buckets[j].maximum);
    }
    destroy_buckets_array(buckets_array);
    distribution_destroy(d);
  }
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    free(cases[i].want_get);
  }
  return 0;
}

DEF_TEST(distribution_new_custom) {
  struct {
    size_t array_size;
    double *custom_boundaries;
    double *want_get;
    int want_err;
  } cases[] = {
      {
          .array_size = 0,
          .want_get = (double[]){INFINITY},
      },
      {
          .array_size = 5,
          .custom_boundaries = (double[]){0, 1, 2, 3, 4},
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .array_size = 3,
          .custom_boundaries = (double[]){-5, 7, 3},
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .array_size = 4,
          .custom_boundaries = (double[]){5.7, 6.0, 6.0, 7.0},
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .array_size = 1,
          .custom_boundaries = (double[]){105.055},
          .want_get = (double[]){105.055, INFINITY},
      },
      {
          .array_size = 5,
          .custom_boundaries = (double[]){8, 100, 1000, 1008, INFINITY},
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .array_size = 7,
          .custom_boundaries = (double[]){2, 4, 8, 6, 2, 16, 77.5},
          .want_get = NULL,
          .want_err = EINVAL,
      },
      {
          .array_size = 10,
          .custom_boundaries = (double[]){77.5, 100.203, 122.01, 137.23, 200,
                                          205, 210, 220, 230, 256},
          .want_get = (double[]){77.5, 100.203, 122.01, 137.23, 200, 205, 210,
                                 220, 230, 256, INFINITY},
      },
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    if (cases[i].want_err != 0) {
      EXPECT_EQ_PTR(cases[i].want_get,
                    distribution_new_custom(cases[i].array_size,
                                            cases[i].custom_boundaries));
      EXPECT_EQ_INT(cases[i].want_err, errno);
      continue;
    }
    distribution_t *d;
    CHECK_NOT_NULL(d = distribution_new_custom(cases[i].array_size,
                                               cases[i].custom_boundaries));
    buckets_array_t buckets_array = get_buckets(d);
    for (size_t j = 0; j < cases[i].array_size + 1; j++) {
      EXPECT_EQ_DOUBLE(cases[i].want_get[j], buckets_array.buckets[j].maximum);
    }
    destroy_buckets_array(buckets_array);
    distribution_destroy(d);
  }
  return 0;
}

DEF_TEST(update) {
  struct {
    distribution_t *dist;
    size_t num_gauges;
    double *gauges;
    uint64_t *want_counters;
    int want_err;
  } cases[] = {
      {
          .dist = distribution_new_linear(6, 5),
          .num_gauges = 10,
          .gauges = (double[]){25, 30, 5, 7, 11, 10.5, 8.03, 1112.4, 35, 12.7},
          .want_counters = (uint64_t[]){0, 3, 3, 0, 0, 4},
      },
      {
          .dist = distribution_new_exponential(4, 1.41, 1),
          .num_gauges = 0,
          .gauges = NULL,
          .want_counters = (uint64_t[]){0, 0, 0, 0},
      },
      {
          .dist = distribution_new_exponential(5, 2, 3),
          .num_gauges = 5,
          .gauges = (double[]){1, 7, 3, 10, 77},
          .want_counters = (uint64_t[]){1, 1, 2, 0, 1},
      },
      {
          .dist = distribution_new_linear(100, 22),
          .num_gauges = 3,
          .gauges = (double[]){1000, 2, -8},
          .want_err = EINVAL,
      },
      {
          .dist = distribution_new_custom(3, (double[]){5, 20, 35}),
          .num_gauges = 7,
          .gauges = (double[]){7.05, 22.37, 40.83, 90.55, 12.34, 14.2, 6.0},
          .want_counters = (uint64_t[]){0, 4, 1, 2},
      },
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    for (size_t j = 0; j < cases[i].num_gauges; j++) {
      distribution_update(cases[i].dist, cases[i].gauges[j]);
    }
    if (cases[i].want_err != 0) {
      EXPECT_EQ_INT(cases[i].want_err, errno);
      distribution_destroy(cases[i].dist);
      continue;
    }
    buckets_array_t buckets_array = get_buckets(cases[i].dist);
    for (size_t j = 0; j < buckets_array.num_buckets; j++) {
      EXPECT_EQ_INT(cases[i].want_counters[j],
                    buckets_array.buckets[j].bucket_counter);
    }
    destroy_buckets_array(buckets_array);
    distribution_destroy(cases[i].dist);
  }
  return 0;
}

DEF_TEST(average) {
  struct {
    distribution_t *dist;
    size_t num_gauges;
    double *update_gauges;
    double want_average;
  } cases[] = {
      {
          .dist = distribution_new_linear(6, 2),
          .num_gauges = 0,
          .want_average = NAN,
      },
      {
          .dist = distribution_new_linear(7, 10),
          .num_gauges = 5,
          .update_gauges = (double[]){3, 2, 5.7, 22.3, 7.5},
          .want_average = 40.5 / 5.0,
      },
      {
          .dist = distribution_new_exponential(10, 2, 0.75),
          .num_gauges = 8,
          .update_gauges = (double[]){2, 4, 6, 8, 22, 11, 77, 1005},
          .want_average = 1135.0 / 8.0,
      },
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    for (size_t j = 0; j < cases[i].num_gauges; j++) {
      distribution_update(cases[i].dist, cases[i].update_gauges[j]);
    }
    EXPECT_EQ_DOUBLE(cases[i].want_average,
                     distribution_average(cases[i].dist));
    /* Check it second time for deadlocks. */
    EXPECT_EQ_DOUBLE(cases[i].want_average,
                     distribution_average(cases[i].dist));
    distribution_destroy(cases[i].dist);
  }
  return 0;
}

DEF_TEST(percentile) {
  struct {
    distribution_t *dist;
    size_t num_gauges;
    double *update_gauges;
    double percent;
    double want_percentile;
    int want_err;
  } cases[] = {
      {
          .dist = distribution_new_linear(5, 7),
          .num_gauges = 1,
          .update_gauges = (double[]){4},
          .percent = 105,
          .want_percentile = NAN,
          .want_err = EINVAL,
      },
      {
          .dist = distribution_new_linear(8, 10),
          .num_gauges = 0,
          .percent = 20,
          .want_percentile = NAN,
      },
      {
          .dist = distribution_new_exponential(5, 2, 0.2),
          .num_gauges = 2,
          .update_gauges = (double[]){4, 30.08},
          .percent = -5,
          .want_percentile = NAN,
          .want_err = EINVAL,
      },
      {
          .dist = distribution_new_exponential(10, 2, 0.75),
          .num_gauges = 8,
          .update_gauges = (double[]){2, 4, 6, 8, 22, 11, 77, 1005},
          .percent = 50,
          .want_percentile = 12,
      },
      {
          .dist = distribution_new_custom(3, (double[]){5, 20, 35}),
          .num_gauges = 7,
          .update_gauges = (double[]){5.5, 10.5, 11.3, 6.7, 24.7, 40.05, 35},
          .percent = 4.0 / 7.0 * 100,
          .want_percentile = 20,
      },
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    for (size_t j = 0; j < cases[i].num_gauges; j++) {
      distribution_update(cases[i].dist, cases[i].update_gauges[j]);
    }
    EXPECT_EQ_DOUBLE(cases[i].want_percentile,
                     distribution_percentile(cases[i].dist, cases[i].percent));
    /* Check it second time for deadlocks. */
    EXPECT_EQ_DOUBLE(cases[i].want_percentile,
                     distribution_percentile(cases[i].dist, cases[i].percent));
    if (cases[i].want_err != 0)
      EXPECT_EQ_INT(cases[i].want_err, errno);
    distribution_destroy(cases[i].dist);
  }
  return 0;
}

DEF_TEST(clone) {
  distribution_t *dist = distribution_new_linear(5, 7);
  distribution_update(dist, 5);
  distribution_update(dist, 7);
  distribution_t *clone = distribution_clone(dist);
  EXPECT_EQ_INT(distribution_num_buckets(clone), 5);
  EXPECT_EQ_DOUBLE(distribution_percentile(clone, 50), 7);
  distribution_update(dist, 10);
  EXPECT_EQ_DOUBLE(distribution_percentile(clone, 50), 7);
  EXPECT_EQ_DOUBLE(distribution_percentile(dist, 50), 14);
  distribution_update(clone, 2);
  EXPECT_EQ_DOUBLE(distribution_percentile(dist, 50), 14);
  distribution_destroy(dist);
  distribution_update(clone, 25);
  distribution_update(clone, 200);
  EXPECT_EQ_DOUBLE(distribution_percentile(clone, 80), 28);
  distribution_destroy(clone);
  return 0;
}

DEF_TEST(getters) {
  struct {
    distribution_t *dist;
    size_t num_buckets;
    size_t num_gauges;
    double *update_gauges;
    uint64_t want_counter;
    double want_total_sum;
    double want_average;
    double want_squared_deviation_sum;
  } cases[] = {
      {
          .dist = distribution_new_linear(5, 7),
          .num_buckets = 5,
          .num_gauges = 1,
          .update_gauges = (double[]){4},
          .want_counter = 1,
          .want_total_sum = 4,
          .want_average = 4,
          .want_squared_deviation_sum = 0,
      },
      {
          .dist = distribution_new_exponential(5, 2, 1),
          .num_buckets = 5,
          .num_gauges = 7,
          .update_gauges = (double[]){5, 17.4, 22.3, 11, 0.5, 13, 11},
          .want_counter = 7,
          .want_total_sum = 80.2,
          .want_average = 80.2 / 7,
          .want_squared_deviation_sum =
              7 * 80.2 / 7 * 80.2 / 7 - 2 * 80.2 / 7 * 80.2 + 1236.3,
      },
      {
          .dist = distribution_new_linear(100, 10),
          .num_buckets = 100,
          .num_gauges = 10,
          .update_gauges = (double[]){5, 11.23, 29.48, 66.77, 11.22, 33.21, 55,
                                      26.27, 96, 2},
          .want_counter = 10,
          .want_total_sum = 336.18,
          .want_average = 33.618,
          .want_squared_deviation_sum = 10 * 336.18 / 10 * 336.18 / 10 -
                                        2 * 336.18 / 10 * 336.18 + 19642.3216,
      },
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    EXPECT_EQ_INT(distribution_num_buckets(cases[i].dist),
                  cases[i].num_buckets);
    for (size_t j = 0; j < cases[i].num_gauges; j++) {
      distribution_update(cases[i].dist, cases[i].update_gauges[j]);
    }
    EXPECT_EQ_INT(distribution_total_counter(cases[i].dist),
                  cases[i].want_counter);
    EXPECT_EQ_DOUBLE(distribution_total_sum(cases[i].dist),
                     cases[i].want_total_sum);
    EXPECT_EQ_DOUBLE(distribution_average(cases[i].dist),
                     cases[i].want_average);
    EXPECT_EQ_DOUBLE(distribution_squared_deviation_sum(cases[i].dist),
                     cases[i].want_squared_deviation_sum);

    distribution_destroy(cases[i].dist);
  }
  return 0;
}
int main() {
  RUN_TEST(distribution_new_linear);
  RUN_TEST(distribution_new_exponential);
  RUN_TEST(distribution_new_custom);
  RUN_TEST(update);
  RUN_TEST(average);
  RUN_TEST(percentile);
  RUN_TEST(clone);
  RUN_TEST(getters);
  END_TEST;
}
