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

struct cu_match_s
{
  regex_t regex;
  int flags;

  int (*callback) (const char *str, void *user_data);
  void *user_data;
};

/*
 * Private functions
 */
static int default_callback (const char *str, void *user_data)
{
  cu_match_value_t *data = (cu_match_value_t *) user_data;

  if (data->ds_type & UTILS_MATCH_DS_TYPE_GAUGE)
  {
    gauge_t value;
    char *endptr = NULL;

    value = strtod (str, &endptr);
    if (str == endptr)
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

    value = strtoll (str, &endptr, 0);
    if (str == endptr)
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
cu_match_t *match_create_callback (const char *regex,
		int (*callback) (const char *str, void *user_data),
		void *user_data)
{
  cu_match_t *obj;
  int status;

  obj = (cu_match_t *) malloc (sizeof (cu_match_t));
  if (obj == NULL)
    return (NULL);
  memset (obj, '\0', sizeof (cu_match_t));

  status = regcomp (&obj->regex, regex, REG_EXTENDED);
  if (status != 0)
  {
    ERROR ("Compiling the regular expression \"%s\" failed.", regex);
    sfree (obj);
    return (NULL);
  }

  obj->callback = callback;
  obj->user_data = user_data;

  return (obj);
} /* cu_match_t *match_create_callback */

cu_match_t *match_create_simple (const char *regex, int match_ds_type)
{
  cu_match_value_t *user_data;
  cu_match_t *obj;

  user_data = (cu_match_value_t *) malloc (sizeof (cu_match_value_t));
  if (user_data == NULL)
    return (NULL);
  memset (user_data, '\0', sizeof (cu_match_value_t));
  user_data->ds_type = match_ds_type;

  obj = match_create_callback (regex, default_callback, user_data);
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
  regmatch_t re_match[2];
  char *sub_match;
  size_t sub_match_len;

  if ((obj == NULL) || (str == NULL))
    return (-1);

  re_match[0].rm_so = -1;
  re_match[0].rm_eo = -1;
  re_match[1].rm_so = -1;
  re_match[1].rm_eo = -1;
  status = regexec (&obj->regex, str, /* nmatch = */ 2, re_match,
      /* eflags = */ 0);

  /* Regex did not match */
  if (status != 0)
    return (0);

  /* re_match[0] is the location of the entire match.
   * re_match[1] is the location of the sub-match. */
  if (re_match[1].rm_so < 0)
  {
    status = obj->callback (str, obj->user_data);
    return (status);
  }

  assert (re_match[1].rm_so < re_match[1].rm_eo);
  sub_match_len = (size_t) (re_match[1].rm_eo - re_match[1].rm_so);
  sub_match = (char *) malloc (sizeof (char) * (sub_match_len + 1));
  if (sub_match == NULL)
  {
    ERROR ("malloc failed.");
    return (-1);
  }
  sstrncpy (sub_match, str + re_match[1].rm_so, sub_match_len + 1);

  DEBUG ("utils_match: match_apply: Dispatching substring \"%s\" to "
      "callback.", sub_match);
  status = obj->callback (sub_match, obj->user_data);

  sfree (sub_match);

  return (status);
} /* int match_apply */

void *match_get_user_data (cu_match_t *obj)
{
  if (obj == NULL)
    return (NULL);
  return (obj->user_data);
} /* void *match_get_user_data */

/* vim: set sw=2 sts=2 ts=8 : */
