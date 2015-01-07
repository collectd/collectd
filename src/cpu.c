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
#include "common.h"
#include "plugin.h"

#ifdef HAVE_MACH_KERN_RETURN_H
# include <mach/kern_return.h>
#endif
#ifdef HAVE_MACH_MACH_INIT_H
# include <mach/mach_init.h>
#endif
#ifdef HAVE_MACH_HOST_PRIV_H
# include <mach/host_priv.h>
#endif
#if HAVE_MACH_MACH_ERROR_H
#  include <mach/mach_error.h>
#endif
#ifdef HAVE_MACH_PROCESSOR_INFO_H
# include <mach/processor_info.h>
#endif
#ifdef HAVE_MACH_PROCESSOR_H
# include <mach/processor.h>
#endif
#ifdef HAVE_MACH_VM_MAP_H
# include <mach/vm_map.h>
#endif

#ifdef HAVE_LIBKSTAT
# include <sys/sysinfo.h>
#endif /* HAVE_LIBKSTAT */

#if (defined(HAVE_SYSCTL) && HAVE_SYSCTL) \
	|| (defined(HAVE_SYSCTLBYNAME) && HAVE_SYSCTLBYNAME)
# ifdef HAVE_SYS_SYSCTL_H
#  include <sys/sysctl.h>
# endif

# ifdef HAVE_SYS_DKSTAT_H
#  include <sys/dkstat.h>
# endif

# if !defined(CP_USER) || !defined(CP_NICE) || !defined(CP_SYS) || !defined(CP_INTR) || !defined(CP_IDLE) || !defined(CPUSTATES)
#  define CP_USER   0
#  define CP_NICE   1
#  define CP_SYS    2
#  define CP_INTR   3
#  define CP_IDLE   4
#  define CPUSTATES 5
# endif
#endif /* HAVE_SYSCTL || HAVE_SYSCTLBYNAME */

#if HAVE_SYSCTL
# if defined(CTL_HW) && defined(HW_NCPU) \
	&& defined(CTL_KERN) && defined(KERN_CPTIME) && defined(CPUSTATES)
#  define CAN_USE_SYSCTL 1
# else
#  define CAN_USE_SYSCTL 0
# endif
#else
# define CAN_USE_SYSCTL 0
#endif

#define COLLECTD_CPU_STATE_USER 0
#define COLLECTD_CPU_STATE_SYSTEM 1
#define COLLECTD_CPU_STATE_WAIT 2
#define COLLECTD_CPU_STATE_NICE 3
#define COLLECTD_CPU_STATE_SWAP 4
#define COLLECTD_CPU_STATE_INTERRUPT 5
#define COLLECTD_CPU_STATE_SOFTIRQ 6
#define COLLECTD_CPU_STATE_STEAL 7
#define COLLECTD_CPU_STATE_IDLE 8
#define COLLECTD_CPU_STATE_ACTIVE 9 /* sum of (!idle) */
#define COLLECTD_CPU_STATE_MAX 10 /* #states */

#if HAVE_STATGRAB_H
# include <statgrab.h>
#endif

# ifdef HAVE_PERFSTAT
#  include <sys/protosw.h>
#  include <libperfstat.h>
# endif /* HAVE_PERFSTAT */

#if !PROCESSOR_CPU_LOAD_INFO && !KERNEL_LINUX && !HAVE_LIBKSTAT \
	&& !CAN_USE_SYSCTL && !HAVE_SYSCTLBYNAME && !HAVE_LIBSTATGRAB && !HAVE_PERFSTAT
# error "No applicable input method."
#endif

static const char *cpu_state_names[] = {
	"user",
	"system",
	"wait",
	"nice",
	"swap",
	"interrupt",
	"softirq",
	"steal",
	"idle",
	"active"
};

#ifdef PROCESSOR_CPU_LOAD_INFO
static mach_port_t port_host;
static processor_port_array_t cpu_list;
static mach_msg_type_number_t cpu_list_len;
/* #endif PROCESSOR_CPU_LOAD_INFO */

