/*
 * turbostat -- Log CPU frequency and C-state residency
 * on modern Intel turbo-capable processors for collectd.
 *
 * Based on the 'turbostat' tool of the Linux kernel, found at
 * linux/tools/power/x86/turbostat/turbostat.c:
 * ----
 * Copyright (c) 2013 Intel Corporation.
 * Len Brown <len.brown@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * ----
 * Ported to collectd by Vincent Brillault <git@lerya.net>
 */

/*
 * _GNU_SOURCE is required because of the following functions:
 * - CPU_ISSET_S
 * - CPU_ZERO_S
 * - CPU_SET_S
 * - CPU_FREE
 * - CPU_ALLOC
 * - CPU_ALLOC_SIZE
 */
#define _GNU_SOURCE

#include <asm/msr-index.h>
#include <stdarg.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <ctype.h>
#include <sched.h>
#include <cpuid.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#define PLUGIN_NAME "turbostat"

/*
 * This tool uses the Model-Specific Registers (MSRs) present on Intel processors.
 * The general description each of these registers, depending on the architecture,
 * can be found in the Intel® 64 and IA-32 Architectures Software Developer Manual,
 * Volume 3 Chapter 35.
 */

/*
 * If set, aperf_mperf_unstable disables a/mperf based stats.
 * This includes: C0 & C1 states, frequency
 *
 * This value is automatically set if mperf or aperf go backward
 */
static _Bool aperf_mperf_unstable;

/*
 * Bitmask of the list of core C states supported by the processor.
 * Currently supported C-states (by this plugin): 3, 6, 7
 */
static unsigned int do_core_cstate;

/*
 * Bitmask of the list of pacages C states supported by the processor.
 * Currently supported C-states (by this plugin): 2, 3, 6, 7, 8, 9, 10
 */
static unsigned int do_pkg_cstate;

/*
 * Boolean indicating if the processor supports 'Digital temperature sensor'
 * This feature enables the monitoring of the temperature of each core
 *
 * This feature has two limitations:
 *  - if MSR_IA32_TEMPERATURE_TARGET is not supported, the absolute temperature might be wrong
 *  - Temperatures above the tcc_activation_temp are not recorded
 */
static _Bool do_dts;

/*
 * Boolean indicating if the processor supports 'Package thermal management'
 * This feature allows the monitoring of the temperature of each package
 *
 * This feature has two limitations:
 *  - if MSR_IA32_TEMPERATURE_TARGET is not supported, the absolute temperature might be wrong
 *  - Temperatures above the tcc_activation_temp are not recorded
 */
static _Bool do_ptm;

/*
 * Thermal Control Circuit Activation Temperature as configured by the user.
 * This override the automated detection via MSR_IA32_TEMPERATURE_TARGET
 * and should only be used if the automated detection fails.
 */
static unsigned int tcc_activation_temp;

static unsigned int do_rapl;
static double rapl_energy_units;

#define RAPL_PKG		(1 << 0)
					/* 0x610 MSR_PKG_POWER_LIMIT */
					/* 0x611 MSR_PKG_ENERGY_STATUS */
#define RAPL_PKG_PERF_STATUS	(1 << 1)
					/* 0x613 MSR_PKG_PERF_STATUS */
#define RAPL_PKG_POWER_INFO	(1 << 2)
					/* 0x614 MSR_PKG_POWER_INFO */

#define RAPL_DRAM		(1 << 3)
					/* 0x618 MSR_DRAM_POWER_LIMIT */
					/* 0x619 MSR_DRAM_ENERGY_STATUS */
					/* 0x61c MSR_DRAM_POWER_INFO */
#define RAPL_DRAM_PERF_STATUS	(1 << 4)
					/* 0x61b MSR_DRAM_PERF_STATUS */

#define RAPL_CORES		(1 << 5)
					/* 0x638 MSR_PP0_POWER_LIMIT */
					/* 0x639 MSR_PP0_ENERGY_STATUS */
#define RAPL_CORE_POLICY	(1 << 6)
					/* 0x63a MSR_PP0_POLICY */


#define RAPL_GFX		(1 << 7)
					/* 0x640 MSR_PP1_POWER_LIMIT */
					/* 0x641 MSR_PP1_ENERGY_STATUS */
					/* 0x642 MSR_PP1_POLICY */
#define	TJMAX_DEFAULT	100

cpu_set_t *cpu_present_set, *cpu_affinity_set, *cpu_saved_affinity_set;
size_t cpu_present_setsize, cpu_affinity_setsize, cpu_saved_affinity_setsize;

struct thread_data {
	unsigned long long tsc;
	unsigned long long aperf;
	unsigned long long mperf;
	unsigned long long c1;
	unsigned int smi_count;
	unsigned int cpu_id;
	unsigned int flags;
#define CPU_IS_FIRST_THREAD_IN_CORE	0x2
#define CPU_IS_FIRST_CORE_IN_PACKAGE	0x4
} *thread_delta, *thread_even, *thread_odd;

struct core_data {
	unsigned long long c3;
	unsigned long long c6;
	unsigned long long c7;
	unsigned int core_temp_c;
	unsigned int core_id;
} *core_delta, *core_even, *core_odd;

struct pkg_data {
	unsigned long long pc2;
	unsigned long long pc3;
	unsigned long long pc6;
	unsigned long long pc7;
	unsigned long long pc8;
	unsigned long long pc9;
	unsigned long long pc10;
	unsigned int package_id;
	unsigned int energy_pkg;	/* MSR_PKG_ENERGY_STATUS */
	unsigned int energy_dram;	/* MSR_DRAM_ENERGY_STATUS */
	unsigned int energy_cores;	/* MSR_PP0_ENERGY_STATUS */
	unsigned int energy_gfx;	/* MSR_PP1_ENERGY_STATUS */
	unsigned int rapl_pkg_perf_status;	/* MSR_PKG_PERF_STATUS */
	unsigned int rapl_dram_perf_status;	/* MSR_DRAM_PERF_STATUS */
	unsigned int tcc_activation_temp;
	unsigned int pkg_temp_c;
} *package_delta, *package_even, *package_odd;

#define DELTA_COUNTERS thread_delta, core_delta, package_delta
#define ODD_COUNTERS thread_odd, core_odd, package_odd
#define EVEN_COUNTERS thread_even, core_even, package_even
static _Bool is_even = 1;

static _Bool allocated = 0;
static _Bool initialized = 0;

#define GET_THREAD(thread_base, thread_no, core_no, pkg_no) \
	(thread_base + \
		(pkg_no) * topology.num_cores * topology.num_threads + \
		(core_no) * topology.num_threads + \
		(thread_no))
