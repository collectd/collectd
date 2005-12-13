/**
 * collectd - src/swap.c
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

#include "swap.h"

#if COLLECT_SWAP
#define MODULE_NAME "swap"

#ifdef KERNEL_SOLARIS
#include <sys/swap.h>
#endif /* KERNEL_SOLARIS */

#include "plugin.h"
#include "common.h"

#undef  MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))

static char *swap_file = "swap.rrd";

/* 1099511627776 == 1TB ought to be enough for anyone ;) */
static char *ds_def[] =
{
	"DS:used:GAUGE:25:0:1099511627776",
	"DS:free:GAUGE:25:0:1099511627776",
	"DS:cached:GAUGE:25:0:1099511627776",
	"DS:resv:GAUGE:25:0:1099511627776",
	NULL
};
static int ds_num = 4;

#ifdef KERNEL_SOLARIS
static int pagesize;
static kstat_t *ksp;
#endif /* KERNEL_SOLARIS */

void module_init (void)
{
#ifdef KERNEL_SOLARIS
	/* getpagesize(3C) tells me this does not fail.. */
	pagesize = getpagesize ();
	if (get_kstat (&ksp, "unix", 0, "system_pages"))
		ksp = NULL;
#endif /* KERNEL_SOLARIS */

	return;
}

void module_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, swap_file, val, ds_def, ds_num);
}

void module_submit (unsigned long long swap_used,
		unsigned long long swap_free,
		unsigned long long swap_cached,
		unsigned long long swap_resv)
{
	char buffer[512];

	if (snprintf (buffer, 512, "N:%llu:%llu:%llu:%llu", swap_used,
				swap_free, swap_cached, swap_resv) >= 512)
		return;

	plugin_submit (MODULE_NAME, "-", buffer);
}

void module_read (void)
{
#ifdef KERNEL_LINUX
	FILE *fh;
	char buffer[1024];
	
	char *fields[8];
	int numfields;

	unsigned long long swap_used   = 0LL;
	unsigned long long swap_cached = 0LL;
	unsigned long long swap_free   = 0LL;
	unsigned long long swap_total  = 0LL;

	if ((fh = fopen ("/proc/meminfo", "r")) == NULL)
	{
		syslog (LOG_WARNING, "memory: fopen: %s", strerror (errno));
		return;
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		unsigned long long *val = NULL;

		if (strncasecmp (buffer, "SwapTotal:", 10) == 0)
			val = &swap_total;
		else if (strncasecmp (buffer, "SwapFree:", 9) == 0)
			val = &swap_free;
		else if (strncasecmp (buffer, "SwapCached:", 11) == 0)
			val = &swap_cached;
		else
			continue;

		numfields = strsplit (buffer, fields, 8);

		if (numfields < 2)
			continue;

		*val = atoll (fields[1]) * 1024LL;
	}

	if (fclose (fh))
		syslog (LOG_WARNING, "memory: fclose: %s", strerror (errno));

	if ((swap_total == 0LL) || ((swap_free + swap_cached) > swap_total))
		return;

	swap_used = swap_total - (swap_free + swap_cached);

	module_submit (swap_used, swap_free, swap_cached, -1LL);
/* #endif defined(KERNEL_LINUX) */

#elif defined(KERNEL_SOLARIS)
	unsigned long long swap_alloc;
	unsigned long long swap_resv;
	unsigned long long swap_avail;
	/* unsigned long long swap_free; */

	long long availrmem;
	long long swapfs_minfree;

	struct anoninfo ai;

	if (swapctl (SC_AINFO, &ai) == -1)
		return;

	availrmem      = get_kstat_value (ksp, "availrmem");
	swapfs_minfree = get_kstat_value (ksp, "minfree");

	if ((availrmem < 0LL) || (swapfs_minfree < 0LL))
		return;

	/* 
	 * Calculations learned by reading
	 * http://www.itworld.com/Comp/2377/UIR980701perf/
	 *
	 * swap_resv += ani_resv
	 * swap_alloc += MAX(ani_resv, ani_max) - ani_free
	 * swap_avail += MAX(ani_max - ani_resv, 0) + (availrmem - swapfs_minfree)
	 * swap_free += ani_free + (availrmem - swapfs_minfree)
	 *
	 * To clear up the terminology a bit:
	 * resv  = reserved (but not neccessarily used)
	 * alloc = used     (neccessarily reserved)
	 * avail = not reserved  (neccessarily free)
	 * free  = not allocates (possibly reserved)
	 */
	swap_resv  = pagesize * ai.ani_resv;
	swap_alloc = pagesize * (MAX(ai.ani_resv, ai.ani_max) - ai.ani_free);
	swap_avail = pagesize * (MAX(ai.ani_max - ai.ani_resv, 0) + (availrmem - swapfs_minfree));
	/* swap_free  = pagesize * (ai.ani_free + (availrmem - swapfs_minfree)); */

	module_submit (swap_alloc, swap_avail, -1LL, swap_resv - swap_alloc);
/* #endif defined(KERNEL_SOLARIS) */

#elif defined(HAVE_LIBSTATGRAB)
	sg_swap_stats *swap;

	if ((swap = sg_get_swap_stats ()) != NULL)
		module_submit (swap->used, swap->free, -1LL, -1LL);
#endif /* HAVE_LIBSTATGRAB */
}

void module_register (void)
{
	plugin_register (MODULE_NAME, module_init, module_read, module_write);
}

#undef MODULE_NAME
#endif /* COLLECT_SWAP */
