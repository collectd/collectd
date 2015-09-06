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
#define TIME_T_TO_CDTIME_T(t) (((cdtime_t) (t)) << 30)

#define MS_TO_CDTIME_T(ms) (((((cdtime_t) (ms)) / 1000) << 30) | \
    ((((((cdtime_t) (ms)) % 1000) << 30) + 500) / 1000))
#define US_TO_CDTIME_T(us) (((((cdtime_t) (us)) / 1000000) << 30) | \
    ((((((cdtime_t) (us)) % 1000000) << 30) + 500000) / 1000000))
#define NS_TO_CDTIME_T(ns) (((((cdtime_t) (ns)) / 1000000000) << 30) | \
    ((((((cdtime_t) (ns)) % 1000000000) << 30) + 500000000) / 1000000000))

#define CDTIME_T_TO_TIME_T(t) ((time_t) (((t) + (1 << 29)) >> 30))
#define CDTIME_T_TO_MS(t)  ((uint64_t) ((((t) >> 30) * 1000) + \
  ((((t) & 0x3fffffff) * 1000 + (1 << 29)) >> 30)))
#define CDTIME_T_TO_US(t)  ((uint64_t) ((((t) >> 30) * 1000000) + \
  ((((t) & 0x3fffffff) * 1000000 + (1 << 29)) >> 30)))
#define CDTIME_T_TO_NS(t)  ((uint64_t) ((((t) >> 30) * 1000000000) + \
  ((((t) & 0x3fffffff) * 1000000000 + (1 << 29)) >> 30)))

#define CDTIME_T_TO_DOUBLE(t) (((double) (t)) / 1073741824.0)
#define DOUBLE_TO_CDTIME_T(d) ((cdtime_t) ((d) * 1073741824.0))

#define CDTIME_T_TO_TIMEVAL(cdt,tvp) do {                                    \
        (tvp)->tv_sec = CDTIME_T_TO_TIME_T (cdt);                            \
        (tvp)->tv_usec = (suseconds_t) CDTIME_T_TO_US ((cdt) & 0x3fffffff);  \
} while (0)
#define TIMEVAL_TO_CDTIME_T(tv) US_TO_CDTIME_T(1000000 * (tv)->tv_sec + (tv)->tv_usec)

#define CDTIME_T_TO_TIMESPEC(cdt,tsp) do {                                   \
  (tsp)->tv_sec = CDTIME_T_TO_TIME_T (cdt);                                  \
  (tsp)->tv_nsec = (long) CDTIME_T_TO_NS ((cdt) & 0x3fffffff);               \
} while (0)
#define TIMESPEC_TO_CDTIME_T(ts) NS_TO_CDTIME_T(1000000000 * (ts)->tv_sec + (ts)->tv_nsec)

cdtime_t cdtime (void);

/* format a cdtime_t value in ISO 8601 format:
 * returns the number of characters written to the string (not including the
 * terminating null byte or 0 on error; the function ensures that the string
 * is null terminated */
size_t cdtime_to_iso8601 (char *s, size_t max, cdtime_t t);

#endif /* UTILS_TIME_H */
/* vim: set sw=2 sts=2 et : */
