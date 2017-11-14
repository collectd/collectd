/**
 * collectd - src/utils_format_atsd.h
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
 **/

#ifndef UTILS_FORMAT_ATSD_H
#define UTILS_FORMAT_ATSD_H 1

#include "plugin.h"
#include "collectd.h"

#define MAX_DERIVED_SERIES 2
#define MAX_VALUE_LEN 64

struct format_info_s {
  char *buffer;
  size_t buffer_len;

  char *entity;
  char *prefix;

  size_t index;
  const data_set_t *ds;
  const value_list_t *vl;
  gauge_t *rates;
};
typedef struct format_info_s format_info_t;

char *escape_atsd_string(char *dst_buf, const char *src_buf, size_t n);

double get_value(format_info_t *format);

int format_value(char *ret, size_t ret_len, format_info_t *format);

int format_entity(char *ret, const int ret_len, const char *entity,
                  const char *host, _Bool short_hostname);

int format_atsd_command(format_info_t *format);

#endif // UTILS_FORMAT_ATSD_H