#define GET_CORE(core_base, core_no, pkg_no) \
	(core_base + \
		(pkg_no) * topology.num_cores + \
		(core_no))
#define GET_PKG(pkg_base, pkg_no) (pkg_base + pkg_no)

struct cpu_topology {
	int package_id;
	int core_id;
	_Bool first_core_in_package;
	_Bool first_thread_in_core;
};

struct topology {
	int max_cpu_id;
	int num_packages;
	int num_cores;
	int num_threads;
	struct cpu_topology *cpus;
} topology;

struct timeval tv_even, tv_odd, tv_delta;

enum return_values {
	OK = 0,
	ERR_CPU_MIGRATE,
	ERR_CPU_SAVE_SCHED_AFFINITY,
	ERR_MSR_IA32_APERF,
	ERR_MSR_IA32_MPERF,
	ERR_MSR_SMI_COUNT,
	ERR_MSR_CORE_C3_RESIDENCY,
	ERR_MSR_CORE_C6_RESIDENCY,
	ERR_MSR_CORE_C7_RESIDENCY,
	ERR_MSR_IA32_THERM_STATUS,
	ERR_MSR_PKG_C3_RESIDENCY,
	ERR_MSR_PKG_C6_RESIDENCY,
	ERR_MSR_PKG_C2_RESIDENCY,
	ERR_MSR_PKG_C7_RESIDENCY,
	ERR_MSR_PKG_C8_RESIDENCY,
	ERR_MSR_PKG_C9_RESIDENCY,
	ERR_MSR_PKG_C10_RESIDENCY,
	ERR_MSR_PKG_ENERGY_STATUS,
	ERR_MSR_PKG_POWER_INFO,
	ERR_MSR_PP0_ENERGY_STATUS,
	ERR_MSR_DRAM_ENERGY_STATUS,
	ERR_MSR_PP1_ENERGY_STATUS,
	ERR_MSR_PKG_PERF_STATUS,
	ERR_MSR_DRAM_PERF_STATUS,
	ERR_MSR_IA32_PACKAGE_THERM_STATUS,
	ERR_MSR_IA32_TSC,
	ERR_CPU_NOT_PRESENT,
	ERR_NO_MSR,
	ERR_CANT_OPEN_MSR,
	ERR_CANT_OPEN_FILE,
	ERR_CANT_READ_NUMBER,
	ERR_CANT_READ_PROC_STAT,
	ERR_NO_INVARIANT_TSC,
	ERR_NO_APERF,
	ERR_CALLOC,
	ERR_CPU_ALLOC,
	ERR_NOT_ROOT,
	UNSUPPORTED_CPU,
};


/*****************************
 *  MSR Manipulation helpers *
 *****************************/

/*
 * Open a MSR device for reading
 * Can change the scheduling affinity of the current process if multiple_read is 1
 */
static int __attribute__((warn_unused_result))
open_msr(int cpu, _Bool multiple_read)
{
	char pathname[32];
	int fd;

	/*
	 * If we need to do multiple read, let's migrate to the CPU
	 * Otherwise, we would lose time calling functions on another CPU
	 *
	 * If we are not yet initialized (cpu_affinity_setsize = 0),
	 * we need to skip this optimisation.
	 */
	if (multiple_read && cpu_affinity_setsize) {
		CPU_ZERO_S(cpu_affinity_setsize, cpu_affinity_set);
		CPU_SET_S(cpu, cpu_affinity_setsize, cpu_affinity_set);
		if (sched_setaffinity(0, cpu_affinity_setsize, cpu_affinity_set) == -1) {
			ERROR("Turbostat plugin: Could not migrate to CPU %d", cpu);
			return -ERR_CPU_MIGRATE;
		}
	}

	ssnprintf(pathname, sizeof(pathname), "/dev/cpu/%d/msr", cpu);
	fd = open(pathname, O_RDONLY);
	if (fd < 0)
		return -ERR_CANT_OPEN_MSR;
	return fd;
}

/*
 * Read a single MSR from an open file descriptor
 */
static int __attribute__((warn_unused_result))
read_msr(int fd, off_t offset, unsigned long long *msr)
{
	ssize_t retval;

	retval = pread(fd, msr, sizeof *msr, offset);

	if (retval != sizeof *msr) {
		ERROR("Turbostat plugin: MSR offset 0x%llx read failed",
		      (unsigned long long)offset);
		return -1;
	}
	return 0;
}

/*
 * Open a MSR device for reading, read the value asked for and close it.
 * This call will not affect the scheduling affinity of this thread.
 */
static int __attribute__((warn_unused_result))
get_msr(int cpu, off_t offset, unsigned long long *msr)
{
	ssize_t retval;
	int fd;

	fd = open_msr(cpu, 0);
	if (fd < 0)
		return fd;
	retval = read_msr(fd, offset, msr);
	close(fd);
	return retval;
}


/********************************
 * Raw data acquisition (1 CPU) *
 ********************************/

/*
 * Read every data avalaible for a single CPU
 *
 * Core data is shared for all threads in one core: extracted only for the first thread
 * Package data is shared for all core in one package: extracted only for the first thread of the first core
 *
 * Side effect: migrates to the targeted CPU
 */
static int __attribute__((warn_unused_result))
get_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	int cpu = t->cpu_id;
	unsigned long long msr;
	int msr_fd;
	int retval = 0;

	msr_fd = open_msr(cpu, 1);
	if (msr_fd < 0)
		return msr_fd;

