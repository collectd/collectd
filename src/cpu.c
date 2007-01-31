/**
 * collectd - src/cpu.c
 * Copyright (C) 2005-2007  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_debug.h"

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

#ifdef HAVE_SYSCTLBYNAME
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
#endif /* HAVE_SYSCTLBYNAME */

#if defined(PROCESSOR_CPU_LOAD_INFO) || defined(KERNEL_LINUX) || defined(HAVE_LIBKSTAT) || defined(HAVE_SYSCTLBYNAME)
# define CPU_HAVE_READ 1
#else
# define CPU_HAVE_READ 0
#endif

static data_source_t dsrc[5] =
{
	{"user", DS_TYPE_COUNTER, 0, 4294967295.0},
	{"nice", DS_TYPE_COUNTER, 0, 4294967295.0},
	{"syst", DS_TYPE_COUNTER, 0, 4294967295.0},
	{"idle", DS_TYPE_COUNTER, 0, 4294967295.0},
	{"wait", DS_TYPE_COUNTER, 0, 4294967295.0}
};

static data_set_t ds =
{
	"cpu", 5, dsrc
};

#if CPU_HAVE_READ
#ifdef PROCESSOR_CPU_LOAD_INFO
static mach_port_t port_host;
static processor_port_array_t cpu_list;
static mach_msg_type_number_t cpu_list_len;

#if PROCESSOR_TEMPERATURE
static int cpu_temp_retry_counter = 0;
static int cpu_temp_retry_step    = 1;
static int cpu_temp_retry_max     = 1;
#endif /* PROCESSOR_TEMPERATURE */
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

#elif defined(HAVE_SYSCTLBYNAME)
static int numcpu;
#endif /* HAVE_SYSCTLBYNAME */

static int init (void)
{
#if PROCESSOR_CPU_LOAD_INFO || PROCESSOR_TEMPERATURE
	kern_return_t status;
	int collectd_step;

	port_host = mach_host_self ();

	/* FIXME: Free `cpu_list' if it's not NULL */
	if ((status = host_processors (port_host, &cpu_list, &cpu_list_len)) != KERN_SUCCESS)
	{
		syslog (LOG_ERR, "cpu plugin: host_processors returned %i", (int) status);
		cpu_list_len = 0;
		return (-1);
	}

	DBG ("host_processors returned %i %s", (int) cpu_list_len, cpu_list_len == 1 ? "processor" : "processors");
	syslog (LOG_INFO, "cpu plugin: Found %i processor%s.", (int) cpu_list_len, cpu_list_len == 1 ? "" : "s");

	collectd_step = atoi (COLLECTD_STEP);
	if ((collectd_step > 0) && (collectd_step <= 86400))
		cpu_temp_retry_max = 86400 / collectd_step;
		
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

#elif defined (HAVE_SYSCTLBYNAME)
	size_t numcpu_size;

	numcpu_size = sizeof (numcpu);

	if (sysctlbyname ("hw.ncpu", &numcpu, &numcpu_size, NULL, 0) < 0)
	{
		syslog (LOG_WARNING, "cpu: sysctlbyname: %s", strerror (errno));
		return (-1);
	}

	if (numcpu != 1)
		syslog (LOG_NOTICE, "cpu: Only one processor supported when using `sysctlbyname' (found %i)", numcpu);
#endif

	return (0);
} /* int init */

static void submit (int cpu_num, unsigned long long user,
		unsigned long long nice, unsigned long long syst,
		unsigned long long idle, unsigned long long wait)
{
	value_t values[5];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = user;
	values[1].counter = nice;
	values[2].counter = syst;
	values[3].counter = idle;
	values[4].counter = wait;

	vl.values = values;
	vl.values_len = 2;
	vl.time = time (NULL);
	strcpy (vl.host, hostname);
	strcpy (vl.plugin, "cpu");
	strcpy (vl.plugin_instance, "");
	snprintf (vl.type_instance, sizeof (vl.type_instance),
			"%i", cpu_num);
	vl.type_instance[DATA_MAX_NAME_LEN - 1] = '\0';

	plugin_dispatch_values ("cpu", &vl);
}

