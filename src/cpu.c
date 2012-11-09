/**
 * collectd - src/cpu.c
 * Copyright (C) 2005-2010  Florian octo Forster
 * Copyright (C) 2008       Oleg King
 * Copyright (C) 2009       Simon Kuhnle
 * Copyright (C) 2009       Manuel Sanmartin
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
 *   Oleg King <king2 at kaluga.ru>
 *   Simon Kuhnle <simon at blarzwurst.de>
 *   Manuel Sanmartin
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
/* #endif KERNEL_LINUX */

#elif defined(HAVE_LIBKSTAT)
/* colleague tells me that Sun doesn't sell systems with more than 100 or so CPUs.. */
# define MAX_NUMCPU 256
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMCPU];
//static int numcpu;
/* #endif HAVE_LIBKSTAT */

#elif CAN_USE_SYSCTL
/* #endif CAN_USE_SYSCTL */

#elif defined(HAVE_SYSCTLBYNAME)
//static int numcpu;
#  ifdef HAVE_SYSCTL_KERN_CP_TIMES
static int maxcpu;
#  endif /* HAVE_SYSCTL_KERN_CP_TIMES */
/* #endif HAVE_SYSCTLBYNAME */

#elif defined(HAVE_LIBSTATGRAB)
/* no variables needed */
/* #endif  HAVE_LIBSTATGRAB */

#elif defined(HAVE_PERFSTAT)
static perfstat_cpu_t *perfcpu;
//static int numcpu;
static int pnumcpu;
#endif /* HAVE_PERFSTAT */

