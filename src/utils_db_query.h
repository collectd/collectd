/**
 * collectd - src/utils_db_query.h
 * Copyright (C) 2008,2009  Florian octo Forster
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

#ifndef UTILS_DB_QUERY_H
#define UTILS_DB_QUERY_H 1

#include "configfile.h"

/*
 * Data types
 */
struct udb_query_s;
typedef struct udb_query_s udb_query_t;

struct udb_query_preparation_area_s;
typedef struct udb_query_preparation_area_s udb_query_preparation_area_t;

typedef int (*udb_query_create_callback_t) (udb_query_t *q,
    oconfig_item_t *ci);

/* 
 * Public functions
 */
int udb_query_create (udb_query_t ***ret_query_list,
    size_t *ret_query_list_len, oconfig_item_t *ci,
    udb_query_create_callback_t cb);
void udb_query_free (udb_query_t **query_list, size_t query_list_len);

int udb_query_pick_from_list_by_name (const char *name,
    udb_query_t **src_list, size_t src_list_len,
    udb_query_t ***dst_list, size_t *dst_list_len);
int udb_query_pick_from_list (oconfig_item_t *ci,
    udb_query_t **src_list, size_t src_list_len,
    udb_query_t ***dst_list, size_t *dst_list_len);

const char *udb_query_get_name (udb_query_t *q);
const char *udb_query_get_statement (udb_query_t *q);

void  udb_query_set_user_data (udb_query_t *q, void *user_data);
void *udb_query_get_user_data (udb_query_t *q);

/* 
 * udb_query_check_version
 *
 * Returns 0 if the query is NOT suitable for `version' and >0 if the
 * query IS suitable.
 */
int udb_query_check_version (udb_query_t *q, unsigned int version);

int udb_query_prepare_result (udb_query_t const *q,
    udb_query_preparation_area_t *prep_area,
    const char *host, const char *plugin, const char *db_name,
    char **column_names, size_t column_num, cdtime_t interval);
int udb_query_handle_result (udb_query_t const *q,
    udb_query_preparation_area_t *prep_area, char **column_values);
void udb_query_finish_result (udb_query_t const *q,
    udb_query_preparation_area_t *prep_area);

udb_query_preparation_area_t *
udb_query_allocate_preparation_area (udb_query_t *q);
void
udb_query_delete_preparation_area (udb_query_preparation_area_t *q_area);

#endif /* UTILS_DB_QUERY_H */
/* vim: set sw=2 sts=2 et : */
