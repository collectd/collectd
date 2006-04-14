/**
 * collectd - src/common.h
 * Copyright (C) 2005,2006  Florian octo Forster
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

/*
 * NAME
 *   strsplit
 *
 * DESCRIPTION
 *   Splits a string into parts and stores pointers to the parts in `fields'.
 *   The characters split at are ` ' (space) and "\t" (tab).
 *
 * PARAMETERS
 *   `string'      String to split. This string will be modified. `fields' will
 *                 contain pointers to parts of this string, so free'ing it
 *                 will destroy `fields' as well.
 *   `fields'      Array of strings where pointers to the parts will be stored.
 *   `size'        Number of elements in the array. No more than `size'
 *                 pointers will be stored in `fields'.
 *
 * RETURN VALUE
 *    Returns the number of parts stored in `fields'.
 */
int strsplit (char *string, char **fields, size_t size);

/*
 * NAME
 *   strjoin
 *
 * DESCRIPTION
 *   Joins together several parts of a string using `sep' as a seperator. This
 *   is equipollent to the perl buildin `join'.
 *
 * PARAMETERS
 *   `dst'         Buffer where the result is stored.
 *   `dst_len'     Length of the destination buffer. No more than this many
 *                 bytes will be written to the memory pointed to by `dst',
 *                 including the trailing null-byte.
 *   `fields'      Array of strings to be joined.
 *   `fields_num'  Number of elements in the `fields' array.
 *   `sep'         String to be inserted between any two elements of `fields'.
 *                 This string is neither prepended nor appended to the result.
 *                 Instead of passing "" (empty string) one can pass NULL.
 *
 * RETURN VALUE
 *   Returns the number of characters in `dst', NOT including the trailing
 *   null-byte. If an error occured (empty array or `dst' too small) a value
 *   smaller than zero will be returned.
 */
int strjoin (char *dst, size_t dst_len, char **fields, size_t fields_num, const char *sep);

/*
 * NAME
 *   escape_slashes
 *
 * DESCRIPTION
 *   Removes slashes from the string `buf' and substitutes them with something
 *   appropriate. This function should be used whenever a path is to be used as
 *   (part of) an instance.
 *
 * PARAMETERS
 *   `buf'         String to be escaped.
 *   `buf_len'     Length of the buffer. No more then this many bytes will be
 *   written to `buf', including the trailing null-byte.
 *
 * RETURN VALUE
 *   Returns zero upon success and a value smaller than zero upon failure.
 */
int escape_slashes (char *buf, int buf_len);

/* FIXME: `timeval_sub_timespec' needs a description */
int timeval_sub_timespec (struct timeval *tv0, struct timeval *tv1, struct timespec *ret);

int rrd_update_file (char *host, char *file, char *values,
		char **ds_def, int ds_num);

#ifdef HAVE_LIBKSTAT
int get_kstat (kstat_t **ksp_ptr, char *module, int instance, char *name);
long long get_kstat_value (kstat_t *ksp, char *name);
#endif

#endif /* COMMON_H */
