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
    }    
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) { 
    printf("## Case %zu:\n", i);
    if (cases[i].want_err != 0) {
      EXPECT_EQ_PTR(cases[i].want_get, distribution_new_linear(cases[i].num_buckets, cases[i].size));
      EXPECT_EQ_INT(cases[i].want_err, errno);
      continue;
    }
    distribution_t *d;
    CHECK_NOT_NULL(d = distribution_new_linear(cases[i].num_buckets, cases[i].size));
    buckets_array_t buckets_array = get_buckets(d);
    for (size_t j = 0; j < cases[i].num_buckets; j++) {
      EXPECT_EQ_DOUBLE(cases[i].want_get[j], buckets_array.buckets[j].maximum);
    }  
    distribution_destroy(d);
  }  
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    free(cases[i].want_get);
  }
  return 0;
}

int main() {
  RUN_TEST(distribution_new_linear);
  END_TEST;
}
