/**
 * collectd - src/zfs_arc.c
 * Copyright (C) 2009  Anthony Dewhurst
 * Copyright (C) 2012  Aurelien Rougemont
 * Copyright (C) 2013  Xin Li
 * Copyright (C) 2014  Marc Fournier
 * Copyright (C) 2014  Wilfried Goesgens
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
 *   Anthony Dewhurst <dewhurst at gmail>
 *   Aurelien Rougemont <beorn at gandi.net>
 *   Xin Li <delphij at FreeBSD.org>
 *   Marc Fournier <marc.fournier at camptocamp.com>
 *   Wilfried Goesgens <dothebart at citadel.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

/*
 * Global variables
 */

#if defined(KERNEL_LINUX)
#include "utils_llist.h"
#define ZOL_ARCSTATS_FILE "/proc/spl/kstat/zfs/arcstats"

typedef	llist_t kstat_t;

static long long get_zfs_value(kstat_t *zfs_stats  __attribute__((unused)),
		char *name)
{
	llentry_t *e;

	e = llist_search (zfs_stats, name);
	if (e == NULL)
	{
		ERROR ("zfs_arc plugin: `llist_search` failed for key: '%s'.", name);
		return (-1);
	}

	return (*(long long int*)e->value);
}

#elif !defined(__FreeBSD__) // Solaris
extern kstat_ctl_t *kc;

static long long get_zfs_value(kstat_t *ksp, char *name)
{

	return (get_kstat_value(ksp, name));
}
#else // FreeBSD
#include <sys/types.h>
#include <sys/sysctl.h>

const char zfs_arcstat[] = "kstat.zfs.misc.arcstats.";

#if !defined(kstat_t)
typedef void kstat_t;
#endif

static long long get_zfs_value(kstat_t *dummy __attribute__((unused)),
		char const *name)
{
	char buffer[256];
	long long value;
	size_t valuelen = sizeof(value);
	int rv;

	ssnprintf (buffer, sizeof (buffer), "%s%s", zfs_arcstat, name);
	rv = sysctlbyname (buffer, (void *) &value, &valuelen,
			/* new value = */ NULL, /* new length = */ (size_t) 0);
	if (rv == 0)
		return (value);

	return (-1);
}
#endif

static void za_submit (const char* type, const char* type_instance, value_t* values, int values_len)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = values;
	vl.values_len = values_len;

	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "zfs_arc", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static void za_submit_gauge (const char* type, const char* type_instance, gauge_t value)
{
	value_t vv;

	vv.gauge = value;
	za_submit (type, type_instance, &vv, 1);
}

static int za_read_derive (kstat_t *ksp, const char *kstat_value,
    const char *type, const char *type_instance)
{
  long long tmp;
  value_t v;

  tmp = get_zfs_value (ksp, (char *)kstat_value);
  if (tmp == -1LL)
  {
    WARNING ("zfs_arc plugin: Reading kstat value \"%s\" failed.", kstat_value);
    return (-1);
  }

  v.derive = (derive_t) tmp;
  za_submit (type, type_instance, /* values = */ &v, /* values_num = */ 1);
  return (0);
}

static int za_read_gauge (kstat_t *ksp, const char *kstat_value,
    const char *type, const char *type_instance)
{
  long long tmp;
  value_t v;

  tmp = get_zfs_value (ksp, (char *)kstat_value);
  if (tmp == -1LL)
  {
    WARNING ("zfs_arc plugin: Reading kstat value \"%s\" failed.", kstat_value);
    return (-1);
  }

  v.gauge = (gauge_t) tmp;
  za_submit (type, type_instance, /* values = */ &v, /* values_num = */ 1);
  return (0);
}

static void za_submit_ratio (const char* type_instance, gauge_t hits, gauge_t misses)
{
	gauge_t ratio = NAN;

	if (!isfinite (hits) || (hits < 0.0))
		hits = 0.0;
	if (!isfinite (misses) || (misses < 0.0))
		misses = 0.0;

	if ((hits != 0.0) || (misses != 0.0))
		ratio = hits / (hits + misses);

	za_submit_gauge ("cache_ratio", type_instance, ratio);
}