#elif defined(KERNEL_LINUX)
/* no variables needed */
/* #endif KERNEL_LINUX */

#elif defined(HAVE_LIBKSTAT)
/* colleague tells me that Sun doesn't sell systems with more than 100 or so CPUs.. */
# define MAX_NUMCPU 256
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMCPU];
static int numcpu;
/* #endif HAVE_LIBKSTAT */

#elif CAN_USE_SYSCTL
static int numcpu;
/* #endif CAN_USE_SYSCTL */

#elif defined(HAVE_SYSCTLBYNAME)
static int numcpu;
#  ifdef HAVE_SYSCTL_KERN_CP_TIMES
static int maxcpu;
#  endif /* HAVE_SYSCTL_KERN_CP_TIMES */
/* #endif HAVE_SYSCTLBYNAME */

#elif defined(HAVE_LIBSTATGRAB)
/* no variables needed */
/* #endif  HAVE_LIBSTATGRAB */

#elif defined(HAVE_PERFSTAT)
static perfstat_cpu_t *perfcpu;
static int numcpu;
static int pnumcpu;
#endif /* HAVE_PERFSTAT */

#define RATE_ADD(sum, val) do { \
	if (isnan (sum))        \
	(sum) = (val);          \
	else if (!isnan (val))  \
	(sum) += (val);         \
} while (0)

struct cpu_state_s
{
	value_to_rate_state_t conv;
	gauge_t rate;
	_Bool has_value;
};
typedef struct cpu_state_s cpu_state_t;

static cpu_state_t *cpu_states = NULL;
static size_t cpu_states_num = 0; /* #cpu_states allocated */

/* Highest CPU number in the current iteration. Used by the dispatch logic to
 * determine how many CPUs there were. Reset to 0 by cpu_reset(). */
static size_t global_cpu_num = 0;

static _Bool report_by_cpu = 1;
static _Bool report_by_state = 1;
static _Bool report_percent = 0;

