/**
 * collectd - src/gstat.c
 * Copyright (C) 2012       Phil Kulin
 *
 * DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
 *                     Version 2, December 2004
 * 
 *  Copyright (C) 2004 Sam Hocevar <s...@hocevar.net>
 * 
 *  Everyone is permitted to copy and distribute verbatim or modified
 *  copies of this license document, and changing it is allowed as long
 *  as the name is changed.
 * 
 *             DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
 *    TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION
 * 
 *   0. You just DO WHAT THE FUCK YOU WANT TO.
 *
 * Authors:
 *   Phil Kulin
 *
 * Code based on collectd disk-plugin and gstat FreeBSD tool
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#include <sys/devicestat.h>
#include <sys/resource.h>
#include <devstat.h>
#include <libgeom.h>


#ifndef HAVE_LIBGEOM_STATS_OPEN
# error "No applicable input method."
#endif

static const char *config_keys[] =
{
	"Disk",
	"IgnoreSelected"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *ignorelist = NULL;

static struct devstat *gsp, *gsq;
static void *sp, *sq;
static double dt;
static struct timespec tp, tq;
static struct gmesh gmp;
static struct gident *gid;

static int gstat_config (const char *key, const char *value)
{
  if (ignorelist == NULL)
    ignorelist = ignorelist_create (/* invert = */ 1);
  if (ignorelist == NULL)
    return (1);

  if (strcasecmp ("Disk", key) == 0)
  {
    ignorelist_add (ignorelist, value);
  }
  else if (strcasecmp ("IgnoreSelected", key) == 0)
  {
    int invert = 1;
    if (IS_TRUE (value))
      invert = 0;
    ignorelist_set_invert (ignorelist, invert);
  }
  else
  {
    return (-1);
  }

  return (0);
} /* int gstat_config */

static int gstat_init (void)
{
	int i;

	i = geom_gettree(&gmp);
	if (i != 0) {
		ERROR ("geom_gettree = %d", i);
		return(-1);
	}
	i = geom_stats_open();
	if (i) {
		ERROR ("geom_stats_open()");
		return(-1);
	}
	sq = NULL;
	sq = geom_stats_snapshot_get();
	if (sq == NULL) {
		ERROR ("geom_stats_snapshot()");
		return(-1);
	};
	geom_stats_snapshot_timestamp(sq, &tq);

	return (0);
} /* int gstat_init */

static void disk_submit (const char *plugin_instance,
		const char *type,
		long double read, long double write, long double delete)
{
	value_t values[3];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = (int)read;
	values[1].gauge = (int)write;
	values[2].gauge = (int)delete;

	vl.values = values;
	vl.values_len = 3;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "gstat", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void disk_submit */

static void submit (const char *plugin_instance,
		const char *type,
		long double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "gstat", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void submit */

static void submit_u (const char *plugin_instance,
		const char *type,
		uint64_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "gstat", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void submit_u */

static int gstat_read (void) 
{
	long double ld[11];
	uint64_t u64;
	struct gprovider *pp;
	int i;
	sp = geom_stats_snapshot_get();
	if (sp == NULL) {
		ERROR ("geom_stats_snapshot()");
		return(-1);
	}
	geom_stats_snapshot_timestamp(sp, &tp);
	dt = tp.tv_sec - tq.tv_sec;
	dt += (tp.tv_nsec - tq.tv_nsec) * 1e-9;
	tq = tp;
	
	geom_stats_snapshot_reset(sp);
	geom_stats_snapshot_reset(sq);
	for (;;) {
		gsp = geom_stats_snapshot_next(sp);
		gsq = geom_stats_snapshot_next(sq);
		if (gsp == NULL || gsq == NULL)
			break;
		if (gsp->id == NULL)
			continue;
		gid = geom_lookupid(&gmp, gsp->id);
		if (gid == NULL) {
			geom_deletetree(&gmp);
			i = geom_gettree(&gmp);
			if (i != 0) {
				ERROR ("geom_gettree = %d", i);
				return(-1);
			}
			gid = geom_lookupid(&gmp, gsp->id);
		}
		if (gid == NULL)
			continue;
		/* Only PROVIDER */
		if (gid->lg_what == ISCONSUMER)
			continue;
		if (gsp->sequence0 != gsp->sequence1) {
			/* I am dont understand this */
			continue;
		}
		if (gid->lg_what == ISPROVIDER) {
			pp = gid->lg_ptr;
			if (ignorelist_match (ignorelist, pp->lg_name) != 0) {
				continue;
			}
			/* Some parametres keep for future*/
			devstat_compute_statistics(gsp, gsq, dt, 
			    DSM_QUEUE_LENGTH, &u64,
			    DSM_TRANSFERS_PER_SECOND, &ld[0],
			    DSM_TRANSFERS_PER_SECOND_READ, &ld[1],
			    DSM_MB_PER_SECOND_READ, &ld[2],
			    DSM_MS_PER_TRANSACTION_READ, &ld[3],
			    DSM_TRANSFERS_PER_SECOND_WRITE, &ld[4],
			    DSM_MB_PER_SECOND_WRITE, &ld[5],
			    DSM_MS_PER_TRANSACTION_WRITE, &ld[6],
			    DSM_BUSY_PCT, &ld[7],
			    DSM_TRANSFERS_PER_SECOND_FREE, &ld[8],
			    DSM_MB_PER_SECOND_FREE, &ld[9],
			    DSM_MS_PER_TRANSACTION_FREE, &ld[10],
			    DSM_NONE);
			disk_submit (pp->lg_name, "gdisk_ops", ld[1], ld[4], ld[8]);
			disk_submit (pp->lg_name, "gdisk_mbytes", ld[2], ld[5], ld[9]);
			disk_submit (pp->lg_name, "gdisk_latency", ld[3], ld[6], ld[10]);
			submit (pp->lg_name, "gdisk_busy", ld[7]);
			submit_u (pp->lg_name, "gdisk_queued", u64);
		}
		*gsq = *gsp;
	}
	geom_stats_snapshot_free(sp);
	return (0);
} /* int gstat_read */

void module_register (void)
{
  plugin_register_config ("gstat", gstat_config,
      config_keys, config_keys_num);
  plugin_register_init ("gstat", gstat_init);
  plugin_register_read ("gstat", gstat_read);
} /* void module_register */
