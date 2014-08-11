/*
 * collectd - src/utils_tail_match.c
 * Copyright (C) 2007-2008  C-Ware, Inc.
 * Copyright (C) 2008       Florian Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author:
 *   Luke Heberling <lukeh at c-ware.com>
 *   Florian Forster <octo at collectd.org>
 *
 * Description:
 *   Encapsulates useful code to plugins which must parse a log file.
 */

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_match.h"
#include "utils_tail.h"
#include "utils_tail_match.h"

struct cu_tail_match_simple_s
{
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
  cdtime_t interval;
};
typedef struct cu_tail_match_simple_s cu_tail_match_simple_t;

struct cu_tail_match_match_s
{
  cu_match_t *match;
  void *user_data;
  int (*submit) (cu_match_t *match, void *user_data);
  void (*free) (void *user_data);
};
typedef struct cu_tail_match_match_s cu_tail_match_match_t;

struct cu_tail_match_s
{
  int flags;
  cu_tail_t *tail;

  cdtime_t interval;
  cu_tail_match_match_t *matches;
  size_t matches_num;
};

/*
 * Private functions
 */
static int simple_submit_match (cu_match_t *match, void *user_data)
{
  cu_tail_match_simple_t *data = (cu_tail_match_simple_t *) user_data;
  cu_match_value_t *match_value;
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[1];

  match_value = (cu_match_value_t *) match_get_user_data (match);
  if (match_value == NULL)
    return (-1);

  if ((match_value->ds_type & UTILS_MATCH_DS_TYPE_GAUGE)
      && (match_value->values_num == 0))
    values[0].gauge = NAN;
  else
    values[0] = match_value->value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, data->plugin, sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, data->plugin_instance,
      sizeof (vl.plugin_instance));
  sstrncpy (vl.type, data->type, sizeof (vl.type));
  sstrncpy (vl.type_instance, data->type_instance,
      sizeof (vl.type_instance));

  vl.interval = data->interval;
  plugin_dispatch_values (&vl);

  if (match_value->ds_type & UTILS_MATCH_DS_TYPE_GAUGE)
  {
    match_value->value.gauge = NAN;
    match_value->values_num = 0;
  }

  return (0);
} /* int simple_submit_match */

static int tail_callback (void *data, char *buf,
    int __attribute__((unused)) buflen)
{
  cu_tail_match_t *obj = (cu_tail_match_t *) data;
  size_t i;

  for (i = 0; i < obj->matches_num; i++)
    match_apply (obj->matches[i].match, buf);

  return (0);
} /* int tail_callback */

/*
 * Public functions
 */
cu_tail_match_t *tail_match_create (const char *filename)
{
  cu_tail_match_t *obj;

  obj = (cu_tail_match_t *) malloc (sizeof (cu_tail_match_t));
  if (obj == NULL)
    return (NULL);
  memset (obj, '\0', sizeof (cu_tail_match_t));

  obj->tail = cu_tail_create (filename);
  if (obj->tail == NULL)
  {
    sfree (obj);
    return (NULL);
  }

  return (obj);
} /* cu_tail_match_t *tail_match_create */

void tail_match_destroy (cu_tail_match_t *obj)
{
  size_t i;

  if (obj == NULL)
    return;

  if (obj->tail != NULL)
  {
    cu_tail_destroy (obj->tail);
    obj->tail = NULL;
  }

  for (i = 0; i < obj->matches_num; i++)
  {
    cu_tail_match_match_t *match = obj->matches + i;
    if (match->match != NULL)
    {
      match_destroy (match->match);
      match->match = NULL;
    }

    if ((match->user_data != NULL)
	&& (match->free != NULL))
      (*match->free) (match->user_data);
    match->user_data = NULL;
  }

  sfree (obj->matches);
  sfree (obj);
} /* void tail_match_destroy */

int tail_match_add_match (cu_tail_match_t *obj, cu_match_t *match,
    int (*submit_match) (cu_match_t *match, void *user_data),
    void *user_data,
    void (*free_user_data) (void *user_data))
{
  cu_tail_match_match_t *temp;

  temp = (cu_tail_match_match_t *) realloc (obj->matches,
      sizeof (cu_tail_match_match_t) * (obj->matches_num + 1));
  if (temp == NULL)
    return (-1);

  obj->matches = temp;
  obj->matches_num++;

  DEBUG ("tail_match_add_match interval %lf", CDTIME_T_TO_DOUBLE(((cu_tail_match_simple_t *)user_data)->interval));
  temp = obj->matches + (obj->matches_num - 1);

  temp->match = match;
  temp->user_data = user_data;
  temp->submit = submit_match;
  temp->free = free_user_data;

  return (0);
} /* int tail_match_add_match */

int tail_match_add_match_simple (cu_tail_match_t *obj,
    const char *regex, const char *excluderegex, int ds_type,
    const char *plugin, const char *plugin_instance,
    const char *type, const char *type_instance, const cdtime_t interval)
{
  cu_match_t *match;
  cu_tail_match_simple_t *user_data;
  int status;

  match = match_create_simple (regex, excluderegex, ds_type);
  if (match == NULL)
    return (-1);

  user_data = (cu_tail_match_simple_t *) malloc (sizeof (cu_tail_match_simple_t));
  if (user_data == NULL)
  {
    match_destroy (match);
    return (-1);
  }
  memset (user_data, '\0', sizeof (cu_tail_match_simple_t));

  sstrncpy (user_data->plugin, plugin, sizeof (user_data->plugin));
  if (plugin_instance != NULL)
    sstrncpy (user_data->plugin_instance, plugin_instance,
	sizeof (user_data->plugin_instance));

  sstrncpy (user_data->type, type, sizeof (user_data->type));
  if (type_instance != NULL)
    sstrncpy (user_data->type_instance, type_instance,
	sizeof (user_data->type_instance));

  user_data->interval = interval;

  status = tail_match_add_match (obj, match, simple_submit_match,
      user_data, free);

  if (status != 0)
  {
    match_destroy (match);
    sfree (user_data);
  }

  return (status);
} /* int tail_match_add_match_simple */

int tail_match_read (cu_tail_match_t *obj)
{
  char buffer[4096];
  int status;
  size_t i;

  status = cu_tail_read (obj->tail, buffer, sizeof (buffer), tail_callback,
      (void *) obj);
  if (status != 0)
  {
    ERROR ("tail_match: cu_tail_read failed.");
    return (status);
  }

  for (i = 0; i < obj->matches_num; i++)
  {
    cu_tail_match_match_t *lt_match = obj->matches + i;

    if (lt_match->submit == NULL)
      continue;

    (*lt_match->submit) (lt_match->match, lt_match->user_data);
  }

  return (0);
} /* int tail_match_read */

/* vim: set sw=2 sts=2 ts=8 : */
