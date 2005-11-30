/**
 * collectd - src/quota_plugin.h
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

#if !COLLECTD_QUOTA_PLUGIN_H
#define COLLECTD_QUOTA_PLUGIN_H 1

#include "common.h"

typedef struct _quota_t quota_t;
struct _quota_t {
	char *type;
	char *name;
	char *dir;
	unsigned long long blocks;
	long long bquota, blimit;
	unsigned long long bgrace, btimeleft;
	unsigned long long inodes;
	long long iquota, ilimit;
	unsigned long long igrace, itimeleft;
	quota_t *next;
};

void module_register(void);

#endif /* !COLLECTD_QUOTA_PLUGIN_H */