static const char *config_keys[] =
{
	"ReportByCpu",
	"ReportByState",
	"ValuesPercentage"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int cpu_config (char const *key, char const *value) /* {{{ */
{
	if (strcasecmp (key, "ReportByCpu") == 0)
		report_by_cpu = IS_TRUE (value) ? 1 : 0;
	else if (strcasecmp (key, "ValuesPercentage") == 0)
		report_percent = IS_TRUE (value) ? 1 : 0;
	else if (strcasecmp (key, "ReportByState") == 0)
		report_by_state = IS_TRUE (value) ? 1 : 0;
	else
		return (-1);

	return (0);
} /* }}} int cpu_config */

static int init (void)
{
#if PROCESSOR_CPU_LOAD_INFO
	kern_return_t status;

	port_host = mach_host_self ();

	/* FIXME: Free `cpu_list' if it's not NULL */
	if ((status = host_processors (port_host, &cpu_list, &cpu_list_len)) != KERN_SUCCESS)
	{
		ERROR ("cpu plugin: host_processors returned %i", (int) status);
		cpu_list_len = 0;
		return (-1);
	}

	DEBUG ("host_processors returned %i %s", (int) cpu_list_len, cpu_list_len == 1 ? "processor" : "processors");
	INFO ("cpu plugin: Found %i processor%s.", (int) cpu_list_len, cpu_list_len == 1 ? "" : "s");
/* #endif PROCESSOR_CPU_LOAD_INFO */

#elif defined(HAVE_LIBKSTAT)
	kstat_t *ksp_chain;

	numcpu = 0;

	if (kc == NULL)
		return (-1);

	/* Solaris doesn't count linear.. *sigh* */
	for (numcpu = 0, ksp_chain = kc->kc_chain;
			(numcpu < MAX_NUMCPU) && (ksp_chain != NULL);
			ksp_chain = ksp_chain->ks_next)
		if (strncmp (ksp_chain->ks_module, "cpu_stat", 8) == 0)
			ksp[numcpu++] = ksp_chain;
/* #endif HAVE_LIBKSTAT */

#elif CAN_USE_SYSCTL
	size_t numcpu_size;
	int mib[2] = {CTL_HW, HW_NCPU};
	int status;

	numcpu = 0;
	numcpu_size = sizeof (numcpu);

	status = sysctl (mib, STATIC_ARRAY_SIZE (mib),
			&numcpu, &numcpu_size, NULL, 0);
	if (status == -1)
	{
		char errbuf[1024];
		WARNING ("cpu plugin: sysctl: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}
/* #endif CAN_USE_SYSCTL */

#elif defined (HAVE_SYSCTLBYNAME)
	size_t numcpu_size;

	numcpu_size = sizeof (numcpu);

	if (sysctlbyname ("hw.ncpu", &numcpu, &numcpu_size, NULL, 0) < 0)
	{
		char errbuf[1024];
		WARNING ("cpu plugin: sysctlbyname(hw.ncpu): %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

#ifdef HAVE_SYSCTL_KERN_CP_TIMES
	numcpu_size = sizeof (maxcpu);

	if (sysctlbyname("kern.smp.maxcpus", &maxcpu, &numcpu_size, NULL, 0) < 0)
	{
		char errbuf[1024];
		WARNING ("cpu plugin: sysctlbyname(kern.smp.maxcpus): %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}
#else
	if (numcpu != 1)
		NOTICE ("cpu: Only one processor supported when using `sysctlbyname' (found %i)", numcpu);
#endif
/* #endif HAVE_SYSCTLBYNAME */

#elif defined(HAVE_LIBSTATGRAB)
	/* nothing to initialize */
/* #endif HAVE_LIBSTATGRAB */

#elif defined(HAVE_PERFSTAT)
	/* nothing to initialize */
#endif /* HAVE_PERFSTAT */

	return (0);
} /* int init */

static void submit_value (int cpu_num, int cpu_state, const char *type, value_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	memcpy(&values[0], &value, sizeof(value));

	vl.values = values;
	vl.values_len = 1;

	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "cpu", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, cpu_state_names[cpu_state],
			sizeof (vl.type_instance));

	if (cpu_num >= 0) {
		ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance),
				"%i", cpu_num);
	}
	plugin_dispatch_values (&vl);
}

static void submit_percent(int cpu_num, int cpu_state, gauge_t percent)
{
	value_t value;

	/* This function is called for all known CPU states, but each read
	 * method will only report a subset. The remaining states are left as
	 * NAN and we ignore them here. */
	if (isnan (percent))
		return;

	value.gauge = percent;
	submit_value (cpu_num, cpu_state, "percent", value);
}

static void submit_derive(int cpu_num, int cpu_state, derive_t derive)
{
	value_t value;

	value.derive = derive;
	submit_value (cpu_num, cpu_state, "cpu", value);
}

/* Takes the zero-index number of a CPU and makes sure that the module-global
 * cpu_states buffer is large enough. Returne ENOMEM on erorr. */
static int cpu_states_alloc (size_t cpu_num) /* {{{ */
{
	cpu_state_t *tmp;
	size_t sz;

	sz = (((size_t) cpu_num) + 1) * COLLECTD_CPU_STATE_MAX;
	assert (sz > 0);

	/* We already have enough space. */
	if (cpu_states_num >= sz)
		return 0;

	tmp = realloc (cpu_states, sz * sizeof (*cpu_states));
	if (tmp == NULL)
	{
		ERROR ("cpu plugin: realloc failed.");
		return (ENOMEM);
	}
	cpu_states = tmp;
	tmp = cpu_states + cpu_states_num;

	memset (tmp, 0, (sz - cpu_states_num) * sizeof (*cpu_states));
	cpu_states_num = sz;
	return 0;
} /* }}} cpu_states_alloc */

static cpu_state_t *get_cpu_state (size_t cpu_num, size_t state) /* {{{ */
{
	size_t index = ((cpu_num * COLLECTD_CPU_STATE_MAX) + state);

	if (index >= cpu_states_num)
		return (NULL);

	return (&cpu_states[index]);
} /* }}} cpu_state_t *get_cpu_state */

/* Populates the per-CPU COLLECTD_CPU_STATE_ACTIVE rate and the global rate_by_state
 * array. */
static void aggregate (gauge_t *sum_by_state) /* {{{ */
{
	size_t cpu_num;
	size_t state;

	for (state = 0; state < COLLECTD_CPU_STATE_MAX; state++)
		sum_by_state[state] = NAN;

	for (cpu_num = 0; cpu_num < global_cpu_num; cpu_num++)
	{
		cpu_state_t *this_cpu_states = get_cpu_state (cpu_num, 0);

		this_cpu_states[COLLECTD_CPU_STATE_ACTIVE].rate = NAN;

		for (state = 0; state < COLLECTD_CPU_STATE_ACTIVE; state++)
		{
			if (!this_cpu_states[state].has_value)
				continue;

			RATE_ADD (sum_by_state[state], this_cpu_states[state].rate);
			if (state != COLLECTD_CPU_STATE_IDLE)
				RATE_ADD (this_cpu_states[COLLECTD_CPU_STATE_ACTIVE].rate, this_cpu_states[state].rate);
		}

		if (!isnan (this_cpu_states[COLLECTD_CPU_STATE_ACTIVE].rate))
			this_cpu_states[COLLECTD_CPU_STATE_ACTIVE].has_value = 1;

		RATE_ADD (sum_by_state[COLLECTD_CPU_STATE_ACTIVE], this_cpu_states[COLLECTD_CPU_STATE_ACTIVE].rate);
	}
} /* }}} void aggregate */

/* Commits (dispatches) the values for one CPU or the global aggregation.
 * cpu_num is the index of the CPU to be committed or -1 in case of the global
 * aggregation. rates is a pointer to COLLECTD_CPU_STATE_MAX gauge_t values holding the
 * current rate; each rate may be NAN. Calculates the percentage of each state
 * and dispatches the metric. */
static void cpu_commit_one (int cpu_num, /* {{{ */
		gauge_t rates[static COLLECTD_CPU_STATE_MAX])
{
	size_t state;
	gauge_t sum;

	sum = rates[COLLECTD_CPU_STATE_ACTIVE];
	RATE_ADD (sum, rates[COLLECTD_CPU_STATE_IDLE]);

	if (!report_by_state)
	{
		gauge_t percent = 100.0 * rates[COLLECTD_CPU_STATE_ACTIVE] / sum;
		submit_percent (cpu_num, COLLECTD_CPU_STATE_ACTIVE, percent);
		return;
	}

	for (state = 0; state < COLLECTD_CPU_STATE_ACTIVE; state++)
	{
		gauge_t percent = 100.0 * rates[state] / sum;
		submit_percent (cpu_num, state, percent);
	}
} /* }}} void cpu_commit_one */

/* Resets the internal aggregation. This is called by the read callback after
 * each iteration / after each call to cpu_commit(). */
static void cpu_reset (void) /* {{{ */
{
	size_t i;

	for (i = 0; i < cpu_states_num; i++)
		cpu_states[i].has_value = 0;

	global_cpu_num = 0;
} /* }}} void cpu_reset */

/* Legacy behavior: Dispatches the raw derive values without any aggregation. */
static void cpu_commit_without_aggregation (void) /* {{{ */
{
	int state;

	for (state = 0; state < COLLECTD_CPU_STATE_ACTIVE; state++)
	{
		size_t cpu_num;

		for (cpu_num = 0; cpu_num < global_cpu_num; cpu_num++)
		{
			cpu_state_t *s = get_cpu_state (cpu_num, state);

			if (!s->has_value)
				continue;

			submit_derive ((int) cpu_num, (int) state, s->conv.last_value.derive);
		}
	}
} /* }}} void cpu_commit_without_aggregation */

/* Aggregates the internal state and dispatches the metrics. */
static void cpu_commit (void) /* {{{ */
{
	gauge_t global_rates[COLLECTD_CPU_STATE_MAX] = {
		NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN /* Batman! */
	};
	size_t cpu_num;

	if (report_by_state && report_by_cpu && !report_percent)
	{
		cpu_commit_without_aggregation ();
		return;
	}

	aggregate (global_rates);

	if (!report_by_cpu)
	{
		cpu_commit_one (-1, global_rates);
		return;
	}

	for (cpu_num = 0; cpu_num < global_cpu_num; cpu_num++)
	{
		cpu_state_t *this_cpu_states = get_cpu_state (cpu_num, 0);
		gauge_t local_rates[COLLECTD_CPU_STATE_MAX] = {
			NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN
		};
		size_t state;

		for (state = 0; state < COLLECTD_CPU_STATE_MAX; state++)
			if (this_cpu_states[state].has_value)
				local_rates[state] = this_cpu_states[state].rate;

		cpu_commit_one ((int) cpu_num, local_rates);
	}
} /* }}} void cpu_commit */

/* Adds a derive value to the internal state. This should be used by each read
 * function for each state. At the end of the iteration, the read function
 * should call cpu_commit(). */
static int cpu_stage (size_t cpu_num, size_t state, derive_t value, cdtime_t now) /* {{{ */
{
	int status;
	cpu_state_t *s;
	value_t v;

	if (state >= COLLECTD_CPU_STATE_ACTIVE)
		return (EINVAL);

	status = cpu_states_alloc (cpu_num);
	if (status != 0)
		return (status);

	if (global_cpu_num <= cpu_num)
		global_cpu_num = cpu_num + 1;

	s = get_cpu_state (cpu_num, state);

	v.gauge = NAN;
	status = value_to_rate (&v, value, &s->conv, DS_TYPE_DERIVE, now);
	if (status != 0)
		return (status);

	s->rate = v.gauge;
	s->has_value = 1;
	return (0);
} /* }}} int cpu_stage */

static int cpu_read (void)
{
	cdtime_t now = cdtime ();

#if PROCESSOR_CPU_LOAD_INFO /* {{{ */
	int cpu;

	kern_return_t status;

	processor_cpu_load_info_data_t cpu_info;
	mach_msg_type_number_t         cpu_info_len;

	host_t cpu_host;

	for (cpu = 0; cpu < cpu_list_len; cpu++)
	{
		cpu_host = 0;
		cpu_info_len = PROCESSOR_BASIC_INFO_COUNT;

		status = processor_info (cpu_list[cpu], PROCESSOR_CPU_LOAD_INFO, &cpu_host,
				(processor_info_t) &cpu_info, &cpu_info_len);
		if (status != KERN_SUCCESS)
		{
			ERROR ("cpu plugin: processor_info (PROCESSOR_CPU_LOAD_INFO) failed: %s",
					mach_error_string (status));
			continue;
		}

		if (cpu_info_len < COLLECTD_CPU_STATE_MAX)
		{
			ERROR ("cpu plugin: processor_info returned only %i elements..", cpu_info_len);
			continue;
		}

		cpu_stage (cpu, COLLECTD_CPU_STATE_USER,   (derive_t) cpu_info.cpu_ticks[COLLECTD_CPU_STATE_USER],   now);
		cpu_stage (cpu, COLLECTD_CPU_STATE_NICE,   (derive_t) cpu_info.cpu_ticks[COLLECTD_CPU_STATE_NICE],   now);
		cpu_stage (cpu, COLLECTD_CPU_STATE_SYSTEM, (derive_t) cpu_info.cpu_ticks[COLLECTD_CPU_STATE_SYSTEM], now);
		cpu_stage (cpu, COLLECTD_CPU_STATE_IDLE,   (derive_t) cpu_info.cpu_ticks[COLLECTD_CPU_STATE_IDLE],   now);
	}
/* }}} #endif PROCESSOR_CPU_LOAD_INFO */

#elif defined(KERNEL_LINUX) /* {{{ */
	int cpu;
	FILE *fh;
	char buf[1024];

	char *fields[9];
	int numfields;

	if ((fh = fopen ("/proc/stat", "r")) == NULL)
	{
		char errbuf[1024];
		ERROR ("cpu plugin: fopen (/proc/stat) failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buf, 1024, fh) != NULL)
	{
		if (strncmp (buf, "cpu", 3))
			continue;
		if ((buf[3] < '0') || (buf[3] > '9'))
			continue;

		numfields = strsplit (buf, fields, 9);
		if (numfields < 5)
			continue;

		cpu = atoi (fields[0] + 3);

		cpu_stage (cpu, COLLECTD_CPU_STATE_USER,   (derive_t) atoll(fields[1]), now);
		cpu_stage (cpu, COLLECTD_CPU_STATE_NICE,   (derive_t) atoll(fields[2]), now);
		cpu_stage (cpu, COLLECTD_CPU_STATE_SYSTEM, (derive_t) atoll(fields[3]), now);
		cpu_stage (cpu, COLLECTD_CPU_STATE_IDLE,   (derive_t) atoll(fields[4]), now);

		if (numfields >= 8)
		{
			cpu_stage (cpu, COLLECTD_CPU_STATE_WAIT,      (derive_t) atoll(fields[5]), now);
			cpu_stage (cpu, COLLECTD_CPU_STATE_INTERRUPT, (derive_t) atoll(fields[6]), now);
			cpu_stage (cpu, COLLECTD_CPU_STATE_SOFTIRQ,   (derive_t) atoll(fields[7]), now);

			if (numfields >= 9)
				cpu_stage (cpu, COLLECTD_CPU_STATE_STEAL, (derive_t) atoll(fields[8]), now);
		}
	}
	fclose (fh);
/* }}} #endif defined(KERNEL_LINUX) */

#elif defined(HAVE_LIBKSTAT) /* {{{ */
	int cpu;
	static cpu_stat_t cs;

	if (kc == NULL)
		return (-1);

	for (cpu = 0; cpu < numcpu; cpu++)
	{
		if (kstat_read (kc, ksp[cpu], &cs) == -1)
			continue; /* error message? */

		cpu_stage (ksp[cpu]->ks_instance, COLLECTD_CPU_STATE_IDLE,   (derive_t) cs.cpu_sysinfo.cpu[CPU_IDLE],   now);
		cpu_stage (ksp[cpu]->ks_instance, COLLECTD_CPU_STATE_USER,   (derive_t) cs.cpu_sysinfo.cpu[CPU_USER],   now);
		cpu_stage (ksp[cpu]->ks_instance, COLLECTD_CPU_STATE_SYSTEM, (derive_t) cs.cpu_sysinfo.cpu[CPU_KERNEL], now);
		cpu_stage (ksp[cpu]->ks_instance, COLLECTD_CPU_STATE_WAIT,   (derive_t) cs.cpu_sysinfo.cpu[CPU_WAIT],   now);
	}
/* }}} #endif defined(HAVE_LIBKSTAT) */

#elif CAN_USE_SYSCTL /* {{{ */
	uint64_t cpuinfo[numcpu][CPUSTATES];
	size_t cpuinfo_size;
	int status;
	int i;

	if (numcpu < 1)
	{
		ERROR ("cpu plugin: Could not determine number of "
				"installed CPUs using sysctl(3).");
		return (-1);
	}

	memset (cpuinfo, 0, sizeof (cpuinfo));

#if defined(KERN_CPTIME2)
	if (numcpu > 1) {
		for (i = 0; i < numcpu; i++) {
			int mib[] = {CTL_KERN, KERN_CPTIME2, i};

			cpuinfo_size = sizeof (cpuinfo[0]);

			status = sysctl (mib, STATIC_ARRAY_SIZE (mib),
					cpuinfo[i], &cpuinfo_size, NULL, 0);
			if (status == -1) {
				char errbuf[1024];
				ERROR ("cpu plugin: sysctl failed: %s.",
						sstrerror (errno, errbuf, sizeof (errbuf)));
				return (-1);
			}
		}
	}
	else
#endif /* defined(KERN_CPTIME2) */
	{
		int mib[] = {CTL_KERN, KERN_CPTIME};
		long cpuinfo_tmp[CPUSTATES];

		cpuinfo_size = sizeof(cpuinfo_tmp);

		status = sysctl (mib, STATIC_ARRAY_SIZE (mib),
					&cpuinfo_tmp, &cpuinfo_size, NULL, 0);
		if (status == -1)
		{
			char errbuf[1024];
			ERROR ("cpu plugin: sysctl failed: %s.",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			return (-1);
		}

		for(i = 0; i < CPUSTATES; i++) {
			cpuinfo[0][i] = cpuinfo_tmp[i];
		}
	}

	for (i = 0; i < numcpu; i++) {
		cpu_stage (i, COLLECTD_CPU_STATE_USER,      (derive_t) cpuinfo[i][CP_USER], now);
		cpu_stage (i, COLLECTD_CPU_STATE_NICE,      (derive_t) cpuinfo[i][CP_NICE], now);
		cpu_stage (i, COLLECTD_CPU_STATE_SYSTEM,    (derive_t) cpuinfo[i][CP_SYS], now);
		cpu_stage (i, COLLECTD_CPU_STATE_IDLE,      (derive_t) cpuinfo[i][CP_IDLE], now);
		cpu_stage (i, COLLECTD_CPU_STATE_INTERRUPT, (derive_t) cpuinfo[i][CP_INTR], now);
	}
/* }}} #endif CAN_USE_SYSCTL */

#elif defined(HAVE_SYSCTLBYNAME) && defined(HAVE_SYSCTL_KERN_CP_TIMES) /* {{{ */
	long cpuinfo[maxcpu][CPUSTATES];
	size_t cpuinfo_size;
	int i;

	memset (cpuinfo, 0, sizeof (cpuinfo));

	cpuinfo_size = sizeof (cpuinfo);
	if (sysctlbyname("kern.cp_times", &cpuinfo, &cpuinfo_size, NULL, 0) < 0)
	{
		char errbuf[1024];
		ERROR ("cpu plugin: sysctlbyname failed: %s.",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	for (i = 0; i < numcpu; i++) {
		cpu_stage (i, COLLECTD_CPU_STATE_USER,      (derive_t) cpuinfo[i][CP_USER], now);
		cpu_stage (i, COLLECTD_CPU_STATE_NICE,      (derive_t) cpuinfo[i][CP_NICE], now);
		cpu_stage (i, COLLECTD_CPU_STATE_SYSTEM,    (derive_t) cpuinfo[i][CP_SYS], now);
		cpu_stage (i, COLLECTD_CPU_STATE_IDLE,      (derive_t) cpuinfo[i][CP_IDLE], now);
		cpu_stage (i, COLLECTD_CPU_STATE_INTERRUPT, (derive_t) cpuinfo[i][CP_INTR], now);
	}
/* }}} #endif HAVE_SYSCTL_KERN_CP_TIMES */

#elif defined(HAVE_SYSCTLBYNAME) /* {{{ */
	long cpuinfo[CPUSTATES];
	size_t cpuinfo_size;

	cpuinfo_size = sizeof (cpuinfo);

	if (sysctlbyname("kern.cp_time", &cpuinfo, &cpuinfo_size, NULL, 0) < 0)
	{
		char errbuf[1024];
		ERROR ("cpu plugin: sysctlbyname failed: %s.",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	cpu_stage (0, COLLECTD_CPU_STATE_USER,      (derive_t) cpuinfo[CP_USER], now);
	cpu_stage (0, COLLECTD_CPU_STATE_NICE,      (derive_t) cpuinfo[CP_NICE], now);
	cpu_stage (0, COLLECTD_CPU_STATE_SYSTEM,    (derive_t) cpuinfo[CP_SYS], now);
	cpu_stage (0, COLLECTD_CPU_STATE_IDLE,      (derive_t) cpuinfo[CP_IDLE], now);
	cpu_stage (0, COLLECTD_CPU_STATE_INTERRUPT, (derive_t) cpuinfo[CP_INTR], now);
/* }}} #endif HAVE_SYSCTLBYNAME */

#elif defined(HAVE_LIBSTATGRAB) /* {{{ */
	sg_cpu_stats *cs;
	cs = sg_get_cpu_stats ();

	if (cs == NULL)
	{
		ERROR ("cpu plugin: sg_get_cpu_stats failed.");
		return (-1);
	}

	cpu_state (0, COLLECTD_CPU_STATE_IDLE,   (derive_t) cs->idle);
	cpu_state (0, COLLECTD_CPU_STATE_NICE,   (derive_t) cs->nice);
	cpu_state (0, COLLECTD_CPU_STATE_SWAP,   (derive_t) cs->swap);
	cpu_state (0, COLLECTD_CPU_STATE_SYSTEM, (derive_t) cs->kernel);
	cpu_state (0, COLLECTD_CPU_STATE_USER,   (derive_t) cs->user);
	cpu_state (0, COLLECTD_CPU_STATE_WAIT,   (derive_t) cs->iowait);
/* }}} #endif HAVE_LIBSTATGRAB */

#elif defined(HAVE_PERFSTAT) /* {{{ */
	perfstat_id_t id;
	int i, cpus;

	numcpu =  perfstat_cpu(NULL, NULL, sizeof(perfstat_cpu_t), 0);
	if(numcpu == -1)
	{
		char errbuf[1024];
		WARNING ("cpu plugin: perfstat_cpu: %s",
			sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	if (pnumcpu != numcpu || perfcpu == NULL)
	{
		if (perfcpu != NULL)
			free(perfcpu);
		perfcpu = malloc(numcpu * sizeof(perfstat_cpu_t));
	}
	pnumcpu = numcpu;

	id.name[0] = '\0';
	if ((cpus = perfstat_cpu(&id, perfcpu, sizeof(perfstat_cpu_t), numcpu)) < 0)
	{
		char errbuf[1024];
		WARNING ("cpu plugin: perfstat_cpu: %s",
			sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	for (i = 0; i < cpus; i++)
	{
		cpu_stage (i, COLLECTD_CPU_STATE_IDLE,   (derive_t) perfcpu[i].idle, now);
		cpu_stage (i, COLLECTD_CPU_STATE_SYSTEM, (derive_t) perfcpu[i].sys,  now);
		cpu_stage (i, COLLECTD_CPU_STATE_USER,   (derive_t) perfcpu[i].user, now);
		cpu_stage (i, COLLECTD_CPU_STATE_WAIT,   (derive_t) perfcpu[i].wait, now);
	}
#endif /* }}} HAVE_PERFSTAT */

	cpu_commit ();
	cpu_reset ();
	return (0);
}

void module_register (void)
{
	plugin_register_init ("cpu", init);
	plugin_register_config ("cpu", cpu_config, config_keys, config_keys_num);
	plugin_register_read ("cpu", cpu_read);
} /* void module_register */

/* vim: set sw=8 sts=8 noet fdm=marker : */
