/**
 * collectd - src/meta_data.h
 * Copyright (C) 2008-2011  Florian octo Forster
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
