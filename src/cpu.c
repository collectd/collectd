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
#include "plugin.h"
#include "common.h"

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

#ifdef HAVE_LIBKSTAT
/* colleague tells me that Sun doesn't sell systems with more than 100 or so CPUs.. */
# define MAX_NUMCPU 256
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMCPU];
static int numcpu;
#endif /* HAVE_LIBKSTAT */

#ifdef HAVE_SYSCTLBYNAME
static int numcpu;
#endif /* HAVE_SYSCTLBYNAME */

#define MODULE_NAME "cpu"

static char *cpu_filename = "cpu-%s.rrd";

static char *ds_def[] =
{
	"DS:user:COUNTER:25:0:100",
	"DS:nice:COUNTER:25:0:100",
	"DS:syst:COUNTER:25:0:100",
	"DS:idle:COUNTER:25:0:100",
	"DS:wait:COUNTER:25:0:100",
	NULL
};
static int ds_num = 5;

void cpu_init (void)
{
#ifdef HAVE_LIBKSTAT
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

void cpu_write (char *host, char *inst, char *val)
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
#ifdef KERNEL_LINUX
#define BUFSIZE 1024
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

#elif defined (HAVE_SYSCTLBYNAME)
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

void module_register (void)
{
	plugin_register (MODULE_NAME, cpu_init, cpu_read, cpu_write);
}

#undef MODULE_NAME
