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

static const char *proc_stat = "/proc/stat";
static unsigned int skip_c0;
static unsigned int skip_c1;
static unsigned int do_nhm_cstates;
static unsigned int do_snb_cstates;
static unsigned int do_c8_c9_c10;
static unsigned int do_slm_cstates;
static unsigned int has_epb;
static unsigned int genuine_intel;
static unsigned int do_nehalem_platform_info;
static int do_smi;
static unsigned int do_rapl;
static unsigned int do_dts;
static unsigned int do_ptm;
static unsigned int tcc_activation_temp;
static unsigned int tcc_activation_temp_override;
static double rapl_power_units, rapl_energy_units, rapl_time_units;
static double rapl_joule_counter_range;

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

int aperf_mperf_unstable;
int backwards_count;
char *progname;

cpu_set_t *cpu_present_set, *cpu_affinity_set;
size_t cpu_present_setsize, cpu_affinity_setsize;

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
} *thread_even, *thread_odd;

struct core_data {
	unsigned long long c3;
	unsigned long long c6;
	unsigned long long c7;
	unsigned int core_temp_c;
	unsigned int core_id;
} *core_even, *core_odd;

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
	unsigned int pkg_temp_c;

} *package_even, *package_odd;

#define ODD_COUNTERS thread_odd, core_odd, package_odd
#define EVEN_COUNTERS thread_even, core_even, package_even
static _Bool is_even = 1;

static _Bool allocated = 0;
static _Bool initialized = 0;

#define GET_THREAD(thread_base, thread_no, core_no, pkg_no) \
	(thread_base + (pkg_no) * topo.num_cores_per_pkg * \
		topo.num_threads_per_core + \
		(core_no) * topo.num_threads_per_core + (thread_no))
#define GET_CORE(core_base, core_no, pkg_no) \
	(core_base + (pkg_no) * topo.num_cores_per_pkg + (core_no))
#define GET_PKG(pkg_base, pkg_no) (pkg_base + pkg_no)

struct topo_params {
	int num_packages;
	int num_cpus;
	int num_cores;
	int max_cpu_num;
	int num_cores_per_pkg;
	int num_threads_per_core;
} topo;

struct timeval tv_even, tv_odd, tv_delta;

enum return_values {
	OK = 0,
	ERR_CPU_MIGRATE,
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
	ERR_MSR_PP0_ENERGY_STATUS,
	ERR_MSR_DRAM_ENERGY_STATUS,
	ERR_MSR_PP1_ENERGY_STATUS,
	ERR_MSR_PKG_PERF_STATUS,
	ERR_MSR_DRAM_PERF_STATUS,
	ERR_MSR_IA32_PACKAGE_THERM_STATUS,
	ERR_CPU_NOT_PRESENT,
	ERR_NO_MSR,
	ERR_CANT_OPEN_FILE,
	ERR_CANT_READ_NUMBER,
	ERR_CANT_READ_PROC_STAT,
	ERR_NO_INVARIANT_TSC,
	ERR_NO_APERF,
	ERR_CALLOC,
	ERR_CPU_ALLOC,
	ERR_NOT_ROOT,
};

static int setup_all_buffers(void);

static int
cpu_is_not_present(int cpu)
{
	return !CPU_ISSET_S(cpu, cpu_present_setsize, cpu_present_set);
}
/*
 * run func(thread, core, package) in topology order
 * skip non-present cpus
 */

static int __attribute__((warn_unused_result))
for_all_cpus(int (func)(struct thread_data *, struct core_data *, struct pkg_data *),
	struct thread_data *thread_base, struct core_data *core_base, struct pkg_data *pkg_base)
{
	int retval, pkg_no, core_no, thread_no;

