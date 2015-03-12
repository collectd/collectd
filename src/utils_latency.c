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
#include "plugin.h"
#include "utils_latency.h"
#include "common.h"

#include <math.h>

#ifndef HISTOGRAM_NUM_BINS
# define HISTOGRAM_NUM_BINS 1000
#endif

static const int HISTOGRAM_DEFAULT_BIN_WIDTH = 1;

struct latency_counter_s
{
  cdtime_t start_time;

  cdtime_t sum;
  size_t num;

  cdtime_t min;
  cdtime_t max;

  int bin_width;
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
*
*/
void change_bin_width (latency_counter_t *lc, size_t val) /* {{{ */
{
  int i=0;
  /* This function is called because the new value is above histogram's range.
   * First find the required bin width:
   *           requiredBinWidth = (value + 1) / numBins
   * then get the next nearest power of 2
   *           newBinWidth = 2^(ceil(log2(requiredBinWidth)))
   */
  double required_bin_width = (double)(val + 1) / HISTOGRAM_NUM_BINS;
  double required_bin_width_logbase2 = log(required_bin_width) / log(2.0);
  int new_bin_width = (int)(pow(2.0, ceil( required_bin_width_logbase2)));
  int old_bin_width = lc->bin_width;
  lc->bin_width = new_bin_width;

  /*
   * bin width has been increased, now iterate through all bins and move the
   * old bin's count to new bin.
   */
  if (lc->num > 0) // if the histogram has data then iterate else skip
  {
      double width_change_ratio = old_bin_width / new_bin_width;
      for (i=0; i<HISTOGRAM_NUM_BINS; i++)
      {
         int new_bin = (int)(i * width_change_ratio);
         if (i == new_bin)
             continue;
         lc->histogram[new_bin] += lc->histogram[i];
         lc->histogram[i] = 0;
      }
      DEBUG("utils_latency: change_bin_width: fixed all bins");
  }

  DEBUG("utils_latency: change_bin_width: val-[%zu], oldBinWidth-[%d], "
          "newBinWidth-[%d], required_bin_width-[%f], "
          "required_bin_width_logbase2-[%f]",
          val, old_bin_width, new_bin_width, required_bin_width,
          required_bin_width_logbase2);

} /* }}} void change_bin_width */

latency_counter_t *latency_counter_create () /* {{{ */
{
  latency_counter_t *lc;

  lc = malloc (sizeof (*lc));
  if (lc == NULL)
    return (NULL);

  latency_counter_reset (lc);
  lc->bin_width = HISTOGRAM_DEFAULT_BIN_WIDTH;
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

  int bin = (int)(latency_ms / lc->bin_width);
  if (bin >= HISTOGRAM_NUM_BINS)
  {
      change_bin_width(lc, latency_ms);
      bin = (int)(latency_ms / lc->bin_width);
      if (bin >= HISTOGRAM_NUM_BINS)
      {
          ERROR("utils_latency: latency_counter_add: Invalid bin %d", bin);
          return;
      }
  }
  lc->histogram[bin]++;
} /* }}} void latency_counter_add */

void latency_counter_reset (latency_counter_t *lc) /* {{{ */
{
  if (lc == NULL)
    return;

  int bin_width = lc->bin_width;
  memset (lc, 0, sizeof (*lc));

  /* preserve bin width */
  lc->bin_width = bin_width;
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
  for (i = 0; i < HISTOGRAM_NUM_BINS; i++)
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

  if (i >= HISTOGRAM_NUM_BINS)
    return (0);

  assert (percent_upper >= percent);
  assert (percent_lower < percent);

  ms_upper = (double) ( (i + 1) * lc->bin_width );
  ms_lower = (double) ( i * lc->bin_width );
  if (i == 0)
    return (MS_TO_CDTIME_T (ms_upper));

  ms_interpolated = (((percent_upper - percent) * ms_lower)
      + ((percent - percent_lower) * ms_upper))
    / (percent_upper - percent_lower);

  return (MS_TO_CDTIME_T (ms_interpolated));
} /* }}} cdtime_t latency_counter_get_percentile */

/* vim: set sw=2 sts=2 et fdm=marker : */
