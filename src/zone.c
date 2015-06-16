/**
 * collectd - src/zone.c
 * Copyright (C) 2011       Mathijs Mohlmann
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
 *   Mathijs Mohlmann
 *   Dagobert Michelsen (forward-porting)
 **/

#if HAVE_CONFIG_H
# include "config.h"
# undef HAVE_CONFIG_H
#endif
/* avoid procfs.h error "Cannot use procfs in the large file compilation environment" */
#if !defined(_LP64) && _FILE_OFFSET_BITS == 64
#  undef _FILE_OFFSET_BITS
#  undef _LARGEFILE64_SOURCE
#endif

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <sys/types.h>
#include <sys/vm_usage.h>
#include <procfs.h>
#include <zone.h>

#include "utils_avltree.h"

#define	MAX_PROCFS_PATH	40
#define FRC2PCT(pp)(((float)(pp))/0x8000*100)

typedef struct zone_stats {
	ushort_t      pctcpu;
	ushort_t      pctmem;
} zone_stats_t;

static long pagesize;

static int zone_init (void)
{
	pagesize = sysconf(_SC_PAGESIZE);
	return (0);
}

static int
zone_compare(const zoneid_t *a, const zoneid_t *b)
{
	if (*a == *b)
		return(0);
	if (*a < *b)
		return(-1);
	return(1);
}

static int
zone_read_procfile(char const *pidstr, char const *name, void *buf, size_t bufsize)
{
	int fd;

	char procfile[MAX_PROCFS_PATH];
	(void)snprintf(procfile, sizeof(procfile), "/proc/%s/%s", pidstr, name);
	if ((fd = open(procfile, O_RDONLY)) == -1) {
		return (1);
	}

	if (sread(fd, buf, bufsize) != 0) {
		char errbuf[1024];
		ERROR ("zone plugin: Reading \"%s\" failed: %s", procfile,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close(fd);
		return (1);
	}

	close(fd);
	return (0);
}

static int
zone_submit_value(char *zone, gauge_t value)
{
	value_list_t vl = VALUE_LIST_INIT;
	value_t      values[1];

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1; /*STATIC_ARRAY_SIZE (values);*/
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "zone", sizeof (vl.plugin));
 	sstrncpy (vl.type, "percent", sizeof (vl.type));
 	sstrncpy (vl.type_instance, zone, sizeof (vl.type_instance));

	return(plugin_dispatch_values (&vl));
}

static zone_stats_t *
zone_find_stats(c_avl_tree_t *tree, zoneid_t zoneid)
{
	zone_stats_t *ret = NULL;
	zoneid_t     *key = NULL;

	if (c_avl_get(tree, (void **)&zoneid, (void **)&ret)) {
		if (!(ret = malloc(sizeof(zone_stats_t)))) {
			WARNING("zone plugin: no memory");
			return(NULL);
		}
		if (!(key = malloc(sizeof(zoneid_t)))) {
			WARNING("zone plugin: no memory");
			return(NULL);
		}
		*key = zoneid;
		if (c_avl_insert(tree, key, ret)) {
			WARNING("zone plugin: error inserting into tree");
			return(NULL);
		}
	}
	return(ret);
}

static void
zone_submit_values(c_avl_tree_t *tree)
{
	char          zonename[ZONENAME_MAX];
	zoneid_t     *zoneid = NULL;
	zone_stats_t *stats  = NULL;

	while (c_avl_pick (tree, (void **)&zoneid, (void **)&stats) == 0)
	{
		if (getzonenamebyid(*zoneid, zonename, sizeof( zonename )) == -1) {
			WARNING("zone plugin: error retreiving zonename");
		} else {
			zone_submit_value(zonename, (gauge_t)FRC2PCT(stats->pctcpu));
		}
		free(stats);
		free(zoneid);
	}
	c_avl_destroy(tree);
}

static c_avl_tree_t *
zone_scandir(DIR *procdir)
{
	pid_t         pid;
	dirent_t     *direntp;
	psinfo_t      psinfo;
	c_avl_tree_t *tree;
	zone_stats_t *stats;

	if (!(tree=c_avl_create((void *) zone_compare))) {
		WARNING("zone plugin: Failed to create tree");
		return(NULL);
	}

	rewinddir(procdir);
	while ((direntp = readdir(procdir))) {
		char const *pidstr = direntp->d_name;
		if (pidstr[0] == '.')	/* skip "." and ".."  */
			continue;

		pid = atoi(pidstr);
		if (pid == 0 || pid == 2 || pid == 3)
			continue;	/* skip sched, pageout and fsflush */

		if (zone_read_procfile(pidstr, "psinfo", &psinfo, sizeof(psinfo_t)) != 0)
			continue;

		stats = zone_find_stats(tree, psinfo.pr_zoneid);
		if( stats ) {
			stats->pctcpu += psinfo.pr_pctcpu;
			stats->pctmem += psinfo.pr_pctmem;
		}
	}
	return(tree);
}

static int zone_read (void)
{
	DIR          *procdir;
	c_avl_tree_t *tree;

	if ((procdir = opendir("/proc")) == NULL) {
		ERROR("zone plugin: cannot open /proc directory\n");
		return (-1);
	}

	tree=zone_scandir(procdir);
	closedir(procdir);
	if (tree == NULL) {
		return (-1);
	}
	zone_submit_values(tree); /* this also frees tree */
	return (0);
}

void module_register (void)
{
	plugin_register_init ("zone", zone_init);
	plugin_register_read ("zone", zone_read);
} /* void module_register */
