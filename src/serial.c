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

#if defined(KERNEL_LINUX)
# define SERIAL_HAVE_READ 1
#else
# define SERIAL_HAVE_READ 0
#endif

static data_source_t octets_dsrc[2] =
{
	{"rx", DS_TYPE_COUNTER, 0, 4294967295.0},
	{"tx", DS_TYPE_COUNTER, 0, 4294967295.0}
};

static data_set_t octets_ds =
{
	"serial_octets", 2, octets_dsrc
};

#if SERIAL_HAVE_READ
static void serial_submit (const char *type_instance,
		counter_t rx, counter_t tx)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = rx;
	values[1].counter = tx;

	vl.values = values;
	vl.values_len = 2;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "serial");
	strncpy (vl.type_instance, type_instance,
			sizeof (vl.type_instance));

	plugin_dispatch_values ("serial_octets", &vl);
}

static int serial_read (void)
{
#ifdef KERNEL_LINUX
	FILE *fh;
	char buffer[1024];

	counter_t rx = 0;
	counter_t tx = 0;
	
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
#endif /* KERNEL_LINUX */
} /* int serial_read */
#endif /* SERIAL_HAVE_READ */

void module_register (void)
{
	plugin_register_data_set (&octets_ds);

#if SERIAL_HAVE_READ
	plugin_register_read ("serial", serial_read);
#endif /* SERIAL_HAVE_READ */
}
