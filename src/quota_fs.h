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
#include "quota_plugin.h"
#include "quota_mnt.h"

/* Quota Filesystem Type */
#define QFT_NONE (0)
#define QFT_EXT2 (1)
#define QFT_EXT3 (2)
#define QFT_XFS  (3)
#define QFT_UFS  (4)
#define QFT_VXFS (5)
#define QFT_ZFS  (6)

int quota_fs_issupported(const char *fsname);
int quota_fs_isnfs(const char *fsname);

quota_t *quota_fs_getquota(quota_t **quota, quota_mnt_t *m);
void quota_fs_freequota(quota_t *quota);

#endif /* !COLLECTD_QUOTA_FS_H */

