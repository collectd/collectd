/**
 * collectd - src/cgroups_cpuacct.c
 * Copyright (C) 2011  Michael Stapelberg
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the license is applicable.
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
 *   Michael Stapelberg <michael at stapelberg.de>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_mount.h"
#include "utils_ignorelist.h"

static const char *config_keys[] =
{
	"CGroup",
	"IgnoreSelected"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *il_cgroup = NULL;

__attribute__ ((nonnull(1)))
__attribute__ ((nonnull(2)))
static void cgroups_submit_one (const char *plugin_instance,
		const char *type, const char *type_instance,
		derive_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "cgroups_cpuacct", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
		sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));
	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance,
				sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void cgroups_submit_one */


/*
 * This callback reads the user/system CPU time for each cgroup.
 *
 */
static int read_cpuacct_procs (const char *dirname, const char *filename,
    void *user_data)
{
	char abs_path[PATH_MAX];
	struct stat statbuf;
	char buf[1024];
	int status;

	if (ignorelist_match (il_cgroup, filename))
		return (0);

	ssnprintf (abs_path, sizeof (abs_path), "%s/%s", dirname, filename);

	status = lstat (abs_path, &statbuf);
	if (status != 0)
	{
		ERROR ("cgroups_cpuacct plugin: stat (%s) failed.", abs_path);
		return (-1);
	}

	/* We are only interested in directories, so skip everything else. */
	if (!S_ISDIR (statbuf.st_mode))
	{
		return (0);
	}

	ssnprintf (abs_path, sizeof (abs_path), "%s/%s/cpuacct.stat", dirname, filename);
	int bytes_read;
	if ((bytes_read = read_file_contents (abs_path, buf, sizeof(buf))) <= 0)
	{
		char errbuf[1024];
		ERROR ("cgroups_cpuacct plugin: read_file_contents(%s): %s",
				abs_path, sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}
	buf[bytes_read] = '\0';

	char *fields[4];
	int numfields = 0;
	if ((numfields = strsplit (buf, fields, 4)) != 4)
	{
		ERROR ("cgroups_cpuacct plugin: unexpected content in file %s", abs_path);
		return (-1);
	}
	uint64_t usertime, systemtime;
	usertime = atoll (fields[1]);
	systemtime = atoll (fields[3]);

	cgroups_submit_one(filename, "cpuacct", "user", (derive_t)usertime);
	cgroups_submit_one(filename, "cpuacct", "system", (derive_t)systemtime);

	return (0);
}

/*
 * Gets called for every file/folder in /sys/fs/cgroup/cpu,cpuacct (or
 * whereever cpuacct is mounted on the system). Calls walk_directory with the
 * read_cpuacct_procs callback on every folder it finds, such as "system".
 *
 */
static int read_cpuacct_root (const char *dirname, const char *filename,
    void *user_data)
{
  char abs_path[PATH_MAX];
  struct stat statbuf;
  int status;

  ssnprintf (abs_path, sizeof (abs_path), "%s/%s", dirname, filename);

  status = lstat (abs_path, &statbuf);
  if (status != 0)
  {
    ERROR ("cgroups_cpuacct plugin: stat (%s) failed.", abs_path);
    return (-1);
  }

  if (S_ISDIR (statbuf.st_mode))
  {
    status = walk_directory (abs_path, read_cpuacct_procs, NULL, 0);
    return (status);
  }

  return (0);
}

static int cgroups_init (void)
{
	if (il_cgroup == NULL)
		il_cgroup = ignorelist_create (1);

	return (0);
}

static int cgroups_config (const char *key, const char *value)
{
	cgroups_init ();

	if (strcasecmp (key, "CGroup") == 0)
	{
		if (ignorelist_add (il_cgroup, value))
			return (1);
		return (0);
	}
	else if (strcasecmp (key, "IgnoreSelected") == 0)
	{
		if (IS_TRUE (value))
		{
			ignorelist_set_invert (il_cgroup, 0);
		}
		else
		{
			ignorelist_set_invert (il_cgroup, 1);
		}
		return (0);
	}

	return (-1);
}

static int cgroups_read (void)
{
	cu_mount_t *mnt_list;
	cu_mount_t *mnt_ptr;
	int cgroup_found = 0;

	mnt_list = NULL;
	if (cu_mount_getlist (&mnt_list) == NULL)
	{
		ERROR ("cgroups_cpuacct plugin: cu_mount_getlist failed.");
		return (-1);
	}

	for (mnt_ptr = mnt_list; mnt_ptr != NULL; mnt_ptr = mnt_ptr->next)
	{
		/* Find the cgroup mountpoint which contains the cpuacct
		 * controller. */
		if (strcmp(mnt_ptr->type, "cgroup") != 0 ||
			!cu_mount_getoptionvalue(mnt_ptr->options, "cpuacct"))
			continue;

		walk_directory (mnt_ptr->dir, read_cpuacct_root, NULL, 0);
		cgroup_found = 1;
		/* It doesn't make sense to check other cpuacct mount-points
		 * (if any), they contain the same data. */
		break;
	}

	if (!cgroup_found)
		WARNING ("cpuacct mountpoint not found. Cannot collect any data.");

	cu_mount_freelist (mnt_list);

	return (0);
} /* int cgroup_read */

void module_register (void)
{
	plugin_register_config ("cgroups_cpuacct", cgroups_config,
			config_keys, config_keys_num);
	plugin_register_init ("cgroups_cpuacct", cgroups_init);
	plugin_register_read ("cgroups_cpuacct", cgroups_read);
} /* void module_register */
