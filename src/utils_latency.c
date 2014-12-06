/**
 * collectd - src/utils_latency.c
 * Copyright (C) 2013       Florian Forster
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
 *   Florian Forster <ff at octo.it>
 **/

#include "collectd.h"
#include "utils_latency.h"
#include "common.h"

#ifndef LATENCY_HISTOGRAM_SIZE
# define LATENCY_HISTOGRAM_SIZE 1000
#endif

struct latency_counter_s
{
  cdtime_t start_time;

  cdtime_t sum;
  size_t num;

  cdtime_t min;
  cdtime_t max;

  int histogram[LATENCY_HISTOGRAM_SIZE];
};

latency_counter_t *latency_counter_create () /* {{{ */
{
  latency_counter_t *lc;

  lc = malloc (sizeof (*lc));
  if (lc == NULL)
    return (NULL);

  latency_counter_reset (lc);
  return (lc);
} /* }}} latency_counter_t *latency_counter_create */

void latency_counter_destroy (latency_counter_t *lc) /* {{{ */
{
  sfree (lc);
} /* }}} void latency_counter_destroy */

void latency_counter_add (latency_counter_t *lc, cdtime_t latency) /* {{{ */
{
  size_t latency_ms;

  if ((lc == NULL) || (latency == 0))
    return;

  lc->sum += latency;
  lc->num++;

  if ((lc->min == 0) && (lc->max == 0))
    lc->min = lc->max = latency;
  if (lc->min > latency)
    lc->min = latency;
  if (lc->max < latency)
    lc->max = latency;

  /* A latency of _exactly_ 1.0 ms should be stored in the buffer 0, so
   * subtract one from the cdtime_t value so that exactly 1.0 ms get sorted
   * accordingly. */
  latency_ms = (size_t) CDTIME_T_TO_MS (latency - 1);
  if (latency_ms < STATIC_ARRAY_SIZE (lc->histogram))
    lc->histogram[latency_ms]++;
} /* }}} void latency_counter_add */

void latency_counter_reset (latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return;

  memset (lc, 0, sizeof (*lc));
  lc->start_time = cdtime ();
} /* }}} void latency_counter_reset */

cdtime_t latency_counter_get_min (latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return (0);
  return (lc->min);
} /* }}} cdtime_t latency_counter_get_min */

cdtime_t latency_counter_get_max (latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return (0);
  return (lc->max);
} /* }}} cdtime_t latency_counter_get_max */

cdtime_t latency_counter_get_sum (latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return (0);
  return (lc->sum);
} /* }}} cdtime_t latency_counter_get_sum */

size_t latency_counter_get_num (latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return (0);
  return (lc->num);
} /* }}} size_t latency_counter_get_num */

cdtime_t latency_counter_get_average (latency_counter_t *lc) /* {{{ */
{
  double average;

  if ((lc == NULL) || (lc->num == 0))
    return (0);

  average = CDTIME_T_TO_DOUBLE (lc->sum) / ((double) lc->num);
  return (DOUBLE_TO_CDTIME_T (average));
} /* }}} cdtime_t latency_counter_get_average */

cdtime_t latency_counter_get_percentile (latency_counter_t *lc,
    double percent)
{
  double percent_upper;
  double percent_lower;
  double ms_upper;
  double ms_lower;
  double ms_interpolated;
  int sum;
  size_t i;

  if ((lc == NULL) || (lc->num == 0) || !((percent > 0.0) && (percent < 100.0)))
    return (0);

  /* Find index i so that at least "percent" events are within i+1 ms. */
  percent_upper = 0.0;
  percent_lower = 0.0;
  sum = 0;
  for (i = 0; i < LATENCY_HISTOGRAM_SIZE; i++)
  {
    percent_lower = percent_upper;
    sum += lc->histogram[i];
    if (sum == 0)
      percent_upper = 0.0;
    else
      percent_upper = 100.0 * ((double) sum) / ((double) lc->num);

    if (percent_upper >= percent)
      break;
  }

  if (i >= LATENCY_HISTOGRAM_SIZE)
    return (0);

  assert (percent_upper >= percent);
  assert (percent_lower < percent);

  ms_upper = (double) (i + 1);
  ms_lower = (double) i;
  if (i == 0)
    return (MS_TO_CDTIME_T (ms_upper));

  ms_interpolated = (((percent_upper - percent) * ms_lower)
      + ((percent - percent_lower) * ms_upper))
    / (percent_upper - percent_lower);

  return (MS_TO_CDTIME_T (ms_interpolated));
} /* }}} cdtime_t latency_counter_get_percentile */

/* vim: set sw=2 sts=2 et fdm=marker : */
