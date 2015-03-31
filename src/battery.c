/**
 * collectd - src/battery.c
 * Copyright (C) 2006-2014  Florian octo Forster
 * Copyright (C) 2008       Michał Mirosław
 * Copyright (C) 2014       Andy Parkins
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
 *   Florian octo Forster <octo at collectd.org>
 *   Michał Mirosław <mirq-linux at rere.qmqm.pl>
 *   Andy Parkins <andyp at fussylogic.co.uk>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include "utils_complain.h"

#if HAVE_MACH_MACH_TYPES_H
#  include <mach/mach_types.h>
#endif
#if HAVE_MACH_MACH_INIT_H
#  include <mach/mach_init.h>
#endif
#if HAVE_MACH_MACH_ERROR_H
#  include <mach/mach_error.h>
#endif
#if HAVE_COREFOUNDATION_COREFOUNDATION_H
#  include <CoreFoundation/CoreFoundation.h>
#endif
#if HAVE_IOKIT_IOKITLIB_H
#  include <IOKit/IOKitLib.h>
#endif
#if HAVE_IOKIT_IOTYPES_H
#  include <IOKit/IOTypes.h>
#endif
#if HAVE_IOKIT_PS_IOPOWERSOURCES_H
#  include <IOKit/ps/IOPowerSources.h>
#endif
#if HAVE_IOKIT_PS_IOPSKEYS_H
#  include <IOKit/ps/IOPSKeys.h>
#endif

#if !HAVE_IOKIT_IOKITLIB_H && !HAVE_IOKIT_PS_IOPOWERSOURCES_H && !KERNEL_LINUX
# error "No applicable input method."
#endif

#if HAVE_IOKIT_IOKITLIB_H || HAVE_IOKIT_PS_IOPOWERSOURCES_H
	/* No global variables */
/* #endif HAVE_IOKIT_IOKITLIB_H || HAVE_IOKIT_PS_IOPOWERSOURCES_H */

#elif KERNEL_LINUX
# define PROC_PMU_PATH_FORMAT "/proc/pmu/battery_%i"
# define PROC_ACPI_PATH "/proc/acpi/battery"
# define PROC_ACPI_FACTOR 0.001
# define SYSFS_PATH "/sys/class/power_supply"
# define SYSFS_FACTOR 0.000001
#endif /* KERNEL_LINUX */

static _Bool report_percent = 0;
static _Bool report_degraded = 0;

static void battery_submit2 (char const *plugin_instance, /* {{{ */
		char const *type, char const *type_instance, gauge_t value)
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
	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* }}} void battery_submit2 */

static void battery_submit (char const *plugin_instance, /* {{{ */
		char const *type, gauge_t value)
{
	battery_submit2 (plugin_instance, type, NULL, value);
} /* }}} void battery_submit */

static void submit_capacity (char const *plugin_instance, /* {{{ */
		gauge_t capacity_charged,
		gauge_t capacity_full,
		gauge_t capacity_design)
{
	if (report_percent && (capacity_charged > capacity_full))
		return;
	if (report_degraded && (capacity_full > capacity_design))
		return;

	if (report_percent)
	{
		gauge_t capacity_max;

		if (report_degraded)
			capacity_max = capacity_design;
		else
			capacity_max = capacity_full;

		battery_submit2 (plugin_instance, "percent", "charged",
				100.0 * capacity_charged / capacity_max);
		battery_submit2 (plugin_instance, "percent", "discharged",
				100.0 * (capacity_full - capacity_charged) / capacity_max);

		if (report_degraded)
			battery_submit2 (plugin_instance, "percent", "degraded",
					100.0 * (capacity_design - capacity_full) / capacity_max);
	}
	else if (report_degraded) /* && !report_percent */
	{
		battery_submit2 (plugin_instance, "capacity", "charged",
				capacity_charged);
		battery_submit2 (plugin_instance, "capacity", "discharged",
				(capacity_full - capacity_charged));
		battery_submit2 (plugin_instance, "capacity", "degraded",
				(capacity_design - capacity_full));
	}
	else /* !report_percent && !report_degraded */
	{
		battery_submit (plugin_instance, "capacity", capacity_charged);
	}
} /* }}} void submit_capacity */