#define READ_MSR(msr, dst)			\
do {						\
	if (read_msr(msr_fd, msr, dst)) {	\
		retval = -ERR_##msr;		\
		goto out;			\
	}					\
} while (0)

	READ_MSR(MSR_IA32_TSC, &t->tsc);

	READ_MSR(MSR_IA32_APERF, &t->aperf);
	READ_MSR(MSR_IA32_MPERF, &t->mperf);

	READ_MSR(MSR_SMI_COUNT, &msr);
	t->smi_count = msr & 0xFFFFFFFF;

	/* collect core counters only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE)) {
		retval = 0;
		goto out;
	}

	if (do_core_cstate & (1 << 3))
		READ_MSR(MSR_CORE_C3_RESIDENCY, &c->c3);
	if (do_core_cstate & (1 << 6))
		READ_MSR(MSR_CORE_C6_RESIDENCY, &c->c6);
	if (do_core_cstate & (1 << 7))
		READ_MSR(MSR_CORE_C7_RESIDENCY, &c->c7);

	if (do_dts) {
		READ_MSR(MSR_IA32_THERM_STATUS, &msr);
		c->core_temp_c = p->tcc_activation_temp - ((msr >> 16) & 0x7F);
	}

	/* collect package counters only for 1st core in package */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE)) {
		retval = 0;
		goto out;
	}

        if (do_pkg_cstate & (1 << 2))
                READ_MSR(MSR_PKG_C2_RESIDENCY, &p->pc2);
        if (do_pkg_cstate & (1 << 3))
                READ_MSR(MSR_PKG_C3_RESIDENCY, &p->pc3);
        if (do_pkg_cstate & (1 << 6))
                READ_MSR(MSR_PKG_C6_RESIDENCY, &p->pc6);
        if (do_pkg_cstate & (1 << 7))
                READ_MSR(MSR_PKG_C7_RESIDENCY, &p->pc7);
        if (do_pkg_cstate & (1 << 8))
                READ_MSR(MSR_PKG_C8_RESIDENCY, &p->pc8);
        if (do_pkg_cstate & (1 << 9))
                READ_MSR(MSR_PKG_C9_RESIDENCY, &p->pc9);
        if (do_pkg_cstate & (1 << 10))
                READ_MSR(MSR_PKG_C10_RESIDENCY, &p->pc10);

	if (do_rapl & RAPL_PKG) {
		READ_MSR(MSR_PKG_ENERGY_STATUS, &msr);
		p->energy_pkg = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_CORES) {
		READ_MSR(MSR_PP0_ENERGY_STATUS, &msr);
		p->energy_cores = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_DRAM) {
		READ_MSR(MSR_DRAM_ENERGY_STATUS, &msr);
		p->energy_dram = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_GFX) {
		READ_MSR(MSR_PP1_ENERGY_STATUS, &msr);
		p->energy_gfx = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_PKG_PERF_STATUS) {
		READ_MSR(MSR_PKG_PERF_STATUS, &msr);
		p->rapl_pkg_perf_status = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_DRAM_PERF_STATUS) {
		READ_MSR(MSR_DRAM_PERF_STATUS, &msr);
		p->rapl_dram_perf_status = msr & 0xFFFFFFFF;
	}
	if (do_ptm) {
		READ_MSR(MSR_IA32_PACKAGE_THERM_STATUS, &msr);
		p->pkg_temp_c = p->tcc_activation_temp - ((msr >> 16) & 0x7F);
	}

out:
	close(msr_fd);
	return retval;
}


/**********************************
 * Evaluating the changes (1 CPU) *
 **********************************/

/*
 * Do delta = new - old on 32bits cyclique intergers
 */
#define DELTA_WRAP32(delta, new, old)			\
	if (new > old) {				\
		delta = new - old;			\
	} else {					\
		delta = 0x100000000 + new - old;	\
	}

/*
 * Extract the evolution old->new in delta at a package level
 * (some are not new-delta, e.g. temperature)
 */
static inline void
delta_package(struct pkg_data *delta, const struct pkg_data *new, const struct pkg_data *old)
{
	delta->pc2 = new->pc2 - old->pc2;
	delta->pc3 = new->pc3 - old->pc3;
	delta->pc6 = new->pc6 - old->pc6;
	delta->pc7 = new->pc7 - old->pc7;
	delta->pc8 = new->pc8 - old->pc8;
	delta->pc9 = new->pc9 - old->pc9;
	delta->pc10 = new->pc10 - old->pc10;
	delta->pkg_temp_c = new->pkg_temp_c;

	DELTA_WRAP32(delta->energy_pkg, new->energy_pkg, old->energy_pkg);
	DELTA_WRAP32(delta->energy_cores, new->energy_cores, old->energy_cores);
	DELTA_WRAP32(delta->energy_gfx, new->energy_gfx, old->energy_gfx);
	DELTA_WRAP32(delta->energy_dram, new->energy_dram, old->energy_dram);
	DELTA_WRAP32(delta->rapl_pkg_perf_status, new->rapl_pkg_perf_status, old->rapl_pkg_perf_status);
	DELTA_WRAP32(delta->rapl_dram_perf_status, new->rapl_dram_perf_status, old->rapl_dram_perf_status);
}

/*
 * Extract the evolution old->new in delta at a core level
 * (some are not new-delta, e.g. temperature)
 */
static inline void
delta_core(struct core_data *delta, const struct core_data *new, const struct core_data *old)
{
	delta->c3 = new->c3 - old->c3;
	delta->c6 = new->c6 - old->c6;
	delta->c7 = new->c7 - old->c7;
	delta->core_temp_c = new->core_temp_c;
}

/*
 * Extract the evolution old->new in delta at a package level
 * core_delta is required for c1 estimation (tsc - c0 - all core cstates)
 */
static inline int __attribute__((warn_unused_result))
delta_thread(struct thread_data *delta, const struct thread_data *new, const struct thread_data *old,
	const struct core_data *core_delta)
{
	delta->tsc = new->tsc - old->tsc;

	/* check for TSC < 1 Mcycles over interval */
	if (delta->tsc < (1000 * 1000)) {
		WARNING("Turbostat plugin: Insanely slow TSC rate, TSC stops "
			"in idle? You can disable all c-states by booting with"
			" 'idle=poll' or just the deep ones with"
			" 'processor.max_cstate=1'");
		return -1;
	}

	delta->c1 = new->c1 - old->c1;

	if ((new->aperf > old->aperf) && (new->mperf > old->mperf)) {
		delta->aperf = new->aperf - old->aperf;
		delta->mperf = new->mperf - old->mperf;
	} else {
		if (!aperf_mperf_unstable) {
			WARNING("Turbostat plugin: APERF or MPERF went "
				"backwards. Frequency results do not cover "
				"the entire interval. Fix this by running "
				"Linux-2.6.30 or later.");

			aperf_mperf_unstable = 1;
		}
	}

	/*
	 * As counter collection is not atomic,
	 * it is possible for mperf's non-halted cycles + idle states
	 * to exceed TSC's all cycles: show c1 = 0% in that case.
	 */
	if ((delta->mperf + core_delta->c3 + core_delta->c6 + core_delta->c7) > delta->tsc)
		delta->c1 = 0;
	else {
		/* normal case, derive c1 */
		delta->c1 = delta->tsc - delta->mperf - core_delta->c3
			- core_delta->c6 - core_delta->c7;
	}

	if (delta->mperf == 0) {
		WARNING("Turbostat plugin: cpu%d MPERF 0!", old->cpu_id);
		delta->mperf = 1;	/* divide by 0 protection */
	}

	delta->smi_count = new->smi_count - old->smi_count;

	return 0;
}

/**********************************
 * Submitting the results (1 CPU) *
 **********************************/

/*
 * Submit one gauge value
 */
