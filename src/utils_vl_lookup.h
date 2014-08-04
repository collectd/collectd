/**
 * collectd - src/utils_vl_lookup.h
 * Copyright (C) 2012       Florian Forster
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
 *   Florian Forster <octo at collectd.org>
 **/

#ifndef UTILS_VL_LOOKUP_H
#define UTILS_VL_LOOKUP_H 1

#include "plugin.h"

/*
 * Types
 */
struct lookup_s;
typedef struct lookup_s lookup_t;

/* Given a user_class, constructs a new user_obj. */
typedef void *(*lookup_class_callback_t) (data_set_t const *ds,
    value_list_t const *vl, void *user_class);

/* Given a user_class and a ds/vl combination, does stuff with the data.
 * This is the main working horse of the module. */
typedef int (*lookup_obj_callback_t) (data_set_t const *ds,
    value_list_t const *vl,
    void *user_class, void *user_obj);

/* Used to free user_class pointers. May be NULL in which case nothing is
 * freed. */
typedef void (*lookup_free_class_callback_t) (void *user_class);

/* Used to free user_obj pointers. May be NULL in which case nothing is
 * freed. */
typedef void (*lookup_free_obj_callback_t) (void *user_obj);

struct identifier_s
{
  char host[DATA_MAX_NAME_LEN];
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
};
typedef struct identifier_s identifier_t;

#define LU_GROUP_BY_HOST            0x01
#define LU_GROUP_BY_PLUGIN          0x02
#define LU_GROUP_BY_PLUGIN_INSTANCE 0x04
/* #define LU_GROUP_BY_TYPE            0x00 */
#define LU_GROUP_BY_TYPE_INSTANCE   0x10

/*
 * Functions
 */
__attribute__((nonnull(1,2)))
lookup_t *lookup_create (lookup_class_callback_t,
    lookup_obj_callback_t,
    lookup_free_class_callback_t,
    lookup_free_obj_callback_t);
void lookup_destroy (lookup_t *obj);

int lookup_add (lookup_t *obj,
    identifier_t const *ident, unsigned int group_by, void *user_class);

/* TODO(octo): Pass lookup_obj_callback_t to lookup_search()? */
int lookup_search (lookup_t *obj,
    data_set_t const *ds, value_list_t const *vl);

#endif /* UTILS_VL_LOOKUP_H */
