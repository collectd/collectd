/**
 * collectd - src/utils_cache.h
 * Copyright (C) 2007  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#ifndef UTILS_CACHE_H
#define UTILS_CACHE_H 1

#include "plugin.h"

#define STATE_OKAY     0
#define STATE_WARNING  1
#define STATE_ERROR    2
#define STATE_MISSING 15

int uc_init (void);
int uc_check_timeout (void);
int uc_update (const data_set_t *ds, const value_list_t *vl);
int uc_get_rate_by_name (const char *name, gauge_t **ret_values, size_t *ret_values_num);
gauge_t *uc_get_rate (const data_set_t *ds, const value_list_t *vl);

int uc_get_names (char ***ret_names, cdtime_t **ret_times, size_t *ret_number);

int uc_get_state (const data_set_t *ds, const value_list_t *vl);
int uc_set_state (const data_set_t *ds, const value_list_t *vl, int state);
int uc_get_hits (const data_set_t *ds, const value_list_t *vl);
int uc_set_hits (const data_set_t *ds, const value_list_t *vl, int hits);
int uc_inc_hits (const data_set_t *ds, const value_list_t *vl, int step);

int uc_get_history (const data_set_t *ds, const value_list_t *vl,
    gauge_t *ret_history, size_t num_steps, size_t num_ds);
int uc_get_history_by_name (const char *name,
    gauge_t *ret_history, size_t num_steps, size_t num_ds);

/*
 * Meta data interface
 */
int uc_meta_data_exists (const value_list_t *vl, const char *key);
int uc_meta_data_delete (const value_list_t *vl, const char *key);

int uc_meta_data_add_string (const value_list_t *vl,
    const char *key,
    const char *value);
int uc_meta_data_add_signed_int (const value_list_t *vl,
    const char *key,
    int64_t value);
int uc_meta_data_add_unsigned_int (const value_list_t *vl,
    const char *key,
    uint64_t value);
int uc_meta_data_add_double (const value_list_t *vl,
    const char *key,
    double value);
int uc_meta_data_add_boolean (const value_list_t *vl,
    const char *key,
    _Bool value);

int uc_meta_data_get_string (const value_list_t *vl,
    const char *key,
    char **value);
int uc_meta_data_get_signed_int (const value_list_t *vl,
    const char *key,
    int64_t *value);
int uc_meta_data_get_unsigned_int (const value_list_t *vl,
    const char *key,
    uint64_t *value);
int uc_meta_data_get_double (const value_list_t *vl,
    const char *key,
    double *value);
int uc_meta_data_get_boolean (const value_list_t *vl,
    const char *key,
    _Bool *value);

/* vim: set shiftwidth=2 softtabstop=2 tabstop=8 : */
#endif /* !UTILS_CACHE_H */
