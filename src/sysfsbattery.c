/**
 * collectd - src/sysfsbattery.c
 * Copyright (C) 2014  Andy Parkins
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
 *   Andy Parkins <andyp@fussylogic.co.uk>
 **/

#include <stdbool.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#define MODULE_NAME "sysfsbattery"
#define BASE_PATH "/sys/class/power_supply/BAT%d/%s"

typedef struct {
	unsigned long energy_full_design;
	unsigned long energy_full;
	unsigned long energy_now;
	unsigned long power_now;
	unsigned long voltage_min_design;
	unsigned long voltage_now;
} sBatterySample;

static int target_battery = -1;

static int battery_init (void)
{
	int status;
	int i;
	char filename[256];

	for (i = 0; i < 10; i++) {
		status = ssnprintf (filename, sizeof (filename),
				BASE_PATH, i, "present");
		if ((status < 1) || ((unsigned int)status >= sizeof (filename)))
			continue;

		if (access (filename, R_OK))
			continue;

		target_battery = i;
		break;
	}

	return (0);
} /* int battery_init */

static void battery_submit (const char *plugin_instance, const char *type, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "battery", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static bool sysfs_file_to_ul(int i, const char *basename, unsigned long *target)
{
	int status;
	FILE *fp;
	char filename[256];
	char buffer[16];

	status = ssnprintf (filename, sizeof (filename),
			BASE_PATH, i, basename);
	if ((status < 1) || ((unsigned int)status >= sizeof (filename)))
		return 0;

	/* No file isn't the end of the world -- not every system will be
	 * reporting the same set of statistics */
	if (access (filename, R_OK))
		return 0;

	if ((fp = fopen (filename, "r")) == NULL)
	{
		char errbuf[1024];
		WARNING ("battery: fopen (%s): %s", filename,
				sstrerror (errno, errbuf,
					sizeof (errbuf)));
		return 0;
	}

	if (fgets (buffer, sizeof(buffer), fp) == NULL)
	{
		char errbuf[1024];
		WARNING ("battery: fgets: %s",
				sstrerror (errno, errbuf,
					sizeof (errbuf)));
		fclose (fp);
		return 0;
	}

	*target = strtoul(buffer, NULL, 10);

	if (fclose (fp))
	{
		char errbuf[1024];
		WARNING ("battery: fclose: %s",
				sstrerror (errno, errbuf,
					sizeof (errbuf)));
	}

	DEBUG (MODULE_NAME " plugin: %s = %lu", filename, *target);

	return 1;
}

static int battery_read (void)
{
	sBatterySample val;

	/* Fill in sample */
	memset(&val, 0, sizeof(val));
//	if (sysfs_file_to_ul(target_battery, "energy_full_design", &(val.energy_full_design)))
//		battery_submit("0", "charge", val.energy_full_design / 1000000.0);
//	if (sysfs_file_to_ul(target_battery, "energy_full", &(val.energy_full)))
//		battery_submit("0", "charge", val.energy_full / 1000000.0);
	if (sysfs_file_to_ul(target_battery, "energy_now", &(val.energy_now)))
		battery_submit("0", "charge", val.energy_now / 1000000.0);
	if (sysfs_file_to_ul(target_battery, "power_now", &(val.power_now)))
		battery_submit("0", "power", val.power_now / 1000000.0);
//	if (sysfs_file_to_ul(target_battery, "voltage_min_design", &(val.voltage_min_design)))
//		battery_submit("0", "voltage", val.voltage_min_design / 1000000.0);
	if (sysfs_file_to_ul(target_battery, "voltage_now", &(val.voltage_now)))
		battery_submit("0", "voltage", val.voltage_now / 1000000.0);

	return (0);
} /* int battery_read */

void module_register (void)
{
	plugin_register_init (MODULE_NAME, battery_init);
	plugin_register_read (MODULE_NAME, battery_read);
}
