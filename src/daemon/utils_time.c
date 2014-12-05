/**
 * collectd - src/utils_time.c
 * Copyright (C) 2010       Florian octo Forster
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
 *   Florian octo Forster <ff at octo.it>
 **/

#include "collectd.h"
#include "utils_time.h"
#include "plugin.h"
#include "common.h"

#if HAVE_CLOCK_GETTIME
cdtime_t cdtime (void) /* {{{ */
{
  int status;
  struct timespec ts = { 0, 0 };

  status = clock_gettime (CLOCK_REALTIME, &ts);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("cdtime: clock_gettime failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (0);
  }

  return (TIMESPEC_TO_CDTIME_T (&ts));
} /* }}} cdtime_t cdtime */
#else
/* Work around for Mac OS X which doesn't have clock_gettime(2). *sigh* */
cdtime_t cdtime (void) /* {{{ */
{
  int status;
  struct timeval tv = { 0, 0 };

  status = gettimeofday (&tv, /* struct timezone = */ NULL);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("cdtime: gettimeofday failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (0);
  }

  return (TIMEVAL_TO_CDTIME_T (&tv));
} /* }}} cdtime_t cdtime */
#endif

size_t cdtime_to_iso8601 (char *s, size_t max, cdtime_t t) /* {{{ */
{
  struct timespec t_spec;
  struct tm t_tm;

  size_t len;

  CDTIME_T_TO_TIMESPEC (t, &t_spec);
  NORMALIZE_TIMESPEC (t_spec);

  if (localtime_r ((time_t *)&t_spec.tv_sec, &t_tm) == NULL) {
    char errbuf[1024];
    ERROR ("cdtime_to_iso8601: localtime_r failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (0);
  }

  len = strftime (s, max, "%Y-%m-%dT%H:%M:%S", &t_tm);
  if (len == 0)
    return 0;

  if (max - len > 2) {
    int n = snprintf (s + len, max - len, ".%09i", (int)t_spec.tv_nsec);
    len += (n < max - len) ? n : max - len;
  }

  if (max - len > 3) {
    int n = strftime (s + len, max - len, "%z", &t_tm);
    len += (n < max - len) ? n : max - len;
  }

  s[max - 1] = '\0';
  return len;
} /* }}} size_t cdtime_to_iso8601 */

/* vim: set sw=2 sts=2 et fdm=marker : */
