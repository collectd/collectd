/**
 * collectd - src/load.c
 * Copyright (C) 2005  Florian octo Forster
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

#define MODULE_NAME "load"

#if defined(HAVE_GETLOADAVG) || defined(KERNEL_LINUX) || defined(HAVE_LIBSTATGRAB)
# define LOAD_HAVE_READ 1
#else
# define LOAD_HAVE_READ 0
#endif

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>
#endif

#ifdef HAVE_GETLOADAVG
#if !defined(LOADAVG_1MIN) || !defined(LOADAVG_5MIN) || !defined(LOADAVG_15MIN)
#define LOADAVG_1MIN  0
#define LOADAVG_5MIN  1
#define LOADAVG_15MIN 2
#endif
#endif /* defined(HAVE_GETLOADAVG) */

static char *load_file = "load.rrd";

static char *ds_def[] =
{
	"DS:shortterm:GAUGE:25:0:100",
	"DS:midterm:GAUGE:25:0:100",
	"DS:longterm:GAUGE:25:0:100",
	NULL
};
static int ds_num = 3;

static void load_init (void)
{
	return;
}

static void load_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, load_file, val, ds_def, ds_num);
}

#if LOAD_HAVE_READ
#define BUFSIZE 256
static void load_submit (double snum, double mnum, double lnum)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%.2f:%.2f:%.2f", (unsigned int) curtime,
				snum, mnum, lnum) >= BUFSIZE)
		return;

	plugin_submit (MODULE_NAME, "-", buf);
}
#undef BUFSIZE

static void load_read (void)
{
#if defined(HAVE_GETLOADAVG)
	double load[3];

	if (getloadavg (load, 3) == 3)
		load_submit (load[LOADAVG_1MIN], load[LOADAVG_5MIN], load[LOADAVG_15MIN]);
	else
		syslog (LOG_WARNING, "load: getloadavg failed: %s", strerror (errno));
/* #endif HAVE_GETLOADAVG */

#elif defined(KERNEL_LINUX)
	double snum, mnum, lnum;
	FILE *loadavg;
	char buffer[16];

	char *fields[8];
	int numfields;
	
	if ((loadavg = fopen ("/proc/loadavg", "r")) == NULL)
	{
		syslog (LOG_WARNING, "load: fopen: %s", strerror (errno));
		return;
	}

	if (fgets (buffer, 16, loadavg) == NULL)
	{
		syslog (LOG_WARNING, "load: fgets: %s", strerror (errno));
		return;
	}

	if (fclose (loadavg))
		syslog (LOG_WARNING, "load: fclose: %s", strerror (errno));

	numfields = strsplit (buffer, fields, 8);

	if (numfields < 3)
		return;

	snum = atof (fields[0]);
	mnum = atof (fields[1]);
	lnum = atof (fields[2]);

	load_submit (snum, mnum, lnum);
/* #endif KERNEL_LINUX */

#elif defined(HAVE_LIBSTATGRAB)
	double snum, mnum, lnum;
	sg_load_stats *ls;

	if ((ls = sg_get_load_stats ()) == NULL)
		return;

	snum = ls->min1;
	mnum = ls->min5;
	lnum = ls->min15;

	load_submit (snum, mnum, lnum);
#endif /* HAVE_LIBSTATGRAB */
}
#else
# define load_read NULL
#endif /* LOAD_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, load_init, load_read, load_write);
}

#undef MODULE_NAME
