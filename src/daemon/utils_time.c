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

#include "common.h"
#include "plugin.h"
#include "utils_time.h"

#ifndef DEFAULT_MOCK_TIME
#define DEFAULT_MOCK_TIME 1542455354518929408ULL
#endif

#ifdef MOCK_TIME
cdtime_t cdtime_mock = (cdtime_t)MOCK_TIME;

cdtime_t cdtime(void) { return cdtime_mock; }
#else /* !MOCK_TIME */
#if HAVE_CLOCK_GETTIME
cdtime_t cdtime(void) /* {{{ */
{
  int status;
  struct timespec ts = {0, 0};

  status = clock_gettime(CLOCK_REALTIME, &ts);
  if (status != 0) {
    ERROR("cdtime: clock_gettime failed: %s", STRERRNO);
    return 0;
  }

  return TIMESPEC_TO_CDTIME_T(&ts);
} /* }}} cdtime_t cdtime */
#else /* !HAVE_CLOCK_GETTIME */
/* Work around for Mac OS X which doesn't have clock_gettime(2). *sigh* */
cdtime_t cdtime(void) /* {{{ */
{
  int status;
  struct timeval tv = {0, 0};

  status = gettimeofday(&tv, /* struct timezone = */ NULL);
  if (status != 0) {
    ERROR("cdtime: gettimeofday failed: %s", STRERRNO);
    return 0;
  }

  return TIMEVAL_TO_CDTIME_T(&tv);
} /* }}} cdtime_t cdtime */
#endif
#endif

/**********************************************************************
 Time retrieval functions
***********************************************************************/

static int get_utc_time(cdtime_t t, struct tm *t_tm, long *nsec) /* {{{ */
{
  struct timespec t_spec = CDTIME_T_TO_TIMESPEC(t);
  NORMALIZE_TIMESPEC(t_spec);

  if (gmtime_r(&t_spec.tv_sec, t_tm) == NULL) {
    int status = errno;
    ERROR("get_utc_time: gmtime_r failed: %s", STRERRNO);
    return status;
  }

  *nsec = t_spec.tv_nsec;
  return 0;
} /* }}} int get_utc_time */

static int get_local_time(cdtime_t t, struct tm *t_tm, long *nsec) /* {{{ */
{
  struct timespec t_spec = CDTIME_T_TO_TIMESPEC(t);
  NORMALIZE_TIMESPEC(t_spec);

  if (localtime_r(&t_spec.tv_sec, t_tm) == NULL) {
    int status = errno;
    ERROR("get_local_time: localtime_r failed: %s", STRERRNO);
    return status;
  }

  *nsec = t_spec.tv_nsec;
  return 0;
} /* }}} int get_local_time */

/**********************************************************************
 Formatting functions
***********************************************************************/

static const char zulu_zone[] = "Z";

/* format_zone reads time zone information from "extern long timezone", exported
 * by <time.h>, and formats it according to RFC 3339. This differs from
 * strftime()'s "%z" format by including a colon between hour and minute. */
static int format_zone(char *buffer, size_t buffer_size,
                       struct tm const *tm) /* {{{ */
{
  char tmp[7];
  size_t sz;

  if ((buffer == NULL) || (buffer_size < 7))
    return EINVAL;

  sz = strftime(tmp, sizeof(tmp), "%z", tm);
  if (sz == 0)
    return ENOMEM;
  if (sz != 5) {
    DEBUG("format_zone: strftime(\"%%z\") = \"%s\", want \"+hhmm\"", tmp);
    sstrncpy(buffer, tmp, buffer_size);
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

int format_rfc3339(char *buffer, size_t buffer_size, struct tm const *t_tm,
                   long nsec, _Bool print_nano, char const *zone) /* {{{ */
{
  int len;
  char *pos = buffer;
  size_t size_left = buffer_size;

  if ((len = strftime(pos, size_left, "%Y-%m-%dT%H:%M:%S", t_tm)) == 0)
    return ENOMEM;
  pos += len;
  size_left -= len;

  if (print_nano) {
    if ((len = snprintf(pos, size_left, ".%09ld", nsec)) == 0)
      return ENOMEM;
    pos += len;
    size_left -= len;
  }

  sstrncpy(pos, zone, size_left);
  return 0;
} /* }}} int format_rfc3339 */

int format_rfc3339_utc(char *buffer, size_t buffer_size, cdtime_t t,
                       _Bool print_nano) /* {{{ */
{
  struct tm t_tm;
  long nsec = 0;
  int status;

  if ((status = get_utc_time(t, &t_tm, &nsec)) != 0)
    return status; /* The error should have already be reported. */

  return format_rfc3339(buffer, buffer_size, &t_tm, nsec, print_nano,
                        zulu_zone);
} /* }}} int format_rfc3339_utc */

int format_rfc3339_local(char *buffer, size_t buffer_size, cdtime_t t,
                         _Bool print_nano) /* {{{ */
{
  struct tm t_tm;
  long nsec = 0;
  int status;
  char zone[7]; /* +00:00 */

  if ((status = get_local_time(t, &t_tm, &nsec)) != 0)
    return status; /* The error should have already be reported. */

  if ((status = format_zone(zone, sizeof(zone), &t_tm)) != 0)
    return status;

  return format_rfc3339(buffer, buffer_size, &t_tm, nsec, print_nano, zone);
} /* }}} int format_rfc3339_local */

/**********************************************************************
 Public functions
***********************************************************************/

int rfc3339(char *buffer, size_t buffer_size, cdtime_t t) /* {{{ */
{
  if (buffer_size < RFC3339_SIZE)
    return ENOMEM;

  return format_rfc3339_utc(buffer, buffer_size, t, 0);
} /* }}} int rfc3339 */

int rfc3339nano(char *buffer, size_t buffer_size, cdtime_t t) /* {{{ */
{
  if (buffer_size < RFC3339NANO_SIZE)
    return ENOMEM;

  return format_rfc3339_utc(buffer, buffer_size, t, 1);
} /* }}} int rfc3339nano */

int rfc3339_local(char *buffer, size_t buffer_size, cdtime_t t) /* {{{ */
{
  if (buffer_size < RFC3339_SIZE)
    return ENOMEM;

  return format_rfc3339_local(buffer, buffer_size, t, 0);
} /* }}} int rfc3339 */

int rfc3339nano_local(char *buffer, size_t buffer_size, cdtime_t t) /* {{{ */
{
  if (buffer_size < RFC3339NANO_SIZE)
    return ENOMEM;

  return format_rfc3339_local(buffer, buffer_size, t, 1);
} /* }}} int rfc3339nano */
