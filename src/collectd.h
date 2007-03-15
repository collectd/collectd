/**
 * collectd - src/collectd.h
 * Copyright (C) 2005,2006  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#ifndef COLLECTD_H
#define COLLECTD_H

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#if STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# if HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#if HAVE_STRING_H
# if !STDC_HEADERS && HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#endif
#if HAVE_STRINGS_H
# include <strings.h>
#endif
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif
#if HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val) ((unsigned int) (stat_val) >> 8)
#endif
#ifndef WIFEXITED
# define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif
#if HAVE_SIGNAL_H
# include <signal.h>
#endif
#if HAVE_FCNTL_H
# include <fcntl.h>
#endif
#if HAVE_ERRNO_H
# include <errno.h>
#endif
#if HAVE_LIMITS_H
# include <limits.h>
#endif
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if HAVE_ASSERT_H
# include <assert.h>
#else
# define assert(...) /* nop */
#endif

#if NAN_STATIC_DEFAULT
# include <math.h>
/* #endif NAN_STATIC_DEFAULT*/
#elif NAN_STATIC_ISOC
# ifndef __USE_ISOC99
#  define DISABLE_ISOC99 1
#  define __USE_ISOC99 1
# endif /* !defined(__USE_ISOC99) */
# include <math.h>
# if DISABLE_ISOC99
#  undef DISABLE_ISOC99
#  undef __USE_ISOC99
# endif /* DISABLE_ISOC99 */
/* #endif NAN_STATIC_ISOC */
#elif NAN_ZERO_ZERO
# include <math.h>
# define NAN (0.0 / 0.0)
# ifndef isnan
#  define isnan(f) ((f) != (f))
# endif /* !defined(isnan) */
#endif /* NAN_ZERO_ZERO */

#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#if HAVE_STDARG_H
# include <stdarg.h>
#endif
#if HAVE_CTYPE_H
# include <ctype.h>
#endif
#if HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#if HAVE_KSTAT_H
# include <kstat.h>
#endif

#if HAVE_RRD_H
# include <rrd.h>
#endif
#if HAVE_PTH_H
# include <pth.h>
#endif
#if HAVE_STATGRAB_H
# include <statgrab.h>
#endif
#if HAVE_SENSORS_SENSORS_H
# include <sensors/sensors.h>
#endif

#ifndef PACKAGE_NAME
#define PACKAGE_NAME "collectd"
#endif

#ifndef PREFIX
#define PREFIX "/opt/" PACKAGE_NAME
#endif

#ifndef SYSCONFDIR
#define SYSCONFDIR PREFIX "/etc"
#endif

#ifndef CONFIGFILE
#define CONFIGFILE SYSCONFDIR"/collectd.conf"
#endif

#ifndef PKGLOCALSTATEDIR
#define PKGLOCALSTATEDIR PREFIX "/var/lib/" PACKAGE_NAME
#endif

#ifndef PIDFILE
#define PIDFILE PREFIX "/var/run/" PACKAGE_NAME ".pid"
#endif

#ifndef LOGFILE
#define LOGFILE PREFIX"/var/log/"PACKAGE_NAME"/"PACKAGE_NAME".log"
#endif

#ifndef PLUGINDIR
#define PLUGINDIR PREFIX "/lib/" PACKAGE_NAME
#endif

#define MODE_SERVER 0x01
#define MODE_CLIENT 0x02
#define MODE_LOCAL  0x04
#define MODE_LOG    0x08

#ifndef COLLECTD_GRP_NAME
# define COLLECTD_GRP_NAME "collectd"
#endif

#ifndef COLLECTD_STEP
#  define COLLECTD_STEP "10"
#endif

#ifndef COLLECTD_HEARTBEAT
#  define COLLECTD_HEARTBEAT "25"
#endif

#ifndef COLLECTD_ROWS
#  define COLLECTD_ROWS "1200"
#endif

#ifndef COLLECTD_XFF
#  define COLLECTD_XFF 0.1
#endif

#define STATIC_ARRAY_LEN(array) (sizeof (array) / sizeof ((array)[0]))

extern char hostname_g[];
extern int  interval_g;

#endif /* COLLECTD_H */
