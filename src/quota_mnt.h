/**
 * collectd - src/quota_mnt.h
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

#if !COLLECTD_QUOTA_MNT_H
#define COLLECTD_QUOTA_MNT_H 1

#include "common.h"
#include "utils_mount.h"

/* Quota Mount Type */
#define QMT_UNKNOWN (0)
#define QMT_EXT2    (1)
#define QMT_EXT3    (2)
#define QMT_XFS     (3)
#define QMT_UFS     (4)
#define QMT_VXFS    (5)
#define QMT_ZFS     (6)

/* Quota Mount Options */
#define QMO_NONE     (0)
#define QMO_USRQUOTA (1)
#define QMO_GRPQUOTA (2)

typedef struct _quota_mnt_t quota_mnt_t;
struct _quota_mnt_t {
	cu_mount_t *m;
	char *usrjquota;   /* "q.u" */
	char *grpjquota;   /* "q.g" */
	char *jqfmt;       /* "TODO" */
	int opts;
	quota_mnt_t *next;
};

int quota_mnt_type(const char *type);

char *quota_mnt_getmountopt(char *line, char *keyword);
char *quota_mnt_checkmountopt(char *line, char *keyword, int full);

/*
  DESCRIPTION
	The quota_mnt_getlist() function creates a list
	of all mountpoints.

	If *list is NULL, a new list is created and *list is
	set to point to the first entry.

	If *list is set, the list is appended and *list is
	not changed.

  RETURN VALUE
	The quota_mnt_getlist() function returns a pointer to
	the last entry of the list, or NULL if an error occured.

  NOTES
	In case of an error, *list is not modified.
*/
quota_mnt_t *quota_mnt_getlist(quota_mnt_t **list);
void quota_mnt_freelist(quota_mnt_t *list);

#endif /* !COLLECTD_QUOTA_MNT_H */

