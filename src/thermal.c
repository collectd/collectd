/**
 * collectd - src/thermal.c
 * Copyright (C) 2008  Micha³ Miros³aw
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
 *   Micha³ Miros³aw <mirq-linux at rere.qmqm.pl>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_ignorelist.h"

#if !KERNEL_LINUX
# error "This module is for Linux only."
#endif

const char *const dirname_sysfs = "/sys/class/thermal";
const char *const dirname_procfs = "/proc/acpi/thermal_zone";

static char force_procfs = 0;
static ignorelist_t *device_list;
static value_list_t vl_temp_template = VALUE_LIST_STATIC;
static value_list_t vl_state_template = VALUE_LIST_STATIC;

enum dev_type {
	TEMP = 0,
	COOLING_DEV
};

static void thermal_submit (const char *plugin_instance, enum dev_type dt, double value)
{
	value_list_t vl = dt == TEMP ? vl_temp_template : vl_state_template;
	value_t vt;

	vt.gauge = value;

	vl.values = &vt;
	vl.time = time (NULL);
	sstrncpy (vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));

	plugin_dispatch_values (dt == TEMP ? "temperature" : "gauge", &vl);
}

static int read_file_contents (const char *filename, char *buf, int bufsize)
{
	FILE *fh;
	int n;

	if ((fh = fopen (filename, "r")) == NULL)
		return -1;

	n = fread(buf, 1, bufsize, fh);
	fclose(fh);

	return n;
}

static int thermal_sysfs_device_read (const char *name)
{
	char filename[256];
	char data[1024];
	int len;
	int ok = 0;

	len = snprintf (filename, sizeof (filename), "%s/%s/temp", dirname_sysfs, name);
	if ((len < 0) || ((unsigned int)len >= sizeof (filename)))
		return -1;

	len = read_file_contents (filename, data, sizeof(data));
	if (len > 1 && data[--len] == '\n') {
		char *endptr = NULL;
		double temp;

		data[len] = 0;
		errno = 0;
		temp = strtod (data, &endptr) / 1000.0;

		if (endptr == data + len && errno == 0) {
			thermal_submit(name, TEMP, temp);
			++ok;
		}
	}

	len = snprintf (filename, sizeof (filename), "%s/%s/cur_state", dirname_sysfs, name);
	if ((len < 0) || ((unsigned int)len >= sizeof (filename)))
		return -1;

	len = read_file_contents (filename, data, sizeof(data));
	if (len > 1 && data[--len] == '\n') {
		char *endptr = NULL;
		double state;

		data[len] = 0;
		errno = 0;
		state = strtod (data, &endptr);

		if (endptr == data + len && errno == 0) {
			thermal_submit(name, COOLING_DEV, state);
			++ok;
		}
	}

	return ok ? 0 : -1;
}

static int thermal_procfs_device_read (const char *name)
{
	const char str_temp[] = "temperature:";
	char filename[256];
	char data[1024];
	int len;

	/**
	 * rechot ~ # cat /proc/acpi/thermal_zone/THRM/temperature
	 * temperature:             55 C
	 */
	
	len = snprintf (filename, sizeof (filename), "%s/%s/temperature", dirname_procfs, name);
	if ((len < 0) || ((unsigned int)len >= sizeof (filename)))
		return -1;

	len = read_file_contents (filename, data, sizeof(data));
	if (len > sizeof(str_temp) && data[--len] == '\n' && !strncmp(data, str_temp, sizeof(str_temp)-1)) {
		char *endptr = NULL;
		double temp;
		double celsius, add;
		
		if (data[--len] == 'C') {
			add = 0;
			celsius = 1;
		} else if (data[len] == 'F') {
			add = -32;
			celsius = 5/9;
		} else if (data[len] == 'K') {
			add = -273.15;
			celsius = 1;
		} else
			return -1;

		while (len > 0 && data[--len] == ' ')
			;
		data[len + 1] = 0;

		while (len > 0 && data[--len] != ' ')
			;
		++len;

		errno = 0;
		temp = (strtod (data + len, &endptr) + add) * celsius;

		if (endptr != data + len && errno == 0) {
			thermal_submit(name, TEMP, temp);
			return 0;
		}
	}

	return -1;
}

