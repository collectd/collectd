/**
 * collectd - src/utils_mount.h
 * Copyright (C) 2005,2006  Niki W. Waibel
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

/* See below for instructions how to use the public functions. */

#ifndef COLLECTD_UTILS_MOUNT_H
#define COLLECTD_UTILS_MOUNT_H 1

#if HAVE_FS_INFO_H
# include <fs_info.h>
#endif
#if HAVE_FSHELP_H
# include <fshelp.h>
#endif
#if HAVE_PATHS_H
# include <paths.h>
#endif
#if HAVE_MNTENT_H
# include <mntent.h>
#endif
#if HAVE_MNTTAB_H
# include <mnttab.h>
#endif
#if HAVE_SYS_FSTYP_H
# include <sys/fstyp.h>
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
#if HAVE_SYS_STATFS_H
# include <sys/statfs.h>
#endif
#if HAVE_SYS_VFS_H
# include <sys/vfs.h>
#endif
#if HAVE_SYS_VFSTAB_H
# include <sys/vfstab.h>
#endif

/* Collectd Utils Mount Type */
#define CUMT_UNKNOWN (0)
#define CUMT_EXT2    (1)
#define CUMT_EXT3    (2)
#define CUMT_XFS     (3)
#define CUMT_UFS     (4)
#define CUMT_VXFS    (5)
#define CUMT_ZFS     (6)

typedef struct _cu_mount_t cu_mount_t;
struct _cu_mount_t {
	char *dir;         /* "/sys" or "/" */
	char *spec_device; /* "LABEL=/" or "none" or "proc" or "/dev/hda1" */
	char *device;      /* "none" or "proc" or "/dev/hda1" */
	char *type;        /* "sysfs" or "ext3" */
	char *options;     /* "rw,noatime,commit=600,quota,grpquota" */
	cu_mount_t *next;
};

cu_mount_t *cu_mount_getlist(cu_mount_t **list);
/*
  DESCRIPTION
	The cu_mount_getlist() function creates a list
	of all mountpoints.

	If *list is NULL, a new list is created and *list is
	set to point to the first entry.

	If *list is not NULL, the list of mountpoints is appended
	and *list is not changed.

  RETURN VALUE
	The cu_mount_getlist() function returns a pointer to
	the last entry of the list, or NULL if an error has
	occured.

  NOTES
	In case of an error, *list is not modified.
*/

void cu_mount_freelist(cu_mount_t *list);
/*
  DESCRIPTION
	The cu_mount_freelist() function free()s all memory
	allocated by *list and *list itself as well.
*/

char *cu_mount_checkoption(char *line, char *keyword, int full);
/*
  DESCRIPTION
	The cu_mount_checkoption() function is a replacement of
	char *hasmntopt(const struct mntent *mnt, const char *opt).
	In fact hasmntopt() just looks for the first occurence of the
	characters at opt in mnt->mnt_opts. cu_mount_checkoption()
	checks for the *option* keyword in line, starting at the
	first character of line or after a ','.

	If full is not 0 then also the end of keyword has to match
	either the end of line or a ',' after keyword.

  RETURN VALUE
	The cu_mount_checkoption() function returns a pointer into
	string line if a match of keyword is found. If no match is
	found cu_mount_checkoption() returns NULL.

  NOTES
	Do *not* try to free() the pointer which is returned! It is
	just part of the string line.

	full should be set to 0 when matching options like: rw, quota,
	noatime. Set full to 1 when matching options like: loop=,
	gid=, commit=.

  EXAMPLES
	If line is "rw,usrquota,grpquota", keyword is "quota", NULL
	will be returned (independend of full).

	If line is "rw,usrquota,grpquota", keyword is "usrquota",
	a pointer to "usrquota,grpquota" is returned (independend
	of full).

	If line is "rw,loop=/dev/loop1,quota", keyword is "loop="
	and full is 0, then a pointer to "loop=/dev/loop1,quota"
	is returned. If full is not 0 then NULL is returned. But
	maybe you might want to try cu_mount_getoptionvalue()...
*/

char *cu_mount_getoptionvalue(char *line, char *keyword);
/*
  DESCRIPTION
	The cu_mount_getoptionvalue() function can be used to grab
	a VALUE out of a mount option (line) like:
		loop=VALUE
	whereas "loop=" is the keyword.

  RETURN VALUE
	If the cu_mount_getoptionvalue() function can find the option
	keyword in line, then memory is allocated for the value of
	that option and a pointer to that value is returned.

	If the option keyword is not found, cu_mount_getoptionvalue()
	returns NULL;

  NOTES
	Internally it calls cu_mount_checkoption(), then it
	allocates memory for VALUE and returns a pointer to that
	string. So *do not forget* to free() the memory returned
	after use!!!
*/

int cu_mount_type(const char *type);
/*
  DESCRIPTION

  RETURN VALUE
*/


#endif /* !COLLECTD_UTILS_MOUNT_H */

