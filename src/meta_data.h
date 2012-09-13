/**
 * collectd - src/meta_data.h
 * Copyright (C) 2008-2011  Florian octo Forster
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

#ifndef META_DATA_H
#define META_DATA_H

#include "collectd.h"

/*
 * Defines
 */
#define MD_TYPE_STRING       1
#define MD_TYPE_SIGNED_INT   2
#define MD_TYPE_UNSIGNED_INT 3
#define MD_TYPE_DOUBLE       4
#define MD_TYPE_BOOLEAN      5

struct meta_data_s;
typedef struct meta_data_s meta_data_t;

meta_data_t *meta_data_create (void);
meta_data_t *meta_data_clone (meta_data_t *orig);
void meta_data_destroy (meta_data_t *md);

int meta_data_exists (meta_data_t *md, const char *key);
int meta_data_type (meta_data_t *md, const char *key);
int meta_data_toc (meta_data_t *md, char ***toc);
int meta_data_delete (meta_data_t *md, const char *key);

int meta_data_add_string (meta_data_t *md,
    const char *key,
    const char *value);
int meta_data_add_signed_int (meta_data_t *md,
    const char *key,
    int64_t value);
int meta_data_add_unsigned_int (meta_data_t *md,
    const char *key,
    uint64_t value);
int meta_data_add_double (meta_data_t *md,
    const char *key,
    double value);
int meta_data_add_boolean (meta_data_t *md,
    const char *key,
    _Bool value);

int meta_data_get_string (meta_data_t *md,
    const char *key,
    char **value);
int meta_data_get_signed_int (meta_data_t *md,
    const char *key,
    int64_t *value);
int meta_data_get_unsigned_int (meta_data_t *md,
    const char *key,
    uint64_t *value);
int meta_data_get_double (meta_data_t *md,
    const char *key,
    double *value);
int meta_data_get_boolean (meta_data_t *md,
    const char *key,
    _Bool *value);

#endif /* META_DATA_H */
/* vim: set sw=2 sts=2 et : */
