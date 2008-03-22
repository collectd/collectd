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

int uc_get_state (const data_set_t *ds, const value_list_t *vl);
int uc_set_state (const data_set_t *ds, const value_list_t *vl, int state);

/* vim: set shiftwidth=2 softtabstop=2 tabstop=8 : */
#endif /* !UTILS_CACHE_H */
