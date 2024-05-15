/**
 * collectd - src/cpufreq.c
 * Copyright (C) 2005-2007  Peter Holik
 * Copyright (C) 2024       Florian Forster
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
 *   Peter Holik <peter at holik.at>
 *   Florian Forster <octo at collectd.org>
 **/

#include "daemon/collectd.h"
#include "daemon/plugin.h"
#include "utils/common/common.h"

#if KERNEL_FREEBSD
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

#if KERNEL_LINUX
static int cpufreq_read_linux(metric_family_t *fam) {
  long nproc = sysconf(_SC_NPROCESSORS_ONLN);
  if (nproc == -1) {
    ERROR("cpufreq plugin: sysconf(_SC_NPROCESSORS_ONLN) failed: %s", STRERRNO);
    return errno;
  }

  for (long i = 0; i < nproc; i++) {
    char filename[PATH_MAX] = {0};
    snprintf(filename, sizeof(filename),
             "/sys/devices/system/cpu/cpu%ld/cpufreq/scaling_cur_freq", i);

    value_t v = {0};
    if (parse_value_file(filename, &v, DS_TYPE_GAUGE) != 0) {
      WARNING("cpufreq plugin: Reading \"%s\" failed.", filename);
      continue;
    }

    /* convert kHz to Hz */
    v.gauge *= 1000.0;

    char label_value[32] = {0};
    snprintf(label_value, sizeof(label_value), "%ld", i);
    metric_family_append(fam, "system.cpu.logical_number", label_value, v,
                         NULL);
  }
  return 0;
}

#elif KERNEL_FREEBSD
static int cpufreq_read_freebsd(metric_family_t *fam) {
  long nproc = sysconf(_SC_NPROCESSORS_ONLN);
  if (nproc == -1) {
    ERROR("cpufreq plugin: sysconf(_SC_NPROCESSORS_ONLN) failed: %s", STRERRNO);
    return errno;
  }

  for (long i = 0; i < nproc; i++) {
    char mib[64] = {0};
    snprintf(mib, sizeof(mib), "dev.cpu.%ld.freq", i);

    int mhz = 0;
    size_t mhz_len = sizeof(mhz);
    int err = sysctlbyname(mib, &mhz, &mhz_len, NULL, 0);
    if (err) {
      WARNING("cpufreq plugin: sysctl \"%s\" failed: %s", mib, STRERRNO);
      continue;
    }

    char label_value[32] = {0};
    snprintf(label_value, sizeof(label_value), "%ld", i);
    metric_family_append(&fam, "system.cpu.logical_number", label_value,
                         (value_t){.gauge = ((gauge_t)mhz) * 1e6}, NULL);
  }
  return 0;
}
#endif

static int cpufreq_read(void) {
  metric_family_t fam = {
      .name = "system.cpu.frequency",
      .help = "Reports the current frequency of the CPU in Hz",
      .unit = "{Hz}",
      .type = METRIC_TYPE_GAUGE,
  };

#if KERNEL_LINUX
  int err = cpufreq_read_linux(&fam);
#elif KERNEL_FREEBSD
  int err = cpufreq_read_freebsd(&fam);
#endif
  if (!err) {
    err = plugin_dispatch_metric_family(&fam);
  }
  metric_family_metric_reset(&fam);
  return err;
} /* int cpufreq_read */

void module_register(void) { plugin_register_read("cpufreq", cpufreq_read); }
