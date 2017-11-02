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

size_t strlcat(char *dst, const char *src, size_t siz);

int format_value(char *ret, size_t ret_len, int ds_index, const data_set_t *ds,
                 const value_list_t *vl, const gauge_t *rates);

/*
 *	Check if entity is not empty and has no spaces.
 *	Otherwise try to use host information from value list as entity.
 *	If host is 'localhost' or started with 'localhost.'
 *	use the output of `gethostname()` command.
 *	If short_hostname option is enabled,
 *	convert host from fully qualified domain name.
 *	Resulted entity returned in first argument.
 */
void check_entity(char *ret, const int ret_len, const char *entity,
                  const char *host, _Bool short_hostname);
#endif // UTILS_FORMAT_ATSD_H
