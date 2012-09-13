/**
 * collectd - src/utils_fbhash.h
 * Copyright (C) 2009  Florian octo Forster
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
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
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
