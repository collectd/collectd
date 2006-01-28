/**
 * collectd - src/cpufreq.c
 * Copyright (C) 2005  Peter Holik
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

#define MODULE_NAME "cpufreq"

#if defined(KERNEL_LINUX)
# define CPUFREQ_HAVE_READ 1
#else
# define CPUFREQ_HAVE_READ 0
#endif

static char *cpufreq_file = "cpufreq-%s.rrd";

static char *ds_def[] =
{
	"DS:value:GAUGE:25:0:U",
	NULL
};
static int ds_num = 1;

#ifdef KERNEL_LINUX
static int num_cpu = 0;
#endif

#define BUFSIZE 256

static void cpufreq_init (void)
{
#ifdef KERNEL_LINUX
        int status;
	char filename[BUFSIZE];

	num_cpu = 0;

	while (1)
	{
		status = snprintf (filename, BUFSIZE, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", num_cpu);
    		if (status < 1 || status >= BUFSIZE)
			break;

		if (access(filename, R_OK))
			break;

		num_cpu++;
	}

	syslog (LOG_INFO, MODULE_NAME" found %d cpu(s)", num_cpu);
#endif /* defined(KERNEL_LINUX) */

	return;
}

static void cpufreq_write (char *host, char *inst, char *val)
{
        int status;
        char file[BUFSIZE];

        status = snprintf (file, BUFSIZE, cpufreq_file, inst);
        if (status < 1 || status >= BUFSIZE)
                return;

	rrd_update_file (host, file, val, ds_def, ds_num);
}

#if CPUFREQ_HAVE_READ
static void cpufreq_submit (int cpu_num, unsigned long long val)
{
	char buf[BUFSIZE];
	char cpu[16];

	if (snprintf (buf, BUFSIZE, "%u:%llu", (unsigned int) curtime, val) >= BUFSIZE)
		return;
        snprintf (cpu, 16, "%i", cpu_num);

	plugin_submit (MODULE_NAME, cpu, buf);
}

static void cpufreq_read (void)
{
#ifdef KERNEL_LINUX
        int status;
	unsigned long long val;
	int i = 0;
	FILE *fp;
	char filename[BUFSIZE];
	char buffer[16];

	for (i = 0; i < num_cpu; i++)
	{
		status = snprintf (filename, BUFSIZE, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
    		if (status < 1 || status >= BUFSIZE)
			return;

		if ((fp = fopen (filename, "r")) == NULL)
		{
			syslog (LOG_WARNING, "cpufreq: fopen: %s", strerror (errno));
			return;
		}

		if (fgets (buffer, 16, fp) == NULL)
		{
			syslog (LOG_WARNING, "cpufreq: fgets: %s", strerror (errno));
			return;
		}

		if (fclose (fp))
			syslog (LOG_WARNING, "cpufreq: fclose: %s", strerror (errno));

		/* You're seeing correctly: The file is reporting kHz values.. */
		val = atoll (buffer) * 1000;

		cpufreq_submit (i, val);
	}
#endif /* defined(KERNEL_LINUX) */

	return;
}
#else
#define cpufreq_read NULL
#endif
#undef BUFSIZE

void module_register (void)
{
	plugin_register (MODULE_NAME, cpufreq_init, cpufreq_read, cpufreq_write);
}

#undef MODULE_NAME
