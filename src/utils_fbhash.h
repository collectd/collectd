/**
 * collectd - src/utils_fbhash.h
 * Copyright (C) 2009       Florian octo Forster
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
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#ifndef UTILS_FBHASH_H
#define UTILS_FBHASH_H 1

/*
 * File-backed hash
 *
 * This module reads a file of the form
 *   key: value
 * into a hash, which can then be queried. The file is given to `fbh_create',
 * the hash is queried using `fbh_get'. If the file is changed during runtime,
 * it will automatically be re-read.
 */

struct fbhash_s;
typedef struct fbhash_s fbhash_t;

fbhash_t *fbh_create (const char *file);
void fbh_destroy (fbhash_t *h);

/* Returns the value as a newly allocated `char *'. It's the caller's
 * responsibility to free this memory. */
char *fbh_get (fbhash_t *h, const char *key);

#endif /* UTILS_FBHASH_H */

/* vim: set sw=2 sts=2 et fdm=marker : */