#if HAVE_IOKIT_PS_IOPOWERSOURCES_H || HAVE_IOKIT_IOKITLIB_H
static double dict_get_double (CFDictionaryRef dict, char *key_string) /* {{{ */
{
	double      val_double;
	long long   val_int;
	CFNumberRef val_obj;
	CFStringRef key_obj;

	key_obj = CFStringCreateWithCString (kCFAllocatorDefault, key_string,
			kCFStringEncodingASCII);
	if (key_obj == NULL)
	{
		DEBUG ("CFStringCreateWithCString (%s) failed.\n", key_string);
		return (NAN);
	}

	if ((val_obj = CFDictionaryGetValue (dict, key_obj)) == NULL)
	{
		DEBUG ("CFDictionaryGetValue (%s) failed.", key_string);
		CFRelease (key_obj);
		return (NAN);
	}
	CFRelease (key_obj);

	if (CFGetTypeID (val_obj) == CFNumberGetTypeID ())
	{
		if (CFNumberIsFloatType (val_obj))
		{
			CFNumberGetValue (val_obj,
					kCFNumberDoubleType,
					&val_double);
		}
		else
		{
			CFNumberGetValue (val_obj,
					kCFNumberLongLongType,
					&val_int);
			val_double = val_int;
		}
	}
	else
	{
		DEBUG ("CFGetTypeID (val_obj) = %i", (int) CFGetTypeID (val_obj));
		return (NAN);
	}

	return (val_double);
} /* }}} double dict_get_double */

# if HAVE_IOKIT_PS_IOPOWERSOURCES_H
static void get_via_io_power_sources (double *ret_charge, /* {{{ */
		double *ret_current,
		double *ret_voltage)
{
	CFTypeRef       ps_raw;
	CFArrayRef      ps_array;
	int             ps_array_len;
	CFDictionaryRef ps_dict;
	CFTypeRef       ps_obj;

	double temp_double;
	int i;

	ps_raw       = IOPSCopyPowerSourcesInfo ();
	ps_array     = IOPSCopyPowerSourcesList (ps_raw);
	ps_array_len = CFArrayGetCount (ps_array);

	DEBUG ("ps_array_len == %i", ps_array_len);

	for (i = 0; i < ps_array_len; i++)
	{
		ps_obj  = CFArrayGetValueAtIndex (ps_array, i);
		ps_dict = IOPSGetPowerSourceDescription (ps_raw, ps_obj);

		if (ps_dict == NULL)
		{
			DEBUG ("IOPSGetPowerSourceDescription failed.");
			continue;
		}

		if (CFGetTypeID (ps_dict) != CFDictionaryGetTypeID ())
		{
			DEBUG ("IOPSGetPowerSourceDescription did not return a CFDictionaryRef");
			continue;
		}

		/* FIXME: Check if this is really an internal battery */

		if (isnan (*ret_charge))
		{
			/* This is the charge in percent. */
			temp_double = dict_get_double (ps_dict,
					kIOPSCurrentCapacityKey);
			if (!isnan ((temp_double))
					&& (temp_double >= 0.0)
					&& (temp_double <= 100.0))
				*ret_charge = temp_double;
		}

		if (isnan (*ret_current))
		{
			temp_double = dict_get_double (ps_dict,
					kIOPSCurrentKey);
			if (!isnan (temp_double))
				*ret_current = temp_double / 1000.0;
		}

		if (isnan (*ret_voltage))
		{
			temp_double = dict_get_double (ps_dict,
					kIOPSVoltageKey);
			if (!isnan (temp_double))
				*ret_voltage = temp_double / 1000.0;
		}
	}

	CFRelease(ps_array);
	CFRelease(ps_raw);
} /* }}} void get_via_io_power_sources */
# endif /* HAVE_IOKIT_PS_IOPOWERSOURCES_H */

