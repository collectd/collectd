/**
 * collectd - src/utils_time.h
 * Copyright (C) 2010  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <ff at octo.it>
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
#define TIME_T_TO_CDTIME_T(t) (((cdtime_t) (t)) * 1073741824)
#define CDTIME_T_TO_TIME_T(t) ((time_t) ((t) / 1073741824))

#define CDTIME_T_TO_DOUBLE(t) (((double) (t)) / 1073741824.0)
#define DOUBLE_TO_CDTIME_T(d) ((cdtime_t) ((d) * 1073741824.0))

#define MS_TO_CDTIME_T(ms) ((cdtime_t)    (((double) (ms)) * 1073741.824))
#define CDTIME_T_TO_MS(t)  ((long)        (((double) (t))  / 1073741.824))
#define US_TO_CDTIME_T(us) ((cdtime_t)    (((double) (us)) * 1073.741824))
#define CDTIME_T_TO_US(t)  ((suseconds_t) (((double) (t))  / 1073.741824))
#define NS_TO_CDTIME_T(ns) ((cdtime_t)    (((double) (ns)) * 1.073741824))
#define CDTIME_T_TO_NS(t)  ((long)        (((double) (t))  / 1.073741824))

#define CDTIME_T_TO_TIMEVAL(cdt,tvp) do {                                    \
        (tvp)->tv_sec = CDTIME_T_TO_TIME_T (cdt);                            \
        (tvp)->tv_usec = CDTIME_T_TO_US ((cdt) % 1073741824);                \
} while (0)
#define TIMEVAL_TO_CDTIME_T(tv) (TIME_T_TO_CDTIME_T ((tv)->tv_sec)           \
    + US_TO_CDTIME_T ((tv)->tv_usec))

#define CDTIME_T_TO_TIMESPEC(cdt,tsp) do {                                   \
  (tsp)->tv_sec = CDTIME_T_TO_TIME_T (cdt);                                  \
  (tsp)->tv_nsec = CDTIME_T_TO_NS ((cdt) % 1073741824);                      \
} while (0)
#define TIMESPEC_TO_CDTIME_T(ts) (TIME_T_TO_CDTIME_T ((ts)->tv_sec)           \
    + NS_TO_CDTIME_T ((ts)->tv_nsec))

cdtime_t cdtime (void);

#endif /* UTILS_TIME_H */
/* vim: set sw=2 sts=2 et : */
