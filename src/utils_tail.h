/**
 * collectd - src/utils_tail.h
 * Copyright (C) 2007-2008  C-Ware, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author:
 *   Luke Heberling <lukeh at c-ware.com>
 *
 * DESCRIPTION
 *   Facilitates reading information that is appended to a file, taking into
 *   account that the file may be rotated and a new file created under the
 *   same name.
 **/

#ifndef UTILS_TAIL_H
#define UTILS_TAIL_H 1

struct cu_tail_s;
typedef struct cu_tail_s cu_tail_t;

typedef int tailfunc_t(void *data, char *buf, int buflen);

/*
 * NAME
 *   cu_tail_create
 *
 * DESCRIPTION
 *   Allocates a new tail object..
 *
 * PARAMETERS
 *   `file'       The name of the file to be tailed.
 */
cu_tail_t *cu_tail_create (const char *file);

/*
 * cu_tail_destroy
 *
 * Takes a tail object returned by `cu_tail_create' and destroys it, freeing
 * all internal memory.
 *
 * Returns 0 when successful and non-zero otherwise.
 */
int cu_tail_destroy (cu_tail_t *obj);

/*
 * cu_tail_readline
 *
 * Reads from the file until `buflen' characters are read, a newline
 * character is read, or an eof condition is encountered. `buf' is
 * always null-terminated on successful return and isn't touched when non-zero
 * is returned.
 *
 * You can check if the EOF condition is reached by looking at the buffer: If
 * the length of the string stored in the buffer is zero, EOF occurred.
 * Otherwise at least the newline character will be in the buffer.
 *
 * Returns 0 when successful and non-zero otherwise.
 */
int cu_tail_readline (cu_tail_t *obj, char *buf, int buflen);

/*
 * cu_tail_readline
 *
 * Reads from the file until eof condition or an error is encountered.
 *
 * Returns 0 when successful and non-zero otherwise.
 */
int cu_tail_read (cu_tail_t *obj, char *buf, int buflen, tailfunc_t *callback,
		void *data);

#endif /* UTILS_TAIL_H */
