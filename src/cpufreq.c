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
 *   rinigus <http://github.com/rinigus>
 *
 * CPU frequency is recorded either as a current value or as changes
 * in used CPU frequency distribution. For current value,
 * "/sys/devices/system/cpu/cpuX/cpufreq/scaling_cur_freq" are
 * used. For reporting distribution changes, amount of used time on
 * each supported CPU frequency is reported using DERIVE
 * type. Distributions are collected from
 * "/sys/devices/system/cpu/cpuX/cpufreq/stats/time_in_state", format
 * description at https://www.kernel.org/doc/Documentation/cpu-freq/cpufreq-stats.txt.
 *
 * Units of reported values:
 *
 *  Frequencies in Hz when reported as a current value
 *  Frequencies in kHz when used as a type instance for distributions
 *  Distribution utilization for each CPU frequency in milliseconds that this frequency was
 *   used per second of wall time
 *
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#define MODULE_NAME "cpufreq"

// Configuration options
static const char *config_keys[] =
{
	"ReportDistribution",
	"ReportByCpu"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

// Global variables

static _Bool report_distribution = 0;  // option
static _Bool report_by_cpu = 0;        // option

static size_t num_cpu = 0;  // number of cpus

// Variables used when CPU frequency distributions are calculated
// that summarize the use for all CPUs together.
static long int *hertz_all_cpus = NULL;  // list of available frequencies
static derive_t *time_all_cpus = NULL;   // time spent at each frequency
static size_t hertz_size = 0;
static size_t reported_last_run = 0;

// Functions

static int cpufreq_config (const char *key, const char *value)
{
	if (strcasecmp (key, "ReportDistribution") == 0)
		report_distribution = IS_TRUE(value);
	else if (strcasecmp (key, "ReportByCpu") == 0)
		report_by_cpu = IS_TRUE(value);
	else return (-1);

	return (0);
}


static int cpufreq_init (void)
{
	char filename[128];
	int status;
	_Bool done;

	// Determine number of cpus. Here, just check whether cpus are
	// there. since cpus can be hot-plugged, cpufreq could be absent
	// at this particualr time moment

	done = 0;
	for (num_cpu = 0; !done; ++num_cpu)
	{
		status = ssnprintf (filename, sizeof (filename),
				    "/sys/devices/system/cpu/cpu%zu",
				    num_cpu);

		if ((status < 1) || ((unsigned int)status >= sizeof (filename)))
		{
			INFO ("cpufreq plugin: error in ssnprintf");
			break;
		}

		if (access (filename, R_OK))
			break;
	}

	INFO ("cpufreq plugin: Found %zu CPU%s", num_cpu,
		  (num_cpu == 1) ? "" : "s");

	if (num_cpu == 0)
		plugin_unregister_read ("cpufreq");

	// when finding CPU frequency distribution that is averaged
	// over all CPUs, we need to make a list of available
	// frequencies first. Since, in theory, CPUs can have
	// different frequencies, all CPUs are checked for available
	// frequencies.
	//
	// Limitation of this approach: if at the time of scan a CPU
	// that has some frequencies not covered by other CPUs is
	// switched off and the cpufreq stat for that CPU is not
	// available, the corresponding frequency would be missing
	// from the list.
	if (num_cpu > 0 && report_distribution && !report_by_cpu)
	{
		FILE *fp;
		char *fields[2];
		char buffer[256];
		int numfields;
		long int hertz;
		int found;

		// initialize list of frequencies
		hertz_all_cpus = NULL;
		hertz_size = 0;

		// scan all CPUs for available frequencies
		for (size_t i=0; i < num_cpu; ++i)
		{
			// To get frequencies, we scan the stats file
			// that is used later as well. Format of the
			// file is:
			//
			// hertz1 time_in_10ms
			// hertz2 time_in_10ms
			// ...
			//
			// At this stage, only hertz is used.
			status = ssnprintf (filename, sizeof (filename),
					    "/sys/devices/system/cpu/cpu%zu/cpufreq/stats/time_in_state",
					    i);

			if ((status < 1) || ((size_t)status >= sizeof (filename)))
			{
				INFO ("cpufreq plugin: error in ssnprintf");
				break;
			}

			if ((fp = fopen (filename, "r")) == NULL)
				// cpu could be switched off, just take the next one
				continue;

			while (fgets(buffer, sizeof(buffer), fp) != NULL)
			{
				numfields = strsplit (buffer, fields, STATIC_ARRAY_SIZE(fields));

				if (numfields != 2) // supported format contains 2 fields
				{
					// if its an empty line, just ignore this line
					if (numfields == 0) continue;

					// unsupported format, report error and unregister
					WARNING ("cpufreq plugin: wrong number of items on a line in %s", filename);
					fclose (fp);
					plugin_unregister_read ("cpufreq");
					return (0);
				}

				hertz = atol (fields[0]);

				// do we have this value already?
				found = 0;
				for (size_t j=0; j < hertz_size && !found; ++j)
					if (hertz == hertz_all_cpus[j])
						found = 1;

				// this is a new frequency. add this
				// to the list at the end. when
				// scanning for stats later,
				// frequencies should come in the same
				// order, so the sorting is not needed
				// as such (see read function below).
				if (!found)
				{
					hertz_size += 1;
					hertz_all_cpus = (long int*)realloc (hertz_all_cpus, hertz_size * sizeof(long int));
					if (hertz_all_cpus == NULL)
					{
						INFO ("cpufreq plugin: realloc failed for hertz_all_cpus size %zu", hertz_size);
						fclose (fp);
						plugin_unregister_read ("cpufreq");
						return (0);
					}
					hertz_all_cpus[ hertz_size-1 ] = hertz;
				}
			}

			fclose (fp);
		}

		if (hertz_size == 0)
		{
			INFO ("cpufreq plugin: cannot find any frequencies in the stats");
			plugin_unregister_read ("cpufreq");
			return (0);
		}

		// Allocate memory for time array that will be used in
		// read function.
		time_all_cpus = malloc (hertz_size * sizeof(*time_all_cpus));
		if (time_all_cpus == NULL)
		{
			INFO ("cpufreq plugin: malloc failed for time_all_cpus size %zu", hertz_size);
			plugin_unregister_read ("cpufreq");
			return (0);
		}

		INFO ("cpufreq plugin: found %zu different frequencies", hertz_size);
	}

	return (0);
} /* int cpufreq_init */

static int cpufreq_shutdown (void)
{
	if (report_distribution)
	{
		sfree (hertz_all_cpus);
		hertz_all_cpus = NULL;

		sfree (time_all_cpus);
		time_all_cpus = NULL;
	}

	return (0);
} /* int cpufreq_shutdown */

// used when single value is reported
static void cpufreq_submit_current_value (size_t cpu_num, double value)
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
			"%zu", cpu_num);

	plugin_dispatch_values (&vl);
	++reported_last_run;
}

