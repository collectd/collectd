/**
 * collectd - src/load.c
 * Copyright (C) 2005,2006  Florian octo Forster
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

static data_source_t dsrc[3] =
{
	{"shortterm", DS_TYPE_GAUGE, 0.0, 100.0},
	{"midterm",   DS_TYPE_GAUGE, 0.0, 100.0},
	{"longterm",  DS_TYPE_GAUGE, 0.0, 100.0}
};

static data_set_t ds =
{
	"load", 3, dsrc
};

#if LOAD_HAVE_READ
static void load_submit (gauge_t snum, gauge_t mnum, gauge_t lnum)
{
	value_t values[3];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = snum;
	values[1].gauge = mnum;
	values[2].gauge = lnum;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "load");

	plugin_dispatch_values ("load", &vl);
}

static int load_read (void)
{
#if defined(HAVE_GETLOADAVG)
	double load[3];

	if (getloadavg (load, 3) == 3)
		load_submit (load[LOADAVG_1MIN], load[LOADAVG_5MIN], load[LOADAVG_15MIN]);
	else
		WARNING ("load: getloadavg failed: %s", strerror (errno));
/* #endif HAVE_GETLOADAVG */

#elif defined(KERNEL_LINUX)
	gauge_t snum, mnum, lnum;
	FILE *loadavg;
	char buffer[16];

	char *fields[8];
	int numfields;
	
	if ((loadavg = fopen ("/proc/loadavg", "r")) == NULL)
	{
		WARNING ("load: fopen: %s", strerror (errno));
		return;
	}

	if (fgets (buffer, 16, loadavg) == NULL)
	{
		WARNING ("load: fgets: %s", strerror (errno));
		fclose (loadavg);
		return;
	}

	if (fclose (loadavg))
		WARNING ("load: fclose: %s", strerror (errno));

	numfields = strsplit (buffer, fields, 8);

	if (numfields < 3)
		return;

	snum = atof (fields[0]);
	mnum = atof (fields[1]);
	lnum = atof (fields[2]);

	load_submit (snum, mnum, lnum);
/* #endif KERNEL_LINUX */

#elif defined(HAVE_LIBSTATGRAB)
	gauge_t snum, mnum, lnum;
	sg_load_stats *ls;

	if ((ls = sg_get_load_stats ()) == NULL)
		return;

	snum = ls->min1;
	mnum = ls->min5;
	lnum = ls->min15;

	load_submit (snum, mnum, lnum);
#endif /* HAVE_LIBSTATGRAB */

	return (0);
}
#endif /* LOAD_HAVE_READ */

void module_register (void)
{
	plugin_register_data_set (&ds);
#if LOAD_HAVE_READ
	plugin_register_read ("load", load_read);
#endif
}

#undef MODULE_NAME
