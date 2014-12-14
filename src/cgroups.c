/**
 * collectd - src/cgroups.c
 * Copyright (C) 2011  Michael Stapelberg
 * Copyright (C) 2013  Florian Forster
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
 *   Florian Forster <octo at collectd.org>
 *   Mathieu Grzybek <mathieu at grzybek.fr>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_mount.h"
#include "utils_ignorelist.h"

static char const *config_keys[] =
{
	"CGroup",
	"IgnoreSelected",
	"Metric"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *il_cgroup = NULL;
static ignorelist_t *mtx_cgroup = NULL;

__attribute__ ((nonnull(1)))
__attribute__ ((nonnull(2)))
static void cgroups_submit_one (char const *plugin_instance,
		char const *type_value, char const *type_instance, value_t value)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = &value;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "cgroups", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type_value, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance,
			sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void cgroups_submit_one */

/*
 * This function reads the given file and submits the metrics
 */
static int process_cgroup_file(const char *cgroup_name, const char* type_value, const char *abs_path)
{
	FILE *fh = NULL;
	char buf[1024];
	int status;

	fh = fopen (abs_path, "r");
	if (fh == NULL)
	{
		char errbuf[1024];
		ERROR ("cgroups plugin: fopen (\"%s\") failed: %s",
			   abs_path,
			   sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buf, sizeof (buf), fh) != NULL)
	{
		char *fields[8];
		int numfields = 0;
		char *key;
		size_t key_len;
		value_t value;
		/* Expected format:
		 *
		 * (\w+):{0,1}\s+(\d+)
		 */
		strstripnewline (buf);
		numfields = strsplit (buf, fields, STATIC_ARRAY_SIZE (fields));
		if (numfields != 2)
			continue;

		key = fields[0];
		key_len = strlen (key);
		if (key_len < 2)
			continue;

		/* Strip colon off the first column, if found */
		if (key[key_len - 1] == ':')
			key[key_len - 1] = 0;

		status = parse_value (fields[1], &value, DS_TYPE_DERIVE);
		if (status != 0)
			continue;

		cgroups_submit_one (cgroup_name, type_value, key, value);
	}

	fclose (fh);
	return (0);
} /* process_cgroup_file */

/*
 * This callback reads the user/system CPU time for each cgroup.
 */
static int read_cpuacct_procs (const char *dirname, char const *cgroup_name,
    void *user_data)
{
	char abs_path[PATH_MAX];
	struct stat statbuf;
	int status;

	if (ignorelist_match (il_cgroup, cgroup_name))
		return (0);

	ssnprintf (abs_path, sizeof (abs_path), "%s/%s", dirname, cgroup_name);

	status = lstat (abs_path, &statbuf);
	if (status != 0)
	{
		ERROR ("cgroups plugin: stat (\"%s\") failed.",
			   abs_path);
		return (-1);
	}

	/* We are only interested in directories, so skip everything else. */
	if (!S_ISDIR (statbuf.st_mode))
		return (0);

	ssnprintf (abs_path, sizeof (abs_path), "%s/%s/cpuacct.stat",
			   dirname, cgroup_name);

	return process_cgroup_file(cgroup_name, "cpu", abs_path);
} /* int read_cpuacct_procs */

/*
 * This callback reads the memory usage for each cgroup.
 */
static int read_memory_procs (const char *dirname, char const *cgroup_name,
							  void *user_data)
{
	char abs_path[PATH_MAX];
	struct stat statbuf;
	int status;
	
	if (ignorelist_match (il_cgroup, cgroup_name))
		return (0);
	
	ssnprintf (abs_path, sizeof (abs_path), "%s/%s", dirname, cgroup_name);
	
	status = lstat (abs_path, &statbuf);
	if (status != 0)
	{
		ERROR ("cgroups plugin: stat (\"%s\") failed.",
			   abs_path);
		return (-1);
	}
	
	/* We are only interested in directories, so skip everything else. */
	if (!S_ISDIR (statbuf.st_mode))
		return (0);
	
	ssnprintf (abs_path, sizeof (abs_path), "%s/%s/memory.stat",
			   dirname, cgroup_name);
	
	return process_cgroup_file(cgroup_name, "memory", abs_path);
} /* int read_memory_procs */

