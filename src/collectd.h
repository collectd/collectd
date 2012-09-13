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
# include "config.h"
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

#if !defined(HAVE__BOOL) || !HAVE__BOOL
typedef int _Bool;
# undef HAVE__BOOL
# define HAVE__BOOL 1
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
# ifdef NAN
#  undef NAN
# endif
# define NAN (0.0 / 0.0)
# ifndef isnan
#  define isnan(f) ((f) != (f))
# endif /* !defined(isnan) */
# ifndef isfinite
#  define isfinite(f) (((f) - (f)) == 0.0)
# endif
# ifndef isinf
#  define isinf(f) (!isfinite(f) && !isnan(f))
# endif
#endif /* NAN_ZERO_ZERO */

/* Try really, really hard to determine endianess. Under NexentaStor 1.0.2 this
 * information is in <sys/isa_defs.h>, possibly some other Solaris versions do
 * this too.. */
#if HAVE_ENDIAN_H
# include <endian.h>
#elif HAVE_SYS_ISA_DEFS_H
# include <sys/isa_defs.h>
#endif

#ifndef BYTE_ORDER
# if defined(_BYTE_ORDER)
#  define BYTE_ORDER _BYTE_ORDER
# elif defined(__BYTE_ORDER)
#  define BYTE_ORDER __BYTE_ORDER
# elif defined(__DARWIN_BYTE_ORDER)
#  define BYTE_ORDER __DARWIN_BYTE_ORDER
# endif
#endif
#ifndef BIG_ENDIAN
# if defined(_BIG_ENDIAN)
#  define BIG_ENDIAN _BIG_ENDIAN
# elif defined(__BIG_ENDIAN)
#  define BIG_ENDIAN __BIG_ENDIAN
# elif defined(__DARWIN_BIG_ENDIAN)
#  define BIG_ENDIAN __DARWIN_BIG_ENDIAN
# endif
#endif
#ifndef LITTLE_ENDIAN
# if defined(_LITTLE_ENDIAN)
#  define LITTLE_ENDIAN _LITTLE_ENDIAN
# elif defined(__LITTLE_ENDIAN)
#  define LITTLE_ENDIAN __LITTLE_ENDIAN
# elif defined(__DARWIN_LITTLE_ENDIAN)
#  define LITTLE_ENDIAN __DARWIN_LITTLE_ENDIAN
# endif
#endif
#ifndef BYTE_ORDER
# if defined(BIG_ENDIAN) && !defined(LITTLE_ENDIAN)
#  undef BIG_ENDIAN
#  define BIG_ENDIAN 4321
#  define LITTLE_ENDIAN 1234
#  define BYTE_ORDER BIG_ENDIAN
# elif !defined(BIG_ENDIAN) && defined(LITTLE_ENDIAN)
#  undef LITTLE_ENDIAN
#  define BIG_ENDIAN 4321
#  define LITTLE_ENDIAN 1234
#  define BYTE_ORDER LITTLE_ENDIAN
# endif
#endif
#if !defined(BYTE_ORDER) || !defined(BIG_ENDIAN)
# error "Cannot determine byte order"
#endif

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

#ifndef LOCALSTATEDIR
#define LOCALSTATEDIR PREFIX "/var"
#endif

#ifndef PKGLOCALSTATEDIR
#define PKGLOCALSTATEDIR PREFIX "/var/lib/" PACKAGE_NAME
#endif

#ifndef PIDFILE
#define PIDFILE PREFIX "/var/run/" PACKAGE_NAME ".pid"
#endif

#ifndef PLUGINDIR
#define PLUGINDIR PREFIX "/lib/" PACKAGE_NAME
#endif

#ifndef PKGDATADIR
#define PKGDATADIR PREFIX "/share/" PACKAGE_NAME
#endif

#ifndef COLLECTD_GRP_NAME
# define COLLECTD_GRP_NAME "collectd"
#endif

#define STATIC_ARRAY_LEN(array) (sizeof (array) / sizeof ((array)[0]))

/* Remove GNU specific __attribute__ settings when using another compiler */
#if !__GNUC__
# define __attribute__(x) /**/
#endif

#if defined(COLLECT_DEBUG) && COLLECT_DEBUG && defined(__GNUC__) && __GNUC__
# undef strcpy
# undef strcat
# undef strtok
# pragma GCC poison strcpy strcat strtok
#endif

/* 
 * Special hack for the perl plugin: Because the later included perl.h defines
 * a macro which is never used, but contains `sprintf', we cannot poison that
 * identifies just yet. The parl plugin will do that itself once perl.h is
 * included.
 */
#ifndef DONT_POISON_SPRINTF_YET
# if defined(COLLECT_DEBUG) && COLLECT_DEBUG && defined(__GNUC__) && __GNUC__
#  undef sprintf
#  pragma GCC poison sprintf
# endif
#endif

/* Type for time as used by "utils_time.h" */
typedef uint64_t cdtime_t;

extern char     hostname_g[];
extern cdtime_t interval_g;
extern int      timeout_g;

#endif /* COLLECTD_H */