# if HAVE_IOKIT_IOKITLIB_H
static void get_via_generic_iokit (double *ret_capacity_full, /* {{{ */
		double *ret_capacity_design,
		double *ret_current,
		double *ret_voltage)
{
	kern_return_t   status;
	io_iterator_t   iterator;
	io_object_t     io_obj;

	CFDictionaryRef bat_root_dict;
	CFArrayRef      bat_info_arry;
	CFIndex         bat_info_arry_len;
	CFIndex         bat_info_arry_pos;
	CFDictionaryRef bat_info_dict;

	double temp_double;

	status = IOServiceGetMatchingServices (kIOMasterPortDefault,
			IOServiceNameMatching ("battery"),
			&iterator);
	if (status != kIOReturnSuccess)
	{
		DEBUG ("IOServiceGetMatchingServices failed.");
		return;
	}

	while ((io_obj = IOIteratorNext (iterator)))
	{
		status = IORegistryEntryCreateCFProperties (io_obj,
				(CFMutableDictionaryRef *) &bat_root_dict,
				kCFAllocatorDefault,
				kNilOptions);
		if (status != kIOReturnSuccess)
		{
			DEBUG ("IORegistryEntryCreateCFProperties failed.");
			continue;
		}

		bat_info_arry = (CFArrayRef) CFDictionaryGetValue (bat_root_dict,
				CFSTR ("IOBatteryInfo"));
		if (bat_info_arry == NULL)
		{
			CFRelease (bat_root_dict);
			continue;
		}
		bat_info_arry_len = CFArrayGetCount (bat_info_arry);

		for (bat_info_arry_pos = 0;
				bat_info_arry_pos < bat_info_arry_len;
				bat_info_arry_pos++)
		{
			bat_info_dict = (CFDictionaryRef) CFArrayGetValueAtIndex (bat_info_arry, bat_info_arry_pos);

			if (isnan (*ret_capacity_full))
			{
				temp_double = dict_get_double (bat_info_dict, "Capacity");
				*ret_capacity_full = temp_double / 1000.0;
			}

			if (isnan (*ret_capacity_design))
			{
				temp_double = dict_get_double (bat_info_dict, "AbsoluteMaxCapacity");
				*ret_capacity_design = temp_double / 1000.0;
			}

			if (isnan (*ret_current))
			{
				temp_double = dict_get_double (bat_info_dict, "Current");
				*ret_current = temp_double / 1000.0;
			}

			if (isnan (*ret_voltage))
			{
				temp_double = dict_get_double (bat_info_dict, "Voltage");
				*ret_voltage = temp_double / 1000.0;
			}
		}
		
		CFRelease (bat_root_dict);
	}

	IOObjectRelease (iterator);
} /* }}} void get_via_generic_iokit */
# endif /* HAVE_IOKIT_IOKITLIB_H */

static int battery_read (void) /* {{{ */
{
	gauge_t current = NAN; /* Current in A */
	gauge_t voltage = NAN; /* Voltage in V */

	/* We only get the charged capacity as a percentage from
	 * IOPowerSources. IOKit, on the other hand, only reports the full
	 * capacity. We use the two to calculate the current charged capacity. */
	gauge_t charge_rel = NAN; /* Current charge in percent */
	gauge_t capacity_charged = NAN; /* Charged capacity */
	gauge_t capacity_full = NAN; /* Total capacity */
	gauge_t capacity_design = NAN; /* Full design capacity */

#if HAVE_IOKIT_PS_IOPOWERSOURCES_H
	get_via_io_power_sources (&charge_rel, &current, &voltage);
#endif
#if HAVE_IOKIT_IOKITLIB_H
	get_via_generic_iokit (&capacity_full, &capacity_design, &current, &voltage);
#endif

	capacity_charged = charge_rel * capacity_full / 100.0;
	submit_capacity ("0", capacity_charged, capacity_full, capacity_design);

	if (!isnan (current))
		battery_submit ("0", "current", current);
	if (!isnan (voltage))
		battery_submit ("0", "voltage", voltage);

	return (0);
} /* }}} int battery_read */
/* #endif HAVE_IOKIT_IOKITLIB_H || HAVE_IOKIT_PS_IOPOWERSOURCES_H */

#elif KERNEL_LINUX
/* Reads a file which contains only a number (and optionally a trailing
 * newline) and parses that number. */
