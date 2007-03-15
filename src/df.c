/**
 * collectd - src/df.c
 * Copyright (C) 2005-2007  Florian octo Forster
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
#include "configfile.h"
#include "utils_mount.h"
#include "utils_ignorelist.h"

#if HAVE_STATFS || HAVE_STATVFS
# define DF_HAVE_READ 1
#else
# define DF_HAVE_READ 0
#endif

#if HAVE_STATVFS
# if HAVE_SYS_STATVFS_H
#  include <sys/statvfs.h>
# endif
# define STATANYFS statvfs
# define BLOCKSIZE(s) ((s).f_frsize ? (s).f_frsize : (s).f_bsize)
#elif HAVE_STATFS
# if HAVE_SYS_STATFS_H
#  include <sys/statfs.h>
# endif
# define STATANYFS statfs
# define BLOCKSIZE(s) (s).f_bsize
#endif

/* 2^50 - 1 == 1125899906842623 = 1 Petabyte */
static data_source_t dsrc[2] =
{
	{"free", DS_TYPE_GAUGE, 0, 1125899906842623.0},
	{"used", DS_TYPE_GAUGE, 0, 1125899906842623.0}
};

static data_set_t ds =
{
	"df", 2, dsrc
};

#if DF_HAVE_READ
static const char *config_keys[] =
{
	"Device",
	"MountPoint",
	"FSType",
	"IgnoreSelected",
	NULL
};
static int config_keys_num = 4;

static ignorelist_t *il_device = NULL;
static ignorelist_t *il_mountpoint = NULL;
static ignorelist_t *il_fstype = NULL;

static int df_init (void)
{
	if (il_device == NULL)
		il_device = ignorelist_create (1);
	if (il_mountpoint == NULL)
		il_mountpoint = ignorelist_create (1);
	if (il_fstype == NULL)
		il_fstype = ignorelist_create (1);

	return (0);
}

static int df_config (const char *key, const char *value)
{
	df_init ();

	if (strcasecmp (key, "Device") == 0)
	{
		if (ignorelist_add (il_device, value))
			return (1);
		return (0);
	}
	else if (strcasecmp (key, "MountPoint") == 0)
	{
		if (ignorelist_add (il_mountpoint, value))
			return (1);
		return (0);
	}
	else if (strcasecmp (key, "FSType") == 0)
	{
		if (ignorelist_add (il_fstype, value))
			return (1);
		return (0);
	}
	else if (strcasecmp (key, "IgnoreSelected") == 0)
	{
		if ((strcasecmp (value, "True") == 0)
				|| (strcasecmp (value, "Yes") == 0)
				|| (strcasecmp (value, "On") == 0))
		{
			ignorelist_set_invert (il_device, 0);
			ignorelist_set_invert (il_mountpoint, 0);
			ignorelist_set_invert (il_fstype, 0);
		}
		else
		{
			ignorelist_set_invert (il_device, 1);
			ignorelist_set_invert (il_mountpoint, 1);
			ignorelist_set_invert (il_fstype, 1);
		}
		return (0);
	}

	return (-1);
}

static void df_submit (char *df_name,
		gauge_t df_used,
		gauge_t df_free)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = df_used;
	values[1].gauge = df_free;

	vl.values = values;
	vl.values_len = 2;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "df");
	strcpy (vl.plugin_instance, "");
	strncpy (vl.type_instance, df_name, sizeof (vl.type_instance));

	plugin_dispatch_values ("df", &vl);
} /* void df_submit */

static int df_read (void)
{
#if HAVE_STATVFS
	struct statvfs statbuf;
#elif HAVE_STATFS
	struct statfs statbuf;
#endif
	/* struct STATANYFS statbuf; */
	cu_mount_t *mnt_list;
	cu_mount_t *mnt_ptr;

	unsigned long long blocksize;
	gauge_t df_free;
	gauge_t df_used;
	char mnt_name[256];

	mnt_list = NULL;
	if (cu_mount_getlist (&mnt_list) == NULL)
		return (-1);

	for (mnt_ptr = mnt_list; mnt_ptr != NULL; mnt_ptr = mnt_ptr->next)
	{
		if (STATANYFS (mnt_ptr->dir, &statbuf) < 0)
		{
			ERROR ("statv?fs failed: %s", strerror (errno));
			continue;
		}

		if (!statbuf.f_blocks)
			continue;

		blocksize = BLOCKSIZE(statbuf);
		df_free = statbuf.f_bfree * blocksize;
		df_used = (statbuf.f_blocks - statbuf.f_bfree) * blocksize;

		if (strcmp (mnt_ptr->dir, "/") == 0)
		{
			strncpy (mnt_name, "root", sizeof (mnt_name));
		}
		else
		{
			int i, len;

			strncpy (mnt_name, mnt_ptr->dir + 1, sizeof (mnt_name));
			len = strlen (mnt_name);

			for (i = 0; i < len; i++)
				if (mnt_name[i] == '/')
					mnt_name[i] = '-';
		}

		if (ignorelist_match (il_device,
					(mnt_ptr->spec_device != NULL)
					? mnt_ptr->spec_device
					: mnt_ptr->device))
			continue;
		if (ignorelist_match (il_mountpoint, mnt_ptr->dir))
			continue;
		if (ignorelist_match (il_fstype, mnt_ptr->type))
			continue;

		df_submit (mnt_name, df_used, df_free);
	}

	cu_mount_freelist (mnt_list);

	return (0);
} /* int df_read */
#endif /* DF_HAVE_READ */

void module_register (void)
{
	plugin_register_data_set (&ds);

#if DF_HAVE_READ
	plugin_register_config ("df", df_config, config_keys, config_keys_num);
	plugin_register_init ("df", df_init);
	plugin_register_read ("df", df_read);
#endif
} /* void module_register */
