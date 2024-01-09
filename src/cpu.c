/**
 * collectd - src/cpu.c
 * Copyright (C) 2005-2014  Florian octo Forster
 * Copyright (C) 2008       Oleg King
 * Copyright (C) 2009       Simon Kuhnle
 * Copyright (C) 2009       Manuel Sanmartin
 * Copyright (C) 2013-2014  Pierre-Yves Ritschard
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
 *   Oleg King <king2 at kaluga.ru>
 *   Simon Kuhnle <simon at blarzwurst.de>
 *   Manuel Sanmartin
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#ifdef HAVE_MACH_KERN_RETURN_H
#include <mach/kern_return.h>
#endif
#ifdef HAVE_MACH_MACH_INIT_H
#include <mach/mach_init.h>
#endif
#ifdef HAVE_MACH_HOST_PRIV_H
#include <mach/host_priv.h>
#endif
#if HAVE_MACH_MACH_ERROR_H
#include <mach/mach_error.h>
#endif
#ifdef HAVE_MACH_PROCESSOR_INFO_H
#include <mach/processor_info.h>
#endif
#ifdef HAVE_MACH_PROCESSOR_H
#include <mach/processor.h>
#endif
#ifdef HAVE_MACH_VM_MAP_H
#include <mach/vm_map.h>
#endif

#ifdef HAVE_LIBKSTAT
#include <sys/sysinfo.h>
#endif /* HAVE_LIBKSTAT */

#if (defined(HAVE_SYSCTL) && defined(HAVE_SYSCTLBYNAME)) || defined(__OpenBSD__)
/* Implies BSD variant */
#include <sys/sysctl.h>
#endif

#ifdef HAVE_SYS_DKSTAT_H
/* implies BSD variant */
#include <sys/dkstat.h>

#if !defined(CP_USER) || !defined(CP_NICE) || !defined(CP_SYS) ||              \
    !defined(CP_INTR) || !defined(CP_IDLE) || !defined(CPUSTATES)
#define CP_USER 0
#define CP_NICE 1
#define CP_SYS 2
#define CP_INTR 3
#define CP_IDLE 4
#define CPUSTATES 5
#endif
#endif /* HAVE_SYS_DKSTAT_H */

#if (defined(HAVE_SYSCTL) && defined(HAVE_SYSCTLBYNAME)) || defined(__OpenBSD__)
/* Implies BSD variant */
#if defined(CTL_HW) && defined(HW_NCPU) && defined(CTL_KERN) &&                \
    (defined(KERN_CPTIME) || defined(KERN_CP_TIME)) && defined(CPUSTATES)
#define CAN_USE_SYSCTL 1
#else
#define CAN_USE_SYSCTL 0
#endif
#else
#define CAN_USE_SYSCTL 0
#endif /* HAVE_SYSCTL_H && HAVE_SYSCTLBYNAME || __OpenBSD__ */

#if HAVE_STATGRAB_H
#include <statgrab.h>
#endif

#ifdef HAVE_PERFSTAT
#include <libperfstat.h>
#include <sys/protosw.h>
#endif /* HAVE_PERFSTAT */

#if !PROCESSOR_CPU_LOAD_INFO && !KERNEL_LINUX && !HAVE_LIBKSTAT &&             \
    !CAN_USE_SYSCTL && !HAVE_SYSCTLBYNAME && !HAVE_LIBSTATGRAB &&              \
    !HAVE_PERFSTAT
#error "No applicable input method."
#endif

#define CPU_ALL SIZE_MAX

typedef enum {
  STATE_USER,
  STATE_SYSTEM,
  STATE_WAIT,
  STATE_NICE,
  STATE_SWAP,
  STATE_INTERRUPT,
  STATE_SOFTIRQ,
  STATE_STEAL,
  STATE_GUEST,
  STATE_GUEST_NICE,
  STATE_IDLE,
  STATE_ACTIVE, /* sum of (!idle) */
  STATE_MAX,    /* #states */
} state_t;

static const char *cpu_state_names[STATE_MAX] = {
    "user",    "system", "wait",  "nice",       "swap", "interrupt",
    "softirq", "steal",  "guest", "guest_nice", "idle", "active"};

typedef struct {
  gauge_t rate;
  bool has_value;
  value_to_rate_state_t conv;

  /* count is a scaled counter, so that all states in sum increase by 1000000
   * per second. */
  derive_t count;
  bool has_count;
  rate_to_value_state_t to_count;
} usage_state_t;

typedef struct {
  cdtime_t time;
  cdtime_t interval;
  size_t cpu_num;
  bool finalized;

  usage_state_t *states;
  size_t states_num;

  usage_state_t global[STATE_MAX];
} usage_t;

