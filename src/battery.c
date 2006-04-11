/**
 * collectd - src/battery.c
 * Copyright (C) 2006  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_debug.h"

#define MODULE_NAME "battery"
#define BUFSIZE 512

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

#if HAVE_IOKIT_PS_IOPOWERSOURCES_H || KERNEL_LINUX
# define BATTERY_HAVE_READ 1
#else
# define BATTERY_HAVE_READ 0
#endif

#define INVALID_VALUE 47841.29

static char *battery_current_file = "battery-%s/current.rrd";
static char *battery_voltage_file = "battery-%s/voltage.rrd";
static char *battery_charge_file  = "battery-%s/charge.rrd";

static char *ds_def_current[] =
{
	"DS:current:GAUGE:"COLLECTD_HEARTBEAT":U:U",
	NULL
};
static int ds_num_current = 1;

static char *ds_def_voltage[] =
{
	"DS:voltage:GAUGE:"COLLECTD_HEARTBEAT":U:U",
	NULL
};
static int ds_num_voltage = 1;

static char *ds_def_charge[] =
{
	"DS:charge:GAUGE:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int ds_num_charge = 1;

#if HAVE_IOKIT_PS_IOPOWERSOURCES_H
	/* No global variables */
/* #endif HAVE_IOKIT_PS_IOPOWERSOURCES_H */

#elif KERNEL_LINUX
static int   battery_pmu_num = 0;
static char *battery_pmu_file = "/proc/pmu/battery_%i";
#endif /* KERNEL_LINUX */

static void battery_init (void)
{
#if HAVE_IOKIT_PS_IOPOWERSOURCES_H
	/* No init neccessary */
/* #endif HAVE_IOKIT_PS_IOPOWERSOURCES_H */

#elif KERNEL_LINUX
	int len;
	char filename[BUFSIZE];

	for (battery_pmu_num = 0; ; battery_pmu_num++)
	{
		len = snprintf (filename, BUFSIZE, battery_pmu_file, battery_pmu_num);

		if ((len >= BUFSIZE) || (len < 0))
			break;

		if (access (filename, R_OK))
			break;
	}
#endif /* KERNEL_LINUX */

	return;
}

static void battery_current_write (char *host, char *inst, char *val)
{
	char filename[BUFSIZE];
	int len;

	len = snprintf (filename, BUFSIZE, battery_current_file, inst);
	if ((len >= BUFSIZE) || (len < 0))
		return;

	rrd_update_file (host, filename, val,
			ds_def_current, ds_num_current);
}

static void battery_voltage_write (char *host, char *inst, char *val)
{
	char filename[BUFSIZE];
	int len;

	len = snprintf (filename, BUFSIZE, battery_voltage_file, inst);
	if ((len >= BUFSIZE) || (len < 0))
		return;

	rrd_update_file (host, filename, val,
			ds_def_voltage, ds_num_voltage);
}

static void battery_charge_write (char *host, char *inst, char *val)
{
	char filename[BUFSIZE];
	int len;

	len = snprintf (filename, BUFSIZE, battery_charge_file, inst);
	if ((len >= BUFSIZE) || (len < 0))
		return;

	rrd_update_file (host, filename, val,
			ds_def_charge, ds_num_charge);
}

#if BATTERY_HAVE_READ
static void battery_submit (char *inst, double current, double voltage, double charge)
{
	int len;
	char buffer[BUFSIZE];

	if (current != INVALID_VALUE)
	{
		len = snprintf (buffer, BUFSIZE, "N:%.3f", current);

		if ((len > 0) && (len < BUFSIZE))
			plugin_submit ("battery_current", inst, buffer);
	}
	else
	{
		plugin_submit ("battery_current", inst, "N:U");
	}

	if (voltage != INVALID_VALUE)
	{
		len = snprintf (buffer, BUFSIZE, "N:%.3f", voltage);

		if ((len > 0) && (len < BUFSIZE))
			plugin_submit ("battery_voltage", inst, buffer);
	}
	else
	{
		plugin_submit ("battery_voltage", inst, "N:U");
	}

	if (charge != INVALID_VALUE)
	{
		len = snprintf (buffer, BUFSIZE, "N:%.3f", charge);

		if ((len > 0) && (len < BUFSIZE))
			plugin_submit ("battery_charge", inst, buffer);
	}
	else
	{
		plugin_submit ("battery_charge", inst, "N:U");
	}
}