static int sysfs_file_to_buffer(char const *dir, /* {{{ */
		char const *power_supply,
		char const *basename,
		char *buffer, size_t buffer_size)
{
	int status;
	FILE *fp;
	char filename[PATH_MAX];

	ssnprintf (filename, sizeof (filename), "%s/%s/%s",
			dir, power_supply, basename);

	/* No file isn't the end of the world -- not every system will be
	 * reporting the same set of statistics */
	if (access (filename, R_OK) != 0)
		return ENOENT;

	fp = fopen (filename, "r");
	if (fp == NULL)
	{
		status = errno;
		if (status != ENOENT)
		{
			char errbuf[1024];
			WARNING ("battery plugin: fopen (%s) failed: %s", filename,
					sstrerror (status, errbuf, sizeof (errbuf)));
		}
		return status;
	}

	if (fgets (buffer, buffer_size, fp) == NULL)
	{
		status = errno;
		if (status != ENODEV)
		{
			char errbuf[1024];
			WARNING ("battery plugin: fgets (%s) failed: %s", filename,
					sstrerror (status, errbuf, sizeof (errbuf)));
		}
		fclose (fp);
		return status;
	}

	strstripnewline (buffer);

	fclose (fp);
	return 0;
} /* }}} int sysfs_file_to_buffer */

/* Reads a file which contains only a number (and optionally a trailing
 * newline) and parses that number. */
static int sysfs_file_to_gauge(char const *dir, /* {{{ */
		char const *power_supply,
		char const *basename, gauge_t *ret_value)
{
	int status;
	char buffer[32] = "";

	status = sysfs_file_to_buffer (dir, power_supply, basename, buffer, sizeof (buffer));
	if (status != 0)
		return (status);

	return (strtogauge (buffer, ret_value));
} /* }}} sysfs_file_to_gauge */

static int read_sysfs_capacity (char const *dir, /* {{{ */
		char const *power_supply,
		char const *plugin_instance)
{
	gauge_t capacity_charged = NAN;
	gauge_t capacity_full = NAN;
	gauge_t capacity_design = NAN;
	int status;

	status = sysfs_file_to_gauge (dir, power_supply, "energy_now", &capacity_charged);
	if (status != 0)
		return (status);

	status = sysfs_file_to_gauge (dir, power_supply, "energy_full", &capacity_full);
	if (status != 0)
		return (status);

	status = sysfs_file_to_gauge (dir, power_supply, "energy_full_design", &capacity_design);
	if (status != 0)
		return (status);

	submit_capacity (plugin_instance,
			capacity_charged * SYSFS_FACTOR,
			capacity_full * SYSFS_FACTOR,
			capacity_design * SYSFS_FACTOR);
	return (0);
} /* }}} int read_sysfs_capacity */

static int read_sysfs_callback (char const *dir, /* {{{ */
		char const *power_supply,
		void *user_data)
{
	int *battery_index = user_data;

	char const *plugin_instance;
	char buffer[32];
	gauge_t v = NAN;
	_Bool discharging = 0;
	int status;

	/* Ignore non-battery directories, such as AC power. */
	status = sysfs_file_to_buffer (dir, power_supply, "type", buffer, sizeof (buffer));
	if (status != 0)
		return (0);
	if (strcasecmp ("Battery", buffer) != 0)
		return (0);

	(void) sysfs_file_to_buffer (dir, power_supply, "status", buffer, sizeof (buffer));
	if (strcasecmp ("Discharging", buffer) == 0)
		discharging = 1;

	/* FIXME: This is a dirty hack for backwards compatibility: The battery
	 * plugin, for a very long time, has had the plugin_instance
	 * hard-coded to "0". So, to keep backwards compatibility, we'll use
	 * "0" for the first battery we find and the power_supply name for all
	 * following. This should be reverted in a future major version. */
	plugin_instance = (*battery_index == 0) ? "0" : power_supply;
	(*battery_index)++;

	read_sysfs_capacity (dir, power_supply, plugin_instance);

	if (sysfs_file_to_gauge (dir, power_supply, "power_now", &v) == 0)
	{
		if (discharging)
			v *= -1.0;
		battery_submit (plugin_instance, "power", v * SYSFS_FACTOR);
	}
	if (sysfs_file_to_gauge (dir, power_supply, "current_now", &v) == 0)
	{
		if (discharging)
			v *= -1.0;
		battery_submit (plugin_instance, "current", v * SYSFS_FACTOR);
	}

	if (sysfs_file_to_gauge (dir, power_supply, "voltage_now", &v) == 0)
		battery_submit (plugin_instance, "voltage", v * SYSFS_FACTOR);

	return (0);
} /* }}} int read_sysfs_callback */