/*
 * Gets called for every file/folder in /sys/fs/cgroup/cpu,cpuacct (or
 * wherever cpuacct is mounted on the system). Calls walk_directory with the
 * read_cpuacct_procs callback on every folder it finds, such as "system".
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
		ERROR ("cgroups plugin: stat (%s) failed.", abs_path);
		return (-1);
	}

	if (S_ISDIR (statbuf.st_mode))
	{
		status = walk_directory (abs_path, read_cpuacct_procs,
				/* user_data = */ NULL,
				/* include_hidden = */ 0);
		return (status);
	}

	return (0);
}

/*
 * Gets called for every file/folder in /sys/fs/cgroup/memory (or
 * wherever memory is mounted on the system). Calls walk_directory with the
 * read_memory_procs callback on every folder it finds, such as "total_rss".
 */
static int read_memory_root (const char *dirname, const char *filename,
							 void *user_data)
{
	char abs_path[PATH_MAX];
	struct stat statbuf;
	int status;
	
	ssnprintf (abs_path, sizeof (abs_path), "%s/%s", dirname, filename);
	
	status = lstat (abs_path, &statbuf);
	if (status != 0)
	{
		ERROR ("cgroups plugin: stat (%s) failed.", abs_path);
		return (-1);
	}
	
	if (S_ISDIR (statbuf.st_mode))
	{
		status = walk_directory (abs_path, read_memory_procs,
								 /* user_data = */ NULL,
								 /* include_hidden = */ 0);
		return (status);
	}
	
	return (0);
} /* read_memory_root */

static int cgroups_init (void)
{
	if (il_cgroup == NULL)
		il_cgroup = ignorelist_create (1);
	if (mtx_cgroup == NULL)
		mtx_cgroup = ignorelist_create (0);

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
			ignorelist_set_invert (il_cgroup, 0);
		else
			ignorelist_set_invert (il_cgroup, 1);
		return (0);
	}
	else if (strcasecmp (key, "Metric") == 0)
	{
		if (ignorelist_add (mtx_cgroup, value))
			return (1);
		return (0);
	}

	return (-1);
}

static int cgroups_read (void)
{
	cu_mount_t *mnt_list;
	cu_mount_t *mnt_ptr;

	_Bool cgroup_cpuacct_found = 0;
	_Bool cgroup_memory_found = 0;

	/* By default we walk nothing. */
	_Bool walk_cpuacct = 0;
	_Bool walk_memory = 0;

	if (ignorelist_match (mtx_cgroup, "cpu"))
		walk_cpuacct = 1;
	if (ignorelist_match (mtx_cgroup, "memory"))
		walk_memory = 1;

	mnt_list = NULL;
	if (cu_mount_getlist (&mnt_list) == NULL)
	{
		ERROR ("cgroups plugin: cu_mount_getlist failed.");
		return (-1);
	}

	for (mnt_ptr = mnt_list; mnt_ptr != NULL; mnt_ptr = mnt_ptr->next)
	{
		/* Find the cgroup mountpoint which contains the cpuacct
		 * or the memory controller. */
		if (strcmp(mnt_ptr->type, "cgroup") == 0)
		{
			if (walk_cpuacct == 1 &&
				cu_mount_checkoption(mnt_ptr->options,
									 "cpuacct", /* full = */ 1))
			{
				walk_directory (mnt_ptr->dir, read_cpuacct_root,
								/* user_data = */ NULL,
								/* include_hidden = */ 0);
				cgroup_cpuacct_found = 1;
			}
			else if (walk_memory == 1 &&
					 cu_mount_checkoption(mnt_ptr->options,
										  "memory", /* full = */ 1))
			{
				walk_directory (mnt_ptr->dir, read_memory_root,
								/* user_data = */ NULL,
								/* include_hidden = */ 0);
				cgroup_memory_found = 1;
			}
		}
	}

	cu_mount_freelist (mnt_list);

	if (!cgroup_cpuacct_found && !cgroup_memory_found)
	{
		WARNING ("cgroups plugin: Unable to find cgroup "
				 "mount-point with the \"cpuacct\" or the "
				 "\"memory\" options.");
		return (-1);
	}

	return (0);
} /* int cgroup_read */

void module_register (void)
{
	plugin_register_config ("cgroups", cgroups_config,
           config_keys, config_keys_num);
	plugin_register_init ("cgroups", cgroups_init);
	plugin_register_read ("cgroups", cgroups_read);
} /* void module_register */