static const char *config_keys[] =
{
	"PerCpuStatistic",
	"ScaleTo100"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int numcpu = 1;
static int per_cpu_statistic_boolean = 1;
static int scale_boolean = 0;
static int hz=100;

static int cpu_config (const char *key, const char *value)
{
	if (strcasecmp (key, "PerCpuStatistic") == 0)
	{
		if (IS_FALSE(value)) 
			per_cpu_statistic_boolean = 0;
		else
			per_cpu_statistic_boolean = 1;
	}
	else if (strcasecmp (key, "ScaleTo100") == 0)
	{
		if (IS_FALSE(value))
			scale_boolean = 0;
		else
			scale_boolean = 1;
	}
	else
	{
		return (-1);
	}

	return (0);
}

static int init (void)
{
#if PROCESSOR_CPU_LOAD_INFO || PROCESSOR_TEMPERATURE
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

	cpu_temp_retry_max = 86400 / CDTIME_T_TO_TIME_T (interval_g);
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

        struct clockinfo ci;
	size_t ci_size = sizeof (ci);
	int mib[2] = {CTL_KERN, KERN_CLOCKRATE};

	status = sysctl (mib, STATIC_ARRAY_SIZE (mib),
			&ci, &ci_size, NULL, 0);
	if (status == -1)
	{
		char errbuf[1024];
		WARNING ("cpu plugin: sysctl: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}
        hz = ci.stathz;
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

        struct clockinfo ci;
	size_t ci_size = sizeof (ci);
	if (sysctlbyname ("kern.clockrate", &ci, &ci_size, NULL, 0) < 0)
	{
		char errbuf[1024];
		WARNING ("cpu plugin: sysctlbyname(kern.clockrate): %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}
        hz = ci.stathz;

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

#if defined(KERNEL_LINUX)
        numcpu = sysconf( _SC_NPROCESSORS_ONLN );
        hz = sysconf(_SC_CLK_TCK);
#endif /* KERNEL_LINUX */

	return (0);
} /* int init */

static void submit (int cpu_num, const char *type_instance, derive_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

        if (hz != 100)
	    values[0].derive = (derive_t) (value * (100.0 / hz));
        else
	    values[0].derive = value;

        if (scale_boolean == 1 && cpu_num < 0)
            values[0].derive = values[0].derive / numcpu;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "cpu", sizeof (vl.plugin));
        if (cpu_num >= 0)
	    ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance),
			"%i", cpu_num);
	sstrncpy (vl.type, "cpu", sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
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
			ERROR ("cpu plugin: processor_info failed with status %i", (int) status);
			continue;
		}

		if (cpu_info_len < CPU_STATE_MAX)
		{
			ERROR ("cpu plugin: processor_info returned only %i elements..", cpu_info_len);
			continue;
		}

		submit (cpu, "user", (derive_t) cpu_info.cpu_ticks[CPU_STATE_USER]);
		submit (cpu, "nice", (derive_t) cpu_info.cpu_ticks[CPU_STATE_NICE]);
		submit (cpu, "system", (derive_t) cpu_info.cpu_ticks[CPU_STATE_SYSTEM]);
		submit (cpu, "idle", (derive_t) cpu_info.cpu_ticks[CPU_STATE_IDLE]);
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
			ERROR ("cpu plugin: processor_info failed: %s",
					mach_error_string (status));

			cpu_temp_retry_counter = cpu_temp_retry_step;
			cpu_temp_retry_step *= 2;
			if (cpu_temp_retry_step > cpu_temp_retry_max)
				cpu_temp_retry_step = cpu_temp_retry_max;

			continue;
		}

		if (cpu_temp_len != 1)
		{
			DEBUG ("processor_info (PROCESSOR_TEMPERATURE) returned %i elements..?",
				       	(int) cpu_temp_len);
			continue;
		}

		cpu_temp_retry_counter = 0;
		cpu_temp_retry_step    = 1;

		DEBUG ("cpu_temp = %i", (int) cpu_temp);
#endif /* PROCESSOR_TEMPERATURE */
	}
/* #endif PROCESSOR_CPU_LOAD_INFO */

#elif defined(KERNEL_LINUX)
	int cpu;
	derive_t user, nice, syst, idle;
	derive_t wait, intr, sitr; /* sitr == soft interrupt */
	FILE *fh;
	char buf[1024];

	char *fields[9];
	int numfields;
     int allcpu_boolean;

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
    
		allcpu_boolean = (buf[3] == ' ' ? 1 : 0);
		if ( ((per_cpu_statistic_boolean == 0) || (buf[3] < '0') || (buf[3] > '9')) && (allcpu_boolean == 0) )
			continue;

		numfields = strsplit (buf, fields, 9);
		if (numfields < 5)
			continue;

                if (allcpu_boolean == 1) 
                    cpu = -1;
                else
                    cpu = atoi (fields[0] + 3);

		user = atoll (fields[1]);
		nice = atoll (fields[2]);
		syst = atoll (fields[3]);
		idle = atoll (fields[4]);

		submit (cpu, "user", user);
		submit (cpu, "nice", nice);
		submit (cpu, "system", syst);
		submit (cpu, "idle", idle);

		if (numfields >= 8)
		{
			wait = atoll (fields[5]);
			intr = atoll (fields[6]);
			sitr = atoll (fields[7]);

			submit (cpu, "wait", wait);
			submit (cpu, "interrupt", intr);
			submit (cpu, "softirq", sitr);

			if (numfields >= 9)
				submit (cpu, "steal", atoll (fields[8]));
		}
	}

	fclose (fh);
/* #endif defined(KERNEL_LINUX) */

#elif defined(HAVE_LIBKSTAT)
	int cpu;
	derive_t user, syst, idle, wait;
	static cpu_stat_t cs;

	if (kc == NULL)
		return (-1);

	for (cpu = 0; cpu < numcpu; cpu++)
	{
		if (kstat_read (kc, ksp[cpu], &cs) == -1)
			continue; /* error message? */

		idle = (derive_t) cs.cpu_sysinfo.cpu[CPU_IDLE];
		user = (derive_t) cs.cpu_sysinfo.cpu[CPU_USER];
		syst = (derive_t) cs.cpu_sysinfo.cpu[CPU_KERNEL];
		wait = (derive_t) cs.cpu_sysinfo.cpu[CPU_WAIT];

		submit (ksp[cpu]->ks_instance, "user", user);
		submit (ksp[cpu]->ks_instance, "system", syst);
		submit (ksp[cpu]->ks_instance, "idle", idle);
		submit (ksp[cpu]->ks_instance, "wait", wait);
	}
/* #endif defined(HAVE_LIBKSTAT) */

