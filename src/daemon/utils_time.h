/**
 * collectd - src/daemon/utils_time.h
 * Copyright (C) 2010-2015  Florian octo Forster
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
 **/

#ifndef UTILS_TIME_H
#define UTILS_TIME_H 1

#include "collectd.h"

#ifdef TESTING_H
/* cdtime_mock is the time returned by cdtime() when build with
 * -DMOCK_TIME */
extern cdtime_t cdtime_mock;
#endif

/*
 * "cdtime_t" is a 64bit unsigned integer. The time is stored at a 2^-30 second
 * resolution, i.e. the most significant 34 bit are used to store the time in
 * seconds, the least significant bits store the sub-second part in something
 * very close to nanoseconds. *The* big advantage of storing time in this
 * manner is that comparing times and calculating differences is as simple as
 * it is with "time_t", i.e. a simple integer comparison / subtraction works.
 */
/*
 * cdtime_t is defined in "collectd.h" */
/* typedef uint64_t cdtime_t; */

/* 2^30 = 1073741824 */
#define TIME_T_TO_CDTIME_T_STATIC(t) (((cdtime_t)(t)) << 30)
#define TIME_T_TO_CDTIME_T(t)                                                  \
  (cdtime_t) { TIME_T_TO_CDTIME_T_STATIC(t) }

#define MS_TO_CDTIME_T(ms)                                                     \
  (cdtime_t) {                                                                 \
    ((((cdtime_t)(ms)) / 1000) << 30) |                                        \
        ((((((cdtime_t)(ms)) % 1000) << 30) + 500) / 1000)                     \
  }
#define US_TO_CDTIME_T(us)                                                     \
  (cdtime_t) {                                                                 \
    ((((cdtime_t)(us)) / 1000000) << 30) |                                     \
        ((((((cdtime_t)(us)) % 1000000) << 30) + 500000) / 1000000)            \
  }
#define NS_TO_CDTIME_T(ns)                                                     \
  (cdtime_t) {                                                                 \
    ((((cdtime_t)(ns)) / 1000000000) << 30) |                                  \
        ((((((cdtime_t)(ns)) % 1000000000) << 30) + 500000000) / 1000000000)   \
  }

#define CDTIME_T_TO_TIME_T(t)                                                  \
  (time_t) { (time_t)(((t) + (1 << 29)) >> 30) }
#define CDTIME_T_TO_MS(t)                                                      \
  (uint64_t) {                                                                 \
    (uint64_t)((((t) >> 30) * 1000) +                                          \
               ((((t)&0x3fffffff) * 1000 + (1 << 29)) >> 30))                  \
  }
#define CDTIME_T_TO_US(t)                                                      \
  (uint64_t) {                                                                 \
    (uint64_t)((((t) >> 30) * 1000000) +                                       \
               ((((t)&0x3fffffff) * 1000000 + (1 << 29)) >> 30))               \
  }
#define CDTIME_T_TO_NS(t)                                                      \
  (uint64_t) {                                                                 \
    (uint64_t)((((t) >> 30) * 1000000000) +                                    \
               ((((t)&0x3fffffff) * 1000000000 + (1 << 29)) >> 30))            \
  }

#define CDTIME_T_TO_DOUBLE(t)                                                  \
  (double) { ((double)(t)) / 1073741824.0 }
#define DOUBLE_TO_CDTIME_T_STATIC(d) ((cdtime_t)((d)*1073741824.0))
#define DOUBLE_TO_CDTIME_T(d)                                                  \
  (cdtime_t) { DOUBLE_TO_CDTIME_T_STATIC(d) }

#define CDTIME_T_TO_TIMEVAL(t)                                                 \
  (struct timeval) {                                                           \
    .tv_sec = (time_t)((t) >> 30),                                             \
    .tv_usec = (suseconds_t)((((t)&0x3fffffff) * 1000000 + (1 << 29)) >> 30),  \
  }
#define TIMEVAL_TO_CDTIME_T(tv)                                                \
  US_TO_CDTIME_T(1000000 * (tv)->tv_sec + (tv)->tv_usec)

#define CDTIME_T_TO_TIMESPEC(t)                                                \
  (struct timespec) {                                                          \
    .tv_sec = (time_t)((t) >> 30),                                             \
    .tv_nsec = (long)((((t)&0x3fffffff) * 1000000000 + (1 << 29)) >> 30),      \
  }
#define TIMESPEC_TO_CDTIME_T(ts)                                               \
  NS_TO_CDTIME_T(1000000000ULL * (ts)->tv_sec + (ts)->tv_nsec)

cdtime_t cdtime(void);

#define RFC3339_SIZE 26     /* 2006-01-02T15:04:05+00:00 */
#define RFC3339NANO_SIZE 36 /* 2006-01-02T15:04:05.999999999+00:00 */

/* rfc3339 formats a cdtime_t time as UTC in RFC 3339 zulu format with second
 * precision, e.g., "2006-01-02T15:04:05Z". */
int rfc3339(char *buffer, size_t buffer_size, cdtime_t t);

/* rfc3339nano formats a cdtime_t as UTC time in RFC 3339 zulu format with
 * nanosecond precision, e.g., "2006-01-02T15:04:05.999999999Z". */
int rfc3339nano(char *buffer, size_t buffer_size, cdtime_t t);

/* rfc3339 formats a cdtime_t time as local in RFC 3339 format with second
 * precision, e.g., "2006-01-02T15:04:05+00:00". */
int rfc3339_local(char *buffer, size_t buffer_size, cdtime_t t);

/* rfc3339nano formats a cdtime_t time as local in RFC 3339 format with
 * nanosecond precision, e.g., "2006-01-02T15:04:05.999999999+00:00". */
int rfc3339nano_local(char *buffer, size_t buffer_size, cdtime_t t);

#endif /* UTILS_TIME_H */
