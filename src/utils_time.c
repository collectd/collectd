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

/* vim: set sw=2 sts=2 et fdm=marker : */
