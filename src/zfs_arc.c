/**
 * collectd - src/zfs_arc.c
 * Copyright (C) 2009  Anthony Dewhurst
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
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

/*
 * Global variables
 */
static kstat_t *ksp;
extern kstat_ctl_t *kc;

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

static void za_submit_derive (const char* type, const char* type_instance, derive_t dv)
{
	value_t vv;

	vv.derive = dv;
	za_submit (type, type_instance, &vv, 1);
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
	gauge_t  arc_size, l2_size;
	derive_t demand_data_hits,
		 demand_metadata_hits,
		 prefetch_data_hits,
		 prefetch_metadata_hits,
		 demand_data_misses,
		 demand_metadata_misses,
		 prefetch_data_misses,
		 prefetch_metadata_misses;
	gauge_t  arc_hits, arc_misses, l2_hits, l2_misses;
	value_t  l2_io[2];

	get_kstat (&ksp, "zfs", 0, "arcstats");
	if (ksp == NULL)
	{
		ERROR ("zfs_arc plugin: Cannot find zfs:0:arcstats kstat.");
		return (-1);
	}

	/* Sizes */
	arc_size   = get_kstat_value(ksp, "size");
	l2_size    = get_kstat_value(ksp, "l2_size");

	za_submit_gauge ("cache_size", "arc", arc_size);
	za_submit_gauge ("cache_size", "L2", l2_size);

	/* Hits / misses */
	demand_data_hits       = get_kstat_value(ksp, "demand_data_hits");
	demand_metadata_hits   = get_kstat_value(ksp, "demand_metadata_hits");
	prefetch_data_hits     = get_kstat_value(ksp, "prefetch_data_hits");
	prefetch_metadata_hits = get_kstat_value(ksp, "prefetch_metadata_hits");

	demand_data_misses       = get_kstat_value(ksp, "demand_data_misses");
	demand_metadata_misses   = get_kstat_value(ksp, "demand_metadata_misses");
	prefetch_data_misses     = get_kstat_value(ksp, "prefetch_data_misses");
	prefetch_metadata_misses = get_kstat_value(ksp, "prefetch_metadata_misses");

	za_submit_derive ("cache_result", "demand_data-hit",       demand_data_hits);
	za_submit_derive ("cache_result", "demand_metadata-hit",   demand_metadata_hits);
	za_submit_derive ("cache_result", "prefetch_data-hit",     prefetch_data_hits);
	za_submit_derive ("cache_result", "prefetch_metadata-hit", prefetch_metadata_hits);

	za_submit_derive ("cache_result", "demand_data-miss",       demand_data_misses);
	za_submit_derive ("cache_result", "demand_metadata-miss",   demand_metadata_misses);
	za_submit_derive ("cache_result", "prefetch_data-miss",     prefetch_data_misses);
	za_submit_derive ("cache_result", "prefetch_metadata-miss", prefetch_metadata_misses);

	/* Ratios */
	arc_hits   = (gauge_t) get_kstat_value(ksp, "hits");
	arc_misses = (gauge_t) get_kstat_value(ksp, "misses");
	l2_hits    = (gauge_t) get_kstat_value(ksp, "l2_hits");
	l2_misses  = (gauge_t) get_kstat_value(ksp, "l2_misses");

	za_submit_ratio ("arc", arc_hits, arc_misses);
	za_submit_ratio ("L2", l2_hits, l2_misses);

	/* I/O */
	l2_io[0].derive = get_kstat_value(ksp, "l2_read_bytes");
	l2_io[1].derive = get_kstat_value(ksp, "l2_write_bytes");

	za_submit ("io_octets", "L2", l2_io, /* num values = */ 2);

	return (0);
} /* int za_read */

static int za_init (void) /* {{{ */
{
	ksp = NULL;

	/* kstats chain already opened by update_kstat (using *kc), verify everything went fine. */
	if (kc == NULL)
	{
		ERROR ("zfs_arc plugin: kstat chain control structure not available.");
		return (-1);
	}

	return (0);
} /* }}} int za_init */

void module_register (void)
{
	plugin_register_init ("zfs_arc", za_init);
	plugin_register_read ("zfs_arc", za_read);
} /* void module_register */

/* vmi: set sw=8 noexpandtab fdm=marker : */
