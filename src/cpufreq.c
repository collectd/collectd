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

#define MAX_AVAIL_FREQS 20

static int num_cpu;

struct cpu_data_t {
  value_to_rate_state_t time_state[MAX_AVAIL_FREQS];
} * cpu_data;

/* Flags denoting capability of reporting stats. */
unsigned report_time_in_state, report_total_trans;

static int counter_init(void) {
  cpu_data = calloc(num_cpu, sizeof(struct cpu_data_t));
  if (cpu_data == NULL)
    return 0;

  report_time_in_state = 1;
  report_total_trans = 1;

  /* Check for stats module and disable if not present. */
  for (int i = 0; i < num_cpu; i++) {
    char filename[PATH_MAX];
    int status = snprintf(filename, sizeof(filename),
                      "/sys/devices/system/cpu/cpu%d/cpufreq/"
                      "stats/time_in_state",
                      num_cpu);
    if ((status < 1) || ((unsigned int)status >= sizeof(filename)))
      report_time_in_state = 0;

    status = snprintf(filename, sizeof(filename),
                      "/sys/devices/system/cpu/cpu%d/cpufreq/"
                      "stats/total_transitions",
                      num_cpu);
    if ((status < 1) || ((unsigned int)status >= sizeof(filename)))
      report_total_trans = 0;
    }
  }
  return 0;
}

static int cpufreq_init(void) {
  char filename[PATH_MAX];

  num_cpu = 0;

  while (1) {
    int status = snprintf(filename, sizeof(filename),
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
  counter_init();

  if (num_cpu == 0)
    plugin_unregister_read("cpufreq");

  return 0;
} /* int cpufreq_init */

static void cpufreq_submit(int cpu_num, const char *type,
                           const char *type_instance, value_t *value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = value;
  vl.values_len = 1;
  sstrncpy(vl.plugin, "cpufreq", sizeof(vl.plugin));
  snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%i", cpu_num);
  if (type != NULL)
    sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static int cpufreq_read(void) {
  for (int i = 0; i < num_cpu; i++) {
    char filename[PATH_MAX];
    /* Read cpu frequency */
    snprintf(filename, sizeof(filename),
             "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);

    value_t v;
    if (parse_value_file(filename, &v, DS_TYPE_GAUGE) != 0) {
      WARNING("cpufreq plugin: Reading \"%s\" failed.", filename);
      continue;
    }

    /* convert kHz to Hz */
    v.gauge *= 1000.0;

    cpufreq_submit(i, "cpufreq", NULL, &v);

    /* Read total transitions for cpu frequency */
    if (report_total_trans) {
      snprintf(filename, sizeof(filename),
               "/sys/devices/system/cpu/cpu%d/cpufreq/stats/total_trans", i);
      if (parse_value_file(filename, &v, DS_TYPE_COUNTER) != 0) {
        WARNING("cpufreq plugin: Reading \"%s\" failed.", filename);
        continue;
      }
      cpufreq_submit(i, "counter", "transitions", &v);
    }

    /*
     * Determine percentage time in each state for cpu during previous interval.
     */
    if (report_time_in_state) {
      int j = 0;
      cdtime_t now = cdtime();
      char state[DATA_MAX_NAME_LEN];

      snprintf(filename, sizeof(filename),
               "/sys/devices/system/cpu/cpu%d/cpufreq/stats/time_in_state", i);

      FILE *fh = fopen(filename, "r");
      if (fh == NULL)
        continue;

      char buffer[DATA_MAX_NAME_LEN];  //128

      while (fgets(buffer, sizeof(buffer), fh) != NULL) {
        unsigned long long time;
        if (!sscanf(buffer, "%s%llu", state, &time)) {
          WARNING("cpufreq plugin: Reading \"%s\" failed.", filename);
          fclose(fh);
          return 0;
        }

        if (j < MAX_AVAIL_FREQS) {
          gauge_t g;
          if (value_to_rate(&g, (value_t){.counter = time}, DS_TYPE_COUNTER, now, &cpu_data[i].time_state[j]) != 0) {
            continue;
            j++;
          }
          cpufreq_submit(i, "percent", state, &(value_t){.gauge = g});
        }
        j++;
      }
      fclose(fh);
    }
  }
  return 0;
} /* int cpufreq_read */

void module_register(void) {
  plugin_register_init("cpufreq", cpufreq_init);
  plugin_register_read("cpufreq", cpufreq_read);
}