static int za_read (void)
{
	gauge_t  arc_hits, arc_misses, l2_hits, l2_misses;
	value_t  l2_io[2];
	kstat_t	 *ksp	= NULL;

#if KERNEL_LINUX
	long long int *llvalues = NULL;
	char file_contents[1024 * 10];
	char *fields[3];
	int numfields;
	ssize_t len;

	ksp = llist_create ();
	if (ksp == NULL)
	{
		ERROR ("zfs_arc plugin: `llist_create' failed.");
		return (-1);
	}

	len = read_file_contents (ZOL_ARCSTATS_FILE, file_contents, sizeof(file_contents));
	if (len > 1)
	{

		int i=0;
		char *pnl = file_contents;
		char *pnnl;

		file_contents[len] = '\0';

		while (pnl != NULL)
		{
			pnl = strchr(pnl, '\n');
			i++;
			if (pnl && (*pnl != '\0'))
				pnl++;
		}

		if (i > 0)
		{
			llentry_t *e;
			llvalues = malloc(sizeof(long long int) * i);
			int j = 0;

			pnl = file_contents;
			while (pnl != NULL)
			{
				pnnl = strchr(pnl, '\n');
				if (pnnl != NULL)
					*pnnl = '\0';

				numfields = strsplit (pnl, fields, 4);
				if (numfields == 3)
				{
					llvalues[j] = atoll (fields[2]);

					e = llentry_create (fields[0], &llvalues[j]);
					if (e == NULL)
					{
						ERROR ("zfs_arc plugin: `llentry_create' failed.");
					}
					else
					{
						llist_append (ksp, e);
					}
					j++;
				}
				pnl = pnnl;
				if (pnl != NULL)
					pnl ++;
			}
		}
	}

#elif !defined(__FreeBSD__) // Solaris
	get_kstat (&ksp, "zfs", 0, "arcstats");
	if (ksp == NULL)
	{
		ERROR ("zfs_arc plugin: Cannot find zfs:0:arcstats kstat.");
		return (-1);
	}
#endif

	/* Sizes */
	za_read_gauge (ksp, "size",    "cache_size", "arc");

	/* The "l2_size" value has disappeared from Solaris some time in
	 * early 2013, and has only reappeared recently in Solaris 11.2.
	 * Stop trying if we ever fail to read it, so we don't spam the log.
	 */
	static int l2_size_avail = 1;
	if (l2_size_avail && za_read_gauge (ksp, "l2_size", "cache_size", "L2") != 0)
		l2_size_avail = 0;

	/* Operations */
	za_read_derive (ksp, "deleted",  "cache_operation", "deleted");
#if __FreeBSD__
	za_read_derive (ksp, "allocated","cache_operation", "allocated");
	za_read_derive (ksp, "stolen",   "cache_operation", "stolen");
#endif

	/* Issue indicators */
	za_read_derive (ksp, "mutex_miss", "mutex_operations", "miss");
	za_read_derive (ksp, "hash_collisions", "hash_collisions", "");

	/* Evictions */
	za_read_derive (ksp, "evict_l2_cached",     "cache_eviction", "cached");
	za_read_derive (ksp, "evict_l2_eligible",   "cache_eviction", "eligible");
	za_read_derive (ksp, "evict_l2_ineligible", "cache_eviction", "ineligible");

	/* Hits / misses */
	za_read_derive (ksp, "demand_data_hits",         "cache_result", "demand_data-hit");
	za_read_derive (ksp, "demand_metadata_hits",     "cache_result", "demand_metadata-hit");
	za_read_derive (ksp, "prefetch_data_hits",       "cache_result", "prefetch_data-hit");
	za_read_derive (ksp, "prefetch_metadata_hits",   "cache_result", "prefetch_metadata-hit");
	za_read_derive (ksp, "demand_data_misses",       "cache_result", "demand_data-miss");
	za_read_derive (ksp, "demand_metadata_misses",   "cache_result", "demand_metadata-miss");
	za_read_derive (ksp, "prefetch_data_misses",     "cache_result", "prefetch_data-miss");
	za_read_derive (ksp, "prefetch_metadata_misses", "cache_result", "prefetch_metadata-miss");

	/* Ratios */
	arc_hits   = (gauge_t) get_zfs_value(ksp, "hits");
	arc_misses = (gauge_t) get_zfs_value(ksp, "misses");
	l2_hits    = (gauge_t) get_zfs_value(ksp, "l2_hits");
	l2_misses  = (gauge_t) get_zfs_value(ksp, "l2_misses");

	za_submit_ratio ("arc", arc_hits, arc_misses);
	za_submit_ratio ("L2", l2_hits, l2_misses);

	/* I/O */
	l2_io[0].derive = get_zfs_value(ksp, "l2_read_bytes");
	l2_io[1].derive = get_zfs_value(ksp, "l2_write_bytes");

	za_submit ("io_octets", "L2", l2_io, /* num values = */ 2);

#if defined(KERNEL_LINUX)
	if (llvalues != NULL)
	{
		free(llvalues);
	}
	if (ksp != NULL)
	{
		llist_destroy (ksp);
	}
#endif

	return (0);
} /* int za_read */

static int za_init (void) /* {{{ */
{
#if !defined(__FreeBSD__) && !defined(KERNEL_LINUX) // Solaris
	/* kstats chain already opened by update_kstat (using *kc), verify everything went fine. */
	if (kc == NULL)
	{
		ERROR ("zfs_arc plugin: kstat chain control structure not available.");
		return (-1);
	}
#endif

	return (0);
} /* }}} int za_init */

void module_register (void)
{
	plugin_register_init ("zfs_arc", za_init);
	plugin_register_read ("zfs_arc", za_read);
} /* void module_register */

/* vmi: set sw=8 noexpandtab fdm=marker : */
