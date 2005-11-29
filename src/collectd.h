#ifndef COLLECTD_H
#define COLLECTD_H

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <limits.h>
#include <time.h>

#include "config.h"

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

#ifndef PLUGINDIR
#define PLUGINDIR "/usr/lib/collectd"
#endif

#define MODE_SERVER 0x01
#define MODE_CLIENT 0x02
#define MODE_LOCAL  0x03

extern time_t curtime;
extern int operating_mode;

#endif /* COLLECTD_H */
