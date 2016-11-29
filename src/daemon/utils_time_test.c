/**
 * collectd - src/daemon/utils_time_test.c
 * Copyright (C) 2015       Florian octo Forster
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
 */

#define DBL_PRECISION 1e-3

#include "collectd.h"

#include "testing.h"
#include "utils_time.h"

DEF_TEST(conversion) {
  struct {
    cdtime_t t;
    double d;
    time_t tt;
    uint64_t ms;
    struct timeval tv;
    struct timespec ts;
  } cases[] = {
      /*              cdtime          double      time_t   milliseconds
         timeval                 timespec */
      {0, 0.0, 0, 0, {0, 0}, {0, 0}},
      {10737418240ULL, 10.0, 10, 10000, {10, 0}, {10, 0}},
      {1542908534771941376ULL,
       1436945549.0,
       1436945549,
       1436945549000ULL,
       {1436945549, 0},
       {1436945549, 0}},
      {1542908535540740522ULL,
       1436945549.716,
       1436945550,
       1436945549716ULL,
       {1436945549, 716000},
       {1436945549, 716000000}},
      // 1426076671.123 * 2^30 = 1531238166015458148.352
      {1531238166015458148ULL,
       1426076671.123,
       1426076671,
       1426076671123ULL,
       {1426076671, 123000},
       {1426076671, 123000000}},
      // 1426076681.234 * 2^30 = 1531238176872061730.816
      {1531238176872061731ULL,
       1426076681.234,
       1426076681,
       1426076681234ULL,
       {1426076681, 234000},
       {1426076681, 234000000}},
      // 1426083986.314 * 2^30 = 1531246020641985396.736
      {1531246020641985397ULL,
       1426083986.314,
       1426083986,
       1426083986314ULL,
       {1426083986, 314000},
       {1426083986, 314000000}},
      // 1426083986.494142531 * 2^30 = 1531246020835411966.5
      {1531246020835411967ULL,
       1426083986.494,
       1426083986,
       1426083986494ULL,
       {1426083986, 494143},
       {1426083986, 494142531}},
      // 1426083986.987410814 * 2^30 = 1531246021365054752.4
      {1531246021365054752ULL,
       1426083986.987,
       1426083987,
       1426083986987ULL,
       {1426083986, 987411},
       {1426083986, 987410814}},

      /* These cases test the cdtime_t -> ns conversion rounds correctly. */
      // 1546167635576736987 / 2^30 = 1439980823.1524536265...
      {1546167635576736987ULL,
       1439980823.152,
       1439980823,
       1439980823152ULL,
       {1439980823, 152454},
       {1439980823, 152453627}},
      // 1546167831554815222 / 2^30 = 1439981005.6712620165...
      {1546167831554815222ULL,
       1439981005.671,
       1439981006,
       1439981005671ULL,
       {1439981005, 671262},
       {1439981005, 671262017}},
      // 1546167986577716567 / 2^30 = 1439981150.0475896215...
      {1546167986577716567ULL,
       1439981150.048,
       1439981150,
       1439981150048ULL,
       {1439981150, 47590},
       {1439981150, 47589622}},
  };

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    // cdtime -> s
    EXPECT_EQ_UINT64(cases[i].tt, CDTIME_T_TO_TIME_T(cases[i].t));

    // cdtime -> ms
    EXPECT_EQ_UINT64(cases[i].ms, CDTIME_T_TO_MS(cases[i].t));

    // cdtime -> us
    struct timeval tv = CDTIME_T_TO_TIMEVAL(cases[i].t);
    EXPECT_EQ_UINT64(cases[i].tv.tv_sec, tv.tv_sec);
    EXPECT_EQ_UINT64(cases[i].tv.tv_usec, tv.tv_usec);

    // cdtime -> ns
    struct timespec ts = CDTIME_T_TO_TIMESPEC(cases[i].t);
    EXPECT_EQ_UINT64(cases[i].ts.tv_sec, ts.tv_sec);
    EXPECT_EQ_UINT64(cases[i].ts.tv_nsec, ts.tv_nsec);

    // cdtime -> double
    EXPECT_EQ_DOUBLE(cases[i].d, CDTIME_T_TO_DOUBLE(cases[i].t));
  }

  return 0;
}

/* These cases test the ns -> cdtime_t conversion rounds correctly. */
DEF_TEST(ns_to_cdtime) {
  struct {
    uint64_t ns;
    cdtime_t want;
  } cases[] = {
      // 1439981652801860766 * 2^30 / 10^9 = 1546168526406004689.4
      {1439981652801860766ULL, 1546168526406004689ULL},
      // 1439981836985281914 * 2^30 / 10^9 = 1546168724171447263.4
      {1439981836985281914ULL, 1546168724171447263ULL},
      // 1439981880053705608 * 2^30 / 10^9 = 1546168770415815077.4
      {1439981880053705608ULL, 1546168770415815077ULL},
  };

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    EXPECT_EQ_UINT64(cases[i].want, NS_TO_CDTIME_T(cases[i].ns));
  }

  return 0;
}

int main(void) {
  RUN_TEST(conversion);
  RUN_TEST(ns_to_cdtime);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
