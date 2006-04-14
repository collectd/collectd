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

#define MODULE_NAME "serial"

#if defined(KERNEL_LINUX)
# define SERIAL_HAVE_READ 1
#else
# define SERIAL_HAVE_READ 0
#endif

static char *serial_filename_template = "serial-%s.rrd";

static char *ds_def[] =
{
	"DS:incoming:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	"DS:outgoing:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int ds_num = 2;

static void serial_init (void)
{
	return;
}

static void serial_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, serial_filename_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, ds_def, ds_num);
}

#if SERIAL_HAVE_READ
#define BUFSIZE 512
static void serial_submit (char *device,
		unsigned long long incoming,
		unsigned long long outgoing)
{
	char buf[BUFSIZE];
        
	if (snprintf (buf, BUFSIZE, "%u:%llu:%llu", (unsigned int) curtime,
				incoming, outgoing) >= BUFSIZE)
		return;

	plugin_submit (MODULE_NAME, device, buf);
}
#undef BUFSIZE

static void serial_read (void)
{
#ifdef KERNEL_LINUX

	FILE *fh;
	char buffer[1024];
	unsigned long long incoming, outgoing;
	
	char *fields[16];
	int i, numfields;
	int len;

	/* there are a variety of names for the serial device */
	if ((fh = fopen ("/proc/tty/driver/serial", "r")) == NULL &&
		(fh = fopen ("/proc/tty/driver/ttyS", "r")) == NULL)
	{
		syslog (LOG_WARNING, "serial: fopen: %s", strerror (errno));
		return;
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		int have_rx = 0, have_tx = 0;

		/* stupid compiler:
		 * serial.c:87: warning: 'incoming' may be used uninitialized in this function
		 * serial.c:87: warning: 'outgoing' may be used uninitialized in this function
		 */
		incoming = 0ULL;
		outgoing = 0ULL;

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
				outgoing = atoll (fields[i] + 3);
				have_tx++;
			}
			else if (strncmp (fields[i], "rx:", 3) == 0)
			{
				incoming = atoll (fields[i] + 3);
				have_rx++;
			}
		}

		if ((have_rx == 0) || (have_tx == 0))
			continue;

		serial_submit (fields[0], incoming, outgoing);
	}

	fclose (fh);
#endif /* KERNEL_LINUX */
}
#else
# define serial_read NULL
#endif /* SERIAL_HAVE_READ */

void module_register (void)
{
   plugin_register (MODULE_NAME, serial_init, serial_read, serial_write);
}

#undef MODULE_NAME
