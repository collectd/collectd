/**
 * collectd - src/cpufreq.c
 * Copyright (C) 2005-2007  Peter Holik
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
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

static int num_cpu = 0;

static int cpufreq_init(void) {
  int status;
  char filename[256];

  num_cpu = 0;

  while (1) {
    status = ssnprintf(filename, sizeof(filename),
                       "/sys/devices/system/cpu/cpu%d/cpufreq/"
                       "scaling_cur_freq",
                       num_cpu);
    if ((status < 1) || ((unsigned int)status >= sizeof(filename)))
      break;

    if (access(filename, R_OK))
      break;

    num_cpu++;
  }

  INFO("cpufreq plugin: Found %d CPU%s", num_cpu, (num_cpu == 1) ? "" : "s");

  if (num_cpu == 0)
    plugin_unregister_read("cpufreq");

  return (0);
} /* int cpufreq_init */

static void cpufreq_submit(int cpu_num, value_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &value;
  vl.values_len = 1;
  sstrncpy(vl.plugin, "cpufreq", sizeof(vl.plugin));
  sstrncpy(vl.type, "cpufreq", sizeof(vl.type));
  ssnprintf(vl.type_instance, sizeof(vl.type_instance), "%i", cpu_num);

  plugin_dispatch_values(&vl);
}

static int cpufreq_read(void) {
  for (int i = 0; i < num_cpu; i++) {
    char filename[PATH_MAX];
    ssnprintf(filename, sizeof(filename),
              "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);

    value_t v;
    if (parse_value_file(filename, &v, DS_TYPE_GAUGE) != 0) {
      WARNING("cpufreq plugin: Reading \"%s\" failed.", filename);
      continue;
    }

    /* convert kHz to Hz */
    v.gauge *= 1000.0;

    cpufreq_submit(i, v);
  }

  return (0);
} /* int cpufreq_read */

void module_register(void) {
  plugin_register_init("cpufreq", cpufreq_init);
  plugin_register_read("cpufreq", cpufreq_read);
}
