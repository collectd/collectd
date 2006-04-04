/**
 * collectd - src/cpu.c
 * Copyright (C) 2005  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_debug.h"

#define MODULE_NAME "cpu"

#ifdef HAVE_MACH_KERN_RETURN_H
# include <mach/kern_return.h>
#endif
#ifdef HAVE_MACH_MACH_INIT_H
# include <mach/mach_init.h>
#endif
#ifdef HAVE_MACH_HOST_PRIV_H
# include <mach/host_priv.h>
#endif
#ifdef HAVE_MACH_PROCESSOR_INFO_H
# include <mach/processor_info.h>
#endif
#ifdef HAVE_MACH_PROCESSOR_H
# include <mach/processor.h>
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

#elif defined(HAVE_SYSCTLBYNAME)
static int numcpu;
#endif /* HAVE_SYSCTLBYNAME */

static char *cpu_filename = "cpu-%s.rrd";

static char *ds_def[] =
{
	"DS:user:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:nice:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:syst:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:idle:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:wait:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int ds_num = 5;

static void cpu_init (void)
{
#ifdef PROCESSOR_CPU_LOAD_INFO
	kern_return_t status;

	port_host = mach_host_self ();

	if ((status = host_processors (port_host, &cpu_list, &cpu_list_len)) != KERN_SUCCESS)
	{
		syslog (LOG_ERR, "cpu-plugin: host_processors returned %i\n", (int) status);
		cpu_list_len = 0;
		return;
	}

	DBG ("host_processors returned %i %s", (int) cpu_list_len, cpu_list_len == 1 ? "processor" : "processors");
	syslog (LOG_INFO, "cpu-plugin: Found %i processor%s.", (int) cpu_list_len, cpu_list_len == 1 ? "" : "s");
/* #endif PROCESSOR_CPU_LOAD_INFO */

#elif defined(HAVE_LIBKSTAT)
	kstat_t *ksp_chain;

	numcpu = 0;

	if (kc == NULL)
		return;

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
		return;
	}

	if (numcpu != 1)
		syslog (LOG_NOTICE, "cpu: Only one processor supported when using `sysctlbyname' (found %i)", numcpu);
#endif

	return;
}

static void cpu_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, cpu_filename, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, ds_def, ds_num);
}

#if CPU_HAVE_READ
#define BUFSIZE 512
static void cpu_submit (int cpu_num, unsigned long long user,
		unsigned long long nice, unsigned long long syst,
		unsigned long long idle, unsigned long long wait)
{
	char buf[BUFSIZE];
	char cpu[16];

	if (snprintf (buf, BUFSIZE, "%u:%llu:%llu:%llu:%llu:%llu", (unsigned int) curtime,
				user, nice, syst, idle, wait) >= BUFSIZE)
		return;
	snprintf (cpu, 16, "%i", cpu_num);

	plugin_submit (MODULE_NAME, cpu, buf);
}
#undef BUFSIZE

static void cpu_read (void)
{
#ifdef PROCESSOR_CPU_LOAD_INFO
	int cpu;

	kern_return_t status;
	
	processor_cpu_load_info_data_t cpu_info;
	processor_cpu_load_info_t      cpu_info_ptr;
	mach_msg_type_number_t         cpu_info_len;

	host_t cpu_host;

	for (cpu = 0; cpu < cpu_list_len; cpu++)
	{
		cpu_host = 0;
		cpu_info_ptr = &cpu_info;
		cpu_info_len = sizeof (cpu_info);

		if ((status = processor_info (cpu_list[cpu],
						PROCESSOR_CPU_LOAD_INFO, &cpu_host,
						(processor_info_t) cpu_info_ptr, &cpu_info_len)) != KERN_SUCCESS)
		{
			syslog (LOG_ERR, "processor_info failed with status %i\n", (int) status);
			continue;
		}

		if (cpu_info_len < CPU_STATE_MAX)
		{
			syslog (LOG_ERR, "processor_info returned only %i elements..\n", cpu_info_len);
			continue;
		}

		cpu_submit (cpu, cpu_info.cpu_ticks[CPU_STATE_USER],
				cpu_info.cpu_ticks[CPU_STATE_NICE],
				cpu_info.cpu_ticks[CPU_STATE_SYSTEM],
				cpu_info.cpu_ticks[CPU_STATE_IDLE],
				0ULL);
	}
/* #endif PROCESSOR_CPU_LOAD_INFO */

#elif defined(KERNEL_LINUX)
# define BUFSIZE 1024
	int cpu;
	unsigned long long user, nice, syst, idle;
	unsigned long long wait, intr, sitr; /* sitr == soft interrupt */
	FILE *fh;
	char buf[BUFSIZE];

	char *fields[9];
	int numfields;

	if ((fh = fopen ("/proc/stat", "r")) == NULL)
	{
		syslog (LOG_WARNING, "cpu: fopen: %s", strerror (errno));
		return;
	}

	while (fgets (buf, BUFSIZE, fh) != NULL)
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

		cpu_submit (cpu, user, nice, syst, idle, wait);
	}

	fclose (fh);
#undef BUFSIZE
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

		cpu_submit (ksp[cpu]->ks_instance,
				user, 0LL, syst, idle, wait);
	}
/* #endif defined(HAVE_LIBKSTAT) */

#elif defined(HAVE_SYSCTLBYNAME)
	long cpuinfo[CPUSTATES];
	size_t cpuinfo_size;

	cpuinfo_size = sizeof (cpuinfo);

	if (sysctlbyname("kern.cp_time", &cpuinfo, &cpuinfo_size, NULL, 0) < 0)
	{
		syslog (LOG_WARNING, "cpu: sysctlbyname: %s", strerror (errno));
		return;
	}

	cpuinfo[CP_SYS] += cpuinfo[CP_INTR];

	/* FIXME: Instance is always `0' */
	cpu_submit (0, cpuinfo[CP_USER], cpuinfo[CP_NICE], cpuinfo[CP_SYS], cpuinfo[CP_IDLE], 0LL);
#endif

	return;
}
#else
# define cpu_read NULL
#endif /* CPU_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, cpu_init, cpu_read, cpu_write);
}

#undef MODULE_NAME
