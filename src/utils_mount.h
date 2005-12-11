/**
 * collectd - src/utils_mount.h
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

#if !COLLECTD_UTILS_MOUNT_H
#define COLLECTD_UTILS_MOUNT_H 1

#if HAVE_MNTENT_H
# include <mntent.h>
#endif
#if HAVE_MNTTAB_H
# include <mnttab.h>
#endif
#if HAVE_PATHS_H
# include <paths.h>
#endif
#if HAVE_SYS_FS_TYPES_H
# include <sys/fs_types.h>
#endif
#if HAVE_SYS_MNTENT_H
# include <sys/mntent.h>
#endif
#if HAVE_SYS_MNTTAB_H
# include <sys/mnttab.h>
#endif
#if HAVE_SYS_MOUNT_H
# include <sys/mount.h>
#endif
#if HAVE_SYS_VFSTAB_H
# include <sys/vfstab.h>
#endif
#if HAVE_SYS_VFS_H
# include <sys/vfs.h>
#endif

#include "common.h"

/* Collectd Utils Mount Type */
#define CUMT_UNKNOWN (0)
#define CUMT_EXT2    (1)
#define CUMT_EXT3    (2)
#define CUMT_XFS     (3)
#define CUMT_UFS     (4)
#define CUMT_VXFS    (5)
#define CUMT_ZFS     (6)

/* Collectd Utils Mount Options */
#define CUMO_NONE     (0)
#define CUMO_USRQUOTA (1)
#define CUMO_GRPQUOTA (2)

typedef struct _cu_mount_t cu_mount_t;
struct _cu_mount_t {
	char *dir;         /* "/sys" or "/" */
	char *spec_device; /* "LABEL=/" or "none" or "proc" or "/dev/hda1" */
	char *device;      /* "none" or "proc" "/dev/hda1" */
	char *type;        /* "sysfs" or "ext3" */
	char *options;     /* "rw,noatime,commit=600,quota,grpquota" */
	cu_mount_t *next;
};

int cu_mount_type(const char *type);

char *cu_mount_getmountopt(char *line, char *keyword);
char *cu_mount_checkmountopt(char *line, char *keyword, int full);

/*
  DESCRIPTION
	The cu_mount_getlist() function creates a list
	of all mountpoints.

	If *list is NULL, a new list is created and *list is
	set to point to the first entry.

	If *list is set, the list is appended and *list is
	not changed.

  RETURN VALUE
	The cu_mount_getlist() function returns a pointer to
	the last entry of the list, or NULL if an error occured.

  NOTES
	In case of an error, *list is not modified.
*/
cu_mount_t *cu_mount_getlist(cu_mount_t **list);
void cu_mount_freelist(cu_mount_t *list);

#endif /* !COLLECTD_UTILS_MOUNT_H */

