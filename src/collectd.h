/**
 * collectd - src/collectd.h
 * Copyright (C) 2005  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
#include <stdlib.h>
#include <stddef.h>
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
#if HAVE_SYSLOG_H
# include <syslog.h>
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

#if HAVE_SYSLOG
# define syslog(...) syslog(__VA_ARGS__)
# if HAVE_OPENLOG
#  define openlog(...) openlog(__VA_ARGS__)
# else
#  define openlog(...) /**/
# endif
# if HAVE_CLOSELOG
#  define closelog(...) closelog(__VA_ARGS__)
# else
#  define closelog(...) /**/
# endif
#else
# define syslog(...) /**/
# define openlog(...) /**/
# define closelog(...) /**/
#endif

#ifndef HAVE_RRD_H
#undef HAVE_LIBRRD
#endif

#ifdef HAVE_LIBRRD
#include <rrd.h>
#endif /* HAVE_LIBRRD */

/* Won't work without the header file */
#ifndef HAVE_KSTAT_H
#undef HAVE_LIBKSTAT
#endif

#ifdef HAVE_LIBKSTAT
#include <kstat.h>
#include <sys/param.h>
#endif /* HAVE_LIBKSTAT */

/* Won't work without the header file */
#ifndef HAVE_STATGRAB_H
#undef HAVE_LIBSTATGRAB
#endif

#ifdef HAVE_LIBSTATGRAB
#include <statgrab.h>
#endif

#define MODE_SERVER 0x01
#define MODE_CLIENT 0x02
#define MODE_LOCAL  0x03

extern time_t curtime;
extern int operating_mode;

#endif /* COLLECTD_H */
