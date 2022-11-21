/**
 * collectd - src/utils_format_influxdb.h
 * Copyright (C) 2007-2009  Florian octo Forster
 * Copyright (C) 2009       Aman Gupta
 * Copyright (C) 2019       Carlos Peon Costa
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
 *   Florian octo Forster <octo at collectd.org>
 *   Aman Gupta <aman at tmm1.net>
 *   Carlos Peon Costa <carlospeon at gmail.com>
 *   multiple Server directives by:
 *   Paul (systemcrash) <newtwen thatfunny_at_symbol gmail.com>
 **/

#ifndef UTILS_FORMAT_INFLUXDB_H
#define UTILS_FORMAT_INFLUXDB_H 1

#include "collectd.h"

#include "plugin.h"

typedef enum {
  NS,
  US,
  MS,
} format_influxdb_time_precision_t;

int format_influxdb_value_list(char *buffer, int buffer_len,
                               const data_set_t *ds, const value_list_t *vl,
                               format_influxdb_time_precision_t time_precision,
                               bool store_rates, bool write_meta);

#endif /* UTILS_FORMAT_INFLUXDB_H */