static int read_sysfs (void) /* {{{ */
{
	int status;
	int battery_counter = 0;

	if (access (SYSFS_PATH, R_OK) != 0)
		return (ENOENT);

	status = walk_directory (SYSFS_PATH, read_sysfs_callback,
			/* user_data = */ &battery_counter,
			/* include hidden */ 0);
	return (status);
} /* }}} int read_sysfs */

static int read_acpi_full_capacity (char const *dir, /* {{{ */
		char const *power_supply,
		gauge_t *ret_capacity_full,
		gauge_t *ret_capacity_design)

{
	char filename[PATH_MAX];
	char buffer[1024];

	FILE *fh;

	ssnprintf (filename, sizeof (filename), "%s/%s/info", dir, power_supply);
	fh = fopen (filename, "r");
	if ((fh = fopen (filename, "r")) == NULL)
		return (errno);

	/* last full capacity:      40090 mWh */
	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		gauge_t *value_ptr;
		int fields_num;
		char *fields[8];
		int index;

		if (strncmp ("last full capacity:", buffer, strlen ("last full capacity:")) == 0)
		{
			value_ptr = ret_capacity_full;
			index = 3;
		}
		else if (strncmp ("design capacity:", buffer, strlen ("design capacity:")) == 0)
		{
			value_ptr = ret_capacity_design;
			index = 2;
		}
		else
		{
			continue;
		}

		fields_num = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));
		if (fields_num <= index)
			continue;

		strtogauge (fields[index], value_ptr);
	}

	fclose (fh);
	return (0);
} /* }}} int read_acpi_full_capacity */

static int read_acpi_callback (char const *dir, /* {{{ */
		char const *power_supply,
		void *user_data)
{
	int *battery_index = user_data;

	gauge_t power = NAN;
	gauge_t voltage = NAN;
	gauge_t capacity_charged = NAN;
	gauge_t capacity_full = NAN;
	gauge_t capacity_design = NAN;
	_Bool charging = 0;
	_Bool is_current = 0;

	char const *plugin_instance;
	char filename[PATH_MAX];
	char buffer[1024];

	FILE *fh;

	ssnprintf (filename, sizeof (filename), "%s/%s/state", dir, power_supply);
	fh = fopen (filename, "r");
	if ((fh = fopen (filename, "r")) == NULL)
	{
		if ((errno == EAGAIN) || (errno == EINTR) || (errno == ENOENT))
			return (0);
		else
			return (errno);
	}

	/*
	 * [11:00] <@tokkee> $ cat /proc/acpi/battery/BAT1/state
	 * [11:00] <@tokkee> present:                 yes
	 * [11:00] <@tokkee> capacity state:          ok
	 * [11:00] <@tokkee> charging state:          charging
	 * [11:00] <@tokkee> present rate:            1724 mA
	 * [11:00] <@tokkee> remaining capacity:      4136 mAh
	 * [11:00] <@tokkee> present voltage:         12428 mV
	 */
	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		char *fields[8];
		int numfields;

		numfields = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));
		if (numfields < 3)
			continue;

		if ((strcmp (fields[0], "charging") == 0)
				&& (strcmp (fields[1], "state:") == 0))
		{
			if (strcmp (fields[2], "charging") == 0)
				charging = 1;
			else
				charging = 0;
			continue;
		}

		/* The unit of "present rate" depends on the battery. Modern
		 * batteries export power (watts), older batteries (used to)
		 * export current (amperes). We check the fourth column and try
		 * to find old batteries this way. */
		if ((strcmp (fields[0], "present") == 0)
				&& (strcmp (fields[1], "rate:") == 0))
		{
			strtogauge (fields[2], &power);

			if ((numfields >= 4) && (strcmp ("mA", fields[3]) == 0))
				is_current = 1;
		}
		else if ((strcmp (fields[0], "remaining") == 0)
				&& (strcmp (fields[1], "capacity:") == 0))
			strtogauge (fields[2], &capacity_charged);
		else if ((strcmp (fields[0], "present") == 0)
				&& (strcmp (fields[1], "voltage:") == 0))
			strtogauge (fields[2], &voltage);
	} /* while (fgets (buffer, sizeof (buffer), fh) != NULL) */

	fclose (fh);

	if (!charging)
		power *= -1.0;

	/* FIXME: This is a dirty hack for backwards compatibility: The battery
	 * plugin, for a very long time, has had the plugin_instance
	 * hard-coded to "0". So, to keep backwards compatibility, we'll use
	 * "0" for the first battery we find and the power_supply name for all
	 * following. This should be reverted in a future major version. */
	plugin_instance = (*battery_index == 0) ? "0" : power_supply;
	(*battery_index)++;

	read_acpi_full_capacity (dir, power_supply, &capacity_full, &capacity_design);

	submit_capacity (plugin_instance,
			capacity_charged * PROC_ACPI_FACTOR,
			capacity_full * PROC_ACPI_FACTOR,
			capacity_design * PROC_ACPI_FACTOR);

	battery_submit (plugin_instance,
			is_current ? "current" : "power",
			power * PROC_ACPI_FACTOR);
	battery_submit (plugin_instance, "voltage", voltage * PROC_ACPI_FACTOR);

	return 0;
} /* }}} int read_acpi_callback */