static void
turbostat_submit (const char *plugin_instance,
	const char *type, const char *type_instance,
	gauge_t value)
{
	value_list_t vl = VALUE_LIST_INIT;
	value_t v;

	v.gauge = value;
	vl.values = &v;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, PLUGIN_NAME, sizeof (vl.plugin));
	if (plugin_instance != NULL)
		sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));
	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

/*
 * Submit every data for a single CPU
 *
 * Core data is shared for all threads in one core: submitted only for the first thread
 * Package data is shared for all core in one package: submitted only for the first thread of the first core
 */
static int
submit_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	char name[12];
	double interval_float;

	interval_float = tv_delta.tv_sec + tv_delta.tv_usec/1000000.0;

	ssnprintf(name, sizeof(name), "cpu%02d", t->cpu_id);

	if (!aperf_mperf_unstable)
		turbostat_submit(name, "percent", "c0", 100.0 * t->mperf/t->tsc);
	if (!aperf_mperf_unstable)
		turbostat_submit(name, "percent", "c1", 100.0 * t->c1/t->tsc);

	/* GHz */
	if ((!aperf_mperf_unstable) || (!(t->aperf > t->tsc || t->mperf > t->tsc)))
		turbostat_submit(NULL, "frequency", name, 1.0 * t->tsc / 1000000000 * t->aperf / t->mperf / interval_float);

	/* SMI */
	turbostat_submit(NULL, "current", name, t->smi_count);

	/* submit per-core data only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		goto done;

	ssnprintf(name, sizeof(name), "core%02d", c->core_id);

	if (do_core_cstate & (1 << 3))
		turbostat_submit(name, "percent", "c3", 100.0 * c->c3/t->tsc);
	if (do_core_cstate & (1 << 6))
		turbostat_submit(name, "percent", "c6", 100.0 * c->c6/t->tsc);
	if (do_core_cstate & (1 << 7))
		turbostat_submit(name, "percent", "c7", 100.0 * c->c7/t->tsc);

	if (do_dts)
		turbostat_submit(NULL, "temperature", name, c->core_temp_c);

	/* submit per-package data only for 1st core in package */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		goto done;

	ssnprintf(name, sizeof(name), "pkg%02d", p->package_id);

	if (do_ptm)
		turbostat_submit(NULL, "temperature", name, p->pkg_temp_c);

	if (do_pkg_cstate & (1 << 2))
		turbostat_submit(name, "percent", "pc2", 100.0 * p->pc2/t->tsc);
	if (do_pkg_cstate & (1 << 3))
		turbostat_submit(name, "percent", "pc3", 100.0 * p->pc3/t->tsc);
	if (do_pkg_cstate & (1 << 6))
		turbostat_submit(name, "percent", "pc6", 100.0 * p->pc6/t->tsc);
	if (do_pkg_cstate & (1 << 7))
		turbostat_submit(name, "percent", "pc7", 100.0 * p->pc7/t->tsc);
	if (do_pkg_cstate & (1 << 8))
		turbostat_submit(name, "percent", "pc8", 100.0 * p->pc8/t->tsc);
	if (do_pkg_cstate & (1 << 9))
		turbostat_submit(name, "percent", "pc9", 100.0 * p->pc9/t->tsc);
	if (do_pkg_cstate & (1 << 10))
		turbostat_submit(name, "percent", "pc10", 100.0 * p->pc10/t->tsc);

	if (do_rapl) {
		if (do_rapl & RAPL_PKG)
			turbostat_submit(name, "power", "Pkg_W", p->energy_pkg * rapl_energy_units / interval_float);
		if (do_rapl & RAPL_CORES)
			turbostat_submit(name, "power", "Cor_W", p->energy_cores * rapl_energy_units / interval_float);
		if (do_rapl & RAPL_GFX)
			turbostat_submit(name, "power", "GFX_W", p->energy_gfx * rapl_energy_units / interval_float);
		if (do_rapl & RAPL_DRAM)
			turbostat_submit(name, "power", "RAM_W", p->energy_dram * rapl_energy_units / interval_float);
	}
done:
	return 0;
}


/**********************************
 * Looping function over all CPUs *
 **********************************/

/*
 * Check if a given cpu id is in our compiled list of existing CPUs
 */
static int
cpu_is_not_present(int cpu)
{
	return !CPU_ISSET_S(cpu, cpu_present_setsize, cpu_present_set);
}

/*
 * Loop on all CPUs in topological order
 *
 * Skip non-present cpus
 * Return the error code at the first error or 0
 */
static int __attribute__((warn_unused_result))
for_all_cpus(int (func)(struct thread_data *, struct core_data *, struct pkg_data *),
	struct thread_data *thread_base, struct core_data *core_base, struct pkg_data *pkg_base)
{
	int retval, pkg_no, core_no, thread_no;

	for (pkg_no = 0; pkg_no < topology.num_packages; ++pkg_no) {
		for (core_no = 0; core_no < topology.num_cores; ++core_no) {
			for (thread_no = 0; thread_no < topology.num_threads; ++thread_no) {
				struct thread_data *t;
				struct core_data *c;
				struct pkg_data *p;

				t = GET_THREAD(thread_base, thread_no, core_no, pkg_no);

				if (cpu_is_not_present(t->cpu_id))
					continue;

				c = GET_CORE(core_base, core_no, pkg_no);
				p = GET_PKG(pkg_base, pkg_no);

				retval = func(t, c, p);
				if (retval)
					return retval;
			}
		}
	}
	return 0;
}

/*
 * Dedicated loop: Extract every data evolution for all CPU
 *
 * Skip non-present cpus
 * Return the error code at the first error or 0
 *
 * Core data is shared for all threads in one core: extracted only for the first thread
 * Package data is shared for all core in one package: extracted only for the first thread of the first core
 */
