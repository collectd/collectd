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
#include "common.h"
#include "plugin.h"

#define MODULE_NAME "cpufreq"

#if defined(KERNEL_LINUX)
# define CPUFREQ_HAVE_READ 1
#else
# define CPUFREQ_HAVE_READ 0
#endif

static data_source_t data_source[1] =
{
	{"value", DS_TYPE_GAUGE, 0, NAN}
};

static data_set_t data_set =
{
	"cpufreq", 1, data_source
};

#if CPUFREQ_HAVE_READ
#ifdef KERNEL_LINUX
static int num_cpu = 0;
#endif

static int cpufreq_init (void)
{
#ifdef KERNEL_LINUX
        int status;
	char filename[256];

	num_cpu = 0;

	while (1)
	{
		status = snprintf (filename, sizeof (filename),
				"/sys/devices/system/cpu/cpu%d/cpufreq/"
				"scaling_cur_freq", num_cpu);
    		if (status < 1 || status >= sizeof (filename))
			break;

		if (access (filename, R_OK))
			break;

		num_cpu++;
	}

	syslog (LOG_INFO, "cpufreq plugin: Found %d CPU%s", num_cpu,
			(num_cpu == 1) ? "" : "s");

	if (num_cpu == 0)
		plugin_unregister_read ("cpufreq");
#endif /* defined(KERNEL_LINUX) */

	return (0);
} /* int cpufreq_init */

static void cpufreq_submit (int cpu_num, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "cpufreq");
	snprintf (vl.type_instance, sizeof (vl.type_instance),
			"%i", cpu_num);

	plugin_dispatch_values ("cpufreq", &vl);
}

static int cpufreq_read (void)
{
#ifdef KERNEL_LINUX
        int status;
	unsigned long long val;
	int i = 0;
	FILE *fp;
	char filename[256];
	char buffer[16];

	for (i = 0; i < num_cpu; i++)
	{
		status = snprintf (filename, sizeof (filename),
				"/sys/devices/system/cpu/cpu%d/cpufreq/"
				"scaling_cur_freq", i);
    		if (status < 1 || status >= sizeof (filename))
			return (-1);

		if ((fp = fopen (filename, "r")) == NULL)
		{
			syslog (LOG_WARNING, "cpufreq: fopen: %s", strerror (errno));
			return (-1);
		}

		if (fgets (buffer, 16, fp) == NULL)
		{
			syslog (LOG_WARNING, "cpufreq: fgets: %s", strerror (errno));
			fclose (fp);
			return (-1);
		}

		if (fclose (fp))
			syslog (LOG_WARNING, "cpufreq: fclose: %s", strerror (errno));

		/* You're seeing correctly: The file is reporting kHz values.. */
		val = atoll (buffer) * 1000;

		cpufreq_submit (i, val);
	}
#endif /* defined(KERNEL_LINUX) */

	return (0);
} /* int cpufreq_read */
#endif /* CPUFREQ_HAVE_READ */
#undef BUFSIZE

void module_register (void)
{
	plugin_register_data_set (&data_set);

#if CPUFREQ_HAVE_READ
	plugin_register_init ("cpufreq", cpufreq_init);
	plugin_register_read ("cpufreq", cpufreq_read);
#endif /* CPUFREQ_HAVE_READ */
}
