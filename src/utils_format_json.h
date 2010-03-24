/**
 * collectd - src/utils_format_json.c
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

#ifndef UTILS_FORMAT_JSON_H
#define UTILS_FORMAT_JSON_H 1

#include "collectd.h"
#include "plugin.h"

int format_json_initialize (char *buffer,
    size_t *ret_buffer_fill, size_t *ret_buffer_free);
int format_json_value_list (char *buffer,
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    const data_set_t *ds, const value_list_t *vl, int store_rates);
int format_json_finalize (char *buffer,
    size_t *ret_buffer_fill, size_t *ret_buffer_free);

#endif /* UTILS_FORMAT_JSON_H */