static int __attribute__((warn_unused_result))
for_all_cpus_delta(const struct thread_data *thread_new_base, const struct core_data *core_new_base, const struct pkg_data *pkg_new_base,
		   const struct thread_data *thread_old_base, const struct core_data *core_old_base, const struct pkg_data *pkg_old_base)
{
	int retval, pkg_no, core_no, thread_no;

	for (pkg_no = 0; pkg_no < topology.num_packages; ++pkg_no) {
		for (core_no = 0; core_no < topology.num_cores; ++core_no) {
			for (thread_no = 0; thread_no < topology.num_threads; ++thread_no) {
				struct thread_data *t_delta;
				const struct thread_data *t_old, *t_new;
				struct core_data *c_delta;

				/* Get correct pointers for threads */
				t_delta = GET_THREAD(thread_delta, thread_no, core_no, pkg_no);
				t_new = GET_THREAD(thread_new_base, thread_no, core_no, pkg_no);
				t_old = GET_THREAD(thread_old_base, thread_no, core_no, pkg_no);

				/* Skip threads that disappeared */
				if (cpu_is_not_present(t_delta->cpu_id))
					continue;

				/* c_delta is always required for delta_thread */
				c_delta = GET_CORE(core_delta, core_no, pkg_no);

				/* calculate core delta only for 1st thread in core */
				if (t_new->flags & CPU_IS_FIRST_THREAD_IN_CORE) {
					const struct core_data *c_old, *c_new;

					c_new = GET_CORE(core_new_base, core_no, pkg_no);
					c_old = GET_CORE(core_old_base, core_no, pkg_no);

					delta_core(c_delta, c_new, c_old);
				}

				/* Always calculate thread delta */
				retval = delta_thread(t_delta, t_new, t_old, c_delta);
				if (retval)
					return retval;

				/* calculate package delta only for 1st core in package */
				if (t_new->flags & CPU_IS_FIRST_CORE_IN_PACKAGE) {
					struct pkg_data *p_delta;
					const struct pkg_data *p_old, *p_new;

					p_delta = GET_PKG(package_delta, pkg_no);
					p_new = GET_PKG(pkg_new_base, pkg_no);
					p_old = GET_PKG(pkg_old_base, pkg_no);

					delta_package(p_delta, p_new, p_old);
				}
			}
		}
	}
	return 0;
}


/***************
 * CPU Probing *
 ***************/

/*
 * MSR_IA32_TEMPERATURE_TARGET indicates the temperature where
 * the Thermal Control Circuit (TCC) activates.
 * This is usually equal to tjMax.
 *
 * Older processors do not have this MSR, so there we guess,
 * but also allow conficuration over-ride with "TCCActivationTemp".
 *
 * Several MSR temperature values are in units of degrees-C
 * below this value, including the Digital Thermal Sensor (DTS),
 * Package Thermal Management Sensor (PTM), and thermal event thresholds.
 */
static int __attribute__((warn_unused_result))
set_temperature_target(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	unsigned long long msr;
	unsigned int target_c_local;

	/* tcc_activation_temp is used only for dts or ptm */
	if (!(do_dts || do_ptm))
		return 0;

	/* this is a per-package concept */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (tcc_activation_temp != 0) {
		p->tcc_activation_temp = tcc_activation_temp;
		return 0;
	}

	if (get_msr(t->cpu_id, MSR_IA32_TEMPERATURE_TARGET, &msr))
		goto guess;

	target_c_local = (msr >> 16) & 0xFF;

	if (!target_c_local)
		goto guess;

	p->tcc_activation_temp = target_c_local;

	return 0;

guess:
	p->tcc_activation_temp = TJMAX_DEFAULT;
	WARNING("Turbostat plugin: cpu%d: Guessing tjMax %d C,"
		" Please use TCCActivationTemp to specify it.",
		t->cpu_id, p->tcc_activation_temp);

	return 0;
}

/*
 * Identify the functionality of the CPU
 */
