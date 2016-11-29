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

#include "common.h"
#include "plugin.h"
#include "utils_latency.h"

#include <limits.h>
#include <math.h>

#ifndef LLONG_MAX
#define LLONG_MAX 9223372036854775807LL
#endif

#ifndef HISTOGRAM_DEFAULT_BIN_WIDTH
/* 1048576 = 2^20 ^= 1/1024 s */
#define HISTOGRAM_DEFAULT_BIN_WIDTH 1048576
#endif

struct latency_counter_s {
  cdtime_t start_time;

  cdtime_t sum;
  size_t num;

  cdtime_t min;
  cdtime_t max;

  cdtime_t bin_width;
  int histogram[HISTOGRAM_NUM_BINS];
};

/*
* Histogram represents the distribution of data, it has a list of "bins".
* Each bin represents an interval and has a count (frequency) of
* number of values fall within its interval.
*
* Histogram's range is determined by the number of bins and the bin width,
* There are 1000 bins and all bins have the same width of default 1 millisecond.
* When a value above this range is added, Histogram's range is increased by
* increasing the bin width (note that number of bins remains always at 1000).
* This operation of increasing bin width is little expensive as each bin need
* to be visited to update it's count. To reduce frequent change of bin width,
* new bin width will be the next nearest power of 2. Example: 2, 4, 8, 16, 32,
* 64, 128, 256, 512, 1024, 2048, 5086, ...
*
* So, if the required bin width is 300, then new bin width will be 512 as it is
* the next nearest power of 2.
*/
static void change_bin_width(latency_counter_t *lc, cdtime_t latency) /* {{{ */
{
  /* This function is called because the new value is above histogram's range.
   * First find the required bin width:
   *           requiredBinWidth = (value + 1) / numBins
   * then get the next nearest power of 2
   *           newBinWidth = 2^(ceil(log2(requiredBinWidth)))
   */
  double required_bin_width =
      ((double)(latency + 1)) / ((double)HISTOGRAM_NUM_BINS);
  double required_bin_width_logbase2 = log(required_bin_width) / log(2.0);
  cdtime_t new_bin_width =
      (cdtime_t)(pow(2.0, ceil(required_bin_width_logbase2)) + .5);
  cdtime_t old_bin_width = lc->bin_width;

  lc->bin_width = new_bin_width;

  /* bin_width has been increased, now iterate through all bins and move the
   * old bin's count to new bin. */
  if (lc->num > 0) // if the histogram has data then iterate else skip
  {
    double width_change_ratio =
        ((double)old_bin_width) / ((double)new_bin_width);

    for (size_t i = 0; i < HISTOGRAM_NUM_BINS; i++) {
      size_t new_bin = (size_t)(((double)i) * width_change_ratio);
      if (i == new_bin)
        continue;
      assert(new_bin < i);

      lc->histogram[new_bin] += lc->histogram[i];
      lc->histogram[i] = 0;
    }
  }

  DEBUG("utils_latency: change_bin_width: latency = %.3f; "
        "old_bin_width = %.3f; new_bin_width = %.3f;",
        CDTIME_T_TO_DOUBLE(latency), CDTIME_T_TO_DOUBLE(old_bin_width),
        CDTIME_T_TO_DOUBLE(new_bin_width));
} /* }}} void change_bin_width */

latency_counter_t *latency_counter_create(void) /* {{{ */
{
  latency_counter_t *lc;

  lc = calloc(1, sizeof(*lc));
  if (lc == NULL)
    return (NULL);

  lc->bin_width = HISTOGRAM_DEFAULT_BIN_WIDTH;
  latency_counter_reset(lc);
  return (lc);
} /* }}} latency_counter_t *latency_counter_create */

void latency_counter_destroy(latency_counter_t *lc) /* {{{ */
{
  sfree(lc);
} /* }}} void latency_counter_destroy */

void latency_counter_add(latency_counter_t *lc, cdtime_t latency) /* {{{ */
{
  cdtime_t bin;

  if ((lc == NULL) || (latency == 0) || (latency > ((cdtime_t)LLONG_MAX)))
    return;

  lc->sum += latency;
  lc->num++;

  if ((lc->min == 0) && (lc->max == 0))
    lc->min = lc->max = latency;
  if (lc->min > latency)
    lc->min = latency;
  if (lc->max < latency)
    lc->max = latency;

  /* A latency of _exactly_ 1.0 ms is stored in the buffer 0, so
   * subtract one from the cdtime_t value so that exactly 1.0 ms get sorted
   * accordingly. */
  bin = (latency - 1) / lc->bin_width;
  if (bin >= HISTOGRAM_NUM_BINS) {
    change_bin_width(lc, latency);
    bin = (latency - 1) / lc->bin_width;
    if (bin >= HISTOGRAM_NUM_BINS) {
      ERROR("utils_latency: latency_counter_add: Invalid bin: %" PRIu64, bin);
      return;
    }
  }
  lc->histogram[bin]++;
} /* }}} void latency_counter_add */

void latency_counter_reset(latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return;

  cdtime_t bin_width = lc->bin_width;
  cdtime_t max_bin = (lc->max - 1) / lc->bin_width;

/*
  If max latency is REDUCE_THRESHOLD times less than histogram's range,
  then cut it in half. REDUCE_THRESHOLD must be >= 2.
  Value of 4 is selected to reduce frequent changes of bin width.
*/
#define REDUCE_THRESHOLD 4
  if ((lc->num > 0) && (lc->bin_width >= HISTOGRAM_DEFAULT_BIN_WIDTH * 2) &&
      (max_bin < HISTOGRAM_NUM_BINS / REDUCE_THRESHOLD)) {
    /* new bin width will be the previous power of 2 */
    bin_width = bin_width / 2;

    DEBUG("utils_latency: latency_counter_reset: max_latency = %.3f; "
          "max_bin = %" PRIu64 "; old_bin_width = %.3f; new_bin_width = %.3f;",
          CDTIME_T_TO_DOUBLE(lc->max), max_bin,
          CDTIME_T_TO_DOUBLE(lc->bin_width), CDTIME_T_TO_DOUBLE(bin_width));
  }

  memset(lc, 0, sizeof(*lc));

  /* preserve bin width */
  lc->bin_width = bin_width;
  lc->start_time = cdtime();
} /* }}} void latency_counter_reset */

