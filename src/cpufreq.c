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

#include "plugin.h"
#include "utils/common/common.h"

#if KERNEL_FREEBSD
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

#if KERNEL_LINUX
static int max_avail_freqs = 128; // default MAX_AVAIL_FREQS

static int num_cpu;

struct cpu_data_t {
  value_to_rate_state_t *time_state;
} * cpu_data;

/* Flags denoting capability of reporting CPU frequency statistics. */
static bool report_p_stats = false;

static void cpufreq_stats_init(void) {
  cpu_data = calloc(num_cpu, sizeof(*cpu_data));
  if (cpu_data == NULL)
    return;

  report_p_stats = true;

  /* Check for stats module and disable if not present. */
  for (int i = 0; i < num_cpu; i++) {
    char filename[PATH_MAX];

    snprintf(filename, sizeof(filename),
             "/sys/devices/system/cpu/cpu%d/cpufreq/stats/time_in_state", i);
    if (access(filename, R_OK)) {
      NOTICE("cpufreq plugin: File %s not exists or no access. P-State "
             "statistics will not be reported. Check if `cpufreq-stats' kernel "
             "module is loaded.",
             filename);
      report_p_stats = false;
      break;
    } else {
      /* Count the number of lines in the file content:
       * /sys/devices/system/cpu/cpu%d/cpufreq/stats/time_in_state */
      FILE *fh = fopen(filename, "r");
      if (fh == NULL) {
        ERROR("cpufreq plugin: File %s not exists or no access.", filename);
        break;
      }
      int state_count = 0;
      char line[DATA_MAX_NAME_LEN] = {0};
      while (fgets(line, sizeof(line), fh) != NULL) {
        state_count++;
      }
      fclose(fh);

      /* variable: max_avail_freqs, takes the maximum value of the actual
       * statistics. */
      max_avail_freqs =
          state_count > max_avail_freqs ? state_count : max_avail_freqs;
    }

    snprintf(filename, sizeof(filename),
             "/sys/devices/system/cpu/cpu%d/cpufreq/stats/total_trans", i);
    if (access(filename, R_OK)) {
      NOTICE("cpufreq plugin: File %s not exists or no access. P-State "
             "statistics will not be reported. Check if `cpufreq-stats' kernel "
             "module is loaded.",
             filename);
      report_p_stats = false;
      break;
    }
  }

  for (int i = 0; i < num_cpu; i++) {
    cpu_data[i].time_state =
        calloc(max_avail_freqs, sizeof(value_to_rate_state_t));
    if (cpu_data[i].time_state == NULL) {
      ERROR("cpufreq plugin: time_state memory allocation failed. P-State "
            "statistics will not be reported.");
      report_p_stats = false;
      return;
    }
  }

  return;
}
#endif /* KERNEL_LINUX */

static int cpufreq_init(void) {
#if KERNEL_LINUX
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
  cpufreq_stats_init();

  if (num_cpu == 0)
    plugin_unregister_read("cpufreq");
#elif KERNEL_FREEBSD
  char mib[] = "dev.cpu.0.freq";
  int cpufreq;
  size_t cf_len = sizeof(cpufreq);

  if (sysctlbyname(mib, &cpufreq, &cf_len, NULL, 0) != 0) {
    WARNING("cpufreq plugin: sysctl \"%s\" failed.", mib);
    plugin_unregister_read("cpufreq");
  }
#endif

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

#if KERNEL_LINUX
static void cpufreq_read_stats(int cpu) {
  char filename[PATH_MAX];
  /* Read total transitions for cpu frequency */
  snprintf(filename, sizeof(filename),
           "/sys/devices/system/cpu/cpu%d/cpufreq/stats/total_trans", cpu);

  value_t v;
  if (parse_value_file(filename, &v, DS_TYPE_DERIVE) != 0) {
    ERROR("cpufreq plugin: Reading \"%s\" failed.", filename);
    return;
  }
  cpufreq_submit(cpu, "transitions", NULL, &v);

  /* Determine percentage time in each state for cpu during previous
   * interval. */
  snprintf(filename, sizeof(filename),
           "/sys/devices/system/cpu/cpu%d/cpufreq/stats/time_in_state", cpu);

  FILE *fh = fopen(filename, "r");
  if (fh == NULL) {
    ERROR("cpufreq plugin: Reading \"%s\" failed.", filename);
    return;
  }

  int state_index = 0;
  cdtime_t now = cdtime();
  char buffer[DATA_MAX_NAME_LEN];

  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    unsigned int frequency;
    unsigned long long time;

    /*
     * State time units is 10ms. To get rate of seconds per second
     * we have to divide by 100. To get percents we have to multiply it
     * by 100 back. So, just use parsed value directly.
     */
    if (!sscanf(buffer, "%u%llu", &frequency, &time)) {
      ERROR("cpufreq plugin: Reading \"%s\" failed.", filename);
      break;
    }

    char state[DATA_MAX_NAME_LEN];
    snprintf(state, sizeof(state), "%u", frequency);

    if (state_index >= max_avail_freqs) {
      NOTICE("cpufreq plugin: Found too many frequency states (%d > %d). "
             "Please open a bug report for "
             "this.",
             (state_index + 1), max_avail_freqs);
      break;
    }

    gauge_t g;
    if (value_to_rate(&g, (value_t){.derive = time}, DS_TYPE_DERIVE, now,
                      &(cpu_data[cpu].time_state[state_index])) == 0) {
      /*
       * Due to some inaccuracy reported value can be a bit greatrer than 100.1.
       * That produces gaps on charts.
       */
      if (g > 100.1)
        g = 100.1;
      cpufreq_submit(cpu, "percent", state, &(value_t){.gauge = g});
    }
    state_index++;
  }
  fclose(fh);
}
#endif /* KERNEL_LINUX */

static int cpufreq_read(void) {
#if KERNEL_LINUX
  for (int cpu = 0; cpu < num_cpu; cpu++) {
    char filename[PATH_MAX];
    /* Read cpu frequency */
    snprintf(filename, sizeof(filename),
             "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);

    value_t v;
    if (parse_value_file(filename, &v, DS_TYPE_GAUGE) != 0) {
      WARNING("cpufreq plugin: Reading \"%s\" failed.", filename);
      continue;
    }

    /* convert kHz to Hz */
    v.gauge *= 1000.0;

    cpufreq_submit(cpu, "cpufreq", NULL, &v);

    if (report_p_stats)
      cpufreq_read_stats(cpu);
  }
#elif KERNEL_FREEBSD
  /* FreeBSD currently only has 1 freq setting.  See BUGS in cpufreq(4) */
  char mib[] = "dev.cpu.0.freq";
  int cpufreq;
  size_t cf_len = sizeof(cpufreq);

  if (sysctlbyname(mib, &cpufreq, &cf_len, NULL, 0) != 0) {
    WARNING("cpufreq plugin: sysctl \"%s\" failed.", mib);
    return 0;
  }

  value_t v;
  /* convert Mhz to Hz */
  v.gauge = cpufreq * 1000000.0;

  cpufreq_submit(0, "cpufreq", NULL, &v);
#endif
  return 0;
} /* int cpufreq_read */

void module_register(void) {
  plugin_register_init("cpufreq", cpufreq_init);
  plugin_register_read("cpufreq", cpufreq_read);
}
