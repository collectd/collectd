/**
 * collectd - src/df.c
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

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_mount.h"

#define MODULE_NAME "df"

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

static char *filename_template = "df-%s.rrd";

static char *ds_def[] =
{
	"DS:used:GAUGE:"COLLECTD_HEARTBEAT":0:U",
	"DS:free:GAUGE:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int ds_num = 2;

#define BUFSIZE 512

static void df_init (void)
{
	return;
}

static void df_write (char *host, char *inst, char *val)
{
	char file[BUFSIZE];
	int status;

	status = snprintf (file, BUFSIZE, filename_template, inst);
	if (status < 1)
		return;
	else if (status >= BUFSIZE)
		return;

	rrd_update_file (host, file, val, ds_def, ds_num);
}

#if DF_HAVE_READ
static void df_submit (char *df_name,
		unsigned long long df_used,
		unsigned long long df_free)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%llu:%llu", (unsigned int) curtime,
				df_used, df_free) >= BUFSIZE)
		return;

	plugin_submit (MODULE_NAME, df_name, buf);
}

static void df_read (void)
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
	unsigned long long df_free;
	unsigned long long df_used;
	char mnt_name[BUFSIZE];

	mnt_list = NULL;
	if (cu_mount_getlist (&mnt_list) == NULL)
	{
		syslog (LOG_WARNING, "cu_mount_getlist returned `NULL'");
		return;
	}

	for (mnt_ptr = mnt_list; mnt_ptr != NULL; mnt_ptr = mnt_ptr->next)
	{
		if (STATANYFS (mnt_ptr->dir, &statbuf) < 0)
		{
			syslog (LOG_ERR, "statv?fs failed: %s", strerror (errno));
			continue;
		}

		if (!statbuf.f_blocks)
			continue;

		blocksize = BLOCKSIZE(statbuf);
		df_free = statbuf.f_bfree * blocksize;
		df_used = (statbuf.f_blocks - statbuf.f_bfree) * blocksize;

		if (strcmp (mnt_ptr->dir, "/") == 0)
		{
			strncpy (mnt_name, "root", BUFSIZE);
		}
		else
		{
			int i, len;

			strncpy (mnt_name, mnt_ptr->dir + 1, BUFSIZE);
			len = strlen (mnt_name);

			for (i = 0; i < len; i++)
				if (mnt_name[i] == '/')
					mnt_name[i] = '-';
		}

		df_submit (mnt_name, df_used, df_free);
	}

	cu_mount_freelist (mnt_list);
} /* static void df_read (void) */
#else
# define df_read NULL
#endif /* DF_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, df_init, df_read, df_write);
}

#undef BUFSIZE
#undef MODULE_NAME