	for (pkg_no = 0; pkg_no < topo.num_packages; ++pkg_no) {
		for (core_no = 0; core_no < topo.num_cores_per_pkg; ++core_no) {
			for (thread_no = 0; thread_no <
				topo.num_threads_per_core; ++thread_no) {
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

static int __attribute__((warn_unused_result))
cpu_migrate(int cpu)
{
	CPU_ZERO_S(cpu_affinity_setsize, cpu_affinity_set);
	CPU_SET_S(cpu, cpu_affinity_setsize, cpu_affinity_set);
	if (sched_setaffinity(0, cpu_affinity_setsize, cpu_affinity_set) == -1)
		return -ERR_CPU_MIGRATE;
	else
		return 0;
}

static int __attribute__((warn_unused_result))
get_msr(int cpu, off_t offset, unsigned long long *msr)
{
	ssize_t retval;
	char pathname[32];
	int fd;

	ssnprintf(pathname, 32, "/dev/cpu/%d/msr", cpu);
	fd = open(pathname, O_RDONLY);
	if (fd < 0)
		return -1;

	retval = pread(fd, msr, sizeof *msr, offset);
	close(fd);

	if (retval != sizeof *msr) {
		ERROR ("%s offset 0x%llx read failed\n", pathname, (unsigned long long)offset);
		return -1;
	}

	return 0;
}

#define DELTA_WRAP32(new, old)			\
	if (new > old) {			\
		old = new - old;		\
	} else {				\
		old = 0x100000000 + new - old;	\
	}

static void
delta_package(struct pkg_data *new, struct pkg_data *old)
{
	old->pc2 = new->pc2 - old->pc2;
	old->pc3 = new->pc3 - old->pc3;
	old->pc6 = new->pc6 - old->pc6;
	old->pc7 = new->pc7 - old->pc7;
	old->pc8 = new->pc8 - old->pc8;
	old->pc9 = new->pc9 - old->pc9;
	old->pc10 = new->pc10 - old->pc10;
	old->pkg_temp_c = new->pkg_temp_c;

	DELTA_WRAP32(new->energy_pkg, old->energy_pkg);
	DELTA_WRAP32(new->energy_cores, old->energy_cores);
	DELTA_WRAP32(new->energy_gfx, old->energy_gfx);
	DELTA_WRAP32(new->energy_dram, old->energy_dram);
	DELTA_WRAP32(new->rapl_pkg_perf_status, old->rapl_pkg_perf_status);
	DELTA_WRAP32(new->rapl_dram_perf_status, old->rapl_dram_perf_status);
}

static void
delta_core(struct core_data *new, struct core_data *old)
{
	old->c3 = new->c3 - old->c3;
	old->c6 = new->c6 - old->c6;
	old->c7 = new->c7 - old->c7;
	old->core_temp_c = new->core_temp_c;
}

/*
 * old = new - old
 */
static int __attribute__((warn_unused_result))
delta_thread(struct thread_data *new, struct thread_data *old,
	struct core_data *core_delta)
{
	old->tsc = new->tsc - old->tsc;

	/* check for TSC < 1 Mcycles over interval */
	if (old->tsc < (1000 * 1000)) {
		WARNING("Insanely slow TSC rate, TSC stops in idle?\n"
			"You can disable all c-states by booting with \"idle=poll\"\n"
			"or just the deep ones with \"processor.max_cstate=1\"");
		return -1;
	}

	old->c1 = new->c1 - old->c1;

	if ((new->aperf > old->aperf) && (new->mperf > old->mperf)) {
		old->aperf = new->aperf - old->aperf;
		old->mperf = new->mperf - old->mperf;
	} else {

		if (!aperf_mperf_unstable) {
			WARNING("%s: APERF or MPERF went backwards *\n", progname);
			WARNING("* Frequency results do not cover entire interval *\n");
			WARNING("* fix this by running Linux-2.6.30 or later *\n");

			aperf_mperf_unstable = 1;
		}
		/*
		 * mperf delta is likely a huge "positive" number
		 * can not use it for calculating c0 time
		 */
		skip_c0 = 1;
		skip_c1 = 1;
	}


	/*
	 * As counter collection is not atomic,
	 * it is possible for mperf's non-halted cycles + idle states
	 * to exceed TSC's all cycles: show c1 = 0% in that case.
	 */
	if ((old->mperf + core_delta->c3 + core_delta->c6 + core_delta->c7) > old->tsc)
		old->c1 = 0;
	else {
		/* normal case, derive c1 */
		old->c1 = old->tsc - old->mperf - core_delta->c3
			- core_delta->c6 - core_delta->c7;
	}

	if (old->mperf == 0) {
		WARNING("cpu%d MPERF 0!\n", old->cpu_id);
		old->mperf = 1;	/* divide by 0 protection */
	}

	if (do_smi)
		old->smi_count = new->smi_count - old->smi_count;

	return 0;
}

static int __attribute__((warn_unused_result))
delta_cpu(struct thread_data *t, struct core_data *c,
	struct pkg_data *p, struct thread_data *t2,
	struct core_data *c2, struct pkg_data *p2)
{
	int ret;

	/* calculate core delta only for 1st thread in core */
	if (t->flags & CPU_IS_FIRST_THREAD_IN_CORE)
		delta_core(c, c2);

	/* always calculate thread delta */
	ret = delta_thread(t, t2, c2);	/* c2 is core delta */
	if (ret != 0)
		return ret;

	/* calculate package delta only for 1st core in package */
	if (t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE)
		delta_package(p, p2);

	return 0;
}

static unsigned long long
rdtsc(void)
{
	unsigned int low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return low | ((unsigned long long)high) << 32;
}


/*
 * get_counters(...)
 * migrate to cpu
 * acquire and record local counters for that cpu
 */
static int __attribute__((warn_unused_result))
get_counters(struct thread_data *t, struct core_data *c, struct pkg_data *p)
{
	int cpu = t->cpu_id;
	unsigned long long msr;

	if (cpu_migrate(cpu)) {
		WARNING("Could not migrate to CPU %d\n", cpu);
		return -ERR_CPU_MIGRATE;
	}

	t->tsc = rdtsc();	/* we are running on local CPU of interest */

	if (get_msr(cpu, MSR_IA32_APERF, &t->aperf))
		return -ERR_MSR_IA32_APERF;
	if (get_msr(cpu, MSR_IA32_MPERF, &t->mperf))
		return -ERR_MSR_IA32_MPERF;

	if (do_smi) {
		if (get_msr(cpu, MSR_SMI_COUNT, &msr))
			return -ERR_MSR_SMI_COUNT;
		t->smi_count = msr & 0xFFFFFFFF;
	}

	/* collect core counters only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		return 0;

	if (do_nhm_cstates && !do_slm_cstates) {
		if (get_msr(cpu, MSR_CORE_C3_RESIDENCY, &c->c3))
			return -ERR_MSR_CORE_C3_RESIDENCY;
	}

	if (do_nhm_cstates) {
		if (get_msr(cpu, MSR_CORE_C6_RESIDENCY, &c->c6))
			return -ERR_MSR_CORE_C6_RESIDENCY;
	}

	if (do_snb_cstates)
		if (get_msr(cpu, MSR_CORE_C7_RESIDENCY, &c->c7))
			return -ERR_MSR_CORE_C7_RESIDENCY;

	if (do_dts) {
		if (get_msr(cpu, MSR_IA32_THERM_STATUS, &msr))
			return -ERR_MSR_IA32_THERM_STATUS;
		c->core_temp_c = tcc_activation_temp - ((msr >> 16) & 0x7F);
	}


	/* collect package counters only for 1st core in package */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	if (do_nhm_cstates && !do_slm_cstates) {
		if (get_msr(cpu, MSR_PKG_C3_RESIDENCY, &p->pc3))
			return -ERR_MSR_PKG_C3_RESIDENCY;
		if (get_msr(cpu, MSR_PKG_C6_RESIDENCY, &p->pc6))
			return -ERR_MSR_PKG_C6_RESIDENCY;
	}
	if (do_snb_cstates) {
		if (get_msr(cpu, MSR_PKG_C2_RESIDENCY, &p->pc2))
			return -ERR_MSR_PKG_C2_RESIDENCY;
		if (get_msr(cpu, MSR_PKG_C7_RESIDENCY, &p->pc7))
			return -ERR_MSR_PKG_C7_RESIDENCY;
	}
	if (do_c8_c9_c10) {
		if (get_msr(cpu, MSR_PKG_C8_RESIDENCY, &p->pc8))
			return -ERR_MSR_PKG_C8_RESIDENCY;
		if (get_msr(cpu, MSR_PKG_C9_RESIDENCY, &p->pc9))
			return -ERR_MSR_PKG_C9_RESIDENCY;
		if (get_msr(cpu, MSR_PKG_C10_RESIDENCY, &p->pc10))
			return -ERR_MSR_PKG_C10_RESIDENCY;
	}
	if (do_rapl & RAPL_PKG) {
		if (get_msr(cpu, MSR_PKG_ENERGY_STATUS, &msr))
			return -ERR_MSR_PKG_ENERGY_STATUS;
		p->energy_pkg = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_CORES) {
		if (get_msr(cpu, MSR_PP0_ENERGY_STATUS, &msr))
			return MSR_PP0_ENERGY_STATUS;
		p->energy_cores = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_DRAM) {
		if (get_msr(cpu, MSR_DRAM_ENERGY_STATUS, &msr))
			return -ERR_MSR_DRAM_ENERGY_STATUS;
		p->energy_dram = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_GFX) {
		if (get_msr(cpu, MSR_PP1_ENERGY_STATUS, &msr))
			return -ERR_MSR_PP1_ENERGY_STATUS;
		p->energy_gfx = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_PKG_PERF_STATUS) {
		if (get_msr(cpu, MSR_PKG_PERF_STATUS, &msr))
			return -ERR_MSR_PKG_PERF_STATUS;
		p->rapl_pkg_perf_status = msr & 0xFFFFFFFF;
	}
	if (do_rapl & RAPL_DRAM_PERF_STATUS) {
		if (get_msr(cpu, MSR_DRAM_PERF_STATUS, &msr))
			return -ERR_MSR_DRAM_PERF_STATUS;
		p->rapl_dram_perf_status = msr & 0xFFFFFFFF;
	}
	if (do_ptm) {
		if (get_msr(cpu, MSR_IA32_PACKAGE_THERM_STATUS, &msr))
			return -ERR_MSR_IA32_PACKAGE_THERM_STATUS;
		p->pkg_temp_c = tcc_activation_temp - ((msr >> 16) & 0x7F);
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
}

/*
 * Parse a file containing a single int.
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
		ERROR("%s: open failed", path);
		return -ERR_CANT_OPEN_FILE;
	}
	if (fscanf(filep, "%d", &value) != 1) {
		ERROR("%s: failed to parse number from file", path);
		return -ERR_CANT_READ_NUMBER;
	}
	fclose(filep);
	return value;
}

/*
 * cpu_is_first_sibling_in_core(cpu)
 * return 1 if given CPU is 1st HT sibling in the core
 */
static int
cpu_is_first_sibling_in_core(int cpu)
{
	return cpu == parse_int_file("/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
}

/*
 * cpu_is_first_core_in_package(cpu)
 * return 1 if given CPU is 1st core in package
 */
static int
cpu_is_first_core_in_package(int cpu)
{
	return cpu == parse_int_file("/sys/devices/system/cpu/cpu%d/topology/core_siblings_list", cpu);
}

static int
get_physical_package_id(int cpu)
{
	return parse_int_file("/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu);
}

static int
get_core_id(int cpu)
{
	return parse_int_file("/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
}

static int
get_num_ht_siblings(int cpu)
{
	char path[80];
	FILE *filep;
	int sib1, sib2;
	int matches;
	char character;

	ssnprintf(path, 80, "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
	filep = fopen(path, "r");
        if (!filep) {
                ERROR("%s: open failed", path);
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
 * run func(thread, core, package) in topology order
 * skip non-present cpus
 */


static int __attribute__((warn_unused_result))
for_all_cpus_2(int (func)(struct thread_data *, struct core_data *,
	struct pkg_data *, struct thread_data *, struct core_data *,
	struct pkg_data *), struct thread_data *thread_base,
	struct core_data *core_base, struct pkg_data *pkg_base,
	struct thread_data *thread_base2, struct core_data *core_base2,
	struct pkg_data *pkg_base2)
{
	int retval, pkg_no, core_no, thread_no;

	for (pkg_no = 0; pkg_no < topo.num_packages; ++pkg_no) {
		for (core_no = 0; core_no < topo.num_cores_per_pkg; ++core_no) {
			for (thread_no = 0; thread_no <
				topo.num_threads_per_core; ++thread_no) {
				struct thread_data *t, *t2;
				struct core_data *c, *c2;
				struct pkg_data *p, *p2;

				t = GET_THREAD(thread_base, thread_no, core_no, pkg_no);

				if (cpu_is_not_present(t->cpu_id))
					continue;

				t2 = GET_THREAD(thread_base2, thread_no, core_no, pkg_no);

				c = GET_CORE(core_base, core_no, pkg_no);
				c2 = GET_CORE(core_base2, core_no, pkg_no);

				p = GET_PKG(pkg_base, pkg_no);
				p2 = GET_PKG(pkg_base2, pkg_no);

				retval = func(t, c, p, t2, c2, p2);
				if (retval)
					return retval;
			}
		}
	}
	return 0;
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

	fp = fopen(proc_stat, "r");
        if (!fp) {
                ERROR("%s: open failed", proc_stat);
                return -ERR_CANT_OPEN_FILE;
        }

	retval = fscanf(fp, "cpu %*d %*d %*d %*d %*d %*d %*d %*d %*d %*d\n");
	if (retval != 0) {
		ERROR("%s: failed to parse format", proc_stat);
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
 * count_cpus()
 * remember the last one seen, it will be the max
 */
static int
count_cpus(int cpu)
{
	if (topo.max_cpu_num < cpu)
		topo.max_cpu_num = cpu;

	topo.num_cpus += 1;
	return 0;
}
static int
mark_cpu_present(int cpu)
{
	CPU_SET_S(cpu, cpu_present_setsize, cpu_present_set);
	return 0;
}


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
 * column formatting convention & formats
 * package: "pk" 2 columns %2d
 * core: "cor" 3 columns %3d
 * CPU: "CPU" 3 columns %3d
 * Pkg_W: %6.2
 * Cor_W: %6.2
 * GFX_W: %5.2
 * RAM_W: %5.2
 * GHz: "GHz" 3 columns %3.2
 * TSC: "TSC" 3 columns %3.2
 * SMI: "SMI" 4 columns %4d
 * percentage " %pc3" %6.2
 * Perf Status percentage: %5.2
 * "CTMP" 4 columns %4d
 */
#define NAME_LEN 12
static int
submit_counters(struct thread_data *t, struct core_data *c,
	struct pkg_data *p)
{
	char name[NAME_LEN];
	double interval_float;

	interval_float = tv_delta.tv_sec + tv_delta.tv_usec/1000000.0;

	snprintf(name, NAME_LEN, "cpu%02d", t->cpu_id);

	if (do_nhm_cstates) {
		if (!skip_c0)
			turbostat_submit(name, "percent", "c0", 100.0 * t->mperf/t->tsc);
		if (!skip_c1)
			turbostat_submit(name, "percent", "c1", 100.0 * t->c1/t->tsc);
	}

	/* GHz */
	if ((!aperf_mperf_unstable) || (!(t->aperf > t->tsc || t->mperf > t->tsc)))
		turbostat_submit(NULL, "frequency", name, 1.0 * t->tsc / 1000000000 * t->aperf / t->mperf / interval_float);

	/* SMI */
	if (do_smi)
		turbostat_submit(NULL, "current", name, t->smi_count);

	/* print per-core data only for 1st thread in core */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE))
		goto done;

	snprintf(name, NAME_LEN, "core%02d", c->core_id);

	if (do_nhm_cstates && !do_slm_cstates)
		turbostat_submit(name, "percent", "c3", 100.0 * c->c3/t->tsc);
	if (do_nhm_cstates)
		turbostat_submit(name, "percent", "c6", 100.0 * c->c6/t->tsc);
	if (do_snb_cstates)
		turbostat_submit(name, "percent", "c7", 100.0 * c->c7/t->tsc);

	if (do_dts)
		turbostat_submit(NULL, "temperature", name, c->core_temp_c);

	/* print per-package data only for 1st core in package */
	if (!(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		goto done;

	snprintf(name, NAME_LEN, "pkg%02d", p->package_id);

	if (do_ptm)
		turbostat_submit(NULL, "temperature", name, p->pkg_temp_c);

	if (do_snb_cstates)
		turbostat_submit(name, "percent", "pc2", 100.0 * p->pc2/t->tsc);
	if (do_nhm_cstates && !do_slm_cstates)
		turbostat_submit(name, "percent", "pc3", 100.0 * p->pc3/t->tsc);
	if (do_nhm_cstates && !do_slm_cstates)
		turbostat_submit(name, "percent", "pc6", 100.0 * p->pc6/t->tsc);
	if (do_snb_cstates)
		turbostat_submit(name, "percent", "pc7", 100.0 * p->pc7/t->tsc);
	if (do_c8_c9_c10) {
		turbostat_submit(name, "percent", "pc8", 100.0 * p->pc8/t->tsc);
		turbostat_submit(name, "percent", "pc9", 100.0 * p->pc9/t->tsc);
		turbostat_submit(name, "percent", "pc10", 100.0 * p->pc10/t->tsc);
	}

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

static int
turbostat_read(user_data_t * not_used)
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

	if (!initialized) {
		if ((ret = for_all_cpus(get_counters, EVEN_COUNTERS)) < 0)
			return ret;
		gettimeofday(&tv_even, (struct timezone *)NULL);
		is_even = 1;
		initialized = 1;
		return 0;
	}

	if (is_even) {
		if ((ret = for_all_cpus(get_counters, ODD_COUNTERS)) < 0)
			return ret;
		gettimeofday(&tv_odd, (struct timezone *)NULL);
		is_even = 0;
		timersub(&tv_odd, &tv_even, &tv_delta);
		if ((ret = for_all_cpus_2(delta_cpu, ODD_COUNTERS, EVEN_COUNTERS)) < 0)
			return ret;
		if ((ret = for_all_cpus(submit_counters, EVEN_COUNTERS)) < 0)
			return ret;
	} else {
		if ((ret = for_all_cpus(get_counters, EVEN_COUNTERS)) < 0)
			return ret;
		gettimeofday(&tv_even, (struct timezone *)NULL);
		is_even = 1;
		timersub(&tv_even, &tv_odd, &tv_delta);
		if ((ret = for_all_cpus_2(delta_cpu, EVEN_COUNTERS, ODD_COUNTERS)) < 0)
			return ret;
		if ((ret = for_all_cpus(submit_counters, ODD_COUNTERS)) < 0)
			return ret;
	}
	return 0;
}

static int __attribute__((warn_unused_result))
check_dev_msr()
{
	struct stat sb;

	if (stat("/dev/cpu/0/msr", &sb)) {
		ERROR("no /dev/cpu/0/msr\n"
			"Try \"# modprobe msr\"");
		return -ERR_NO_MSR;
	}
	return 0;
}

static int __attribute__((warn_unused_result))
check_super_user()
{
	if (getuid() != 0) {
		ERROR("must be root");
		return -ERR_NOT_ROOT;
	}
	return 0;
}


#define	RAPL_POWER_GRANULARITY	0x7FFF	/* 15 bit power granularity */
#define	RAPL_TIME_GRANULARITY	0x3F /* 6 bit time granularity */

static double
get_tdp(unsigned int model)
{
	unsigned long long msr;

	if (do_rapl & RAPL_PKG_POWER_INFO)
		if (!get_msr(0, MSR_PKG_POWER_INFO, &msr))
			return ((msr >> 0) & RAPL_POWER_GRANULARITY) * rapl_power_units;

	switch (model) {
	case 0x37:
	case 0x4D:
		return 30.0;
	default:
		return 135.0;
	}
}


/*
 * rapl_probe()
 *
 * sets do_rapl, rapl_power_units, rapl_energy_units, rapl_time_units
 */
static void
rapl_probe(unsigned int family, unsigned int model)
{
	unsigned long long msr;
	unsigned int time_unit;
	double tdp;

	if (!genuine_intel)
		return;

	if (family != 6)
		return;

	switch (model) {
	case 0x2A:
	case 0x3A:
	case 0x3C:	/* HSW */
	case 0x45:	/* HSW */
	case 0x46:	/* HSW */
		do_rapl = RAPL_PKG | RAPL_CORES | RAPL_CORE_POLICY | RAPL_GFX | RAPL_PKG_POWER_INFO;
		break;
	case 0x3F:	/* HSX */
		do_rapl = RAPL_PKG | RAPL_DRAM | RAPL_DRAM_PERF_STATUS | RAPL_PKG_PERF_STATUS | RAPL_PKG_POWER_INFO;
		break;
	case 0x2D:
	case 0x3E:
		do_rapl = RAPL_PKG | RAPL_CORES | RAPL_CORE_POLICY | RAPL_DRAM | RAPL_PKG_PERF_STATUS | RAPL_DRAM_PERF_STATUS | RAPL_PKG_POWER_INFO;
		break;
	case 0x37:	/* BYT */
	case 0x4D:	/* AVN */
		do_rapl = RAPL_PKG | RAPL_CORES ;
		break;
	default:
		return;
	}

	/* units on package 0, verify later other packages match */
	if (get_msr(0, MSR_RAPL_POWER_UNIT, &msr))
		return;

	rapl_power_units = 1.0 / (1 << (msr & 0xF));
	if (model == 0x37)
		rapl_energy_units = 1.0 * (1 << (msr >> 8 & 0x1F)) / 1000000;
	else
		rapl_energy_units = 1.0 / (1 << (msr >> 8 & 0x1F));

	time_unit = msr >> 16 & 0xF;
	if (time_unit == 0)
		time_unit = 0xA;

	rapl_time_units = 1.0 / (1 << (time_unit));

	tdp = get_tdp(model);

	rapl_joule_counter_range = 0xFFFFFFFF * rapl_energy_units / tdp;
//	if (verbose)
//		fprintf(stderr, "RAPL: %.0f sec. Joule Counter Range, at %.0f Watts\n", rapl_joule_counter_range, tdp);

	return;
}

static int
is_snb(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	switch (model) {
	case 0x2A:
	case 0x2D:
	case 0x3A:	/* IVB */
	case 0x3E:	/* IVB Xeon */
	case 0x3C:	/* HSW */
	case 0x3F:	/* HSW */
	case 0x45:	/* HSW */
	case 0x46:	/* HSW */
		return 1;
	}
	return 0;
}

static int
has_c8_c9_c10(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;

	switch (model) {
	case 0x45:
		return 1;
	}
	return 0;
}


static int
is_slm(unsigned int family, unsigned int model)
{
	if (!genuine_intel)
		return 0;
	switch (model) {
	case 0x37:	/* BYT */
	case 0x4D:	/* AVN */
		return 1;
	}
	return 0;
}

/*
 * MSR_IA32_TEMPERATURE_TARGET indicates the temperature where
 * the Thermal Control Circuit (TCC) activates.
 * This is usually equal to tjMax.
 *
 * Older processors do not have this MSR, so there we guess,
 * but also allow cmdline over-ride with -T.
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
	int cpu;

	/* tcc_activation_temp is used only for dts or ptm */
	if (!(do_dts || do_ptm))
		return 0;

	/* this is a per-package concept */
	if (!(t->flags & CPU_IS_FIRST_THREAD_IN_CORE) || !(t->flags & CPU_IS_FIRST_CORE_IN_PACKAGE))
		return 0;

	cpu = t->cpu_id;
	if (cpu_migrate(cpu)) {
		ERROR("Could not migrate to CPU %d\n", cpu);
		return -ERR_CPU_MIGRATE;
	}

	if (tcc_activation_temp_override != 0) {
		tcc_activation_temp = tcc_activation_temp_override;
		ERROR("cpu%d: Using cmdline TCC Target (%d C)\n",
			cpu, tcc_activation_temp);
		return 0;
	}

	/* Temperature Target MSR is Nehalem and newer only */
	if (!do_nehalem_platform_info)
		goto guess;

	if (get_msr(0, MSR_IA32_TEMPERATURE_TARGET, &msr))
		goto guess;

	target_c_local = (msr >> 16) & 0x7F;

	if (target_c_local < 85 || target_c_local > 127)
		goto guess;

	tcc_activation_temp = target_c_local;

	return 0;

guess:
	tcc_activation_temp = TJMAX_DEFAULT;
	WARNING("cpu%d: Guessing tjMax %d C, Please use -T to specify\n",
		cpu, tcc_activation_temp);

	return 0;
}

static int __attribute__((warn_unused_result))
check_cpuid()
{
	unsigned int eax, ebx, ecx, edx, max_level;
	unsigned int fms, family, model;

	eax = ebx = ecx = edx = 0;

	__get_cpuid(0, &max_level, &ebx, &ecx, &edx);

	if (ebx == 0x756e6547 && edx == 0x49656e69 && ecx == 0x6c65746e)
		genuine_intel = 1;

	fms = 0;
	__get_cpuid(1, &fms, &ebx, &ecx, &edx);
	family = (fms >> 8) & 0xf;
	model = (fms >> 4) & 0xf;
	if (family == 6 || family == 0xf)
		model += ((fms >> 16) & 0xf) << 4;

	if (!(edx & (1 << 5))) {
		ERROR("CPUID: no MSR");
		return -ERR_NO_MSR;
	}

	/*
	 * check max extended function levels of CPUID.
	 * This is needed to check for invariant TSC.
	 * This check is valid for both Intel and AMD.
	 */
	ebx = ecx = edx = 0;
	__get_cpuid(0x80000000, &max_level, &ebx, &ecx, &edx);

	if (max_level < 0x80000007) {
		ERROR("CPUID: no invariant TSC (max_level 0x%x)", max_level);
		return -ERR_NO_INVARIANT_TSC;
	}

	/*
	 * Non-Stop TSC is advertised by CPUID.EAX=0x80000007: EDX.bit8
	 * this check is valid for both Intel and AMD
	 */
	__get_cpuid(0x80000007, &eax, &ebx, &ecx, &edx);
	if (!(edx & (1 << 8))) {
		ERROR("No invariant TSC");
		return -ERR_NO_INVARIANT_TSC;
	}

	/*
	 * APERF/MPERF is advertised by CPUID.EAX=0x6: ECX.bit0
	 * this check is valid for both Intel and AMD
	 */

	__get_cpuid(0x6, &eax, &ebx, &ecx, &edx);
	do_dts = eax & (1 << 0);
	do_ptm = eax & (1 << 6);
	has_epb = ecx & (1 << 3);

	if (!(ecx & (1 << 0))) {
		ERROR("No APERF");
		return -ERR_NO_APERF;
	}

   do_nehalem_platform_info = genuine_intel;
	do_nhm_cstates = genuine_intel;	/* all Intel w/ non-stop TSC have NHM counters */
	do_smi = do_nhm_cstates;
	do_snb_cstates = is_snb(family, model);
	do_c8_c9_c10 = has_c8_c9_c10(family, model);
	do_slm_cstates = is_slm(family, model);

	rapl_probe(family, model);

	return 0;
}



static int __attribute__((warn_unused_result))
topology_probe()
{
	int i;
	int ret;
	int max_core_id = 0;
	int max_package_id = 0;
	int max_siblings = 0;
	struct cpu_topology {
		int core_id;
		int physical_package_id;
	} *cpus;

	/* Initialize num_cpus, max_cpu_num */
	topo.num_cpus = 0;
	topo.max_cpu_num = 0;
	ret = for_all_proc_cpus(count_cpus);
	if (ret < 0)
		return ret;

	DEBUG("num_cpus %d max_cpu_num %d\n", topo.num_cpus, topo.max_cpu_num);

	cpus = calloc(1, (topo.max_cpu_num  + 1) * sizeof(struct cpu_topology));
	if (cpus == NULL) {
		ERROR("calloc cpus");
		return -ERR_CALLOC;
	}

	/*
	 * Allocate and initialize cpu_present_set
	 */
	cpu_present_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_present_set == NULL) {
		free(cpus);
		ERROR("CPU_ALLOC");
		return -ERR_CPU_ALLOC;
	}
	cpu_present_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_present_setsize, cpu_present_set);
	ret = for_all_proc_cpus(mark_cpu_present);
	if (ret < 0) {
		free(cpus);
		return ret;
	}

	/*
	 * Allocate and initialize cpu_affinity_set
	 */
	cpu_affinity_set = CPU_ALLOC((topo.max_cpu_num + 1));
	if (cpu_affinity_set == NULL) {
		free(cpus);
		ERROR("CPU_ALLOC");
		return -ERR_CPU_ALLOC;
	}
	cpu_affinity_setsize = CPU_ALLOC_SIZE((topo.max_cpu_num + 1));
	CPU_ZERO_S(cpu_affinity_setsize, cpu_affinity_set);


	/*
	 * For online cpus
	 * find max_core_id, max_package_id
	 */
	for (i = 0; i <= topo.max_cpu_num; ++i) {
		int siblings;

		if (cpu_is_not_present(i)) {
			//if (verbose > 1)
				fprintf(stderr, "cpu%d NOT PRESENT\n", i);
			continue;
		}
		cpus[i].core_id = get_core_id(i);
		if (cpus[i].core_id < 0)
			return cpus[i].core_id;
		if (cpus[i].core_id > max_core_id)
			max_core_id = cpus[i].core_id;

		cpus[i].physical_package_id = get_physical_package_id(i);
		if (cpus[i].physical_package_id < 0)
			return cpus[i].physical_package_id;
		if (cpus[i].physical_package_id > max_package_id)
			max_package_id = cpus[i].physical_package_id;

		siblings = get_num_ht_siblings(i);
		if (siblings < 0)
			return siblings;
		if (siblings > max_siblings)
			max_siblings = siblings;
		DEBUG("cpu %d pkg %d core %d\n",
			i, cpus[i].physical_package_id, cpus[i].core_id);
	}
	topo.num_cores_per_pkg = max_core_id + 1;
	DEBUG("max_core_id %d, sizing for %d cores per package\n",
		max_core_id, topo.num_cores_per_pkg);

	topo.num_packages = max_package_id + 1;
	DEBUG("max_package_id %d, sizing for %d packages\n",
		max_package_id, topo.num_packages);

	topo.num_threads_per_core = max_siblings;
	DEBUG("max_siblings %d\n", max_siblings);

	free(cpus);
	return 0;
}

static int
allocate_counters(struct thread_data **t, struct core_data **c, struct pkg_data **p)
{
	int i;

	*t = calloc(topo.num_threads_per_core * topo.num_cores_per_pkg *
		topo.num_packages, sizeof(struct thread_data));
	if (*t == NULL)
		goto error;

	for (i = 0; i < topo.num_threads_per_core *
		topo.num_cores_per_pkg * topo.num_packages; i++)
		(*t)[i].cpu_id = -1;

	*c = calloc(topo.num_cores_per_pkg * topo.num_packages,
		sizeof(struct core_data));
	if (*c == NULL)
		goto error;

	for (i = 0; i < topo.num_cores_per_pkg * topo.num_packages; i++)
		(*c)[i].core_id = -1;

	*p = calloc(topo.num_packages, sizeof(struct pkg_data));
	if (*p == NULL)
		goto error;

	for (i = 0; i < topo.num_packages; i++)
		(*p)[i].package_id = i;

	return 0;
error:
	ERROR("calloc counters");
	return -ERR_CALLOC;
}
/*
 * init_counter()
 *
 * set cpu_id, core_num, pkg_num
 * set FIRST_THREAD_IN_CORE and FIRST_CORE_IN_PACKAGE
 *
 * increment topo.num_cores when 1st core in pkg seen
 */
static int
init_counter(struct thread_data *thread_base, struct core_data *core_base,
	struct pkg_data *pkg_base, int thread_num, int core_num,
	int pkg_num, int cpu_id)
{
	int ret;
	struct thread_data *t;
	struct core_data *c;
	struct pkg_data *p;

	t = GET_THREAD(thread_base, thread_num, core_num, pkg_num);
	c = GET_CORE(core_base, core_num, pkg_num);
	p = GET_PKG(pkg_base, pkg_num);

	t->cpu_id = cpu_id;
	if (thread_num == 0) {
		t->flags |= CPU_IS_FIRST_THREAD_IN_CORE;
		if ((ret = cpu_is_first_core_in_package(cpu_id)) < 0) {
			return ret;
		} else if (ret != 0) {
			t->flags |= CPU_IS_FIRST_CORE_IN_PACKAGE;
		}
	}

	c->core_id = core_num;
	p->package_id = pkg_num;

	return 0;
}


static int
initialize_counters(int cpu_id)
{
	int my_thread_id, my_core_id, my_package_id;
	int ret;

	my_package_id = get_physical_package_id(cpu_id);
	if (my_package_id < 0)
		return my_package_id;
	my_core_id = get_core_id(cpu_id);
	if (my_core_id < 0)
		return my_core_id;

	if ((ret = cpu_is_first_sibling_in_core(cpu_id)) < 0) {
		return ret;
	} else if (ret != 0) {
		my_thread_id = 0;
		topo.num_cores++;
	} else {
		my_thread_id = 1;
	}

	ret = init_counter(EVEN_COUNTERS, my_thread_id, my_core_id, my_package_id, cpu_id);
	if (ret < 0)
		return ret;
	ret = init_counter(ODD_COUNTERS, my_thread_id, my_core_id, my_package_id, cpu_id);
	if (ret < 0)
		return ret;
	return 0;
}

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
	DO_OR_GOTO_ERR(for_all_proc_cpus(initialize_counters));

	allocated = 1;
	return 0;
err:
	free_all_buffers();
	return ret;
}

static int
turbostat_init(void)
{
	int ret;

	DO_OR_GOTO_ERR(check_cpuid());
	DO_OR_GOTO_ERR(check_dev_msr());
	DO_OR_GOTO_ERR(check_super_user());
	DO_OR_GOTO_ERR(setup_all_buffers());
	DO_OR_GOTO_ERR(for_all_cpus(set_temperature_target, EVEN_COUNTERS));

	plugin_register_complex_read(NULL, PLUGIN_NAME, turbostat_read, NULL, NULL);

	return 0;
err:
	free_all_buffers();
	return ret;
}

void module_register(void);
void module_register(void)
{
	plugin_register_init(PLUGIN_NAME, turbostat_init);
}