static void battery_read (void)
{
#if HAVE_IOKIT_PS_IOPOWERSOURCES_H
	CFTypeRef       ps_raw;
	CFArrayRef      ps_array;
	int             ps_array_len;
	CFDictionaryRef ps_dict;
	CFTypeRef       ps_obj;
	CFTypeRef       ps_value;

	int i;

	char   name[128];
	double charge  = INVALID_VALUE;
	double current = INVALID_VALUE;
	double voltage = INVALID_VALUE;

	ps_raw       = IOPSCopyPowerSourcesInfo ();
	ps_array     = IOPSCopyPowerSourcesList (ps_raw);
	ps_array_len = CFArrayGetCount (ps_array);

	DBG ("ps_array_len == %i", ps_array_len);

	for (i = 0; i < ps_array_len; i++)
	{
		ps_obj  = CFArrayGetValueAtIndex (ps_array, i);
		ps_dict = IOPSGetPowerSourceDescription (ps_raw, ps_obj);

		if (CFGetTypeID (ps_dict) != CFDictionaryGetTypeID ())
		{
			DBG ("IOPSGetPowerSourceDescription did not return a CFDictionaryRef");
			continue;
		}

		if (ps_dict != NULL)
		{
			/* Get the current capacity/charge */
			ps_value = NULL;
			charge   = INVALID_VALUE;
			if (CFDictionaryGetValueIfPresent (ps_dict,
						CFSTR (kIOPSCurrentCapacityKey),
						&ps_value))
			{
				if (CFGetTypeID (ps_value) != CFNumberGetTypeID ())
					CFNumberGetValue (ps_value,
							kCFNumberDoubleType,
							&charge);
				else
					DBG ("kIOPSCurrentCapacityKey: Not a CFNumber");

				DBG ("charge = %f", charge);
			}
			else
				DBG ("`%s' does not exist", kIOPSCurrentCapacityKey);

			/* Get the current */
			ps_value = NULL;
			current  = INVALID_VALUE;
			if (CFDictionaryGetValueIfPresent (ps_dict,
						CFSTR (kIOPSCurrentKey),
						&ps_value))
			{
				if (CFGetTypeID (ps_value) != CFNumberGetTypeID ())
					CFNumberGetValue (ps_value,
							kCFNumberDoubleType,
							&current);
				else
					DBG ("kIOPSCurrentKey: Not a CFNumber");
				DBG ("current = %f", current);
			}
			else
				DBG ("`%s' does not exist", kIOPSCurrentKey);

			/* Get the voltage */
			ps_value = NULL;
			voltage  = INVALID_VALUE;
			if (CFDictionaryGetValueIfPresent (ps_dict,
						CFSTR (kIOPSVoltageKey),
						&ps_value))
			{
				if (CFGetTypeID (ps_value) != CFNumberGetTypeID ())
					CFNumberGetValue (ps_value,
							kCFNumberDoubleType,
							&voltage);
				else
					DBG ("kIOPSVoltageKey: Not a CFNumber");
				DBG ("voltage = %f", voltage);
			}
			else
				DBG ("`%s' does not exist", kIOPSVoltageKey);

			/* Get the name of the device.. */
			ps_value = NULL;
			if (CFDictionaryGetValueIfPresent (ps_dict,
						CFSTR (kIOPSNameKey),
						&ps_value))
			{
				if (CFGetTypeID (ps_value) != CFStringGetTypeID ())
					if (!CFStringGetCString (ps_value,
								name, 128,
								kCFStringEncodingASCII))
						continue;
				else
					DBG ("kIOPSNameKey: Not a CFStringGetTypeID");
				DBG ("Original string: `%s'", name);
			}
			else
			{
				strncpy (name, "unknown", 128);
			}
			name[127] = '\0';
			for (i = 0; i < 128; i++)
			{
				if (name[i] == '\0')
					break;
				else if (isalnum (name[i]))
					name[i] = (char) tolower (name[i]);
				else
					name[i] = '_';
			}

			battery_submit (name, current, voltage, charge);
		}
	}

	CFRelease(ps_array);
	CFRelease(ps_raw);
/* #endif HAVE_IOKIT_PS_IOPOWERSOURCES_H */

#elif KERNEL_LINUX
	FILE *fh;
	char buffer[BUFSIZE];
	char filename[BUFSIZE];
	
	char *fields[8];
	int numfields;

	int i;
	int len;

	for (i = 0; i < battery_pmu_num; i++)
	{
		char    batnum_str[BUFSIZE];
		double  current = INVALID_VALUE;
		double  voltage = INVALID_VALUE;
		double  charge  = INVALID_VALUE;
		double *valptr = NULL;

		len = snprintf (filename, BUFSIZE, battery_pmu_file, i);
		if ((len >= BUFSIZE) || (len < 0))
			continue;

		len = snprintf (batnum_str, BUFSIZE, "%i", i);
		if ((len >= BUFSIZE) || (len < 0))
			continue;

		if ((fh = fopen (filename, "r")) == NULL)
			continue;

		while (fgets (buffer, BUFSIZE, fh) != NULL)
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

		if ((current != INVALID_VALUE)
				|| (voltage != INVALID_VALUE)
				|| (charge  != INVALID_VALUE))
			battery_submit (batnum_str, current, voltage, charge);

		fclose (fh);
		fh = NULL;
	}

	if (access ("/proc/acpi/battery", R_OK | X_OK) == 0)
	{
		double  current = INVALID_VALUE;
		double  voltage = INVALID_VALUE;
		double  charge  = INVALID_VALUE;
		double *valptr = NULL;
		int charging = 0;

		struct dirent *ent;
		DIR *dh;

		if ((dh = opendir ("/proc/acpi/battery")) == NULL)
		{
			syslog (LOG_ERR, "Cannot open `/proc/acpi/battery': %s", strerror (errno));
			return;
		}

		while ((ent = readdir (dh)) != NULL)
		{
			if (ent->d_name[0] == '.')
				continue;

			len = snprintf (filename, BUFSIZE, "/proc/acpi/battery/%s/state", ent->d_name);
			if ((len >= BUFSIZE) || (len < 0))
				continue;

			if ((fh = fopen (filename, "r")) == NULL)
			{
				syslog (LOG_ERR, "Cannot open `%s': %s", filename, strerror (errno));
				continue;
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
			while (fgets (buffer, BUFSIZE, fh) != NULL)
			{
				numfields = strsplit (buffer, fields, 8);

				if (numfields < 3)
					continue;

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
					valptr = NULL;

				if ((strcmp (fields[0], "charging") == 0)
						&& (strcmp (fields[1], "state:") == 0))
				{
					if (strcmp (fields[2], "charging") == 0)
						charging = 1;
					else
						charging = 0;
				}

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

			if ((current != INVALID_VALUE) && (charging == 0))
					current *= -1;

			if ((current != INVALID_VALUE)
					|| (voltage != INVALID_VALUE)
					|| (charge  != INVALID_VALUE))
				battery_submit (ent->d_name, current, voltage, charge);

			fclose (fh);
		}

		closedir (dh);
	}
#endif /* KERNEL_LINUX */
}
#else
# define battery_read NULL
#endif /* BATTERY_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, battery_init, battery_read, NULL);
	plugin_register ("battery_current", NULL, NULL, battery_current_write);
	plugin_register ("battery_voltage", NULL, NULL, battery_voltage_write);
	plugin_register ("battery_charge",  NULL, NULL, battery_charge_write);
}

#undef BUFSIZE
#undef MODULE_NAME
