/**
 * collectd - utils_backoff.h
 * Copyright (C) 2017       Florian octo Forster
 *
 * MIT license
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#ifndef UTILS_BACKOFF_H
#define UTILS_BACKOFF_H 1

#include "utils_time.h"

typedef struct {
  cdtime_t base;
  cdtime_t max;

  pthread_mutex_t lock;
  cdtime_t interval;
  cdtime_t retry_time;
} backoff_t;

void backoff_init(backoff_t *bo, cdtime_t base, cdtime_t max);
void backoff_destroy(backoff_t *bo);

/* backoff_check checks the status of a callback. If the callback is in a good
 * state and should be called, it returns zero. If the callback is in a bad
 * state and should be skipped, it returns an error code. */
int backoff_check(backoff_t *bo);

/* backoff_update tracks successes and failures from callbacks and manages an
 * exponential backoff. When status is zero (success), the exponential backoff
 * is reset, otherwise it's increased. */
void backoff_update(backoff_t *bo, int status);

#endif /* UTILS_BACKOFF_H */
