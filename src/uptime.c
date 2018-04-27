/**
 * collectd - src/uptime.c
 * Copyright (C) 2009	Marco Chiappero
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
 *   Marco Chiappero <marco at absence.it>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#if KERNEL_LINUX
#include <sys/sysinfo.h>
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
/* Using kstats chain to retrieve the boot time on Solaris / OpenSolaris systems
 */
/* #endif HAVE_LIBKSTAT */

#elif HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
/* Using sysctl interface to retrieve the boot time on *BSD / Darwin / OS X
 * systems */
/* #endif HAVE_SYS_SYSCTL_H */

#elif HAVE_PERFSTAT
#include <libperfstat.h>
#include <sys/protosw.h>
/* Using perfstat_cpu_total to retrive the boot time in AIX */
/* #endif HAVE_PERFSTAT */

#else
#error "No applicable input method."
#endif

/*
 * Global variables
 */

#if HAVE_KSTAT_H
#include <kstat.h>
#endif

#if HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
#endif /* #endif HAVE_LIBKSTAT */

static void uptime_submit(gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, "uptime", sizeof(vl.plugin));
  sstrncpy(vl.type, "uptime", sizeof(vl.type));

  plugin_dispatch_values(&vl);
}

/*
 * On most unix systems the uptime is calculated by looking at the boot
 * time (stored in unix time, since epoch) and the current one. We are
 * going to do the same, reading the boot time value while executing
 * the uptime_init function (there is no need to read, every time the
 * plugin_read is called, a value that won't change). However, since
 * uptime_init is run only once, if the function fails in retrieving
 * the boot time, the plugin is unregistered and there is no chance to
 * try again later. Nevertheless, this is very unlikely to happen.
 */
static time_t uptime_get_sys(void) { /* {{{ */
  time_t result;
#if KERNEL_LINUX
  struct sysinfo info;
  int status;

  status = sysinfo(&info);
  if (status != 0) {
    ERROR("uptime plugin: Error calling sysinfo: %s", STRERRNO);
    return -1;
  }

  result = (time_t)info.uptime;
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
  kstat_t *ksp;
  kstat_named_t *knp;

  ksp = NULL;
  knp = NULL;

  /* kstats chain already opened by update_kstat (using *kc), verify everything
   * went fine. */
  if (kc == NULL) {
    ERROR("uptime plugin: kstat chain control structure not available.");
    return -1;
  }

  ksp = kstat_lookup(kc, "unix", 0, "system_misc");
  if (ksp == NULL) {
    ERROR("uptime plugin: Cannot find unix:0:system_misc kstat.");
    return -1;
  }

  if (kstat_read(kc, ksp, NULL) < 0) {
    ERROR("uptime plugin: kstat_read failed.");
    return -1;
  }

  knp = (kstat_named_t *)kstat_data_lookup(ksp, "boot_time");
  if (knp == NULL) {
    ERROR("uptime plugin: kstat_data_lookup (boot_time) failed.");
    return -1;
  }

  if (knp->value.ui32 == 0) {
    ERROR("uptime plugin: kstat_data_lookup returned success, "
          "but `boottime' is zero!");
    return -1;
  }

  result = time(NULL) - (time_t)knp->value.ui32;
/* #endif HAVE_LIBKSTAT */

#elif HAVE_SYS_SYSCTL_H
  struct timeval boottv = {0};
  size_t boottv_len;
  int status;

  int mib[] = {CTL_KERN, KERN_BOOTTIME};

  boottv_len = sizeof(boottv);

  status = sysctl(mib, STATIC_ARRAY_SIZE(mib), &boottv, &boottv_len,
                  /* new_value = */ NULL, /* new_length = */ 0);
  if (status != 0) {
    ERROR("uptime plugin: No value read from sysctl interface: %s", STRERRNO);
    return -1;
  }

  if (boottv.tv_sec == 0) {
    ERROR("uptime plugin: sysctl(3) returned success, "
          "but `boottime' is zero!");
    return -1;
  }

  result = time(NULL) - boottv.tv_sec;
/* #endif HAVE_SYS_SYSCTL_H */

#elif HAVE_PERFSTAT
  int status;
  perfstat_cpu_total_t cputotal;
  int hertz;

  status = perfstat_cpu_total(NULL, &cputotal, sizeof(perfstat_cpu_total_t), 1);
  if (status < 0) {
    ERROR("uptime plugin: perfstat_cpu_total: %s", STRERRNO);
    return -1;
  }

  hertz = sysconf(_SC_CLK_TCK);
  if (hertz <= 0)
    hertz = HZ;

  result = cputotal.lbolt / hertz;
#endif /* HAVE_PERFSTAT */

  return result;
} /* }}} int uptime_get_sys */

static int uptime_read(void) {
  gauge_t uptime;
  time_t elapsed;

  /* calculate the amount of time elapsed since boot, AKA uptime */
  elapsed = uptime_get_sys();

  uptime = (gauge_t)elapsed;

  uptime_submit(uptime);

  return 0;
}

void module_register(void) {
  plugin_register_read("uptime", uptime_read);
} /* void module_register */
