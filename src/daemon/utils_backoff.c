/**
 * collectd - utils_backoff.c
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

#include "collectd.h"

#include "utils_backoff.h"
#include "utils_random.h"

#include <pthread.h>

void backoff_init(backoff_t *bo, cdtime_t base, cdtime_t max) {
  *bo = (backoff_t){
      .base = base, .max = max,
  };
  pthread_mutex_init(&bo->lock, NULL);
}

void backoff_destroy(backoff_t *bo) { pthread_mutex_destroy(&bo->lock); }

int backoff_check(backoff_t *bo) {
  pthread_mutex_lock(&bo->lock);

  if (bo->interval == 0) {
    /* callback is healthy */
    pthread_mutex_unlock(&bo->lock);
    return 1;
  }

  if (bo->retry_time == 0) {
    /* another thread is currently retrying */
    pthread_mutex_unlock(&bo->lock);
    return 0;
  }

  cdtime_t now = cdtime();
  if (now >= bo->retry_time) {
    /* this thread is retrying this callback */
    bo->retry_time = 0;
    pthread_mutex_unlock(&bo->lock);
    return 1;
  }

  /* still in failure mode */
  pthread_mutex_unlock(&bo->lock);
  return 0;
}

void backoff_update(backoff_t *bo, int status) {
  pthread_mutex_lock(&bo->lock);

  /* success: clear failures. */
  if (status == 0) {
    bo->interval = 0;
    bo->retry_time = 0;
    pthread_mutex_unlock(&bo->lock);
    return;
  }

  /* while bo->retry_time != 0, no (new) threads should call the
   * callback and
   * report a status. It's possible that we get a late status update, though,
   * which we will ignore. */
  if (bo->retry_time != 0) {
    pthread_mutex_unlock(&bo->lock);
    return;
  }

  /* this is the first error. Ensure that retry_interval will be at least
   * base. */
  if (bo->interval == 0) {
    bo->interval = bo->base;
  }

  bo->interval *= 2;
  if (bo->interval > bo->max) {
    bo->interval = bo->max;
  }

  cdtime_t retry_interval =
      (cdtime_t)cdrand_range((long)(bo->interval / 2), (long)bo->interval);
  bo->retry_time = cdtime() + retry_interval;

  pthread_mutex_unlock(&bo->lock);
}
