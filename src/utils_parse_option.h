/**
 * collectd - src/utils_parse_option.h
 * Copyright (C) 2008  Florian Forster
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

#ifndef UTILS_PARSE_OPTION
#define UTILS_PARSE_OPTION 1

int parse_string (char **ret_buffer, char **ret_string);
int parse_option (char **ret_buffer, char **ret_key, char **ret_value);

int escape_string (char *buffer, size_t buffer_size);

#endif /* UTILS_PARSE_OPTION */

/* vim: set sw=2 ts=8 tw=78 et : */