// used when distribuiton is reported
static void cpufreq_submit_distribution_value (const char *plugin_instance, long int hertz, derive_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "cpufreq", sizeof (vl.plugin));
	if (plugin_instance != NULL)
		sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "total_time_in_ms", sizeof (vl.type));
	ssnprintf (vl.type_instance, sizeof (vl.type_instance),
		   "%ld", hertz);

	plugin_dispatch_values (&vl);
	++reported_last_run;
}

static int cpufreq_read (void)
{
	FILE *fp;
	char filename[128];
	char buffer[512];
	int status;
	unsigned long long val;

	reported_last_run = 0;

	if (!report_distribution) // current value reported only
	{
		for (size_t i = 0; i < num_cpu; i++)
		{
			status = ssnprintf (filename, sizeof (filename),
					    "/sys/devices/system/cpu/cpu%zu/cpufreq/"
					    "scaling_cur_freq", i);
			if ((status < 1) || ((size_t)status >= sizeof (filename)))
				return (-1);

			if ((fp = fopen (filename, "r")) == NULL)
			{
				// cpu could be just switched off
				// let's check the next one
				continue;
			}

			if (fgets (buffer, sizeof(buffer), fp) == NULL)
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

			cpufreq_submit_current_value (i, val);
		}
	} /* end of if ( !report_distribution ) */

	else // stats on CPU frequency distribution is used
	{
		char *fields[2];
		char statind[64];
		int numfields;
		long int hertz;
		derive_t time;
		size_t last_index = hertz_size - 1;

		if (!report_by_cpu)
			for (size_t j=0; j < hertz_size; ++j)
				time_all_cpus[j] = 0;

		for (size_t i=0; i < num_cpu; ++i)
		{
			status = ssnprintf (filename, sizeof (filename),
					    "/sys/devices/system/cpu/cpu%zu/cpufreq/stats/time_in_state",
					    i);

			if ((status < 1) || ((size_t)status >= sizeof (filename)))
			{
				WARNING("cpufreq plugin: error in ssnprintf");
				return (-1);
			}

			if ((fp = fopen (filename, "r")) == NULL)
			{
				// CPU could be just switched off
				// let's check the next one
				continue;
			}

			while (fgets(buffer, sizeof(buffer), fp) != NULL)
			{
				numfields = strsplit (buffer, fields, STATIC_ARRAY_SIZE(fields));

				if (numfields != 2) // supported format contains 2 fields
				{
					// if its an empty line, just ignore line
					if ( numfields == 0 ) continue;

					WARNING ("cpufreq: wrong number of items on a line in %s", filename);
					fclose (fp);
					return (-1);
				}

				hertz = atol (fields[0]);

				time = atoll (fields[1]);
				time *= 10; // cpufreq-stat module reports in 10ms

				if (hertz <= 0)
				{
					WARNING ("cpufreq: something is wrong in %s: hertz = %ld", filename, hertz);
					WARNING ("cpufreq: line in question %s %s", fields[0], fields[1]);
					fclose (fp);
					return (-1);
				}

				if (report_by_cpu)
				{
					status = ssnprintf (statind, sizeof (statind),
										"%zu", i);

					if ((status < 1) || ((size_t)status >= sizeof (statind)))
					{
						WARNING("cpufreq plugin: error in ssnprintf");
						fclose (fp);
						return (-1);
					}

					cpufreq_submit_distribution_value (statind, hertz, time);
				}
				else
				{
					int found = 0;

					if (hertz_size==1)
					{
						// This is a corner case when we have only one
						// frequency for all CPUs. No search is needed
						// here, just a check that the frequency
						// matches.
						if (hertz == hertz_all_cpus[0])
							time_all_cpus[0] += time;
						else
						{
							WARNING ("cpufreq: cannot find frequency %ld", hertz);
							fclose (fp);
							return (-1);
						}
					}
					else
					{
						// It is assumed that the frequencies in the
						// statistics file come in the same
						// order. So, we can take the last_index value
						// that was pointing to the previous frequency
						// and increment it by one. In practice, when
						// all CPUs have the same frequencies it would
						// work perfectly. If CPUs have different
						// frequencies, the further iteration maybe
						// required. For the first CPU and its first
						// frequency, last_index is set to
						// hertz_size-1, which, after increment and %
						// operator would lead to zero.
						size_t j = last_index + 1;
						while (!found && j!=last_index)
						{
							j = j % hertz_size;
							if (hertz == hertz_all_cpus[j])
							{
								found = 1;
								time_all_cpus[j] += time;
							}
							else
								++j;
						}

						if (found)
							last_index = j;
						else
						{
							WARNING ("cpufreq: cannot find frequency %ld", hertz);
							fclose (fp);
							return (-1);
						}
					}
				}
			}

			fclose (fp);
		}

		// When distributions are reported as an average among all
		// CPUs, the times are divided by number of CPUs.
		if (!report_by_cpu)
			for (size_t i=0; i < hertz_size; ++i)
				cpufreq_submit_distribution_value( NULL, hertz_all_cpus[i],
								   time_all_cpus[i] / num_cpu );
	}

	if (reported_last_run == 0)
	{
		WARNING("cpufreq plugin: nothing was reported, possibly cpufreq or cpufreq-stat is not supported");
		return (-1);
	}

	return (0);
} /* int cpufreq_read */

void module_register (void)
{
	plugin_register_config ("cpufreq", cpufreq_config, config_keys, config_keys_num);
	plugin_register_init ("cpufreq", cpufreq_init);
	plugin_register_read ("cpufreq", cpufreq_read);
	plugin_register_shutdown ("cpufreq", cpufreq_shutdown);
}
