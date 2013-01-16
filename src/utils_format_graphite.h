/**
 * collectd - src/utils_format_graphite.h
 * Copyright (C) 2012  Thomas Meson
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
 *   Thomas Meson <zllak at hycik.org>
 **/

#ifndef UTILS_FORMAT_GRAPHITE_H
#define UTILS_FORMAT_GRAPHITE_H 1

#include "collectd.h"
#include "plugin.h"

#define GRAPHITE_STORE_RATES        0x01
#define GRAPHITE_SEPARATE_INSTANCES 0x02
#define GRAPHITE_ALWAYS_APPEND_DS   0x04

int format_graphite (char *buffer,
    size_t buffer_size, const data_set_t *ds,
    const value_list_t *vl, const char *prefix,
    const char *postfix, const char escape_char,
    unsigned int flags);

#endif /* UTILS_FORMAT_GRAPHITE_H */
