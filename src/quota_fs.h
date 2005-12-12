/**
 * collectd - src/quota_fs.h
 * Copyright (C) 2005  Niki W. Waibel
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 * Author:
 *   Niki W. Waibel <niki.waibel@gmx.net>
**/

#if !COLLECTD_QUOTA_FS_H
#define COLLECTD_QUOTA_FS_H 1

#include "common.h"
#include "utils_debug.h"
#include "quota_mnt.h"

/* Quota Filesystem Type */
#define QFT_USRQUOTA "usrquota"
#define QFT_GRPQUOTA "grpquota"

typedef struct _quota_t quota_t;
struct _quota_t {
	char *type;
	char *name;
	char *id;
	char *dir;
	unsigned long long blocks;
	long long bquota, blimit;
	long long bgrace, btimeleft;
	unsigned long long inodes;
	long long iquota, ilimit;
	long long igrace, itimeleft;
	quota_t *next;
};

int quota_fs_issupported(const char *fsname);
int quota_fs_isnfs(const char *fsname);

void quota_fs_printquota_dbg(quota_t *quota);
/*
  DESCRIPTION
	The quota_fs_printquota_dbg() function prints
	the quota list to the log.

	If debugging is switched off in quota_debug.h
	then this function does nothing.
*/

quota_t *quota_fs_getquota(quota_t **quota, quota_mnt_t *m);
/*
  DESCRIPTION
	The quota_fs_getquota() function goes through the mount
	list m and gets the quotas for all mountpoints.

	If *quota is NULL, a new list is created and *quota is
	set to point to the first entry.

	If *quota is set, the list is appended and *quota is
	not changed.

  RETURN VALUE
	The quota_fs_getquota() function returns a pointer to
	the last entry of the list, or NULL if an error occued.

  NOTES
	In case of an error, *quota is not modified.
*/

void quota_fs_freequota(quota_t *quota);
/*
  DESCRIPTION
	The quota_fs_freequota() function goes through all entries
	and frees all allocated memory of all data and structures
	not NULL.
*/

#endif /* !COLLECTD_QUOTA_FS_H */