static int read_acpi (void) /* {{{ */
{
	int status;
	int battery_counter = 0;

	if (access (PROC_ACPI_PATH, R_OK) != 0)
		return (ENOENT);

	status = walk_directory (PROC_ACPI_PATH, read_acpi_callback,
			/* user_data = */ &battery_counter,
			/* include hidden */ 0);
	return (status);
} /* }}} int read_acpi */

static int read_pmu (void) /* {{{ */
{
	int i;

	/* The upper limit here is just a safeguard. If there is a system with
	 * more than 100 batteries, this can easily be increased. */
	for (i = 0; i < 100; i++)
	{
		FILE *fh;

		char buffer[1024];
		char filename[PATH_MAX];
		char plugin_instance[DATA_MAX_NAME_LEN];

		gauge_t current = NAN;
		gauge_t voltage = NAN;
		gauge_t charge  = NAN;

		ssnprintf (filename, sizeof (filename), PROC_PMU_PATH_FORMAT, i);
		if (access (filename, R_OK) != 0)
			break;

		ssnprintf (plugin_instance, sizeof (plugin_instance), "%i", i);

		fh = fopen (filename, "r");
		if (fh == NULL)
		{
			if (errno == ENOENT)
				break;
			else if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			else
				return (errno);
		}

		while (fgets (buffer, sizeof (buffer), fh) != NULL)
		{
			char *fields[8];
			int numfields;

			numfields = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));
			if (numfields < 3)
				continue;

			if (strcmp ("current", fields[0]) == 0)
				strtogauge (fields[2], &current);
			else if (strcmp ("voltage", fields[0]) == 0)
				strtogauge (fields[2], &voltage);
			else if (strcmp ("charge", fields[0]) == 0)
				strtogauge (fields[2], &charge);
		}

		fclose (fh);
		fh = NULL;

		battery_submit (plugin_instance, "charge", charge / 1000.0);
		battery_submit (plugin_instance, "current", current / 1000.0);
		battery_submit (plugin_instance, "voltage", voltage / 1000.0);
	}

	if (i == 0)
		return (ENOENT);
	return (0);
} /* }}} int read_pmu */

static int battery_read (void) /* {{{ */
{
	int status;

	DEBUG ("battery plugin: Trying sysfs ...");
	status = read_sysfs ();
	if (status == 0)
		return (0);

	DEBUG ("battery plugin: Trying acpi ...");
	status = read_acpi ();
	if (status == 0)
		return (0);

	DEBUG ("battery plugin: Trying pmu ...");
	status = read_pmu ();
	if (status == 0)
		return (0);

	ERROR ("battery plugin: Add available input methods failed.");
	return (-1);
} /* }}} int battery_read */
#endif /* KERNEL_LINUX */

static int battery_config (oconfig_item_t *ci)
{
	int i;

	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("ValuesPercentage", child->key) == 0)
			cf_util_get_boolean (child, &report_percent);
		else if (strcasecmp ("ReportDegraded", child->key) == 0)
			cf_util_get_boolean (child, &report_degraded);
		else
			WARNING ("battery plugin: Ignoring unknown "
					"configuration option \"%s\".",
					child->key);
	}

	return (0);
} /* }}} int battery_config */

void module_register (void)
{
	plugin_register_complex_config ("battery", battery_config);
	plugin_register_read ("battery", battery_read);
} /* void module_register */
