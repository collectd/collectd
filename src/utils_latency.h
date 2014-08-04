/**
 * collectd - src/utils_latency.h
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
#include "utils_time.h"

struct latency_counter_s;
typedef struct latency_counter_s latency_counter_t;

latency_counter_t *latency_counter_create ();
void latency_counter_destroy (latency_counter_t *lc);

void latency_counter_add (latency_counter_t *lc, cdtime_t latency);
void latency_counter_reset (latency_counter_t *lc);

cdtime_t latency_counter_get_min (latency_counter_t *lc);
cdtime_t latency_counter_get_max (latency_counter_t *lc);
cdtime_t latency_counter_get_sum (latency_counter_t *lc);
size_t   latency_counter_get_num (latency_counter_t *lc);
cdtime_t latency_counter_get_average (latency_counter_t *lc);
cdtime_t latency_counter_get_percentile (latency_counter_t *lc,
    double percent);

/* vim: set sw=2 sts=2 et : */
