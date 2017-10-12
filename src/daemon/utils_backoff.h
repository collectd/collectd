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

/* backoff_t holds the state for an exponential back-off.
 *
 * The functions in this module are meant to be used like this:
 *
 *    if (backoff_check(&bo) != 0) {
 *      continue;
 *    }
 *    int status = protected_function();
 *    backoff_update(&bo, status);
 *
 * After an initial failure is reported via backoff_update(), backoff_check()
 * will return non-zero for a random duration in the [base,2*base] range. The
 * bounds are doubled after each failure until they reach [max/2,max].
 *
 * Additional failures reported to backoff_update() before the end of that
 * duration is reached are discarded, because initially many threads may return
 * errors almost simultaneously.
 *
 * Once the end of the duration is reached, backoff_check() will return zero
 * exactly once, so that *one* thread proceeds to call protected_function(). If
 * that "canary" thread signals success, backoff_check() will return zero for
 * all threads again. Otherwise, the back-off is increased as discussed above.
 *
 * It is important that every call to backoff_check() is matched with a call to
 * backoff_update(), otherwise the assumptions made in these functions don't
 * hold: if the canary thread never reports back, calls will be blocked
 * indefinitely.
 */
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
