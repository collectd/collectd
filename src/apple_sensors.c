/**
 * collectd - src/apple_sensors.c
 * Copyright (C) 2006,2007  Florian octo Forster
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
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_CTYPE_H
#  include <ctype.h>
#endif

#if HAVE_MACH_MACH_TYPES_H
#  include <mach/mach_types.h>
#endif
#if HAVE_MACH_MACH_INIT_H
#  include <mach/mach_init.h>
#endif
#if HAVE_MACH_MACH_ERROR_H
#  include <mach/mach_error.h>
#endif
#if HAVE_MACH_MACH_PORT_H
#  include <mach/mach_port.h>
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

static mach_port_t io_master_port = MACH_PORT_NULL;

static int as_init (void)
{
	kern_return_t status;
	
	if (io_master_port != MACH_PORT_NULL)
	{
		mach_port_deallocate (mach_task_self (),
				io_master_port);
		io_master_port = MACH_PORT_NULL;
	}

	status = IOMasterPort (MACH_PORT_NULL, &io_master_port);
	if (status != kIOReturnSuccess)
	{
		ERROR ("IOMasterPort failed: %s",
				mach_error_string (status));
		io_master_port = MACH_PORT_NULL;
		return (-1);
	}

	return (0);
}

static void as_submit (const char *type, const char *type_instance,
		double val)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	DEBUG ("type = %s; type_instance = %s; val = %f;",
			type, type_instance, val);

	values[0].gauge = val;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "apple_sensors", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, "", sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static int as_read (void)
{
	kern_return_t   status;
	io_iterator_t   iterator;
	io_object_t     io_obj;
	CFMutableDictionaryRef prop_dict;
	CFTypeRef       property;

	char   type[128];
	char   inst[128];
	int    value_int;
	double value_double;
	int    i;

	if (!io_master_port || (io_master_port == MACH_PORT_NULL))
		return (-1);

	status = IOServiceGetMatchingServices (io_master_port,
		       	IOServiceNameMatching("IOHWSensor"),
		       	&iterator);
	if (status != kIOReturnSuccess)
       	{
		ERROR ("IOServiceGetMatchingServices failed: %s",
				mach_error_string (status));
		return (-1);
	}

	while ((io_obj = IOIteratorNext (iterator)))
	{
		prop_dict = NULL;
		status = IORegistryEntryCreateCFProperties (io_obj,
				&prop_dict,
				kCFAllocatorDefault,
				kNilOptions);
		if (status != kIOReturnSuccess)
		{
			DEBUG ("IORegistryEntryCreateCFProperties failed: %s",
					mach_error_string (status));
			continue;
		}

		/* Copy the sensor type. */
		property = NULL;
		if (!CFDictionaryGetValueIfPresent (prop_dict,
					CFSTR ("type"),
					&property))
			continue;
		if (CFGetTypeID (property) != CFStringGetTypeID ())
			continue;
		if (!CFStringGetCString (property,
					type, sizeof (type),
					kCFStringEncodingASCII))
			continue;
		type[sizeof (type) - 1] = '\0';

		/* Copy the sensor location. This will be used as `instance'. */
		property = NULL;
		if (!CFDictionaryGetValueIfPresent (prop_dict,
					CFSTR ("location"),
					&property))
			continue;
		if (CFGetTypeID (property) != CFStringGetTypeID ())
			continue;
		if (!CFStringGetCString (property,
					inst, sizeof (inst),
					kCFStringEncodingASCII))
			continue;
		inst[sizeof (inst) - 1] = '\0';
		for (i = 0; i < 128; i++)
		{
			if (inst[i] == '\0')
				break;
			else if (isalnum (inst[i]))
				inst[i] = (char) tolower (inst[i]);
			else
				inst[i] = '_';
		}

		/* Get the actual value. Some computation, based on the `type'
		 * is neccessary. */
		property = NULL;
		if (!CFDictionaryGetValueIfPresent (prop_dict,
					CFSTR ("current-value"),
					&property))
			continue;
		if (CFGetTypeID (property) != CFNumberGetTypeID ())
			continue;
		if (!CFNumberGetValue (property,
				       	kCFNumberIntType,
				       	&value_int))
			continue;

		/* Found e.g. in the 1.5GHz PowerBooks */
		if (strcmp (type, "temperature") == 0)
		{
			value_double = ((double) value_int) / 65536.0;
			sstrncpy (type, "temperature", sizeof (type));
		}
		else if (strcmp (type, "temp") == 0)
		{
			value_double = ((double) value_int) / 10.0;
			sstrncpy (type, "temperature", sizeof (type));
		}
		else if (strcmp (type, "fanspeed") == 0)
		{
			value_double = ((double) value_int) / 65536.0;
			sstrncpy (type, "fanspeed", sizeof (type));
		}
		else if (strcmp (type, "voltage") == 0)
		{
			/* Leave this to the battery plugin. */
			continue;
		}
		else if (strcmp (type, "adc") == 0)
		{
			value_double = ((double) value_int) / 10.0;
			sstrncpy (type, "fanspeed", sizeof (type));
		}
		else
		{
			DEBUG ("apple_sensors: Read unknown sensor type: %s",
					type);
			value_double = (double) value_int;
		}

		as_submit (type, inst, value_double);

		CFRelease (prop_dict);
		IOObjectRelease (io_obj);
	} /* while (iterator) */

	IOObjectRelease (iterator);

	return (0);
} /* int as_read */

void module_register (void)
{
	plugin_register_init ("apple_sensors", as_init);
	plugin_register_read ("apple_sensors", as_read);
} /* void module_register */
