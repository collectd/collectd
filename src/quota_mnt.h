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

/* Quota Mount Options */
#define QMO_NONE     (0)
#define QMO_USRQUOTA (1)
#define QMO_GRPQUOTA (2)

typedef struct _quota_mnt_t quota_mnt_t;
struct _quota_mnt_t {
	char *dir;
	char *device;
	char *type;
	char *usrjquota;
	char *grpjquota;
	int opts;
	quota_mnt_t *next;
};

quota_mnt_t *quota_mnt_getlist(quota_mnt_t **list);
void quota_mnt_freelist(quota_mnt_t *list);

#endif /* !COLLECTD_QUOTA_MNT_H */

