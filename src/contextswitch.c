/**
 * collectd - src/contextswitch.c
 * Copyright (C) 2009  Patrik Weiskircher
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
 *   Patrik Weiskircher <weiskircher at inqnet.at>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

static void cs_submit (unsigned long context_switches)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = (derive_t) context_switches;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "contextswitch", sizeof (vl.plugin));
	sstrncpy (vl.type, "contextswitch", sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static int cs_read (void)
{
	FILE *fh;
	char buffer[64];
	int numfields;
	char *fields[3];
	unsigned long result = 0;
	int status = -2;

	fh = fopen ("/proc/stat", "r");
	if (fh == NULL) {
		ERROR ("contextswitch plugin: unable to open /proc/stat: %s",
				sstrerror (errno, buffer, sizeof (buffer)));
		return (-1);
	}

	while (fgets(buffer, sizeof(buffer), fh) != NULL)
	{
		char *endptr;

		numfields = strsplit(buffer, fields, STATIC_ARRAY_SIZE (fields));
		if (numfields != 2)
			continue;

		if (strcmp("ctxt", fields[0]) != 0)
			continue;

		errno = 0;
		endptr = NULL;
		result = strtoul(fields[1], &endptr, 10);
		if ((endptr == fields[1]) || (errno != 0)) {
			ERROR ("contextswitch plugin: Cannot parse ctxt value: %s",
					fields[1]);
			status = -1;
			break;
		}

		cs_submit(result);
		status = 0;
		break;
	}
	fclose(fh);

	if (status == -2)
		ERROR ("contextswitch plugin: Unable to find context switch value.");

	return status;
}

void module_register (void)
{
	plugin_register_read ("contextswitch", cs_read);
} /* void module_register */