cdtime_t latency_counter_get_min(latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return (0);
  return (lc->min);
} /* }}} cdtime_t latency_counter_get_min */

cdtime_t latency_counter_get_max(latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return (0);
  return (lc->max);
} /* }}} cdtime_t latency_counter_get_max */

cdtime_t latency_counter_get_sum(latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return (0);
  return (lc->sum);
} /* }}} cdtime_t latency_counter_get_sum */

size_t latency_counter_get_num(latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return (0);
  return (lc->num);
} /* }}} size_t latency_counter_get_num */

cdtime_t latency_counter_get_average(latency_counter_t *lc) /* {{{ */
{
  double average;

  if ((lc == NULL) || (lc->num == 0))
    return (0);

  average = CDTIME_T_TO_DOUBLE(lc->sum) / ((double)lc->num);
  return (DOUBLE_TO_CDTIME_T(average));
} /* }}} cdtime_t latency_counter_get_average */

cdtime_t latency_counter_get_percentile(latency_counter_t *lc, /* {{{ */
                                        double percent) {
  double percent_upper;
  double percent_lower;
  double p;
  cdtime_t latency_lower;
  cdtime_t latency_interpolated;
  int sum;
  size_t i;

  if ((lc == NULL) || (lc->num == 0) || !((percent > 0.0) && (percent < 100.0)))
    return (0);

  /* Find index i so that at least "percent" events are within i+1 ms. */
  percent_upper = 0.0;
  percent_lower = 0.0;
  sum = 0;
  for (i = 0; i < HISTOGRAM_NUM_BINS; i++) {
    percent_lower = percent_upper;
    sum += lc->histogram[i];
    if (sum == 0)
      percent_upper = 0.0;
    else
      percent_upper = 100.0 * ((double)sum) / ((double)lc->num);

    if (percent_upper >= percent)
      break;
  }

  if (i >= HISTOGRAM_NUM_BINS)
    return (0);

  assert(percent_upper >= percent);
  assert(percent_lower < percent);

  if (i == 0)
    return (lc->bin_width);

  latency_lower = ((cdtime_t)i) * lc->bin_width;
  p = (percent - percent_lower) / (percent_upper - percent_lower);

  latency_interpolated =
      latency_lower + DOUBLE_TO_CDTIME_T(p * CDTIME_T_TO_DOUBLE(lc->bin_width));

  DEBUG("latency_counter_get_percentile: latency_interpolated = %.3f",
        CDTIME_T_TO_DOUBLE(latency_interpolated));
  return (latency_interpolated);
} /* }}} cdtime_t latency_counter_get_percentile */

double latency_counter_get_rate(const latency_counter_t *lc, /* {{{ */
                                cdtime_t lower, cdtime_t upper,
                                const cdtime_t now) {
  if ((lc == NULL) || (lc->num == 0))
    return (NAN);

  if (upper && (upper < lower))
    return (NAN);
  if (lower == upper)
    return (0);

  /* Buckets have an exclusive lower bound and an inclusive upper bound. That
   * means that the first bucket, index 0, represents (0-bin_width]. That means
   * that latency==bin_width needs to result in bin=0, that's why we need to
   * subtract one before dividing by bin_width. */
  cdtime_t lower_bin = 0;
  if (lower)
    /* lower is *exclusive* => determine bucket for lower+1 */
    lower_bin = ((lower + 1) - 1) / lc->bin_width;

  /* lower is greater than the longest latency observed => rate is zero. */
  if (lower_bin >= HISTOGRAM_NUM_BINS)
    return (0);

  cdtime_t upper_bin = HISTOGRAM_NUM_BINS - 1;
  if (upper)
    upper_bin = (upper - 1) / lc->bin_width;

  if (upper_bin >= HISTOGRAM_NUM_BINS) {
    upper_bin = HISTOGRAM_NUM_BINS - 1;
    upper = 0;
  }

  double sum = 0;
  for (size_t i = lower_bin; i <= upper_bin; i++)
    sum += lc->histogram[i];

  if (lower) {
    /* Approximate ratio of requests in lower_bin, that fall between
     * lower_bin_boundary and lower. This ratio is then subtracted from sum to
     * increase accuracy. */
    cdtime_t lower_bin_boundary = lower_bin * lc->bin_width;
    assert(lower >= lower_bin_boundary);
    double lower_ratio =
        (double)(lower - lower_bin_boundary) / ((double)lc->bin_width);
    sum -= lower_ratio * lc->histogram[lower_bin];
  }

  if (upper) {
    /* As above: approximate ratio of requests in upper_bin, that fall between
     * upper and upper_bin_boundary. */
    cdtime_t upper_bin_boundary = (upper_bin + 1) * lc->bin_width;
    assert(upper <= upper_bin_boundary);
    double ratio = (double)(upper_bin_boundary - upper) / (double)lc->bin_width;
    sum -= ratio * lc->histogram[upper_bin];
  }

  return sum / (CDTIME_T_TO_DOUBLE(now - lc->start_time));
} /* }}} double latency_counter_get_rate */

/* vim: set sw=2 sts=2 et fdm=marker : */
