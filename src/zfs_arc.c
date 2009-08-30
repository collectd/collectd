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
	value_t values[1];

	values[0].gauge = value;

	za_submit (type, type_instance, values, STATIC_ARRAY_SIZE(values));
}

static void za_submit_size (gauge_t size, gauge_t size_target, gauge_t limit_min, gauge_t limit_max)
{
	value_t values[4];

	values[0].gauge = size;
	values[1].gauge = size_target;
	values[2].gauge = limit_min;
	values[3].gauge = limit_max;

	za_submit ("arc_size", "", values, STATIC_ARRAY_SIZE(values));
}

static void za_submit_bytes (counter_t read, counter_t write)
{
	value_t values[2];

	values[0].counter = read;
	values[1].counter = write;

	za_submit ("arc_l2_bytes", "", values, STATIC_ARRAY_SIZE(values));
}

static void za_submit_counts (char *type_instance, counter_t demand_data, counter_t demand_metadata,
	counter_t prefetch_data, counter_t prefetch_metadata)
{
	value_t values[4];

	values[0].counter = demand_data;
	values[1].counter = demand_metadata;
	values[2].counter = prefetch_data;
	values[3].counter = prefetch_metadata;

	za_submit ("arc_counts", type_instance, values, STATIC_ARRAY_SIZE(values));
}

static int za_read (void)
{
	gauge_t   arcsize, targetsize, minlimit, maxlimit, hits, misses, l2_size, l2_hits, l2_misses;
	counter_t demand_data_hits, demand_metadata_hits, prefetch_data_hits, prefetch_metadata_hits;
	counter_t demand_data_misses, demand_metadata_misses, prefetch_data_misses, prefetch_metadata_misses;
	counter_t l2_read_bytes, l2_write_bytes;

	get_kstat (&ksp, "zfs", 0, "arcstats");
	if (ksp == NULL)
	{
		ERROR ("zfs_arc plugin: Cannot find zfs:0:arcstats kstat.");
		return (-1);
	}

	arcsize    = get_kstat_value(ksp, "size");
	targetsize = get_kstat_value(ksp, "c");
	minlimit   = get_kstat_value(ksp, "c_min");
	maxlimit   = get_kstat_value(ksp, "c_max");

	demand_data_hits       = get_kstat_value(ksp, "demand_data_hits");
	demand_metadata_hits   = get_kstat_value(ksp, "demand_metadata_hits");
	prefetch_data_hits     = get_kstat_value(ksp, "prefetch_data_hits");
	prefetch_metadata_hits = get_kstat_value(ksp, "prefetch_metadata_hits");

	demand_data_misses       = get_kstat_value(ksp, "demand_data_misses");
	demand_metadata_misses   = get_kstat_value(ksp, "demand_metadata_misses");
	prefetch_data_misses     = get_kstat_value(ksp, "prefetch_data_misses");
	prefetch_metadata_misses = get_kstat_value(ksp, "prefetch_metadata_misses");

	hits   = get_kstat_value(ksp, "hits");
	misses = get_kstat_value(ksp, "misses");

	l2_size        = get_kstat_value(ksp, "l2_size");
	l2_read_bytes  = get_kstat_value(ksp, "l2_read_bytes");
	l2_write_bytes = get_kstat_value(ksp, "l2_write_bytes");
	l2_hits        = get_kstat_value(ksp, "l2_hits");
	l2_misses      = get_kstat_value(ksp, "l2_misses");


	za_submit_size (arcsize, targetsize, minlimit, maxlimit);
	za_submit_gauge ("arc_l2_size", "", l2_size);

	za_submit_counts ("hits",   demand_data_hits,     demand_metadata_hits,
	                            prefetch_data_hits,   prefetch_metadata_hits);
	za_submit_counts ("misses", demand_data_misses,   demand_metadata_misses,
	                            prefetch_data_misses, prefetch_metadata_misses);

	za_submit_gauge ("arc_ratio", "L1", hits / (hits + misses));
	za_submit_gauge ("arc_ratio", "L2", l2_hits / (l2_hits + l2_misses));

	za_submit_bytes (l2_read_bytes, l2_write_bytes);

	return (0);
}

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
