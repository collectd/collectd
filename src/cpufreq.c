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
 *   Igor Solovyov <igor dot solovyov at gmail dot com>
 **/

/**
 * For quite a while, Linux kernel exposes the current CPU frequency via
 * "/sys/devices/system/cpu/cpuX/cpufreq/scaling_cur_freq" file.
 *
 * Nevertheless, in CentOS 7.1 it was observed that kernel doesn't provide the file anymore, with
 * the employed intel_pstate CPU frequency driver.
 *
 * The issue above has been fixed in https://github.com/torvalds/linux/commit/c034b02e213d271b98c45c4a7b54af8f69aaac1e .
 * But up to now with for example, CentOS 7.2, the fix has not been back-ported yet.
 *
 * Note that there is another "cpuinfo_cur_freq" file under "/sys/devices/system/cpu/cpuX/cpufreq/" directory.
 * This file exposes not exactly the same thing as "scaling_cur_freq" does, but close by nature.
 * More details: http://www.pantz.org/software/cpufreq/usingcpufreqonlinux.html
 *
 * Overall it is better to have a workaround for monitoring some CPU frequency on even with kernels that lack
 * the aforementioned fix.
 *
 * Also, an optional parameter "Path" is introduced to specify alternative CPU frequency path.
 * Example:
 * <Plugin cpufreq>
 *     Path "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_cur_freq"
 * </Plugin>
 *
 * Note, "%d" has to be used only once as CPU index placeholder.
**/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#define MODULE_NAME "cpufreq"
#define MAX_STR_L 256

static int num_cpu = 0;

static char const * freq_fname_def = "/sys/devices/system/cpu/cpu%d/cpufreq/"
									 "scaling_cur_freq";
static char freq_fname[MAX_STR_L] = {0};

static const char *config_keys[] =
{
	"Path"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

int check_format (char const * src)
{
	int pc = -2;
	int d = -2;
	char const * p;

	for (p=src; *p; ++p) {
		switch (*p) {
			case '%':
				if (-2 != pc) return 0;
				pc=p-src;
				break;

			case 'd':
				if (pc+1 == p-src) d=p-src;
				break;
		}
	}

	return pc+1==d;
} /* check_format */

static int cpufreq_config (char const * key, char const * value)
{
	if (strcasecmp ("Path", key) == 0) {
		if (check_format (value)) {
			sstrncpy (freq_fname, value, sizeof(freq_fname));
		}
		else {
			WARNING ("cpufreq: Path parameter is wrong: %s", value);
			return -1;
		}
	}

	return 0;
} /* cpufreq_config */

static int setup_freq_fname ()
{
	int status;
	char filename[MAX_STR_L];

	if (!freq_fname[0]) {
		sstrncpy (freq_fname, freq_fname_def, sizeof (freq_fname));
	}

	status = ssnprintf (filename, sizeof (filename), freq_fname, 0);
	if ((status < 1) || ((unsigned int)status >= sizeof (filename)))
		return 0;

	if (!access (filename, R_OK)) {
		return 1;
	}

	return 0;
} /* setup_freq_fname */

static int cpufreq_init (void)
{
	int status;
	char filename[MAX_STR_L];

	if (!setup_freq_fname ()) {
		return -1;
	}

	num_cpu = 0;

	while (1)
	{
		status = ssnprintf (filename, sizeof (filename), freq_fname, num_cpu);
		if ((status < 1) || ((unsigned int)status >= sizeof (filename)))
			break;

		if (access (filename, R_OK))
			break;

		num_cpu++;
	}

	INFO ("cpufreq plugin: Found %d CPU%s", num_cpu,
			(num_cpu == 1) ? "" : "s");

	if (num_cpu == 0)
		return -1;

	return 0;
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
	char filename[MAX_STR_L];
	char buffer[16];

	for (i = 0; i < num_cpu; i++)
	{
		status = ssnprintf (filename, sizeof (filename), freq_fname, i);
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
	plugin_register_config ("cpufreq", cpufreq_config,
							config_keys, config_keys_num);
	plugin_register_init ("cpufreq", cpufreq_init);
	plugin_register_read ("cpufreq", cpufreq_read);
}