static usage_t usage = {0};

static char const *const label_state = "system.cpu.state";
static char const *const label_number = "system.cpu.logical_number";

#ifdef PROCESSOR_CPU_LOAD_INFO
static mach_port_t port_host;
static processor_port_array_t cpu_list;
static mach_msg_type_number_t cpu_list_len;
/* #endif PROCESSOR_CPU_LOAD_INFO */

#elif defined(KERNEL_LINUX)
/* no variables needed */
/* #endif KERNEL_LINUX */

#elif defined(HAVE_LIBKSTAT)
#if HAVE_KSTAT_H
#include <kstat.h>
#endif
/* colleague tells me that Sun doesn't sell systems with more than 100 or so
 * CPUs.. */
#define MAX_NUMCPU 256
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMCPU];
static int numcpu;
/* #endif HAVE_LIBKSTAT */

#elif CAN_USE_SYSCTL
/* Only possible for (Open) BSD variant */
static int numcpu;
/* #endif CAN_USE_SYSCTL */

#elif defined(HAVE_SYSCTLBYNAME)
/* Implies BSD variant */
static int numcpu;
#ifdef HAVE_SYSCTL_KERN_CP_TIMES
static int maxcpu;
#endif /* HAVE_SYSCTL_KERN_CP_TIMES */
/* #endif HAVE_SYSCTLBYNAME */

#elif defined(HAVE_LIBSTATGRAB)
/* no variables needed */
/* #endif  HAVE_LIBSTATGRAB */

#elif defined(HAVE_PERFSTAT)
#define TOTAL_IDLE 0
#define TOTAL_USER 1
#define TOTAL_SYS 2
#define TOTAL_WAIT 3
#define TOTAL_STAT_NUM 4
static value_to_rate_state_t total_conv[TOTAL_STAT_NUM];
static perfstat_cpu_t *perfcpu;
static int numcpu;
static int pnumcpu;
#endif /* HAVE_PERFSTAT */

#define RATE_ADD(sum, val)                                                     \
  do {                                                                         \
    if (isnan(sum))                                                            \
      (sum) = (val);                                                           \
    else if (!isnan(val))                                                      \
      (sum) += (val);                                                          \
  } while (0)

/* Highest CPU number in the current iteration. Used by the dispatch logic to
 * determine how many CPUs there were. Reset to 0 by cpu_reset(). */
static size_t global_cpu_num;

static bool report_by_cpu = true;
static bool report_by_state = true;
static bool report_usage = true;
static bool report_utilization = true;
static bool report_num_cpu;
static bool report_guest;
static bool subtract_guest = true;

