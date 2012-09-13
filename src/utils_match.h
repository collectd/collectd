/**
 * collectd - src/utils_match.h
 * Copyright (C) 2008  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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

#ifndef UTILS_MATCH_H
#define UTILS_MATCH_H 1

#include "plugin.h"

/*
 * Defines
 */
#define UTILS_MATCH_DS_TYPE_GAUGE    0x10
#define UTILS_MATCH_DS_TYPE_COUNTER  0x20
#define UTILS_MATCH_DS_TYPE_DERIVE   0x40
#define UTILS_MATCH_DS_TYPE_ABSOLUTE 0x80

#define UTILS_MATCH_CF_GAUGE_AVERAGE 0x01
#define UTILS_MATCH_CF_GAUGE_MIN     0x02
#define UTILS_MATCH_CF_GAUGE_MAX     0x04
#define UTILS_MATCH_CF_GAUGE_LAST    0x08

#define UTILS_MATCH_CF_COUNTER_SET   0x01
#define UTILS_MATCH_CF_COUNTER_ADD   0x02
#define UTILS_MATCH_CF_COUNTER_INC   0x04

#define UTILS_MATCH_CF_DERIVE_SET   0x01
#define UTILS_MATCH_CF_DERIVE_ADD   0x02
#define UTILS_MATCH_CF_DERIVE_INC   0x04

#define UTILS_MATCH_CF_ABSOLUTE_SET   0x01
#define UTILS_MATCH_CF_ABSOLUTE_ADD   0x02
#define UTILS_MATCH_CF_ABSOLUTE_INC   0x04

/*
 * Data types
 */
struct cu_match_s;
typedef struct cu_match_s cu_match_t;

struct cu_match_value_s
{
  int ds_type;
  value_t value;
  unsigned int values_num;
};
typedef struct cu_match_value_s cu_match_value_t;

/*
 * Prototypes
 */
/*
 * NAME
 *  match_create_callback
 *
 * DESCRIPTION
 *  Creates a new `cu_match_t' object which will use the regular expression
 *  `regex' to match lines, see the `match_apply' method below. If the line
 *  matches, the callback passed in `callback' will be called along with the
 *  pointer `user_pointer'.
 *  The string that's passed to the callback depends on the regular expression:
 *  If the regular expression includes a sub-match, i. e. something like
 *    "value=([0-9][0-9]*)"
 *  then only the submatch (the part in the parenthesis) will be passed to the
 *  callback. If there is no submatch, then the entire string is passed to the
 *  callback.
 *  The optional `excluderegex' allows to exclude the line from the match, if
 *  the excluderegex matches.
 */
cu_match_t *match_create_callback (const char *regex, const char *excluderegex,
		int (*callback) (const char *str,
		  char * const *matches, size_t matches_num, void *user_data),
		void *user_data);

/*
 * NAME
 *  match_create_simple
 *
 * DESCRIPTION
 *  Creates a new `cu_match_t' with a default callback. The user data for that
 *  default callback will be a `cu_match_value_t' structure, with
 *  `ds_type' copied to the structure. The default callback will handle the
 *  string as containing a number (see strtoll(3) and strtod(3)) and store that
 *  number in the `value' member. How that is done depends on `ds_type':
 *
 *  UTILS_MATCH_DS_TYPE_GAUGE
 *    The function will search for a floating point number in the string and
 *    store it in value.gauge.
 *  UTILS_MATCH_DS_TYPE_COUNTER_SET
 *    The function will search for an integer in the string and store it in
 *    value.counter.
 *  UTILS_MATCH_DS_TYPE_COUNTER_ADD
 *    The function will search for an integer in the string and add it to the
 *    value in value.counter.
 *  UTILS_MATCH_DS_TYPE_COUNTER_INC
 *    The function will not search for anything in the string and increase
 *    value.counter by one.
 */
cu_match_t *match_create_simple (const char *regex,
				 const char *excluderegex, int ds_type);

/*
 * NAME
 *  match_destroy
 *
 * DESCRIPTION
 *  Destroys the object and frees all internal resources.
 */
void match_destroy (cu_match_t *obj);

/*
 * NAME
 *  match_apply
 *
 * DESCRIPTION
 *  Tries to match the string `str' with the regular expression of `obj'. If
 *  the string matches, calls the callback in `obj' with the (sub-)match.
 *
 *  The user_data pointer passed to `match_create_callback' is NOT freed
 *  automatically. The `cu_match_value_t' structure allocated by
 *  `match_create_callback' is freed automatically.
 */
int match_apply (cu_match_t *obj, const char *str);

/*
 * NAME
 *  match_get_user_data
 *
 * DESCRIPTION
 *  Returns the pointer passed to `match_create_callback' or a pointer to the
 *  `cu_match_value_t' structure allocated by `match_create_simple'.
 */
void *match_get_user_data (cu_match_t *obj);

#endif /* UTILS_MATCH_H */

/* vim: set sw=2 sts=2 ts=8 : */
