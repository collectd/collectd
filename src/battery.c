/**
 * collectd - src/battery.c
 * Copyright (C) 2006,2007  Florian octo Forster
 * Copyright (C) 2008       Michał Mirosław
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
 *   Florian octo Forster <octo at verplant.org>
 *   Michał Mirosław <mirq-linux at rere.qmqm.pl>
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

#define INVALID_VALUE 47841.29

#if HAVE_IOKIT_IOKITLIB_H || HAVE_IOKIT_PS_IOPOWERSOURCES_H
	/* No global variables */
/* #endif HAVE_IOKIT_IOKITLIB_H || HAVE_IOKIT_PS_IOPOWERSOURCES_H */

#elif KERNEL_LINUX
static int   battery_pmu_num = 0;
static char *battery_pmu_file = "/proc/pmu/battery_%i";
static const char *battery_acpi_dir = "/proc/acpi/battery";
#endif /* KERNEL_LINUX */

static int battery_init (void)
{
#if HAVE_IOKIT_IOKITLIB_H || HAVE_IOKIT_PS_IOPOWERSOURCES_H
	/* No init neccessary */
/* #endif HAVE_IOKIT_IOKITLIB_H || HAVE_IOKIT_PS_IOPOWERSOURCES_H */

#elif KERNEL_LINUX
	int len;
	char filename[128];

	for (battery_pmu_num = 0; ; battery_pmu_num++)
	{
		len = ssnprintf (filename, sizeof (filename), battery_pmu_file, battery_pmu_num);

		if ((len < 0) || ((unsigned int)len >= sizeof (filename)))
			break;

		if (access (filename, R_OK))
			break;
	}
#endif /* KERNEL_LINUX */

	return (0);
}

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
} /* void battery_submit */

#if HAVE_IOKIT_PS_IOPOWERSOURCES_H || HAVE_IOKIT_IOKITLIB_H
double dict_get_double (CFDictionaryRef dict, char *key_string)
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
		return (INVALID_VALUE);
	}

	if ((val_obj = CFDictionaryGetValue (dict, key_obj)) == NULL)
	{
		DEBUG ("CFDictionaryGetValue (%s) failed.", key_string);
		CFRelease (key_obj);
		return (INVALID_VALUE);
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
		return (INVALID_VALUE);
	}

	return (val_double);
}
#endif /* HAVE_IOKIT_PS_IOPOWERSOURCES_H || HAVE_IOKIT_IOKITLIB_H */

#if HAVE_IOKIT_PS_IOPOWERSOURCES_H
static void get_via_io_power_sources (double *ret_charge,
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

		if (*ret_charge == INVALID_VALUE)
		{
			/* This is the charge in percent. */
			temp_double = dict_get_double (ps_dict,
					kIOPSCurrentCapacityKey);
			if ((temp_double != INVALID_VALUE)
					&& (temp_double >= 0.0)
					&& (temp_double <= 100.0))
				*ret_charge = temp_double;
		}

		if (*ret_current == INVALID_VALUE)
		{
			temp_double = dict_get_double (ps_dict,
					kIOPSCurrentKey);
			if (temp_double != INVALID_VALUE)
				*ret_current = temp_double / 1000.0;
		}

		if (*ret_voltage == INVALID_VALUE)
		{
			temp_double = dict_get_double (ps_dict,
					kIOPSVoltageKey);
			if (temp_double != INVALID_VALUE)
				*ret_voltage = temp_double / 1000.0;
		}
	}

	CFRelease(ps_array);
	CFRelease(ps_raw);
}
#endif /* HAVE_IOKIT_PS_IOPOWERSOURCES_H */

#if HAVE_IOKIT_IOKITLIB_H
static void get_via_generic_iokit (double *ret_charge,
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

			if (*ret_charge == INVALID_VALUE)
			{
				temp_double = dict_get_double (bat_info_dict,
						"Capacity");
				if (temp_double != INVALID_VALUE)
					*ret_charge = temp_double / 1000.0;
			}

			if (*ret_current == INVALID_VALUE)
			{
				temp_double = dict_get_double (bat_info_dict,
						"Current");
				if (temp_double != INVALID_VALUE)
					*ret_current = temp_double / 1000.0;
			}

			if (*ret_voltage == INVALID_VALUE)
			{
				temp_double = dict_get_double (bat_info_dict,
						"Voltage");
				if (temp_double != INVALID_VALUE)
					*ret_voltage = temp_double / 1000.0;
			}
		}
		
		CFRelease (bat_root_dict);
	}

	IOObjectRelease (iterator);
}
#endif /* HAVE_IOKIT_IOKITLIB_H */

