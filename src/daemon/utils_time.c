/**
 * collectd - src/utils_time.c
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

#include "collectd.h"

#include "utils_time.h"
#include "plugin.h"
#include "common.h"

#ifndef DEFAULT_MOCK_TIME
# define DEFAULT_MOCK_TIME 1542455354518929408ULL
#endif

#ifdef MOCK_TIME
cdtime_t cdtime_mock = (cdtime_t) MOCK_TIME;

cdtime_t cdtime (void)
{
  return cdtime_mock;
}
#else /* !MOCK_TIME */
# if HAVE_CLOCK_GETTIME
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
# else /* !HAVE_CLOCK_GETTIME */
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
# endif
#endif

/* format_zone reads time zone information from "extern long timezone", exported
 * by <time.h>, and formats it according to RFC 3339. This differs from
 * strftime()'s "%z" format by including a colon between hour and minute. */
static int format_zone (char *buffer, size_t buffer_size, struct tm const *tm) /* {{{ */
{
  char tmp[7];
  size_t sz;

  if ((buffer == NULL) || (buffer_size < 7))
    return EINVAL;

  sz = strftime (tmp, sizeof (tmp), "%z", tm);
  if (sz == 0)
    return ENOMEM;
  if (sz != 5)
  {
    DEBUG ("format_zone: strftime(\"%%z\") = \"%s\", want \"+hhmm\"", tmp);
    sstrncpy (buffer, tmp, buffer_size);
    return 0;
  }

  buffer[0] = tmp[0];
  buffer[1] = tmp[1];
  buffer[2] = tmp[2];
  buffer[3] = ':';
  buffer[4] = tmp[3];
  buffer[5] = tmp[4];
  buffer[6] = 0;

  return 0;
} /* }}} int format_zone */

static int format_rfc3339 (char *buffer, size_t buffer_size, cdtime_t t, _Bool print_nano) /* {{{ */
{
  struct timespec t_spec;
  struct tm t_tm;
  char base[20]; /* 2006-01-02T15:04:05 */
  char nano[11]; /* .999999999 */
  char zone[7];  /* +00:00 */
  char *fields[] = {base, nano, zone};
  size_t len;
  int status;

  CDTIME_T_TO_TIMESPEC (t, &t_spec);
  NORMALIZE_TIMESPEC (t_spec);

  if (localtime_r (&t_spec.tv_sec, &t_tm) == NULL) {
    char errbuf[1024];
    status = errno;
    ERROR ("format_rfc3339: localtime_r failed: %s",
        sstrerror (status, errbuf, sizeof (errbuf)));
    return (status);
  }

  len = strftime (base, sizeof (base), "%Y-%m-%dT%H:%M:%S", &t_tm);
  if (len == 0)
    return ENOMEM;

  if (print_nano)
    ssnprintf (nano, sizeof (nano), ".%09ld", (long) t_spec.tv_nsec);
  else
    sstrncpy (nano, "", sizeof (nano));

  status = format_zone (zone, sizeof (zone), &t_tm);
  if (status != 0)
    return status;

  if (strjoin (buffer, buffer_size, fields, STATIC_ARRAY_SIZE (fields), "") < 0)
    return ENOMEM;
  return 0;
} /* }}} int format_rfc3339 */

int rfc3339 (char *buffer, size_t buffer_size, cdtime_t t) /* {{{ */
{
  if (buffer_size < RFC3339_SIZE)
    return ENOMEM;

  return format_rfc3339 (buffer, buffer_size, t, 0);
} /* }}} size_t cdtime_to_rfc3339 */

int rfc3339nano (char *buffer, size_t buffer_size, cdtime_t t) /* {{{ */
{
  if (buffer_size < RFC3339NANO_SIZE)
    return ENOMEM;

  return format_rfc3339 (buffer, buffer_size, t, 1);
} /* }}} size_t cdtime_to_rfc3339nano */

/* vim: set sw=2 sts=2 et fdm=marker : */