static int __attribute__((warn_unused_result))
probe_cpu()
{
	unsigned int eax, ebx, ecx, edx, max_level;
	unsigned int fms, family, model;

	/* CPUID(0):
	 * - EAX: Maximum Input Value for Basic CPUID Information
	 * - EBX: "Genu" (0x756e6547)
	 * - EDX: "ineI" (0x49656e69)
         * - ECX: "ntel" (0x6c65746e)
         */
	max_level = ebx = ecx = edx = 0;
	__get_cpuid(0, &max_level, &ebx, &ecx, &edx);
	if (ebx != 0x756e6547 && edx != 0x49656e69 && ecx != 0x6c65746e) {
		ERROR("Turbostat plugin: Unsupported CPU (not Intel)");
		return -UNSUPPORTED_CPU;
	}

	/* CPUID(1):
	 * - EAX: Version Information: Type, Family, Model, and Stepping ID
         *  + 4-7:   Model ID
         *  + 8-11:  Family ID
	 *  + 12-13: Processor type
	 *  + 16-19: Extended Model ID
	 *  + 20-27: Extended Family ID
	 * - EDX: Feature Information:
	 *  + 5: Support for MSR read/write operations
         */
	fms = ebx = ecx = edx = 0;
	__get_cpuid(1, &fms, &ebx, &ecx, &edx);
	family = (fms >> 8) & 0xf;
	model = (fms >> 4) & 0xf;
	if (family == 0xf)
		family += (fms >> 20) & 0xf;
	if (family == 6 || family == 0xf)
		model += ((fms >> 16) & 0xf) << 4;
	if (!(edx & (1 << 5))) {
		ERROR("Turbostat plugin: Unsupported CPU (no MSR support)");
		return -ERR_NO_MSR;
	}

	/*
	 * CPUID(0x80000000):
	 * - EAX: Maximum Input Value for Extended Function CPUID Information
	 *
	 * This allows us to verify if the CPUID(0x80000007) can be called
	 *
	 * This check is valid for both Intel and AMD.
	 */
	max_level = ebx = ecx = edx = 0;
	__get_cpuid(0x80000000, &max_level, &ebx, &ecx, &edx);
	if (max_level < 0x80000007) {
		ERROR("Turbostat plugin: Unsupported CPU (no invariant TSC, "
		      " Maximum Extended Function: 0x%x)", max_level);
		return -ERR_NO_INVARIANT_TSC;
	}

	/*
	 * CPUID(0x80000007):
	 * - EDX:
	 *  + 8: Invariant TSC available if set
	 *
	 * This check is valid for both Intel and AMD
	 */
	eax = ebx = ecx = edx = 0;
	__get_cpuid(0x80000007, &eax, &ebx, &ecx, &edx);
	if (!(edx & (1 << 8))) {
		ERROR("Turbostat plugin: Unsupported CPU (No invariant TSC)");
		return -ERR_NO_INVARIANT_TSC;
	}

	/*
	 * CPUID(6):
	 * - EAX:
	 *  + 0: Digital temperature sensor is supported if set
	 *  + 6: Package thermal management is supported if set
	 * - ECX:
	 *  + 0: Hardware Coordination Feedback Capability (Presence of IA32_MPERF and IA32_APERF).
	 *  + 3: The processor supports performance-energy bias preference if set.
	 *       It also implies the presence of a new architectural MSR called IA32_ENERGY_PERF_BIAS
	 *
	 * This check is valid for both Intel and AMD
	 */
	eax = ebx = ecx = edx = 0;
	__get_cpuid(0x6, &eax, &ebx, &ecx, &edx);
	do_dts = eax & (1 << 0);
	do_ptm = eax & (1 << 6);
	if (!(ecx & (1 << 0))) {
		ERROR("Turbostat plugin: Unsupported CPU (No APERF)");
		return -ERR_NO_APERF;
	}

	/*
	 * Enable or disable C states depending on the model and family
	 */
	if (family == 6) {
		switch (model) {
		/* Atom (partial) */
		case 0x27:
			do_core_cstate = 0;
			do_pkg_cstate = (1 << 2) | (1 << 4) | (1 << 6);
			break;
		/* Silvermont */
		case 0x37: /* BYT */
		case 0x4A:
		case 0x4D: /* AVN */
		case 0x5A:
		case 0x5D:
			do_core_cstate = (1 << 1) | (1 << 6);
			do_pkg_cstate = (1 << 6);
			break;
		/* Nehalem */
		case 0x1A: /* Core i7, Xeon 5500 series - Bloomfield, Gainstown NHM-EP */
		case 0x1E: /* Core i7 and i5 Processor - Clarksfield, Lynnfield, Jasper Forest */
		case 0x1F: /* Core i7 and i5 Processor - Nehalem */
		case 0x2E: /* Nehalem-EX Xeon - Beckton */
			do_core_cstate = (1 << 3) | (1 << 6);
			do_pkg_cstate = (1 << 3) | (1 << 6) | (1 << 7);
			break;
		/* Westmere */
		case 0x25: /* Westmere Client - Clarkdale, Arrandale */
		case 0x2C: /* Westmere EP - Gulftown */
		case 0x2F: /* Westmere-EX Xeon - Eagleton */
			do_core_cstate = (1 << 3) | (1 << 6);
			do_pkg_cstate = (1 << 3) | (1 << 6) | (1 << 7);
			break;
		/* Sandy Bridge */
		case 0x2A: /* SNB */
		case 0x2D: /* SNB Xeon */
			do_core_cstate = (1 << 3) | (1 << 6) | (1 << 7);
			do_pkg_cstate = (1 << 2) | (1 << 3) | (1 << 6) | (1 << 7);
			break;
		/* Ivy Bridge */
		case 0x3A: /* IVB */
		case 0x3E: /* IVB Xeon */
			do_core_cstate = (1 << 3) | (1 << 6) | (1 << 7);
			do_pkg_cstate = (1 << 2) | (1 << 3) | (1 << 6) | (1 << 7);
			break;
		/* Haswell Bridge */
		case 0x3C: /* HSW */
		case 0x3F: /* HSW */
		case 0x46: /* HSW */
			do_core_cstate = (1 << 3) | (1 << 6) | (1 << 7);
			do_pkg_cstate = (1 << 2) | (1 << 3) | (1 << 6) | (1 << 7);
			break;
		case 0x45: /* HSW */
			do_core_cstate = (1 << 3) | (1 << 6) | (1 << 7);
			do_pkg_cstate = (1 << 2) | (1 << 3) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10);
			break;
		/* Broadwel */
		case 0x4F: /* BDW */
		case 0x56: /* BDX-DE */
			do_core_cstate = (1 << 3) | (1 << 6) | (1 << 7);
			do_pkg_cstate = (1 << 2) | (1 << 3) | (1 << 6) | (1 << 7);
			break;
		case 0x3D: /* BDW */
			do_core_cstate = (1 << 3) | (1 << 6) | (1 << 7);
			do_pkg_cstate = (1 << 2) | (1 << 3) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10);
			break;
		default:
			ERROR("Turbostat plugin: Unsupported CPU (family: %#x,"
			      " model: %#x)", family, model);
		}
		switch (model) {
		case 0x2A:
		case 0x3A:
		case 0x3C:
		case 0x45:
		case 0x46:
			do_rapl = RAPL_PKG | RAPL_CORES | RAPL_CORE_POLICY | RAPL_PKG_POWER_INFO | RAPL_GFX;
			break;
		case 0x3F:
			do_rapl = RAPL_PKG | RAPL_PKG_POWER_INFO | RAPL_PKG_PERF_STATUS | RAPL_DRAM | RAPL_DRAM_PERF_STATUS;
			break;
		case 0x2D:
		case 0x3E:
			do_rapl = RAPL_PKG | RAPL_CORES | RAPL_CORE_POLICY | RAPL_PKG_POWER_INFO | RAPL_PKG_PERF_STATUS | RAPL_DRAM | RAPL_DRAM_PERF_STATUS;
			break;
		case 0x37:
		case 0x4D:
			do_rapl = RAPL_PKG | RAPL_CORES;
			break;
		default:
			do_rapl = 0;
		}
	} else {
		ERROR("Turbostat plugin: Unsupported CPU (family: %#x, "
		      "model: %#x)", family, model);
		return -UNSUPPORTED_CPU;
	}

	if (do_rapl) {
		unsigned long long msr;
		if (get_msr(0, MSR_RAPL_POWER_UNIT, &msr))
			return 0;

		if (model == 0x37)
			rapl_energy_units = 1.0 * (1 << (msr >> 8 & 0x1F)) / 1000000;
		else
			rapl_energy_units = 1.0 / (1 << (msr >> 8 & 0x1F));
	}

	return 0;
}


/********************
 * Topology Probing *
 ********************/

/*
 * Read a single int from a file.
 */
static int __attribute__ ((format(printf,1,2)))
parse_int_file(const char *fmt, ...)
{
	va_list args;
	char path[PATH_MAX];
	FILE *filep;
	int value;

	va_start(args, fmt);
	vsnprintf(path, sizeof(path), fmt, args);
	va_end(args);
	filep = fopen(path, "r");
	if (!filep) {
		ERROR("Turbostat plugin: Failed to open '%s'", path);
		return -ERR_CANT_OPEN_FILE;
	}
	if (fscanf(filep, "%d", &value) != 1) {
		ERROR("Turbostat plugin: Failed to parse number from '%s'", path);
		return -ERR_CANT_READ_NUMBER;
	}
	fclose(filep);
	return value;
}

static int
get_threads_on_core(int cpu)
{
	char path[80];
	FILE *filep;
	int sib1, sib2;
	int matches;
	char character;

	ssnprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
	filep = fopen(path, "r");
        if (!filep) {
		ERROR("Turbostat plugin: Failed to open '%s'", path);
                return -ERR_CANT_OPEN_FILE;
        }
	/*
	 * file format:
	 * if a pair of number with a character between: 2 siblings (eg. 1-2, or 1,4)
	 * otherwinse 1 sibling (self).
	 */
	matches = fscanf(filep, "%d%c%d\n", &sib1, &character, &sib2);

	fclose(filep);

	if (matches == 3)
		return 2;
	else
		return 1;
}

/*
 * run func(cpu) on every cpu in /proc/stat
 * return max_cpu number
 */
