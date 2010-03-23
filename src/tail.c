/**
 * collectd - src/tail.c
 * Copyright (C) 2008  Florian octo Forster
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

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_tail_match.h"

/*
 *  <Plugin tail>
 *    <File "/var/log/exim4/mainlog">
 *	Instance "exim"
 *	<Match>
 *	  Regex "S=([1-9][0-9]*)"
 *	  ExcludeRegex "U=root.*S="
 *	  DSType "CounterAdd"
 *	  Type "ipt_bytes"
 *	  Instance "total"
 *	</Match>
 *    </File>
 *  </Plugin>
 */

struct ctail_config_match_s
{
  char *regex;
  char *excluderegex;
  int flags;
  char *type;
  char *type_instance;
};
typedef struct ctail_config_match_s ctail_config_match_t;

cu_tail_match_t **tail_match_list = NULL;
size_t tail_match_list_num = 0;

static int ctail_config_add_string (const char *name, char **dest, oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("tail plugin: `%s' needs exactly one string argument.", name);
    return (-1);
  }

  sfree (*dest);
  *dest = strdup (ci->values[0].value.string);
  if (*dest == NULL)
    return (-1);

  return (0);
} /* int ctail_config_add_string */

static int ctail_config_add_match_dstype (ctail_config_match_t *cm,
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("tail plugin: `DSType' needs exactly one string argument.");
    return (-1);
  }

  if (strncasecmp ("Gauge", ci->values[0].value.string, strlen ("Gauge")) == 0)
  {
    cm->flags = UTILS_MATCH_DS_TYPE_GAUGE;
    if (strcasecmp ("GaugeAverage", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_GAUGE_AVERAGE;
    else if (strcasecmp ("GaugeMin", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_GAUGE_MIN;
    else if (strcasecmp ("GaugeMax", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_GAUGE_MAX;
    else if (strcasecmp ("GaugeLast", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_GAUGE_LAST;
    else
      cm->flags = 0;
  }
  else if (strncasecmp ("Counter", ci->values[0].value.string, strlen ("Counter")) == 0)
  {
    cm->flags = UTILS_MATCH_DS_TYPE_COUNTER;
    if (strcasecmp ("CounterSet", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_COUNTER_SET;
    else if (strcasecmp ("CounterAdd", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_COUNTER_ADD;
    else if (strcasecmp ("CounterInc", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_COUNTER_INC;
    else
      cm->flags = 0;
  }
  else if (strncasecmp ("Derive", ci->values[0].value.string, strlen ("Derive")) == 0)
  {
    cm->flags = UTILS_MATCH_DS_TYPE_DERIVE;
    if (strcasecmp ("DeriveSet", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_DERIVE_SET;
    else if (strcasecmp ("DeriveAdd", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_DERIVE_ADD;
    else if (strcasecmp ("DeriveInc", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_DERIVE_INC;
    else
      cm->flags = 0;
  }
  else if (strncasecmp ("Absolute", ci->values[0].value.string, strlen ("Absolute")) == 0)
  {
    cm->flags = UTILS_MATCH_DS_TYPE_ABSOLUTE;
    if (strcasecmp ("AbsoluteSet", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_ABSOLUTE_SET;
    else
      cm->flags = 0;
  }
  else
  {
    cm->flags = 0;
  }

  if (cm->flags == 0)
  {
    WARNING ("tail plugin: `%s' is not a valid argument to `DSType'.",
	ci->values[0].value.string);
    return (-1);
  }

  return (0);
} /* int ctail_config_add_match_dstype */

static int ctail_config_add_match (cu_tail_match_t *tm,
    const char *plugin_instance, oconfig_item_t *ci)
{
  ctail_config_match_t cm;
  int status;
  int i;

  memset (&cm, '\0', sizeof (cm));

  if (ci->values_num != 0)
  {
    WARNING ("tail plugin: Ignoring arguments for the `Match' block.");
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Regex", option->key) == 0)
      status = ctail_config_add_string ("Regex", &cm.regex, option);
    else if (strcasecmp ("ExcludeRegex", option->key) == 0)
      status = ctail_config_add_string ("ExcludeRegex", &cm.excluderegex,
					option);
    else if (strcasecmp ("DSType", option->key) == 0)
      status = ctail_config_add_match_dstype (&cm, option);
    else if (strcasecmp ("Type", option->key) == 0)
      status = ctail_config_add_string ("Type", &cm.type, option);
    else if (strcasecmp ("Instance", option->key) == 0)
      status = ctail_config_add_string ("Instance", &cm.type_instance, option);
    else
    {
      WARNING ("tail plugin: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  while (status == 0)
  {
    if (cm.regex == NULL)
    {
      WARNING ("tail plugin: `Regex' missing in `Match' block.");
      status = -1;
      break;
    }

    if (cm.type == NULL)
    {
      WARNING ("tail plugin: `Type' missing in `Match' block.");
      status = -1;
      break;
    }

    if (cm.flags == 0)
    {
      WARNING ("tail plugin: `DSType' missing in `Match' block.");
      status = -1;
      break;
    }

    break;
  } /* while (status == 0) */

  if (status == 0)
  {
    status = tail_match_add_match_simple (tm, cm.regex, cm.excluderegex,
	cm.flags, "tail", plugin_instance, cm.type, cm.type_instance);

    if (status != 0)
    {
      ERROR ("tail plugin: tail_match_add_match_simple failed.");
    }
  }

  sfree (cm.regex);
  sfree (cm.excluderegex);
  sfree (cm.type);
  sfree (cm.type_instance);

  return (status);
} /* int ctail_config_add_match */

static int ctail_config_add_file (oconfig_item_t *ci)
{
  cu_tail_match_t *tm;
  char *plugin_instance = NULL;
  int num_matches = 0;
  int status;
  int i;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("tail plugin: `File' needs exactly one string argument.");
    return (-1);
  }

  tm = tail_match_create (ci->values[0].value.string);
  if (tm == NULL)
  {
    ERROR ("tail plugin: tail_match_create (%s) failed.",
	ci->values[0].value.string);
    return (-1);
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Match", option->key) == 0)
    {
      status = ctail_config_add_match (tm, plugin_instance, option);
      if (status == 0)
	num_matches++;
      /* Be mild with failed matches.. */
      status = 0;
    }
    else if (strcasecmp ("Instance", option->key) == 0)
      status = ctail_config_add_string ("Instance", &plugin_instance, option);
    else
    {
      WARNING ("tail plugin: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (num_matches == 0)
  {
    ERROR ("tail plugin: No (valid) matches found for file `%s'.",
	ci->values[0].value.string);
    tail_match_destroy (tm);
    return (-1);
  }
  else
  {
    cu_tail_match_t **temp;

    temp = (cu_tail_match_t **) realloc (tail_match_list,
	sizeof (cu_tail_match_t *) * (tail_match_list_num + 1));
    if (temp == NULL)
    {
      ERROR ("tail plugin: realloc failed.");
      tail_match_destroy (tm);
      return (-1);
    }

    tail_match_list = temp;
    tail_match_list[tail_match_list_num] = tm;
    tail_match_list_num++;
  }

  return (0);
} /* int ctail_config_add_file */

static int ctail_config (oconfig_item_t *ci)
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("File", option->key) == 0)
      ctail_config_add_file (option);
    else
    {
      WARNING ("tail plugin: Option `%s' not allowed here.", option->key);
    }
  } /* for (i = 0; i < ci->children_num; i++) */

  return (0);
} /* int ctail_config */

static int ctail_init (void)
{
  if (tail_match_list_num == 0)
  {
    WARNING ("tail plugin: File list is empty. Returning an error.");
    return (-1);
  }

  return (0);
} /* int ctail_init */

static int ctail_read (void)
{
  int success = 0;
  size_t i;

  for (i = 0; i < tail_match_list_num; i++)
  {
    int status;

    status = tail_match_read (tail_match_list[i]);
    if (status != 0)
    {
      ERROR ("tail plugin: tail_match_read[%zu] failed.", i);
    }
    else
    {
      success++;
    }
  }

  if (success == 0)
    return (-1);
  return (0);
} /* int ctail_read */

static int ctail_shutdown (void)
{
  size_t i;

  for (i = 0; i < tail_match_list_num; i++)
  {
    tail_match_destroy (tail_match_list[i]);
    tail_match_list[i] = NULL;
  }
  sfree (tail_match_list);
  tail_match_list_num = 0;

  return (0);
} /* int ctail_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("tail", ctail_config);
  plugin_register_init ("tail", ctail_init);
  plugin_register_read ("tail", ctail_read);
  plugin_register_shutdown ("tail", ctail_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 : */
