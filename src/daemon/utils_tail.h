/**
 * collectd - src/utils_tail.h
 * Copyright (C) 2007-2008  C-Ware, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
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
