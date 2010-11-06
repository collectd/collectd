/**
 * collectd - src/utils_match.c
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

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include "utils_match.h"

#include <regex.h>

#define UTILS_MATCH_FLAGS_FREE_USER_DATA 0x01
#define UTILS_MATCH_FLAGS_EXCLUDE_REGEX 0x02

struct cu_match_s
{
  regex_t regex;
  regex_t excluderegex;
  int flags;

  int (*callback) (const char *str, char * const *matches, size_t matches_num,
      void *user_data);
  void *user_data;
};

/*
 * Private functions
 */
static char *match_substr (const char *str, int begin, int end)
{
  char *ret;
  size_t ret_len;

  if ((begin < 0) || (end < 0) || (begin >= end))
    return (NULL);
  if ((size_t) end > (strlen (str) + 1))
  {
    ERROR ("utils_match: match_substr: `end' points after end of string.");
    return (NULL);
  }

  ret_len = end - begin;
  ret = (char *) malloc (sizeof (char) * (ret_len + 1));
  if (ret == NULL)
  {
    ERROR ("utils_match: match_substr: malloc failed.");
    return (NULL);
  }

  sstrncpy (ret, str + begin, ret_len + 1);
  return (ret);
} /* char *match_substr */

static int default_callback (const char __attribute__((unused)) *str,
    char * const *matches, size_t matches_num, void *user_data)
{
  cu_match_value_t *data = (cu_match_value_t *) user_data;

  if (data->ds_type & UTILS_MATCH_DS_TYPE_GAUGE)
  {
    gauge_t value;
    char *endptr = NULL;

    if (matches_num < 2)
      return (-1);

    value = (gauge_t) strtod (matches[1], &endptr);
    if (matches[1] == endptr)
      return (-1);

    if ((data->values_num == 0)
	|| (data->ds_type & UTILS_MATCH_CF_GAUGE_LAST))
    {
      data->value.gauge = value;
    }
    else if (data->ds_type & UTILS_MATCH_CF_GAUGE_AVERAGE)
    {
      double f = ((double) data->values_num)
	/ ((double) (data->values_num + 1));
      data->value.gauge = (data->value.gauge * f) + (value * (1.0 - f));
    }
    else if (data->ds_type & UTILS_MATCH_CF_GAUGE_MIN)
    {
      if (data->value.gauge > value)
	data->value.gauge = value;
    }
    else if (data->ds_type & UTILS_MATCH_CF_GAUGE_MAX)
    {
      if (data->value.gauge < value)
	data->value.gauge = value;
    }
    else
    {
      ERROR ("utils_match: default_callback: obj->ds_type is invalid!");
      return (-1);
    }

    data->values_num++;
  }
  else if (data->ds_type & UTILS_MATCH_DS_TYPE_COUNTER)
  {
    counter_t value;
    char *endptr = NULL;

    if (data->ds_type & UTILS_MATCH_CF_COUNTER_INC)
    {
      data->value.counter++;
      data->values_num++;
      return (0);
    }

    if (matches_num < 2)
      return (-1);

    value = (counter_t) strtoull (matches[1], &endptr, 0);
    if (matches[1] == endptr)
      return (-1);

    if (data->ds_type & UTILS_MATCH_CF_COUNTER_SET)
      data->value.counter = value;
    else if (data->ds_type & UTILS_MATCH_CF_COUNTER_ADD)
      data->value.counter += value;
    else
    {
      ERROR ("utils_match: default_callback: obj->ds_type is invalid!");
      return (-1);
    }

    data->values_num++;
  }
  else if (data->ds_type & UTILS_MATCH_DS_TYPE_DERIVE)
  {
    derive_t value;
    char *endptr = NULL;

    if (data->ds_type & UTILS_MATCH_CF_DERIVE_INC)
    {
      data->value.counter++;
      data->values_num++;
      return (0);
    }

    if (matches_num < 2)
      return (-1);

    value = (derive_t) strtoll (matches[1], &endptr, 0);
    if (matches[1] == endptr)
      return (-1);

    if (data->ds_type & UTILS_MATCH_CF_DERIVE_SET)
      data->value.derive = value;
    else if (data->ds_type & UTILS_MATCH_CF_DERIVE_ADD)
      data->value.derive += value;
    else
    {
      ERROR ("utils_match: default_callback: obj->ds_type is invalid!");
      return (-1);
    }

    data->values_num++;
  }
  else if (data->ds_type & UTILS_MATCH_DS_TYPE_ABSOLUTE)
  {
    absolute_t value;
    char *endptr = NULL;

    if (matches_num < 2)
      return (-1);

    value = (absolute_t) strtoull (matches[1], &endptr, 0);
    if (matches[1] == endptr)
      return (-1);

    if (data->ds_type & UTILS_MATCH_CF_ABSOLUTE_SET)
      data->value.absolute = value;
    else
    {
      ERROR ("utils_match: default_callback: obj->ds_type is invalid!");
      return (-1);
    }

    data->values_num++;
  }
  else
  {
    ERROR ("utils_match: default_callback: obj->ds_type is invalid!");
    return (-1);
  }

  return (0);
} /* int default_callback */

