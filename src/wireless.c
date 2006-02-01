/**
 * collectd - src/wireless.c
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
 * Author:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <math.h>

#define MODULE_NAME "wireless"
#define BUFSIZE 1024

#if defined(KERNEL_LINUX)
# define WIRELESS_HAVE_READ 1
#else
# define WIRELESS_HAVE_READ 0
#endif

#define WIRELESS_PROC_FILE "/proc/net/wireless"

static char *filename_template = "wireless-%s.rrd";

static char *ds_def[] =
{
	"DS:quality:GAUGE:25:0:U",
	"DS:power:GAUGE:25:0:U",
	"DS:noise:GAUGE:25:0:U",
	NULL
};
static int ds_num = 3;

#if WIRELESS_HAVE_READ
static int proc_file_found = 0;
#endif

static void wireless_init (void)
{
#if WIRELESS_HAVE_READ
	if (access (WIRELESS_PROC_FILE, R_OK) == 0)
		proc_file_found = 1;
	else
		proc_file_found = 0;
#endif

	return;
}

static void wireless_write (char *host, char *inst, char *val)
{
	char file[BUFSIZE];
	int status;

	status = snprintf (file, BUFSIZE, filename_template, inst);
	if (status < 1)
		return;
	else if (status >= BUFSIZE)
		return;

	rrd_update_file (host, file, val, ds_def, ds_num);
}

#if WIRELESS_HAVE_READ
static double wireless_dbm_to_watt (double dbm)
{
	double watt;

	/*
	 * dbm = 10 * log_{10} (1000 * power / W)
	 * power = 10^(dbm/10) * W/1000 
	 */

	watt = pow (10.0, (dbm / 10.0)) / 1000.0;

	return (watt);
}

static void wireless_submit (char *device,
		double quality, double power, double noise)
{
	char buf[BUFSIZE];
	int  status;

	status = snprintf (buf, BUFSIZE, "%u:%f:%f:%f",
			(unsigned int) curtime,
			quality, power, noise);
	if ((status < 1) || (status >= BUFSIZE))
		return;

	plugin_submit (MODULE_NAME, device, buf);
}

static void wireless_read (void)
{
#ifdef KERNEL_LINUX

	FILE *fh;
	char buffer[BUFSIZE];

	char   *device;
	double  quality;
	double  power;
	double  noise;
	
	char *fields[8];
	int   numfields;

	int len;

	if (!proc_file_found)
		return;

	/* there are a variety of names for the wireless device */
	if ((fh = fopen (WIRELESS_PROC_FILE, "r")) == NULL)
	{
		syslog (LOG_WARNING, "wireless: fopen: %s", strerror (errno));
		return;
	}

	while (fgets (buffer, BUFSIZE, fh) != NULL)
	{
		numfields = strsplit (buffer, fields, 8);

		if (numfields < 5)
			continue;

		len = strlen (fields[0]) - 1;
		if (len < 1)
			continue;
		if (fields[0][len] != ':')
			continue;
		fields[0][len] = '\0';

		device  = fields[0];
		quality = atof (fields[2]);
		power   = atof (fields[3]);
		noise   = atof (fields[4]);

		if (quality == 0.0)
			quality = -1.0;

		if (power >= 0.0)
			power = -1.0;
		else
			power = wireless_dbm_to_watt (power);

		if (noise >= 0.0)
			noise = -1.0;
		else
			noise = wireless_dbm_to_watt (noise);

		wireless_submit (device, quality, power, noise);
	}

	fclose (fh);
#endif /* KERNEL_LINUX */
}
#else
# define wireless_read NULL
#endif /* WIRELESS_HAVE_READ */

void module_register (void)
{
   plugin_register (MODULE_NAME, wireless_init, wireless_read, wireless_write);
}

#undef BUFSIZE
#undef MODULE_NAME