static const char *config_keys[] = {
    "ReportByCpu",        "ReportByState",    "ReportNumCpu",
    "ReportUtilization",  "ValuesPercentage", "ReportGuestState",
    "SubtractGuestState",
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int cpu_config(char const *key, char const *value) /* {{{ */
{
  if (strcasecmp(key, "ReportByCpu") == 0)
    report_by_cpu = IS_TRUE(value);
  else if (strcasecmp(key, "ReportUsage") == 0)
    report_usage = IS_TRUE(value);
  else if (strcasecmp(key, "ReportUtilization") == 0 ||
           strcasecmp(key, "ValuesPercentage") == 0)
    report_utilization = IS_TRUE(value);
  else if (strcasecmp(key, "ReportByState") == 0)
    report_by_state = IS_TRUE(value);
  else if (strcasecmp(key, "ReportNumCpu") == 0)
    report_num_cpu = IS_TRUE(value);
  else if (strcasecmp(key, "ReportGuestState") == 0)
    report_guest = IS_TRUE(value);
  else if (strcasecmp(key, "SubtractGuestState") == 0)
    subtract_guest = IS_TRUE(value);
  else
    return -1;

  return 0;
} /* }}} int cpu_config */

static int init(void) {
#if PROCESSOR_CPU_LOAD_INFO
  kern_return_t status;

  port_host = mach_host_self();

  status = host_processors(port_host, &cpu_list, &cpu_list_len);
  if (status == KERN_INVALID_ARGUMENT) {
    ERROR("cpu plugin: Don't have a privileged host control port. "
          "The most common cause for this problem is "
          "that collectd is running without root "
          "privileges, which are required to read CPU "
          "load information. "
          "<https://collectd.org/bugs/22>");
    cpu_list_len = 0;
    return -1;
  }
  if (status != KERN_SUCCESS) {
    ERROR("cpu plugin: host_processors() failed with status %d.", (int)status);
    cpu_list_len = 0;
    return -1;
  }

  INFO("cpu plugin: Found %i processor%s.", (int)cpu_list_len,
       cpu_list_len == 1 ? "" : "s");
  /* #endif PROCESSOR_CPU_LOAD_INFO */

#elif defined(HAVE_LIBKSTAT)
  kstat_t *ksp_chain;

  numcpu = 0;

  if (kc == NULL)
    return -1;

  /* Solaris doesn't count linear.. *sigh* */
  for (numcpu = 0, ksp_chain = kc->kc_chain;
       (numcpu < MAX_NUMCPU) && (ksp_chain != NULL);
       ksp_chain = ksp_chain->ks_next)
    if (strncmp(ksp_chain->ks_module, "cpu_stat", 8) == 0)
      ksp[numcpu++] = ksp_chain;
      /* #endif HAVE_LIBKSTAT */

#elif CAN_USE_SYSCTL
  /* Only on (Open) BSD variant */
  size_t numcpu_size;
  int mib[2] = {CTL_HW, HW_NCPU};
  int status;

  numcpu = 0;
  numcpu_size = sizeof(numcpu);

  status = sysctl(mib, STATIC_ARRAY_SIZE(mib), &numcpu, &numcpu_size, NULL, 0);
  if (status == -1) {
    WARNING("cpu plugin: sysctl: %s", STRERRNO);
    return -1;
  }
  /* #endif CAN_USE_SYSCTL */

#elif defined(HAVE_SYSCTLBYNAME)
  /* Only on BSD varient */
  size_t numcpu_size;

  numcpu_size = sizeof(numcpu);

  if (sysctlbyname("hw.ncpu", &numcpu, &numcpu_size, NULL, 0) < 0) {
    WARNING("cpu plugin: sysctlbyname(hw.ncpu): %s", STRERRNO);
    return -1;
  }

#ifdef HAVE_SYSCTL_KERN_CP_TIMES
  numcpu_size = sizeof(maxcpu);

  if (sysctlbyname("kern.smp.maxcpus", &maxcpu, &numcpu_size, NULL, 0) < 0) {
    WARNING("cpu plugin: sysctlbyname(kern.smp.maxcpus): %s", STRERRNO);
    return -1;
  }
#else
  if (numcpu != 1)
    NOTICE("cpu: Only one processor supported when using `sysctlbyname' (found "
           "%i)",
           numcpu);
#endif
  /* #endif HAVE_SYSCTLBYNAME */

#elif defined(HAVE_LIBSTATGRAB)
  /* nothing to initialize */
  /* #endif HAVE_LIBSTATGRAB */

#elif defined(HAVE_PERFSTAT)
/* nothing to initialize */
#endif /* HAVE_PERFSTAT */

  return 0;
} /* int init */

static int usage_init(usage_t *u, cdtime_t now) {
  if (u == NULL || now == 0) {
    return EINVAL;
  }

  if (u->time != 0 && u->time < now) {
    u->interval = now - u->time;
  }
  u->time = now;
  u->cpu_num = 0;
  u->finalized = false;
  for (size_t i = 0; i < u->states_num; i++) {
    u->states[i].rate = 0;
    u->states[i].has_value = false;
  }
  for (state_t s = 0; s < STATE_MAX; s++) {
    u->global[s].rate = 0;
    u->global[s].has_value = false;
  }

  return 0;
}

static int usage_resize(usage_t *u, size_t cpu) {
  size_t num = (cpu + 1) * STATE_MAX;
  if (u->states_num >= num) {
    return 0;
  }

  usage_state_t *ptr = realloc(u->states, sizeof(*u->states) * num);
  if (ptr == NULL) {
    return ENOMEM;
  }
  u->states = ptr;
  ptr = u->states + u->states_num;
  memset(ptr, 0, sizeof(*ptr) * (num - u->states_num));
  u->states_num = num;

  return 0;
}

static int usage_record(usage_t *u, size_t cpu, state_t state, derive_t count) {
  if (u == NULL || state >= STATE_ACTIVE) {
    return EINVAL;
  }

  int status = usage_resize(u, cpu);
  if (status != 0) {
    return status;
  }

  if (u->cpu_num < (cpu + 1)) {
    u->cpu_num = cpu + 1;
  }

  size_t index = (cpu * STATE_MAX) + state;
  assert(index < u->states_num);
  usage_state_t *us = u->states + index;

  status = value_to_rate(&us->rate, (value_t){.derive = count}, DS_TYPE_DERIVE,
                         u->time, &us->conv);
  if (status == EAGAIN) {
    return 0;
  }
  if (status != 0) {
    return status;
  }

  us->has_value = true;
  return 0;
}

static void usage_finalize(usage_t *u) {
  if (u->finalized) {
    return;
  }

  gauge_t global_rate = 0;
  size_t cpu_num = u->states_num / STATE_MAX;
  gauge_t state_ratio[STATE_MAX] = {0};
  for (size_t cpu = 0; cpu < cpu_num; cpu++) {
    size_t active_index = (cpu * STATE_MAX) + STATE_ACTIVE;
    usage_state_t *active = u->states + active_index;

    active->rate = 0;
    active->has_value = false;

    gauge_t cpu_rate = 0;

    for (state_t s = 0; s < STATE_ACTIVE; s++) {
      size_t index = (cpu * STATE_MAX) + s;
      usage_state_t *us = u->states + index;

      if (!us->has_value) {
        continue;
      }

      // aggregate by cpu
      cpu_rate += us->rate;

      // aggregate by state
      u->global[s].rate += us->rate;
      u->global[s].has_value = true;

      // global aggregate
      global_rate += us->rate;

      if (s != STATE_IDLE) {
        active->rate += us->rate;
        active->has_value = true;
      }
    }

    if (active->has_value) {
      u->global[STATE_ACTIVE].rate += active->rate;
      u->global[STATE_ACTIVE].has_value = true;
    }

    /* With cpu_rate available, calculate a counter for each state that is
     * normalized to microseconds. I.e. all states of one CPU sum up to 1000000
     * us per second. */
    for (state_t s = 0; s < STATE_MAX; s++) {
      size_t index = (cpu * STATE_MAX) + s;
      usage_state_t *us = u->states + index;

      us->count = -1;
      if (!us->has_value) {
        /* Ensure that us->to_count is initialized. */
        rate_to_value(&(value_t){0}, 0.0, &us->to_count, DS_TYPE_DERIVE,
                      u->time);
        continue;
      }

      gauge_t ratio = us->rate / cpu_rate;
      value_t v = {0};
      int status = rate_to_value(&v, 1000000.0 * ratio, &us->to_count,
                                 DS_TYPE_DERIVE, u->time);
      if (status == 0) {
        us->count = v.derive;
        us->has_count = true;
      }

      state_ratio[s] += ratio;
    }
  }

  for (state_t s = 0; s < STATE_MAX; s++) {
    usage_state_t *us = &u->global[s];

    us->count = -1;
    if (!us->has_value) {
      /* Ensure that us->to_count is initialized. */
      rate_to_value(&(value_t){0}, 0.0, &us->to_count, DS_TYPE_DERIVE, u->time);
      continue;
    }

    value_t v = {0};
    int status = rate_to_value(&v, 1000000.0 * state_ratio[s], &us->to_count,
                               DS_TYPE_DERIVE, u->time);
    if (status == 0) {
      us->count = v.derive;
      us->has_count = true;
    }
  }

  u->finalized = true;
}

static void usage_reset(usage_t *u) {
  if (u == NULL) {
    return;
  }
  free(u->states);
  memset(u, 0, sizeof(*u));
}

static gauge_t usage_global_rate(usage_t *u, state_t state) {
  usage_finalize(u);

  return u->global[state].has_value ? u->global[state].rate : NAN;
}

static gauge_t usage_rate(usage_t *u, size_t cpu, state_t state) {
  usage_finalize(u);

  if (cpu == CPU_ALL) {
    return usage_global_rate(u, state);
  }

  size_t index = (cpu * STATE_MAX) + state;
  if (index >= u->states_num) {
    return NAN;
  }

  usage_state_t us = u->states[index];
  return us.has_value ? us.rate : NAN;
}

static gauge_t usage_global_ratio(usage_t *u, state_t state) {
  usage_finalize(u);

  gauge_t global_rate =
      usage_global_rate(u, STATE_ACTIVE) + usage_global_rate(u, STATE_IDLE);
  return usage_global_rate(u, state) / global_rate;
}

static gauge_t usage_ratio(usage_t *u, size_t cpu, state_t state) {
  usage_finalize(u);

  if (cpu == CPU_ALL) {
    return usage_global_ratio(u, state);
  }

  gauge_t global_rate =
      usage_global_rate(u, STATE_ACTIVE) + usage_global_rate(u, STATE_IDLE);
  return usage_rate(u, cpu, state) / global_rate;
}

static derive_t usage_global_count(usage_t *u, state_t state) {
  usage_finalize(u);

  return u->global[state].count;
}

static derive_t usage_count(usage_t *u, size_t cpu, state_t state) {
  usage_finalize(u);

  if (cpu == CPU_ALL) {
    return usage_global_count(u, state);
  }

  size_t index = (cpu * STATE_MAX) + state;
  if (index >= u->states_num) {
    return -1;
  }
  usage_state_t *us = u->states + index;

  return us->count;
}

/* Commits the number of cores */
static void cpu_commit_num_cpu(gauge_t value) /* {{{ */
{
  metric_family_t fam = {
      .name = "system.cpu.logical.count",
      .help = "Reports the number of logical (virtual) processor cores created "
              "by the operating system to manage multitasking",
      .unit = "{cpu}",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_metric_append(&fam, (metric_t){
                                        .value.gauge = value,
                                    });

  int status = plugin_dispatch_metric_family(&fam);
  if (status != 0) {
    ERROR("plugin_dispatch_metric_family failed: %s", STRERROR(status));
  }

  metric_family_metric_reset(&fam);
  return;
} /* }}} void cpu_commit_num_cpu */

static void commit_cpu_usage(usage_t *u, size_t cpu_num) {
  metric_family_t fam = {
      .name = "system.cpu.time",
      .help = "Microseconds each logical CPU spent in each state",
      .unit = "us",
      .type = METRIC_TYPE_COUNTER,
  };

  metric_t m = {0};
  if (cpu_num != CPU_ALL) {
    char cpu_num_str[64];
    ssnprintf(cpu_num_str, sizeof(cpu_num_str), "%zu", cpu_num);
    metric_label_set(&m, label_number, cpu_num_str);
  }

  if (report_by_state) {
    for (state_t state = 0; state < STATE_ACTIVE; state++) {
      derive_t usage = usage_count(u, cpu_num, state);
      metric_family_append(&fam, label_state, cpu_state_names[state],
                           (value_t){.derive = usage}, &m);
    }
  } else {
    derive_t usage = usage_count(u, cpu_num, STATE_ACTIVE);
    metric_family_append(&fam, label_state, cpu_state_names[STATE_ACTIVE],
                         (value_t){.derive = usage}, &m);
  }

  int status = plugin_dispatch_metric_family(&fam);
  if (status != 0) {
    ERROR("cpu plugin: plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
  }

  metric_reset(&m);
  metric_family_metric_reset(&fam);
}

static void commit_usage(usage_t *u) {
  if (!report_by_cpu) {
    commit_cpu_usage(u, CPU_ALL);
    return;
  }

  for (size_t cpu_num = 0; cpu_num < global_cpu_num; cpu_num++) {
    commit_cpu_usage(u, cpu_num);
  }
}

/* Commits (dispatches) the values for one CPU or the global aggregation.
 * cpu_num is the index of the CPU to be committed or -1 in case of the global
 * aggregation. rates is a pointer to STATE_MAX gauge_t values
 * holding the
 * current rate; each rate may be NAN. Calculates the percentage of each state
 * and dispatches the metric. */
static void commit_cpu_utilization(usage_t *u, size_t cpu_num) {
  metric_family_t fam = {
      .name = "system.cpu.utilization",
      .help = "Difference in system.cpu.time since the last measurement, "
              "divided by the elapsed time and number of logical CPUs",
      .unit = "1",
      .type = METRIC_TYPE_GAUGE,
  };

  metric_t m = {0};
  if (cpu_num != CPU_ALL) {
    char cpu_num_str[64];
    ssnprintf(cpu_num_str, sizeof(cpu_num_str), "%zu", cpu_num);
    metric_label_set(&m, label_number, cpu_num_str);
  }

  if (!report_by_state) {
    gauge_t ratio = usage_ratio(u, cpu_num, STATE_ACTIVE);
    metric_family_append(&fam, label_state, cpu_state_names[STATE_ACTIVE],
                         (value_t){.gauge = ratio}, &m);
  } else {
    for (state_t state = 0; state < STATE_ACTIVE; state++) {
      gauge_t ratio = usage_ratio(u, cpu_num, state);
      metric_family_append(&fam, label_state, cpu_state_names[state],
                           (value_t){.gauge = ratio}, &m);
    }
  }

  int status = plugin_dispatch_metric_family(&fam);
  if (status != 0) {
    ERROR("cpu plugin: plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
  }

  metric_reset(&m);
  metric_family_metric_reset(&fam);
}

static void commit_utilization(usage_t *u) {
  if (!report_by_cpu) {
    commit_cpu_utilization(u, CPU_ALL);
    return;
  }

  for (size_t cpu_num = 0; cpu_num < global_cpu_num; cpu_num++) {
    commit_cpu_utilization(u, cpu_num);
  }
}

/* Aggregates the internal state and dispatches the metrics. */
static void cpu_commit(usage_t *u) /* {{{ */
{
  if (report_num_cpu) {
    cpu_commit_num_cpu((gauge_t)global_cpu_num);
  }

  if (report_usage) {
    commit_usage(u);
  }

  if (report_utilization) {
    commit_utilization(u);
  }
} /* }}} void cpu_commit */

static int cpu_read(void) {
  cdtime_t now = cdtime();
  usage_init(&usage, now);

#if PROCESSOR_CPU_LOAD_INFO /* {{{ */
  kern_return_t status;

  processor_cpu_load_info_data_t cpu_info;
  mach_msg_type_number_t cpu_info_len;

  host_t cpu_host;

  for (mach_msg_type_number_t cpu = 0; cpu < cpu_list_len; cpu++) {
    cpu_host = 0;
    cpu_info_len = PROCESSOR_BASIC_INFO_COUNT;

    status = processor_info(cpu_list[cpu], PROCESSOR_CPU_LOAD_INFO, &cpu_host,
                            (processor_info_t)&cpu_info, &cpu_info_len);
    if (status != KERN_SUCCESS) {
      ERROR("cpu plugin: processor_info (PROCESSOR_CPU_LOAD_INFO) failed: %s",
            mach_error_string(status));
      continue;
    }

    if (cpu_info_len < CPU_STATE_MAX) {
      ERROR("cpu plugin: processor_info returned only %i elements..",
            cpu_info_len);
      continue;
    }

    usage_record(&usage, (size_t)cpu, STATE_USER,
                 (derive_t)cpu_info.cpu_ticks[CPU_STATE_USER]);
    usage_record(&usage, (size_t)cpu, STATE_NICE,
                 (derive_t)cpu_info.cpu_ticks[CPU_STATE_NICE]);
    usage_record(&usage, (size_t)cpu, STATE_SYSTEM,
                 (derive_t)cpu_info.cpu_ticks[CPU_STATE_SYSTEM]);
    usage_record(&usage, (size_t)cpu, STATE_IDLE,
                 (derive_t)cpu_info.cpu_ticks[CPU_STATE_IDLE]);
  }
  /* }}} #endif PROCESSOR_CPU_LOAD_INFO */

#elif defined(KERNEL_LINUX) /* {{{ */
  FILE *fh;
  char buf[1024];

  char *fields[11];
  int numfields;

  if ((fh = fopen("/proc/stat", "r")) == NULL) {
    ERROR("cpu plugin: fopen (/proc/stat) failed: %s", STRERRNO);
    return -1;
  }

  while (fgets(buf, 1024, fh) != NULL) {
    if (strncmp(buf, "cpu", 3))
      continue;
    if ((buf[3] < '0') || (buf[3] > '9'))
      continue;

    numfields = strsplit(buf, fields, STATIC_ARRAY_SIZE(fields));
    if (numfields < 5)
      continue;

    size_t cpu = (size_t)strtoul(fields[0] + 3, NULL, 10);

    /* Do not stage User and Nice immediately: we may need to alter them later:
     */
    long long user_value = atoll(fields[1]);
    long long nice_value = atoll(fields[2]);
    usage_record(&usage, cpu, STATE_SYSTEM, (derive_t)atoll(fields[3]));
    usage_record(&usage, cpu, STATE_IDLE, (derive_t)atoll(fields[4]));

    if (numfields >= 8) {
      usage_record(&usage, cpu, STATE_WAIT, (derive_t)atoll(fields[5]));
      usage_record(&usage, cpu, STATE_INTERRUPT, (derive_t)atoll(fields[6]));
      usage_record(&usage, cpu, STATE_SOFTIRQ, (derive_t)atoll(fields[7]));
    }

    if (numfields >= 9) { /* Steal (since Linux 2.6.11) */
      usage_record(&usage, cpu, STATE_STEAL, (derive_t)atoll(fields[8]));
    }

    if (numfields >= 10) { /* Guest (since Linux 2.6.24) */
      if (report_guest) {
        long long value = atoll(fields[9]);
        usage_record(&usage, cpu, STATE_GUEST, (derive_t)value);
        /* Guest is included in User; optionally subtract Guest from User: */
        if (subtract_guest) {
          user_value -= value;
          if (user_value < 0)
            user_value = 0;
        }
      }
    }

    if (numfields >= 11) { /* Guest_nice (since Linux 2.6.33) */
      if (report_guest) {
        long long value = atoll(fields[10]);
        usage_record(&usage, cpu, STATE_GUEST_NICE, (derive_t)value);
        /* Guest_nice is included in Nice; optionally subtract Guest_nice from
           Nice: */
        if (subtract_guest) {
          nice_value -= value;
          if (nice_value < 0)
            nice_value = 0;
        }
      }
    }

    /* Eventually stage User and Nice: */
    usage_record(&usage, cpu, STATE_USER, (derive_t)user_value);
    usage_record(&usage, cpu, STATE_NICE, (derive_t)nice_value);
  }
  fclose(fh);
  /* }}} #endif defined(KERNEL_LINUX) */

#elif defined(HAVE_LIBKSTAT) /* {{{ */
  static cpu_stat_t cs;

  if (kc == NULL)
    return -1;

  for (int cpu = 0; cpu < numcpu; cpu++) {
    if (kstat_read(kc, ksp[cpu], &cs) == -1)
      continue; /* error message? */

    usage_record(&usage, ksp[cpu]->ks_instance, STATE_IDLE,
                 (derive_t)cs.cpu_sysinfo.cpu[CPU_IDLE]);
    usage_record(&usage, ksp[cpu]->ks_instance, STATE_USER,
                 (derive_t)cs.cpu_sysinfo.cpu[CPU_USER]);
    usage_record(&usage, ksp[cpu]->ks_instance, STATE_SYSTEM,
                 (derive_t)cs.cpu_sysinfo.cpu[CPU_KERNEL]);
    usage_record(&usage, ksp[cpu]->ks_instance, STATE_WAIT,
                 (derive_t)cs.cpu_sysinfo.cpu[CPU_WAIT]);
  }
  /* }}} #endif defined(HAVE_LIBKSTAT) */

#elif CAN_USE_SYSCTL /* {{{ */
  /* Only on (Open) BSD variant */
  uint64_t cpuinfo[numcpu][CPUSTATES];
  size_t cpuinfo_size;
  int status;

  if (numcpu < 1) {
    ERROR("cpu plugin: Could not determine number of "
          "installed CPUs using sysctl(3).");
    return -1;
  }

  memset(cpuinfo, 0, sizeof(cpuinfo));

#if defined(KERN_CP_TIME) && defined(KERNEL_NETBSD)
  {
    int mib[] = {CTL_KERN, KERN_CP_TIME};

    cpuinfo_size = sizeof(cpuinfo[0]) * numcpu * CPUSTATES;
    status = sysctl(mib, 2, cpuinfo, &cpuinfo_size, NULL, 0);
    if (status == -1) {
      char errbuf[1024];

      ERROR("cpu plugin: sysctl failed: %s.",
            sstrerror(errno, errbuf, sizeof(errbuf)));
      return -1;
    }
    if (cpuinfo_size == (sizeof(cpuinfo[0]) * CPUSTATES)) {
      numcpu = 1;
    }
  }
#else /* defined(KERN_CP_TIME) && defined(KERNEL_NETBSD) */
#if defined(KERN_CPTIME2)
  if (numcpu > 1) {
    for (int i = 0; i < numcpu; i++) {
      int mib[] = {CTL_KERN, KERN_CPTIME2, i};

      cpuinfo_size = sizeof(cpuinfo[0]);

      status = sysctl(mib, STATIC_ARRAY_SIZE(mib), cpuinfo[i], &cpuinfo_size,
                      NULL, 0);
      if (status == -1) {
        ERROR("cpu plugin: sysctl failed: %s.", STRERRNO);
        return -1;
      }
    }
  } else
#endif /* defined(KERN_CPTIME2) */
  {
    int mib[] = {CTL_KERN, KERN_CPTIME};
    long cpuinfo_tmp[CPUSTATES];

    cpuinfo_size = sizeof(cpuinfo_tmp);

    status = sysctl(mib, STATIC_ARRAY_SIZE(mib), &cpuinfo_tmp, &cpuinfo_size,
                    NULL, 0);
    if (status == -1) {
      ERROR("cpu plugin: sysctl failed: %s.", STRERRNO);
      return -1;
    }

    for (int i = 0; i < CPUSTATES; i++) {
      cpuinfo[0][i] = cpuinfo_tmp[i];
    }
  }
#endif /* defined(KERN_CP_TIME) && defined(KERNEL_NETBSD) */

  for (int i = 0; i < numcpu; i++) {
    usage_record(&usage, i, STATE_USER, (derive_t)cpuinfo[i][CP_USER]);
    usage_record(&usage, i, STATE_NICE, (derive_t)cpuinfo[i][CP_NICE]);
    usage_record(&usage, i, STATE_SYSTEM, (derive_t)cpuinfo[i][CP_SYS]);
    usage_record(&usage, i, STATE_IDLE, (derive_t)cpuinfo[i][CP_IDLE]);
    usage_record(&usage, i, STATE_INTERRUPT, (derive_t)cpuinfo[i][CP_INTR]);
  }
  /* }}} #endif CAN_USE_SYSCTL */

#elif defined(HAVE_SYSCTLBYNAME) && defined(HAVE_SYSCTL_KERN_CP_TIMES) /* {{{  \
                                                                        */
  /* Only on BSD variant */
  long cpuinfo[maxcpu][CPUSTATES];
  size_t cpuinfo_size;

  memset(cpuinfo, 0, sizeof(cpuinfo));

  cpuinfo_size = sizeof(cpuinfo);
  if (sysctlbyname("kern.cp_times", &cpuinfo, &cpuinfo_size, NULL, 0) < 0) {
    ERROR("cpu plugin: sysctlbyname failed: %s.", STRERRNO);
    return -1;
  }

  for (int i = 0; i < numcpu; i++) {
    usage_record(&usage, i, STATE_USER, (derive_t)cpuinfo[i][CP_USER]);
    usage_record(&usage, i, STATE_NICE, (derive_t)cpuinfo[i][CP_NICE]);
    usage_record(&usage, i, STATE_SYSTEM, (derive_t)cpuinfo[i][CP_SYS]);
    usage_record(&usage, i, STATE_IDLE, (derive_t)cpuinfo[i][CP_IDLE]);
    usage_record(&usage, i, STATE_INTERRUPT, (derive_t)cpuinfo[i][CP_INTR]);
  }
  /* }}} #endif HAVE_SYSCTL_KERN_CP_TIMES */

#elif defined(HAVE_SYSCTLBYNAME) /* {{{ */
  /* Only on BSD variant */
  long cpuinfo[CPUSTATES];
  size_t cpuinfo_size;

  cpuinfo_size = sizeof(cpuinfo);

  if (sysctlbyname("kern.cp_time", &cpuinfo, &cpuinfo_size, NULL, 0) < 0) {
    ERROR("cpu plugin: sysctlbyname failed: %s.", STRERRNO);
    return -1;
  }

  usage_record(&usage, 0, STATE_USER, (derive_t)cpuinfo[CP_USER]);
  usage_record(&usage, 0, STATE_NICE, (derive_t)cpuinfo[CP_NICE]);
  usage_record(&usage, 0, STATE_SYSTEM, (derive_t)cpuinfo[CP_SYS]);
  usage_record(&usage, 0, STATE_IDLE, (derive_t)cpuinfo[CP_IDLE]);
  usage_record(&usage, 0, STATE_INTERRUPT, (derive_t)cpuinfo[CP_INTR]);
  /* }}} #endif HAVE_SYSCTLBYNAME */

#elif defined(HAVE_LIBSTATGRAB) /* {{{ */
  sg_cpu_stats *cs;
  cs = sg_get_cpu_stats();

  if (cs == NULL) {
    ERROR("cpu plugin: sg_get_cpu_stats failed.");
    return -1;
  }

  usage_record(&usage, 0, STATE_IDLE, (derive_t)cs->idle);
  usage_record(&usage, 0, STATE_NICE, (derive_t)cs->nice);
  usage_record(&usage, 0, STATE_SWAP, (derive_t)cs->swap);
  usage_record(&usage, 0, STATE_SYSTEM, (derive_t)cs->kernel);
  usage_record(&usage, 0, STATE_USER, (derive_t)cs->user);
  usage_record(&usage, 0, STATE_WAIT, (derive_t)cs->iowait);
  /* }}} #endif HAVE_LIBSTATGRAB */

#elif defined(HAVE_PERFSTAT) /* {{{ */
  perfstat_id_t id;
  int cpus;

  numcpu = perfstat_cpu(NULL, NULL, sizeof(perfstat_cpu_t), 0);
  if (numcpu == -1) {
    WARNING("cpu plugin: perfstat_cpu: %s", STRERRNO);
    return -1;
  }

  if (pnumcpu != numcpu || perfcpu == NULL) {
    free(perfcpu);
    perfcpu = malloc(numcpu * sizeof(perfstat_cpu_t));
  }
  pnumcpu = numcpu;

  id.name[0] = '\0';
  if ((cpus = perfstat_cpu(&id, perfcpu, sizeof(perfstat_cpu_t), numcpu)) < 0) {
    WARNING("cpu plugin: perfstat_cpu: %s", STRERRNO);
    return -1;
  }

  for (int i = 0; i < cpus; i++) {
    usage_record(&usage, i, STATE_IDLE, (derive_t)perfcpu[i].idle);
    usage_record(&usage, i, STATE_SYSTEM, (derive_t)perfcpu[i].sys);
    usage_record(&usage, i, STATE_USER, (derive_t)perfcpu[i].user);
    usage_record(&usage, i, STATE_WAIT, (derive_t)perfcpu[i].wait);
  }
#endif                       /* }}} HAVE_PERFSTAT */

  cpu_commit(&usage);
  return 0;
}

static int cpu_shutdown(void) {
  usage_reset(&usage);
  return 0;
}

void module_register(void) {
  plugin_register_init("cpu", init);
  plugin_register_config("cpu", cpu_config, config_keys, config_keys_num);
  plugin_register_read("cpu", cpu_read);
  plugin_register_shutdown("cpu", cpu_shutdown);
} /* void module_register */