/*
 * Public functions
 */
cu_match_t *match_create_callback (const char *regex, const char *excluderegex,
		int (*callback) (const char *str,
		  char * const *matches, size_t matches_num, void *user_data),
		void *user_data)
{
  cu_match_t *obj;
  int status;

  DEBUG ("utils_match: match_create_callback: regex = %s, excluderegex = %s",
	 regex, excluderegex);

  obj = (cu_match_t *) malloc (sizeof (cu_match_t));
  if (obj == NULL)
    return (NULL);
  memset (obj, '\0', sizeof (cu_match_t));

  status = regcomp (&obj->regex, regex, REG_EXTENDED | REG_NEWLINE);
  if (status != 0)
  {
    ERROR ("Compiling the regular expression \"%s\" failed.", regex);
    sfree (obj);
    return (NULL);
  }

  if (excluderegex && strcmp(excluderegex, "") != 0) {
    status = regcomp (&obj->excluderegex, excluderegex, REG_EXTENDED);
    if (status != 0)
    {
	ERROR ("Compiling the excluding regular expression \"%s\" failed.",
	       excluderegex);
	sfree (obj);
	return (NULL);
    }
    obj->flags |= UTILS_MATCH_FLAGS_EXCLUDE_REGEX;
  }

  obj->callback = callback;
  obj->user_data = user_data;

  return (obj);
} /* cu_match_t *match_create_callback */

cu_match_t *match_create_simple (const char *regex,
				 const char *excluderegex, int match_ds_type)
{
  cu_match_value_t *user_data;
  cu_match_t *obj;

  user_data = (cu_match_value_t *) malloc (sizeof (cu_match_value_t));
  if (user_data == NULL)
    return (NULL);
  memset (user_data, '\0', sizeof (cu_match_value_t));
  user_data->ds_type = match_ds_type;

  obj = match_create_callback (regex, excluderegex,
			       default_callback, user_data);
  if (obj == NULL)
  {
    sfree (user_data);
    return (NULL);
  }

  obj->flags |= UTILS_MATCH_FLAGS_FREE_USER_DATA;

  return (obj);
} /* cu_match_t *match_create_simple */

void match_destroy (cu_match_t *obj)
{
  if (obj == NULL)
    return;

  if (obj->flags & UTILS_MATCH_FLAGS_FREE_USER_DATA)
  {
    sfree (obj->user_data);
  }

  sfree (obj);
} /* void match_destroy */

int match_apply (cu_match_t *obj, const char *str)
{
  int status;
  regmatch_t re_match[32];
  char *matches[32];
  size_t matches_num;
  size_t i;

  if ((obj == NULL) || (str == NULL))
    return (-1);

  if (obj->flags & UTILS_MATCH_FLAGS_EXCLUDE_REGEX) {
    status = regexec (&obj->excluderegex, str,
		      STATIC_ARRAY_SIZE (re_match), re_match,
		      /* eflags = */ 0);
    /* Regex did match, so exclude this line */
    if (status == 0) {
      DEBUG("ExludeRegex matched, don't count that line\n");
      return (0);
    }
  }

  status = regexec (&obj->regex, str,
      STATIC_ARRAY_SIZE (re_match), re_match,
      /* eflags = */ 0);

  /* Regex did not match */
  if (status != 0)
    return (0);

  memset (matches, '\0', sizeof (matches));
  for (matches_num = 0; matches_num < STATIC_ARRAY_SIZE (matches); matches_num++)
  {
    if ((re_match[matches_num].rm_so < 0)
	|| (re_match[matches_num].rm_eo < 0))
      break;

    matches[matches_num] = match_substr (str,
	re_match[matches_num].rm_so, re_match[matches_num].rm_eo);
    if (matches[matches_num] == NULL)
    {
      status = -1;
      break;
    }
  }

  if (status != 0)
  {
    ERROR ("utils_match: match_apply: match_substr failed.");
  }
  else
  {
    status = obj->callback (str, matches, matches_num, obj->user_data);
    if (status != 0)
    {
      ERROR ("utils_match: match_apply: callback failed.");
    }
  }

  for (i = 0; i < matches_num; i++)
  {
    sfree (matches[i]);
  }

  return (status);
} /* int match_apply */

void *match_get_user_data (cu_match_t *obj)
{
  if (obj == NULL)
    return (NULL);
  return (obj->user_data);
} /* void *match_get_user_data */

/* vim: set sw=2 sts=2 ts=8 : */