static int __attribute__((warn_unused_result))
for_all_proc_cpus(int (func)(int))
{
	FILE *fp;
	int cpu_num;
	int retval;

	fp = fopen("/proc/stat", "r");
        if (!fp) {
		ERROR("Turbostat plugin: Failed to open /proc/stat");
                return -ERR_CANT_OPEN_FILE;
        }

	retval = fscanf(fp, "cpu %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d\n");
	if (retval != 0) {
		ERROR("Turbostat plugin: Failed to parse /proc/stat");
		fclose(fp);
		return -ERR_CANT_READ_PROC_STAT;
	}

	while (1) {
		retval = fscanf(fp, "cpu%u %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d\n", &cpu_num);
		if (retval != 1)
			break;

		retval = func(cpu_num);
		if (retval) {
			fclose(fp);
			return(retval);
		}
	}
	fclose(fp);
	return 0;
}

/*
 * Update the stored topology.max_cpu_id
 */
static int
update_max_cpu_id(int cpu)
{
	if (topology.max_cpu_id < cpu)
		topology.max_cpu_id = cpu;
	return 0;
}

static int
mark_cpu_present(int cpu)
{
	CPU_SET_S(cpu, cpu_present_setsize, cpu_present_set);
	return 0;
}

static int __attribute__((warn_unused_result))
allocate_cpu_set(cpu_set_t ** set, size_t * size) {
	*set = CPU_ALLOC(topology.max_cpu_id  + 1);
	if (*set == NULL) {
		ERROR("Turbostat plugin: Unable to allocate CPU state");
		return -ERR_CPU_ALLOC;
	}
	*size = CPU_ALLOC_SIZE(topology.max_cpu_id  + 1);
	CPU_ZERO_S(*size, *set);
	return 0;
}

/*
 * Build a local representation of the cpu distribution
 */