#elif CAN_USE_SYSCTL
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
	if (per_cpu_statistic_boolean == 1 && numcpu > 1) {
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
	        for (i = 0; i < numcpu; i++) {
		        submit (i, "user",      cpuinfo[i][CP_USER]);
		        submit (i, "nice",      cpuinfo[i][CP_NICE]);
		        submit (i, "system",    cpuinfo[i][CP_SYS]);
		        submit (i, "idle",      cpuinfo[i][CP_IDLE]);
		        submit (i, "interrupt", cpuinfo[i][CP_INTR]);
	        }
	}
#endif /* defined(KERN_CPTIME2) */
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

	submit (-1, "user",      cpuinfo_tmp[CP_USER]);
	submit (-1, "nice",      cpuinfo_tmp[CP_NICE]);
	submit (-1, "system",    cpuinfo_tmp[CP_SYS]);
	submit (-1, "idle",      cpuinfo_tmp[CP_IDLE]);
	submit (-1, "interrupt", cpuinfo_tmp[CP_INTR]);
/* #endif CAN_USE_SYSCTL */
#elif defined(HAVE_SYSCTLBYNAME) && defined(HAVE_SYSCTL_KERN_CP_TIMES)
    	size_t cpuinfo_size;
        if (per_cpu_statistic_boolean == 1) {
        	long cpuinfo[maxcpu][CPUSTATES];
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
        		submit (i, "user", cpuinfo[i][CP_USER]);
        		submit (i, "nice", cpuinfo[i][CP_NICE]);
        		submit (i, "system", cpuinfo[i][CP_SYS]);
        		submit (i, "idle", cpuinfo[i][CP_IDLE]);
        		submit (i, "interrupt", cpuinfo[i][CP_INTR]);
        	}
        }

	long cpuinfo2[CPUSTATES];

	cpuinfo_size = sizeof (cpuinfo2);

	if (sysctlbyname("kern.cp_time", &cpuinfo2, &cpuinfo_size, NULL, 0) < 0)
	{
		char errbuf[1024];
		ERROR ("cpu plugin: sysctlbyname failed: %s.",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}
	submit (-1, "user", cpuinfo2[CP_USER]);
	submit (-1, "nice", cpuinfo2[CP_NICE]);
	submit (-1, "system", cpuinfo2[CP_SYS]);
	submit (-1, "idle", cpuinfo2[CP_IDLE]);
	submit (-1, "interrupt", cpuinfo2[CP_INTR]);

/* #endif HAVE_SYSCTL_KERN_CP_TIMES */
#elif defined(HAVE_SYSCTLBYNAME)
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

	submit (-1, "user", cpuinfo[CP_USER]);
	submit (-1, "nice", cpuinfo[CP_NICE]);
	submit (-1, "system", cpuinfo[CP_SYS]);
	submit (-1, "idle", cpuinfo[CP_IDLE]);
	submit (-1, "interrupt", cpuinfo[CP_INTR]);
/* #endif HAVE_SYSCTLBYNAME */

#elif defined(HAVE_LIBSTATGRAB)
	sg_cpu_stats *cs;
	cs = sg_get_cpu_stats ();

	if (cs == NULL)
	{
		ERROR ("cpu plugin: sg_get_cpu_stats failed.");
		return (-1);
	}

	submit (0, "idle",   (derive_t) cs->idle);
	submit (0, "nice",   (derive_t) cs->nice);
	submit (0, "swap",   (derive_t) cs->swap);
	submit (0, "system", (derive_t) cs->kernel);
	submit (0, "user",   (derive_t) cs->user);
	submit (0, "wait",   (derive_t) cs->iowait);
/* #endif HAVE_LIBSTATGRAB */

#elif defined(HAVE_PERFSTAT)
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
		submit (i, "idle",   (derive_t) perfcpu[i].idle);
		submit (i, "system", (derive_t) perfcpu[i].sys);
		submit (i, "user",   (derive_t) perfcpu[i].user);
		submit (i, "wait",   (derive_t) perfcpu[i].wait);
	}
#endif /* HAVE_PERFSTAT */

	return (0);
}

void module_register (void)
{
	plugin_register_config ("cpu", cpu_config,
			config_keys, config_keys_num);
	plugin_register_init ("cpu", init);
	plugin_register_read ("cpu", cpu_read);
} /* void module_register */
