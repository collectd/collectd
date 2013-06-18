/**
 * collectd - src/load.c
 * Copyright (C) 2005-2008  Florian octo Forster
 * Copyright (C) 2009       Manuel Sanmartin
 * Copyright (C) 2013       Vedran Bartonicek
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
 *   Manuel Sanmartin
 *   Vedran Bartonicek <vbartoni at gmail.com>
 **/

#define _BSD_SOURCE

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#include <unistd.h>

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>
#endif

#if HAVE_STATGRAB_H
# include <statgrab.h>
#endif

#ifdef HAVE_GETLOADAVG
#if !defined(LOADAVG_1MIN) || !defined(LOADAVG_5MIN) || !defined(LOADAVG_15MIN)
#define LOADAVG_1MIN  0
#define LOADAVG_5MIN  1
#define LOADAVG_15MIN 2
#endif
#endif /* defined(HAVE_GETLOADAVG) */

#ifdef HAVE_PERFSTAT
# include <sys/proc.h> /* AIX 5 */
# include <sys/protosw.h>
# include <libperfstat.h>
#endif /* HAVE_PERFSTAT */

static const char *config_keys[] =
{
	"ReportAbsoluteLoad",
	"ReportRelativeLoad"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *il_absolute = NULL;
static ignorelist_t *il_relative = NULL;

static _Bool report_absolute_load = 1;
static _Bool report_relative_load = 0;

static int load_init (void)
{
	if (il_absolute == NULL)
		il_absolute = ignorelist_create (1);
	if (il_relative == NULL)
		il_relative = ignorelist_create (1);

	return (0);
}

static int load_config (const char *key, const char *value)
{
	load_init ();
	
	if (strcasecmp (key, "ReportAbsoluteLoad") == 0)
		report_absolute_load = IS_TRUE (value) ? 1 : 0;
	
	else if (strcasecmp (key, "ReportRelativeLoad") == 0)
		report_relative_load = IS_TRUE (value) ? 1 : 0;

	return (-1);

}
static int cpu_cores()
{
	int cores =  sysconf(_SC_NPROCESSORS_ONLN);
	if (cores < 1){
		char errbuf[1024];
		 WARNING ("load: sysconf failed : %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}
	return cores;
}

static void load_submit (gauge_t snum, gauge_t mnum, gauge_t lnum, char* type)
{
	value_t values[3];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = snum;
	values[1].gauge = mnum;
	values[2].gauge = lnum;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "load", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static int load_read (void)
{
    gauge_t snum = 0;
    gauge_t mnum = 0;
    gauge_t lnum = 0;
    int cores = cpu_cores();
    
#if defined(HAVE_GETLOADAVG)
	double load[3];

	if (getloadavg (load, 3) != 3)
	{
		char errbuf[1024];
		WARNING ("load: getloadavg failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}
	else
	{
		snum = load[LOADAVG_1MIN];
		mnum = load[LOADAVG_5MIN];
		lnum = load[LOADAVG_15MIN];
	}
/* #endif HAVE_GETLOADAVG */

#elif defined(KERNEL_LINUX)
	FILE *loadavg;
	char buffer[16];

	char *fields[8];
	int numfields;

	if ((loadavg = fopen ("/proc/loadavg", "r")) == NULL)
	{
		char errbuf[1024];
		WARNING ("load: fopen: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (loadavg);
		return (-1);
	}

	if (fgets (buffer, 16, loadavg) == NULL)
	{
		char errbuf[1024];
		WARNING ("load: fgets: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (loadavg);
		return (-1);
	}

	if (fclose (loadavg))
	{
		char errbuf[1024];
		WARNING ("load: fclose: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}

	numfields = strsplit (buffer, fields, 8);

	if (numfields < 3)
		return (-1);

	snum = atof (fields[0]);
	mnum = atof (fields[1]);
	lnum = atof (fields[2]);

/* #endif KERNEL_LINUX */

#elif HAVE_LIBSTATGRAB
	sg_load_stats *ls;

	if ((ls = sg_get_load_stats ()) == NULL)
		return;

	snum = ls->min1;
	mnum = ls->min5;
	lnum = ls->min15;

/* #endif HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
	perfstat_cpu_total_t cputotal;

	if (perfstat_cpu_total(NULL,  &cputotal, sizeof(perfstat_cpu_total_t), 1) < 0)
	{
		char errbuf[1024];
		WARNING ("load: perfstat_cpu : %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	snum = (float)cputotal.loadavg[0]/(float)(1<<SBITS);
	mnum = (float)cputotal.loadavg[1]/(float)(1<<SBITS);
	lnum = (float)cputotal.loadavg[2]/(float)(1<<SBITS);

/* #endif HAVE_PERFSTAT */
#else
# error "No applicable input method."
#endif
	if (report_absolute_load)
		load_submit(snum, mnum, lnum, "load");
	if (report_relative_load && cores > 0) 
		load_submit(snum/cores, mnum/cores, lnum/cores, "load_relative");

	return (0);
}

void module_register (void)
{
	plugin_register_config ("load", load_config, config_keys, config_keys_num);
	plugin_register_read ("load", load_read);
} /* void module_register */
