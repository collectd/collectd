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

#include "testing.h"
#include "collectd.h"
#include "utils_time.h"

DEF_TEST(conversion)
{
  struct {
    cdtime_t t;
    double d;
    time_t tt;
    struct timeval tv;
    struct timespec ts;
  } cases[] = {
  /*              cdtime          double      time_t               timeval                 timespec */
    {                  0,          0.0  ,          0, {         0,      0}, {         0,         0}},
    {        10737418240,         10.0  ,         10, {        10,      0}, {        10,         0}},
    {1542908534771941376, 1436945549.0  , 1436945549, {1436945549,      0}, {1436945549,         0}},
    {1542908535540740522, 1436945549.716, 1436945550, {1436945549, 716000}, {1436945549, 716000000}},
    // 1426076671.123 * 2^30 = 1531238166015458148.352
    {1531238166015458148, 1426076671.123, 1426076671, {1426076671, 123000}, {1426076671, 123000000}},
    // 1426076681.234 * 2^30 = 1531238176872061730.816
    {1531238176872061731, 1426076681.234, 1426076681, {1426076681, 234000}, {1426076681, 234000000}},
    // 1426083986.314 * 2^30 = 1531246020641985396.736
    {1531246020641985397, 1426083986.314, 1426083986, {1426083986, 314000}, {1426083986, 314000000}},
    // 1426083986.494142531 * 2^30 = 1531246020835411966.5
    {1531246020835411967, 1426083986.494, 1426083986, {1426083986, 494143}, {1426083986, 494142531}},
    // 1426083986.987410814 * 2^30 = 1531246021365054752.4
    {1531246021365054752, 1426083986.987, 1426083987, {1426083986, 987411}, {1426083986, 987410814}},

    /* These cases test the cdtime_t -> ns conversion rounds correctly. */
    // 1546167635576736987 / 2^30 = 1439980823.1524536265...
    {1546167635576736987, 1439980823.152, 1439980823, {1439980823, 152454}, {1439980823, 152453627}},
    // 1546167831554815222 / 2^30 = 1439981005.6712620165...
    {1546167831554815222, 1439981005.671, 1439981006, {1439981005, 671262}, {1439981005, 671262017}},
    // 1546167986577716567 / 2^30 = 1439981150.0475896215...
    {1546167986577716567, 1439981150.048, 1439981150, {1439981150,  47590}, {1439981005,  47589622}},
  };
  size_t i;

  for (i = 0; i < (sizeof (cases) / sizeof (cases[0])); i++) {
    struct timeval tv;
    struct timespec ts;
    time_t tt;

    // cdtime -> ns
    CDTIME_T_TO_TIMESPEC (cases[i].t, &ts);
    EXPECT_EQ_UINT64 ((uint64_t) cases[i].ts.tv_nsec, (uint64_t) ts.tv_nsec);

    // cdtime -> us
    CDTIME_T_TO_TIMEVAL (cases[i].t, &tv);
    EXPECT_EQ_UINT64 ((uint64_t) cases[i].tv.tv_usec, (uint64_t) tv.tv_usec);

    // cdtime -> s
    tt = CDTIME_T_TO_TIME_T (cases[i].t);
    EXPECT_EQ_UINT64 ((uint64_t) cases[i].tt, (uint64_t) tt);

    // cdtime -> double
    DBLEQ (cases[i].d, CDTIME_T_TO_DOUBLE (cases[i].t));
  }

  return 0;
}

/* These cases test the ns -> cdtime_t conversion rounds correctly. */
DEF_TEST(ns_to_cdtime)
{
  struct {
    long ns;
    cdtime_t want;
  } cases[] = {
    // 1439981652801860766 * 2^30 / 10^9 = 1546168526406004689.4
    {1439981652801860766, 1546168526406004689},
    // 1439981836985281914 * 2^30 / 10^9 = 1546168724171447263.4
    {1439981836985281914, 1546168724171447263},
    // 1439981880053705608 * 2^30 / 10^9 = 1546168770415815077.4
    {1439981880053705608, 1546168770415815077},
  };
  size_t i;

  for (i = 0; i < (sizeof (cases) / sizeof (cases[0])); i++) {
    cdtime_t got = NS_TO_CDTIME_T (cases[i].ns);
    EXPECT_EQ_UINT64 ((uint64_t) cases[i].want, (uint64_t) got);
  }

  return 0;
}

int main (void)
{
  RUN_TEST(conversion);
  RUN_TEST(ns_to_cdtime);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
