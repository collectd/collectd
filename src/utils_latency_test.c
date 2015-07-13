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

#define DBL_PRECISION 1e-9

#include "testing.h"
#include "collectd.h"
#include "common.h" /* for STATIC_ARRAY_SIZE */
#include "utils_time.h"
#include "utils_latency.h"

DEF_TEST(simple)
{
  struct {
    double val;
    double min;
    double max;
    double sum;
    double avg;
  } cases[] = {
  /* val  min  max  sum  avg */
    {0.5, 0.5, 0.5, 0.5, 0.5},
    {0.3, 0.3, 0.5, 0.8, 0.4},
    {0.7, 0.3, 0.7, 1.5, 0.5},
    {2.5, 0.3, 2.5, 4.0, 1.0},
    { -1, 0.3, 2.5, 4.0, 1.0},
  };
  size_t i;
  latency_counter_t *l;

  CHECK_NOT_NULL (l = latency_counter_create ());

  for (i = 0; i < STATIC_ARRAY_SIZE (cases); i++) {
    latency_counter_add (l, DOUBLE_TO_CDTIME_T (cases[i].val));

    DBLEQ (cases[i].min, CDTIME_T_TO_DOUBLE (latency_counter_get_min (l)));
    DBLEQ (cases[i].max, CDTIME_T_TO_DOUBLE (latency_counter_get_max (l)));
    DBLEQ (cases[i].sum, CDTIME_T_TO_DOUBLE (latency_counter_get_sum (l)));
    DBLEQ (cases[i].avg, CDTIME_T_TO_DOUBLE (latency_counter_get_average (l)));
  }

  latency_counter_destroy (l);
  return 0;
}

DEF_TEST(percentile)
{
  size_t i;
  latency_counter_t *l;

  CHECK_NOT_NULL (l = latency_counter_create ());

  for (i = 0; i < 100; i++) {
    latency_counter_add (l, TIME_T_TO_CDTIME_T (((time_t) i) + 1));
  }

  DBLEQ (  1.0, CDTIME_T_TO_DOUBLE (latency_counter_get_min (l)));
  DBLEQ (100.0, CDTIME_T_TO_DOUBLE (latency_counter_get_max (l)));
  DBLEQ (100.0 * 101.0 / 2.0, CDTIME_T_TO_DOUBLE (latency_counter_get_sum (l)));
  DBLEQ ( 50.5, CDTIME_T_TO_DOUBLE (latency_counter_get_average (l)));

  DBLEQ (50.0, CDTIME_T_TO_DOUBLE (latency_counter_get_percentile (l, 50.0)));
  DBLEQ (80.0, CDTIME_T_TO_DOUBLE (latency_counter_get_percentile (l, 80.0)));
  DBLEQ (95.0, CDTIME_T_TO_DOUBLE (latency_counter_get_percentile (l, 95.0)));
  DBLEQ (99.0, CDTIME_T_TO_DOUBLE (latency_counter_get_percentile (l, 99.0)));

  CHECK_ZERO (latency_counter_get_percentile (l, -1.0));
  CHECK_ZERO (latency_counter_get_percentile (l, 101.0));

  latency_counter_destroy (l);
  return 0;
}

int main (void)
{
  RUN_TEST(simple);
  RUN_TEST(percentile);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
