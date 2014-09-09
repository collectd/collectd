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

static int num_cpu = 0;

static int cpufreq_init (void)
{
        int status;
	char filename[256];

	num_cpu = 0;

	while (1)
	{
		status = ssnprintf (filename, sizeof (filename),
				"/sys/devices/system/cpu/cpu%d/cpufreq/"
				"scaling_cur_freq", num_cpu);
		if ((status < 1) || ((unsigned int)status >= sizeof (filename)))
			break;

		if (access (filename, R_OK))
			break;

		num_cpu++;
	}

	INFO ("cpufreq plugin: Found %d CPU%s", num_cpu,
			(num_cpu == 1) ? "" : "s");

	if (num_cpu == 0)
		plugin_unregister_read ("cpufreq");

	return (0);
} /* int cpufreq_init */

static void cpufreq_submit (int cpu_num, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "cpufreq", sizeof (vl.plugin));
	sstrncpy (vl.type, "cpufreq", sizeof (vl.type));
	ssnprintf (vl.type_instance, sizeof (vl.type_instance),
			"%i", cpu_num);

	plugin_dispatch_values (&vl);
}

static int cpufreq_read (void)
{
        int status;
	unsigned long long val;
	int i = 0;
	FILE *fp;
	char filename[256];
	char buffer[16];

	for (i = 0; i < num_cpu; i++)
	{
		status = ssnprintf (filename, sizeof (filename),
				"/sys/devices/system/cpu/cpu%d/cpufreq/"
				"scaling_cur_freq", i);
		if ((status < 1) || ((unsigned int)status >= sizeof (filename)))
			return (-1);

		if ((fp = fopen (filename, "r")) == NULL)
		{
			char errbuf[1024];
			WARNING ("cpufreq: fopen (%s): %s", filename,
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			return (-1);
		}

		if (fgets (buffer, 16, fp) == NULL)
		{
			char errbuf[1024];
			WARNING ("cpufreq: fgets: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			fclose (fp);
			return (-1);
		}

		if (fclose (fp))
		{
			char errbuf[1024];
			WARNING ("cpufreq: fclose: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
		}


		/* You're seeing correctly: The file is reporting kHz values.. */
		val = atoll (buffer) * 1000;

		cpufreq_submit (i, val);
	}

	return (0);
} /* int cpufreq_read */

void module_register (void)
{
	plugin_register_init ("cpufreq", cpufreq_init);
	plugin_register_read ("cpufreq", cpufreq_read);
}