#if KERNEL_LINUX
static int battery_read_acpi (const char __attribute__((unused)) *dir,
		const char *name, void __attribute__((unused)) *user_data)
{
	double  current = INVALID_VALUE;
	double  voltage = INVALID_VALUE;
	double  charge  = INVALID_VALUE;
	double *valptr = NULL;
	int charging = 0;

	char filename[256];
	FILE *fh;

	char buffer[1024];
	char *fields[8];
	int numfields;
	char *endptr;
	int len;

	len = ssnprintf (filename, sizeof (filename), "%s/%s/state", battery_acpi_dir, name);

	if ((len < 0) || ((unsigned int)len >= sizeof (filename)))
		return -1;

	if ((fh = fopen (filename, "r")) == NULL) {
		char errbuf[1024];
		ERROR ("Cannot open `%s': %s", filename,
			sstrerror (errno, errbuf, sizeof (errbuf)));
		return -1;
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
		numfields = strsplit (buffer, fields, 8);

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

		if ((strcmp (fields[0], "present") == 0)
				&& (strcmp (fields[1], "rate:") == 0))
			valptr = &current;
		else if ((strcmp (fields[0], "remaining") == 0)
				&& (strcmp (fields[1], "capacity:") == 0))
			valptr = &charge;
		else if ((strcmp (fields[0], "present") == 0)
				&& (strcmp (fields[1], "voltage:") == 0))
			valptr = &voltage;
		else
			continue;

		endptr = NULL;
		errno  = 0;
		*valptr = strtod (fields[2], &endptr) / 1000.0;

		if ((fields[2] == endptr) || (errno != 0))
			*valptr = INVALID_VALUE;
	} /* while (fgets (buffer, sizeof (buffer), fh) != NULL) */

	fclose (fh);

	if ((current != INVALID_VALUE) && (charging == 0))
			current *= -1;

	if (charge != INVALID_VALUE)
		battery_submit ("0", "charge", charge);
	if (current != INVALID_VALUE)
		battery_submit ("0", "current", current);
	if (voltage != INVALID_VALUE)
		battery_submit ("0", "voltage", voltage);

	return 0;
}
#endif /* KERNEL_LINUX */


static int battery_read (void)
{
#if HAVE_IOKIT_IOKITLIB_H || HAVE_IOKIT_PS_IOPOWERSOURCES_H
	double charge  = INVALID_VALUE; /* Current charge in Ah */
	double current = INVALID_VALUE; /* Current in A */
	double voltage = INVALID_VALUE; /* Voltage in V */

	double charge_rel = INVALID_VALUE; /* Current charge in percent */
	double charge_abs = INVALID_VALUE; /* Total capacity */

#if HAVE_IOKIT_PS_IOPOWERSOURCES_H
	get_via_io_power_sources (&charge_rel, &current, &voltage);
#endif
#if HAVE_IOKIT_IOKITLIB_H
	get_via_generic_iokit (&charge_abs, &current, &voltage);
#endif

	if ((charge_rel != INVALID_VALUE) && (charge_abs != INVALID_VALUE))
		charge = charge_abs * charge_rel / 100.0;

	if (charge != INVALID_VALUE)
		battery_submit ("0", "charge", charge);
	if (current != INVALID_VALUE)
		battery_submit ("0", "current", current);
	if (voltage != INVALID_VALUE)
		battery_submit ("0", "voltage", voltage);
/* #endif HAVE_IOKIT_IOKITLIB_H || HAVE_IOKIT_PS_IOPOWERSOURCES_H */

#elif KERNEL_LINUX
	static c_complain_t acpi_dir_complaint = C_COMPLAIN_INIT_STATIC;

	FILE *fh;
	char buffer[1024];
	char filename[256];
	
	char *fields[8];
	int numfields;

	int i;
	int len;

	for (i = 0; i < battery_pmu_num; i++)
	{
		char    batnum_str[256];
		double  current = INVALID_VALUE;
		double  voltage = INVALID_VALUE;
		double  charge  = INVALID_VALUE;
		double *valptr = NULL;

		len = ssnprintf (filename, sizeof (filename), battery_pmu_file, i);
		if ((len < 0) || ((unsigned int)len >= sizeof (filename)))
			continue;

		len = ssnprintf (batnum_str, sizeof (batnum_str), "%i", i);
		if ((len < 0) || ((unsigned int)len >= sizeof (batnum_str)))
			continue;

		if ((fh = fopen (filename, "r")) == NULL)
			continue;

		while (fgets (buffer, sizeof (buffer), fh) != NULL)
		{
			numfields = strsplit (buffer, fields, 8);

			if (numfields < 3)
				continue;

			if (strcmp ("current", fields[0]) == 0)
				valptr = &current;
			else if (strcmp ("voltage", fields[0]) == 0)
				valptr = &voltage;
			else if (strcmp ("charge", fields[0]) == 0)
				valptr = &charge;
			else
				valptr = NULL;

			if (valptr != NULL)
			{
				char *endptr;

				endptr = NULL;
				errno  = 0;

				*valptr = strtod (fields[2], &endptr) / 1000.0;

				if ((fields[2] == endptr) || (errno != 0))
					*valptr = INVALID_VALUE;
			}
		}

		fclose (fh);
		fh = NULL;

		if (charge != INVALID_VALUE)
			battery_submit ("0", "charge", charge);
		if (current != INVALID_VALUE)
			battery_submit ("0", "current", current);
		if (voltage != INVALID_VALUE)
			battery_submit ("0", "voltage", voltage);
	}

	if (0 == access (battery_acpi_dir, R_OK))
		walk_directory (battery_acpi_dir, battery_read_acpi,
				/* user_data = */ NULL,
				/* include hidden */ 0);
	else
	{
		char errbuf[1024];
		c_complain_once (LOG_WARNING, &acpi_dir_complaint,
				"battery plugin: Failed to access `%s': %s",
				battery_acpi_dir,
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}

#endif /* KERNEL_LINUX */

	return (0);
}

void module_register (void)
{
	plugin_register_init ("battery", battery_init);
	plugin_register_read ("battery", battery_read);
} /* void module_register */
