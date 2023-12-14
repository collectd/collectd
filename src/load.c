/**
 * collectd - src/load.c
 * Copyright (C) 2005-2008  Florian octo Forster
 * Copyright (C) 2009       Manuel Sanmartin
 * Copyright (C) 2013       Vedran Bartonicek
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
 *   Florian octo Forster <octo at collectd.org>
 *   Manuel Sanmartin
 *   Vedran Bartonicek <vbartoni at gmail.com>
 **/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <unistd.h>

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>
#endif

#if HAVE_STATGRAB_H
#include <statgrab.h>
#endif

#ifdef HAVE_GETLOADAVG
#if !defined(LOADAVG_1MIN) || !defined(LOADAVG_5MIN) || !defined(LOADAVG_15MIN)
#define LOADAVG_1MIN 0
#define LOADAVG_5MIN 1
#define LOADAVG_15MIN 2
#endif
#endif /* defined(HAVE_GETLOADAVG) */

#ifdef HAVE_PERFSTAT
#include <libperfstat.h>
#include <sys/proc.h> /* AIX 5 */
#include <sys/protosw.h>
#endif /* HAVE_PERFSTAT */

static bool report_relative_load;

static const char *config_keys[] = {"ReportRelative"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int load_config(const char *key, const char *value) {
  if (strcasecmp(key, "ReportRelative") == 0) {
#ifdef _SC_NPROCESSORS_ONLN
    report_relative_load = IS_TRUE(value);
#else
    WARNING("load plugin: The \"ReportRelative\" configuration "
            "is not available, because I can't determine the "
            "number of CPUS on this system. Sorry.");
#endif
    return 0;
  }
  return -1;
}

static void load_submit(gauge_t l1, gauge_t l5, gauge_t l15) {
  int cores = 0;

#ifdef _SC_NPROCESSORS_ONLN
  if (report_relative_load) {
    if ((cores = sysconf(_SC_NPROCESSORS_ONLN)) < 1) {
      WARNING("load: sysconf failed : %s", STRERRNO);
    }
  }
#endif

  metric_family_t fam = {
      .name = "system.load",
      .help = "System load average over a given duration",
      .type = METRIC_TYPE_GAUGE,
  };

  if (cores > 0) {
    fam.name = "system.load.scaled";
    fam.help = "System load average over a given duration divided the by "
               "number of CPU cores";

    l1 /= cores;
    l5 /= cores;
    l15 /= cores;
  }

  metric_family_append(&fam, "duration", "1m", (value_t){.gauge = l1}, NULL);
  metric_family_append(&fam, "duration", "5m", (value_t){.gauge = l5}, NULL);
  metric_family_append(&fam, "duration", "15m", (value_t){.gauge = l15}, NULL);

  plugin_dispatch_metric_family(&fam);
  metric_family_metric_reset(&fam);
}

static int load_read(void) {
#if defined(HAVE_GETLOADAVG)
  double load[3];

  if (getloadavg(load, 3) == 3)
    load_submit(load[LOADAVG_1MIN], load[LOADAVG_5MIN], load[LOADAVG_15MIN]);
  else {
    WARNING("load: getloadavg failed: %s", STRERRNO);
  }
    /* #endif HAVE_GETLOADAVG */

#elif defined(KERNEL_LINUX)
  char buffer[64] = {0};

  ssize_t status =
      read_text_file_contents("/proc/loadavg", buffer, sizeof(buffer));
  if (status < 0) {
    ERROR("load plugin: Reading \"/proc/loadavg\" failed.");
    return (int)status;
  }

  char *fields[4] = {NULL};
  int numfields = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));
  if (numfields < 3) {
    ERROR("load plugin: strsplit returned %d field(s), want 3", numfields);
    return EIO;
  }

  load_submit(atof(fields[0]), atof(fields[1]), atof(fields[2]));
  /* #endif KERNEL_LINUX */

#elif HAVE_LIBSTATGRAB
  sg_load_stats *ls;

  if ((ls = sg_get_load_stats()) == NULL)
    return;

  load_submit(ls->min1, ls->min5, ls->min15);
  /* #endif HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
  gauge_t snum, mnum, lnum;
  perfstat_cpu_total_t cputotal;

  if (perfstat_cpu_total(NULL, &cputotal, sizeof(perfstat_cpu_total_t), 1) <
      0) {
    WARNING("load: perfstat_cpu : %s", STRERRNO);
    return -1;
  }

  snum = (float)cputotal.loadavg[0] / (float)(1 << SBITS);
  mnum = (float)cputotal.loadavg[1] / (float)(1 << SBITS);
  lnum = (float)cputotal.loadavg[2] / (float)(1 << SBITS);
  load_submit(snum, mnum, lnum);
  /* #endif HAVE_PERFSTAT */

#else
#error "No applicable input method."
#endif

  return 0;
}

void module_register(void) {
  plugin_register_config("load", load_config, config_keys, config_keys_num);
  plugin_register_read("load", load_read);
} /* void module_register */
