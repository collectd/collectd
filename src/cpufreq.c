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

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#define MODULE_NAME "cpufreq"
#define MAX_STR_L 256

static int num_cpu = 0;

static char const * freq_fname_def = "/sys/devices/system/cpu/cpu%d/cpufreq/"
									 "scaling_cur_freq";

/* The path "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq"
 * was seen first in CentOS 7.1 and old one has 400 access rights.
 * The cpuinfo_cur_freq just disappeared.
 * It's rather a bug and fix was made in
 * https://github.com/torvalds/linux/commit/c034b02e213d271b98c45c4a7b54af8f69aaac1e .
 * But such issue at this moments is still present in CentOS 7.2.
 * Thus the workaround was created to have a possibility to monitor CPU frequency
 * even in "broken" kernels.
 * Note that scaling_cur_freq and cpuinfo_cur_freq aren't exactly the same,
 * but by nature they are very close.
 *
 * Example:
 * <Plugin cpufreq>
 *      Path "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq"
 * </Plugin>
 *
 */

static char freq_fname[MAX_STR_L] = {0};

static const char *config_keys[] =
{
	"Path"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char const * str_replace (char const *src, char const *what, char const * by)
{
	static char buf[MAX_STR_L];
	char const * p = strstr (src, what);

	if (!p)
		return NULL;

	size_t n = p-src;
	size_t n_by = strlen (by);
	size_t n_what = strlen (what);
	size_t n_buf = sizeof (buf);

	if (MAX_STR_L <= n+n_by)
		return NULL; /* too long input strings */

	sstrncpy (buf, src, n);
	sstrncpy (buf+n, by, n_buf-n);
	sstrncpy (buf+n+n_by, src+n+n_what, n_buf-n-n_by);

	return buf;
}

static int cpufreq_config (char const * key, char const * value)
{
	if (strcasecmp ("Path", key) == 0) {
		char const * custom_path = str_replace (value, "cpu0", "cpu%d");
		if (custom_path) {
			sstrncpy(freq_fname, custom_path, sizeof(freq_fname));
		}
		else {
			WARNING ("cpufreq: Path parameter is wrong: %s", value);
			return -1;
		}
	}

	return 0;
}

static int setup_freq_fname ()
{
	int status;
	char filename[MAX_STR_L];

	if (!freq_fname[0]) {
		sstrncpy (freq_fname, freq_fname_def, sizeof (freq_fname));
	}

	status = ssnprintf (filename, sizeof (filename),
						freq_fname, 0);
	if ((status < 1) || ((unsigned int)status >= sizeof (filename)))
		return 0;

	if (!access (filename, R_OK)) {
		return 1;
	}

	return 0;
}

static int cpufreq_init (void)
{
	int status;
	char filename[MAX_STR_L];

	if (!setup_freq_fname ()) {
		/* The plugin is being unregistered by daemon itself if negative
		 * value is returned. */
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
