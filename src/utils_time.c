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
