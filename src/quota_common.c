/**
 * collectd - src/quota_common.c
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

#include "common.h"
#include "quota_debug.h"
#include "quota_common.h"

void
sstrncpy(char *d, const char *s, int len)
{
	strncpy(d, s, len);
	d[len - 1] = 0;
}

char *
sstrdup(const char *s)
{
	char *r = strdup(s);
	if(r == NULL) {
		DBG("Not enough memory.");
		exit(3);
	}
	return r;
}

void *
smalloc(size_t size)
{
	void *r = malloc(size);
	if(r == NULL) {
		DBG("Not enough memory.");
		exit(3);
	}
	return r;
}

void
sfree(void *ptr)
{
	if(ptr != NULL) {
		free(ptr);
	}
}

