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
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <limits.h>
#include <time.h>

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

#ifndef DEBUG
#define DEBUG 0
#endif

#ifndef LOCALSTATEDIR
#define LOCALSTATEDIR "/opt/collectd/var"
#endif

#ifndef DATADIR
#define DATADIR LOCALSTATEDIR"/lib/collectd"
#endif

#ifndef PLUGINDIR
#define PLUGINDIR "/opt/collectd/lib/collectd"
#endif

#ifndef PIDFILE
#define PIDFILE LOCALSTATEDIR"/run/collectd.pid"
#endif

#define MODE_SERVER 0x01
#define MODE_CLIENT 0x02
#define MODE_LOCAL  0x03

extern time_t curtime;
extern int operating_mode;

#endif /* COLLECTD_H */
