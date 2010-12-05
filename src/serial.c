/**
 * collectd - src/serial.c
 * Copyright (C) 2005,2006  David Bacher
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
 *   David Bacher <drbacher at gmail.com>
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

static void serial_submit (const char *type_instance,
		derive_t rx, derive_t tx)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = rx;
	values[1].derive = tx;

	vl.values = values;
	vl.values_len = 2;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "serial", sizeof (vl.plugin));
	sstrncpy (vl.type, "serial_octets", sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance,
			sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static int serial_read (void)
{
	FILE *fh;
	char buffer[1024];

	derive_t rx = 0;
	derive_t tx = 0;
	
	char *fields[16];
	int i, numfields;
	int len;

	/* there are a variety of names for the serial device */
	if ((fh = fopen ("/proc/tty/driver/serial", "r")) == NULL &&
		(fh = fopen ("/proc/tty/driver/ttyS", "r")) == NULL)
	{
		char errbuf[1024];
		WARNING ("serial: fopen: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		int have_rx = 0, have_tx = 0;

		numfields = strsplit (buffer, fields, 16);

		if (numfields < 6)
			continue;

		/*
		 * 0: uart:16550A port:000003F8 irq:4 tx:0 rx:0
		 * 1: uart:16550A port:000002F8 irq:3 tx:0 rx:0
		 */
		len = strlen (fields[0]) - 1;
		if (len < 1)
			continue;
		if (fields[0][len] != ':')
			continue;
		fields[0][len] = '\0';

		for (i = 1; i < numfields; i++)
		{
			len = strlen (fields[i]);
			if (len < 4)
				continue;

			if (strncmp (fields[i], "tx:", 3) == 0)
			{
				tx = atoll (fields[i] + 3);
				have_tx++;
			}
			else if (strncmp (fields[i], "rx:", 3) == 0)
			{
				rx = atoll (fields[i] + 3);
				have_rx++;
			}
		}

		if ((have_rx == 0) || (have_tx == 0))
			continue;

		serial_submit (fields[0], rx, tx);
	}

	fclose (fh);
	return (0);
} /* int serial_read */

void module_register (void)
{
	plugin_register_read ("serial", serial_read);
} /* void module_register */
