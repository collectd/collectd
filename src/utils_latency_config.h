/**
 * collectd - src/utils_latency_config.c
 * Copyright (C) 2013-2016   Florian octo Forster
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
 *   Pavel Rochnyack <pavel2000 at ngs.ru>
 */

#ifndef UTILS_LATENCY_CONFIG_H
#define UTILS_LATENCY_CONFIG_H 1

#include "collectd.h"
#include "utils_time.h"

struct latency_config_s
{
  double *percentile;
  size_t percentile_num;
  char   *percentile_type;
  cdtime_t *rates;
  size_t   rates_num;
  char     *rates_type;
  _Bool lower;
  _Bool upper;
  //_Bool sum;
  _Bool avg;
  //_Bool count;
};
typedef struct latency_config_s latency_config_t;


int latency_config_add_percentile (const char *plugin, latency_config_t *cl,
    oconfig_item_t *ci);

int latency_config_add_rate (const char *plugin, latency_config_t *cl,
    oconfig_item_t *ci);

int latency_config_copy (latency_config_t *dst, const latency_config_t src);

void latency_config_free (latency_config_t lc);

#endif /* UTILS_LATENCY_CONFIG_H */
