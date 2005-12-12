/**
 * collectd - src/common.h
 * Copyright (C) 2005  Florian octo Forster
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
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 *   Niki W. Waibel <niki.waibel@gmx.net>
**/

#ifndef COMMON_H
#define COMMON_H

#include "collectd.h"

#define sfree(ptr) \
	if((ptr) != NULL) { \
		free(ptr); \
	} \
	(ptr) = NULL

void sstrncpy(char *d, const char *s, int len);
char *sstrdup(const char *s);
void *smalloc(size_t size);

int strsplit (char *string, char **fields, size_t size);

int rrd_update_file (char *host, char *file, char *values, char **ds_def,
		int ds_num);

#ifdef HAVE_LIBKSTAT
int get_kstat (kstat_t **ksp_ptr, char *module, int instance, char *name);
long long get_kstat_value (kstat_t *ksp, char *name);
#endif

#endif /* COMMON_H */