static int __attribute__((warn_unused_result))
topology_probe()
{
	int i;
	int ret;
	int max_package_id, max_core_id, max_thread_id;
	max_package_id = max_core_id = max_thread_id = 0;

	/* Clean topology */
	free(topology.cpus);
	memset(&topology, 0, sizeof(topology));

	ret = for_all_proc_cpus(update_max_cpu_id);
	if (ret != 0)
		goto err;

	topology.cpus = calloc(1, (topology.max_cpu_id  + 1) * sizeof(struct cpu_topology));
	if (topology.cpus == NULL) {
		ERROR("Turbostat plugin: Unable to allocate memory for CPU topology");
		return -ERR_CALLOC;
	}

	ret = allocate_cpu_set(&cpu_present_set, &cpu_present_setsize);
	if (ret != 0)
		goto err;
	ret = allocate_cpu_set(&cpu_affinity_set, &cpu_affinity_setsize);
	if (ret != 0)
		goto err;
	ret = allocate_cpu_set(&cpu_saved_affinity_set, &cpu_saved_affinity_setsize);
	if (ret != 0)
		goto err;

	ret = for_all_proc_cpus(mark_cpu_present);
	if (ret != 0)
		goto err;

	/*
	 * For online cpus
	 * find max_core_id, max_package_id
	 */
	for (i = 0; i <= topology.max_cpu_id; ++i) {
		int num_threads;
		struct cpu_topology *cpu = &topology.cpus[i];

		if (cpu_is_not_present(i)) {
			WARNING("Turbostat plugin: cpu%d NOT PRESENT", i);
			continue;
		}

		ret = parse_int_file("/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
		if (ret < 0)
			goto err;
		else
			cpu->package_id = ret;
		if (cpu->package_id > max_package_id)
			max_package_id = cpu->package_id;

		ret = parse_int_file("/sys/devices/system/cpu/cpu%d/topology/core_id", i);
		if (ret < 0)
			goto err;
		else
			cpu->core_id = ret;
		if (cpu->core_id > max_core_id)
			max_core_id = cpu->core_id;
		ret = parse_int_file("/sys/devices/system/cpu/cpu%d/topology/core_siblings_list", i);
		if (ret < 0)
			goto err;
		else if (ret == i)
			cpu->first_core_in_package = 1;

		ret = get_threads_on_core(i);
		if (ret < 0)
			goto err;
		else
			num_threads = ret;
		if (num_threads > max_thread_id)
			max_thread_id = num_threads;
		ret = parse_int_file("/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", i);
		if (ret < 0)
			goto err;
		else if (ret == i)
			cpu->first_thread_in_core = 1;

		DEBUG("Turbostat plugin: cpu %d pkg %d core %d\n",
			i, cpu->package_id, cpu->core_id);
	}
	/* Num is max + 1 (need to count 0) */
	topology.num_packages = max_package_id + 1;
	topology.num_cores = max_core_id + 1;
	topology.num_threads = max_thread_id + 1;

	return 0;
err:
	free(topology.cpus);
	return ret;
}


/************************
 * Main alloc/init/free *
 ************************/

static int
allocate_counters(struct thread_data **threads, struct core_data **cores, struct pkg_data **packages)
{
	int i;
	int total_threads, total_cores;

	total_threads = topology.num_threads * topology.num_cores * topology.num_packages;
	*threads = calloc(total_threads, sizeof(struct thread_data));
	if (*threads == NULL)
		goto err;

	for (i = 0; i < total_threads; ++i)
		(*threads)[i].cpu_id = -1;

	total_cores = topology.num_cores * topology.num_packages;
	*cores = calloc(total_cores, sizeof(struct core_data));
	if (*cores == NULL)
		goto err_clean_threads;

	for (i = 0; i < total_cores; ++i)
		(*cores)[i].core_id = -1;

	*packages = calloc(topology.num_packages, sizeof(struct pkg_data));
	if (*packages == NULL)
		goto err_clean_cores;

	for (i = 0; i < topology.num_packages; i++)
		(*packages)[i].package_id = i;

	return 0;

err_clean_cores:
	free(*cores);
err_clean_threads:
	free(*threads);
err:
	ERROR("Turbostat plugin: Failled to allocate memory for counters");
	return -ERR_CALLOC;
}

static int
init_counter(struct thread_data *thread_base, struct core_data *core_base,
	struct pkg_data *pkg_base, int cpu_id)
{
	struct thread_data *t;
	struct core_data *c;
	struct pkg_data *p;
	struct cpu_topology *cpu = &topology.cpus[cpu_id];

	t = GET_THREAD(thread_base, !(cpu->first_thread_in_core), cpu->core_id, cpu->package_id);
	c = GET_CORE(core_base, cpu->core_id, cpu->package_id);
	p = GET_PKG(pkg_base, cpu->package_id);

	t->cpu_id = cpu_id;
	if (cpu->first_thread_in_core)
		t->flags |= CPU_IS_FIRST_THREAD_IN_CORE;
	if (cpu->first_core_in_package)
		t->flags |= CPU_IS_FIRST_CORE_IN_PACKAGE;

	c->core_id = cpu->core_id;
	p->package_id = cpu->package_id;

	return 0;
}

static int
initialize_counters(void)
{
	int ret;
	int cpu_id;

	for (cpu_id = 0; cpu_id <= topology.max_cpu_id; ++cpu_id) {
		if (cpu_is_not_present(cpu_id)) {
			continue;
		}

		ret = init_counter(EVEN_COUNTERS, cpu_id);
		if (ret < 0)
			return ret;
		ret = init_counter(ODD_COUNTERS, cpu_id);
		if (ret < 0)
			return ret;
		ret = init_counter(DELTA_COUNTERS, cpu_id);
		if (ret < 0)
			return ret;
	}
	return 0;
}



static void
free_all_buffers(void)
{
	allocated = 0;
	initialized = 0;

	CPU_FREE(cpu_present_set);
	cpu_present_set = NULL;
	cpu_present_set = 0;

	CPU_FREE(cpu_affinity_set);
	cpu_affinity_set = NULL;
	cpu_affinity_setsize = 0;

	CPU_FREE(cpu_saved_affinity_set);
	cpu_saved_affinity_set = NULL;
	cpu_saved_affinity_setsize = 0;

	free(thread_even);
	free(core_even);
	free(package_even);

	thread_even = NULL;
	core_even = NULL;
	package_even = NULL;

	free(thread_odd);
	free(core_odd);
	free(package_odd);

	thread_odd = NULL;
	core_odd = NULL;
	package_odd = NULL;

	free(thread_delta);
	free(core_delta);
	free(package_delta);

	thread_delta = NULL;
	core_delta = NULL;
	package_delta = NULL;
}


/**********************
 * Collectd functions *
 **********************/

#define DO_OR_GOTO_ERR(something) \
do {                         \
	ret = (something);     \
	if (ret < 0)         \
		goto err;    \
} while (0)

static int setup_all_buffers(void)
{
	int ret;

	DO_OR_GOTO_ERR(topology_probe());
	DO_OR_GOTO_ERR(allocate_counters(&thread_even, &core_even, &package_even));
	DO_OR_GOTO_ERR(allocate_counters(&thread_odd, &core_odd, &package_odd));
	DO_OR_GOTO_ERR(allocate_counters(&thread_delta, &core_delta, &package_delta));
	DO_OR_GOTO_ERR(initialize_counters());
	DO_OR_GOTO_ERR(for_all_cpus(set_temperature_target, EVEN_COUNTERS));
	DO_OR_GOTO_ERR(for_all_cpus(set_temperature_target, ODD_COUNTERS));

	allocated = 1;
	return 0;
err:
	free_all_buffers();
	return ret;
}

static int
turbostat_read(void)
{
	int ret;

	if (!allocated) {
		if ((ret = setup_all_buffers()) < 0)
			return ret;
	}

	if (for_all_proc_cpus(cpu_is_not_present)) {
		free_all_buffers();
		if ((ret = setup_all_buffers()) < 0)
			return ret;
		if (for_all_proc_cpus(cpu_is_not_present))
			return -ERR_CPU_NOT_PRESENT;
	}

	/* Saving the scheduling affinity, as it will be modified by get_counters */
	if (sched_getaffinity(0, cpu_saved_affinity_setsize, cpu_saved_affinity_set) != 0)
		return -ERR_CPU_SAVE_SCHED_AFFINITY;

	if (!initialized) {
		if ((ret = for_all_cpus(get_counters, EVEN_COUNTERS)) < 0)
			goto out;
		gettimeofday(&tv_even, (struct timezone *)NULL);
		is_even = 1;
		initialized = 1;
		ret = 0;
		goto out;
	}

	if (is_even) {
		if ((ret = for_all_cpus(get_counters, ODD_COUNTERS)) < 0)
			goto out;
		gettimeofday(&tv_odd, (struct timezone *)NULL);
		is_even = 0;
		timersub(&tv_odd, &tv_even, &tv_delta);
		if ((ret = for_all_cpus_delta(ODD_COUNTERS, EVEN_COUNTERS)) < 0)
			goto out;
		if ((ret = for_all_cpus(submit_counters, DELTA_COUNTERS)) < 0)
			goto out;
	} else {
		if ((ret = for_all_cpus(get_counters, EVEN_COUNTERS)) < 0)
			goto out;
		gettimeofday(&tv_even, (struct timezone *)NULL);
		is_even = 1;
		timersub(&tv_even, &tv_odd, &tv_delta);
		if ((ret = for_all_cpus_delta(EVEN_COUNTERS, ODD_COUNTERS)) < 0)
			goto out;
		if ((ret = for_all_cpus(submit_counters, DELTA_COUNTERS)) < 0)
			goto out;
	}
	ret = 0;
out:
	/*
	 * Let's restore the affinity
	 * This might fail if the number of CPU changed, but we can't do anything in that case..
	 */
	(void)sched_setaffinity(0, cpu_saved_affinity_setsize, cpu_saved_affinity_set);
	return ret;
}

static int
turbostat_init(void)
{
	struct stat sb;
	int ret;

	if (getuid() != 0) {
		ERROR("Turbostat plugin: Initialization failed: this plugin "
		      "requires collectd to run as root in order to read "
		      "special CPU registers");
		return -ERR_NOT_ROOT;
	}

	DO_OR_GOTO_ERR(probe_cpu());

	if (stat("/dev/cpu/0/msr", &sb)) {
		ERROR("Turbostat plugin: Initialization failed: /dev/cpu/0/msr"
		      " does not exist while the CPU supports MSR. You may be "
		      "missing the corresponding kernel module, please try '# "
		      "modprobe msr'");
		return -ERR_NO_MSR;
	}

	DO_OR_GOTO_ERR(setup_all_buffers());

	plugin_register_read(PLUGIN_NAME, turbostat_read);

	return 0;
err:
	free_all_buffers();
	return ret;
}

static const char *config_keys[] =
{
	"TCCActivationTemp",
};
static const int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int
turbostat_config(const char *key, const char *value)
{
	long unsigned int tmp_val;
	char *end;

	if (strcasecmp("TCCActivationTemp", key) == 0) {
		tmp_val = strtoul(value, &end, 0);
		if (*end != '\0' || tmp_val > UINT_MAX)
			return -1;
		tcc_activation_temp = (unsigned int) tmp_val;
	} else {
		return -1;
	}
	return 0;
}

void module_register(void)
{
	plugin_register_init(PLUGIN_NAME, turbostat_init);
	plugin_register_config(PLUGIN_NAME, turbostat_config, config_keys, config_keys_num);
}