static int cpu_read (void)
{
#if PROCESSOR_CPU_LOAD_INFO || PROCESSOR_TEMPERATURE
	int cpu;

	kern_return_t status;
	
#if PROCESSOR_CPU_LOAD_INFO
	processor_cpu_load_info_data_t cpu_info;
	mach_msg_type_number_t         cpu_info_len;
#endif
#if PROCESSOR_TEMPERATURE
	processor_info_data_t          cpu_temp;
	mach_msg_type_number_t         cpu_temp_len;
#endif

	host_t cpu_host;

	for (cpu = 0; cpu < cpu_list_len; cpu++)
	{
#if PROCESSOR_CPU_LOAD_INFO
		cpu_host = 0;
		cpu_info_len = PROCESSOR_BASIC_INFO_COUNT;

		if ((status = processor_info (cpu_list[cpu],
						PROCESSOR_CPU_LOAD_INFO, &cpu_host,
						(processor_info_t) &cpu_info, &cpu_info_len)) != KERN_SUCCESS)
		{
			syslog (LOG_ERR, "cpu plugin: processor_info failed with status %i", (int) status);
			continue;
		}

		if (cpu_info_len < CPU_STATE_MAX)
		{
			syslog (LOG_ERR, "cpu plugin: processor_info returned only %i elements..", cpu_info_len);
			continue;
		}

		submit (cpu, cpu_info.cpu_ticks[CPU_STATE_USER],
				cpu_info.cpu_ticks[CPU_STATE_NICE],
				cpu_info.cpu_ticks[CPU_STATE_SYSTEM],
				cpu_info.cpu_ticks[CPU_STATE_IDLE],
				0ULL);
#endif /* PROCESSOR_CPU_LOAD_INFO */
#if PROCESSOR_TEMPERATURE
		/*
		 * Not all Apple computers do have this ability. To minimize
		 * the messages sent to the syslog we do an exponential
		 * stepback if `processor_info' fails. We still try ~once a day
		 * though..
		 */
		if (cpu_temp_retry_counter > 0)
		{
			cpu_temp_retry_counter--;
			continue;
		}

		cpu_temp_len = PROCESSOR_INFO_MAX;

		status = processor_info (cpu_list[cpu],
				PROCESSOR_TEMPERATURE,
				&cpu_host,
				cpu_temp, &cpu_temp_len);
		if (status != KERN_SUCCESS)
		{
			syslog (LOG_ERR, "cpu plugin: processor_info failed: %s",
					mach_error_string (status));

			cpu_temp_retry_counter = cpu_temp_retry_step;
			cpu_temp_retry_step *= 2;
			if (cpu_temp_retry_step > cpu_temp_retry_max)
				cpu_temp_retry_step = cpu_temp_retry_max;

			continue;
		}

		if (cpu_temp_len != 1)
		{
			DBG ("processor_info (PROCESSOR_TEMPERATURE) returned %i elements..?",
				       	(int) cpu_temp_len);
			continue;
		}

		cpu_temp_retry_counter = 0;
		cpu_temp_retry_step    = 1;

		DBG ("cpu_temp = %i", (int) cpu_temp);
#endif /* PROCESSOR_TEMPERATURE */
	}
/* #endif PROCESSOR_CPU_LOAD_INFO */

#elif defined(KERNEL_LINUX)
	int cpu;
	unsigned long long user, nice, syst, idle;
	unsigned long long wait, intr, sitr; /* sitr == soft interrupt */
	FILE *fh;
	char buf[1024];

	char *fields[9];
	int numfields;

	static complain_t complain_obj;

	if ((fh = fopen ("/proc/stat", "r")) == NULL)
	{
		plugin_complain (LOG_ERR, &complain_obj, "cpu plugin: "
				"fopen (/proc/stat) failed: %s",
				strerror (errno));
		return (-1);
	}

	plugin_relief (LOG_NOTICE, &complain_obj, "cpu plugin: "
			"fopen (/proc/stat) succeeded.");

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
		user = atoll (fields[1]);
		nice = atoll (fields[2]);
		syst = atoll (fields[3]);
		idle = atoll (fields[4]);

		if (numfields >= 8)
		{
			wait = atoll (fields[5]);
			intr = atoll (fields[6]);
			sitr = atoll (fields[7]);

			/* I doubt anyone cares about the time spent in
			 * interrupt handlers.. */
			syst += intr + sitr;
		}
		else
		{
			wait = 0LL;
		}

		submit (cpu, user, nice, syst, idle, wait);
	}

	fclose (fh);
/* #endif defined(KERNEL_LINUX) */

#elif defined(HAVE_LIBKSTAT)
	int cpu;
	unsigned long long user, syst, idle, wait;
	static cpu_stat_t cs;

	if (kc == NULL)
		return;

	for (cpu = 0; cpu < numcpu; cpu++)
	{
		if (kstat_read (kc, ksp[cpu], &cs) == -1)
			continue; /* error message? */

		idle = (unsigned long long) cs.cpu_sysinfo.cpu[CPU_IDLE];
		user = (unsigned long long) cs.cpu_sysinfo.cpu[CPU_USER];
		syst = (unsigned long long) cs.cpu_sysinfo.cpu[CPU_KERNEL];
		wait = (unsigned long long) cs.cpu_sysinfo.cpu[CPU_WAIT];

		submit (ksp[cpu]->ks_instance,
				user, 0LL, syst, idle, wait);
	}
/* #endif defined(HAVE_LIBKSTAT) */

#elif defined(HAVE_SYSCTLBYNAME)
	long cpuinfo[CPUSTATES];
	size_t cpuinfo_size;

	static complain_t complain_obj;

	cpuinfo_size = sizeof (cpuinfo);

	if (sysctlbyname("kern.cp_time", &cpuinfo, &cpuinfo_size, NULL, 0) < 0)
	{
		plugin_complain (LOG_ERR, &complain_obj, "cpu plugin: "
				"sysctlbyname failed: %s.",
				strerror (errno));
		return;
	}

	plugin_relief (LOG_NOTICE, &complain_obj, "cpu plugin: "
			"sysctlbyname succeeded.");

	cpuinfo[CP_SYS] += cpuinfo[CP_INTR];

	/* FIXME: Instance is always `0' */
	submit (0, cpuinfo[CP_USER], cpuinfo[CP_NICE], cpuinfo[CP_SYS], cpuinfo[CP_IDLE], 0LL);
#endif

	return (0);
}
#endif /* CPU_HAVE_READ */

void module_register (void)
{
	plugin_register_data_set (&ds);

#if CPU_HAVE_READ
	plugin_register_init ("cpu", init);
	plugin_register_read ("cpu", cpu_read);
#endif /* CPU_HAVE_READ */
}