static const char *config_keys[] = {
	"Device",
	"IgnoreSelected",
	"ForceUseProcfs"
};

static int thermal_config (const char *key, const char *value)
{
	if (device_list == NULL)
		device_list = ignorelist_create (1);

	if (strcasecmp (key, "Device") == 0)
	{
		if (ignorelist_add (device_list, value))
		{
			ERROR ("thermal plugin: "
					"Cannot add value to ignorelist.");
			return 1;
		}
	}
	else if (strcasecmp (key, "IgnoreSelected") == 0)
	{
		ignorelist_set_invert (device_list, 1);
		if ((strcasecmp (value, "True") == 0)
				|| (strcasecmp (value, "Yes") == 0)
				|| (strcasecmp (value, "On") == 0))
			ignorelist_set_invert (device_list, 0);
	}
	else if (strcasecmp (key, "ForceUseProcfs") == 0)
	{
		force_procfs = 0;
		if ((strcasecmp (value, "True") == 0)
				|| (strcasecmp (value, "Yes") == 0)
				|| (strcasecmp (value, "On") == 0))
			force_procfs = 1;
	}
	else
	{
		return -1;
	}

	return 0;
}

static int walk_directory (const char *dir, int (*callback)(const char *dev))
{
	struct dirent *ent;
	DIR *dh;
	int ok = 0;

	if ((dh = opendir (dir)) == NULL)
	{
		char errbuf[1024];
		ERROR ("Cannot open '%s': %s", dir,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return -1;
	}

	while ((ent = readdir (dh)) != NULL)
	{
		if (ent->d_name[0] == '.')
			continue;

		if (device_list) {
			DEBUG ("thermal plugin: Checking ignorelist for '%s'", ent->d_name);
			if (ignorelist_match (device_list, ent->d_name))
				continue;
		}

		if (!callback(ent->d_name))
			++ok;
	}

	closedir (dh);

	return ok ? 0 : -1;
}

static int thermal_sysfs_read (void)
{
	return walk_directory (dirname_sysfs, thermal_sysfs_device_read);
}

static int thermal_procfs_read (void)
{
	return walk_directory (dirname_procfs, thermal_procfs_device_read);
}

static int thermal_init (void)
{
	int ret = -1;

	if (!force_procfs && access (dirname_sysfs, R_OK | X_OK) == 0) {
		ret = plugin_register_read ("thermal", thermal_sysfs_read);
	} else if (access (dirname_procfs, R_OK | X_OK) == 0) {
		ret = plugin_register_read ("thermal", thermal_procfs_read);
	}

	if (!ret) {
		vl_temp_template.values_len = 1;
		vl_temp_template.interval = interval_g;
		sstrncpy (vl_temp_template.host, hostname_g,
			sizeof(vl_temp_template.host));
		sstrncpy (vl_temp_template.plugin, "thermal",
			sizeof(vl_temp_template.plugin));
		sstrncpy (vl_temp_template.type_instance, "temperature",
			sizeof(vl_temp_template.type_instance));

		vl_state_template = vl_temp_template;
		sstrncpy (vl_state_template.type_instance, "cooling_state",
			sizeof(vl_state_template.type_instance));
	}

	return ret;
}

static int thermal_shutdown (void)
{
	ignorelist_free (device_list);

	return 0;
}

void module_register (void)
{
	plugin_register_config ("thermal", thermal_config,
			config_keys, STATIC_ARRAY_SIZE(config_keys));
	plugin_register_init ("thermal", thermal_init);
	plugin_register_shutdown ("thermal", thermal_shutdown);
}

