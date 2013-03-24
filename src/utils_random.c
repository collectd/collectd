/**
 * collectd - src/utils_random.c
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
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "utils_time.h"

#include <pthread.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static _Bool have_seed = 0;
static unsigned short seed[3];

static void cdrand_seed (void)
{
  cdtime_t t;

  if (have_seed)
    return;

  t = cdtime();

  seed[0] = (unsigned short) t;
  seed[1] = (unsigned short) (t >> 16);
  seed[2] = (unsigned short) (t >> 32);

  have_seed = 1;
}

double cdrand_d (void)
{
  double r;

  pthread_mutex_lock (&lock);
  cdrand_seed ();
  r = erand48 (seed);
  pthread_mutex_unlock (&lock);

  return (r);
}

long cdrand_range (long min, long max)
{
  long range;
  long r;

  range = 1 + max - min;

  r = (long) (0.5 + (cdrand_d () * range));
  r += min;

  return (r);
}
