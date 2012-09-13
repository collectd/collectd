/**
 * collectd - src/match_value.c
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
 * Authors:
 *   Florian Forster <octo at verplant.org>
 **/

/*
 * This module allows to filter and rewrite value lists based on
 * Perl-compatible regular expressions.
 */

#include "collectd.h"
#include "common.h"
#include "utils_cache.h"
#include "filter_chain.h"

#define SATISFY_ALL 0
#define SATISFY_ANY 1

/*
 * private data types
 */
struct mv_match_s;
typedef struct mv_match_s mv_match_t;
struct mv_match_s
{
  gauge_t min;
  gauge_t max;
  int invert;
  int satisfy;

  char **data_sources;
  size_t data_sources_num;
};

/*
 * internal helper functions
 */
static void mv_free_match (mv_match_t *m) /* {{{ */
{
  int i;
  
  if (m == NULL)
    return;

  if (m->data_sources != NULL)
  {
    for (i = 0; i < m->data_sources_num; ++i)
      free(m->data_sources[i]);
    free(m->data_sources);
  }
  
  free (m);
} /* }}} void mv_free_match */

static int mv_config_add_satisfy (mv_match_t *m, /* {{{ */
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    ERROR ("`value' match: `%s' needs exactly one string argument.",
        ci->key);
    return (-1);
  }

  if (strcasecmp ("All", ci->values[0].value.string) == 0)
    m->satisfy = SATISFY_ALL;
  else if (strcasecmp ("Any", ci->values[0].value.string) == 0)
    m->satisfy = SATISFY_ANY;
  else
  {
    ERROR ("`value' match: Passing `%s' to the `%s' option is invalid. "
        "The argument must either be `All' or `Any'.",
        ci->values[0].value.string, ci->key);
    return (-1);
  }

  return (0);
} /* }}} int mv_config_add_satisfy */

static int mv_config_add_data_source (mv_match_t *m, /* {{{ */
    oconfig_item_t *ci)
{
  size_t new_data_sources_num;
  char **temp;
  int i;

  /* Check number of arbuments. */
  if (ci->values_num < 1)
  {
    ERROR ("`value' match: `%s' needs at least one argument.",
        ci->key);
    return (-1);
  }

  /* Check type of arguments */
  for (i = 0; i < ci->values_num; i++)
  {
    if (ci->values[i].type == OCONFIG_TYPE_STRING)
      continue;

    ERROR ("`value' match: `%s' accepts only string arguments "
        "(argument %i is a %s).",
        ci->key, i + 1,
        (ci->values[i].type == OCONFIG_TYPE_BOOLEAN)
        ? "truth value" : "number");
    return (-1);
  }

  /* Allocate space for the char pointers */
  new_data_sources_num = m->data_sources_num + ((size_t) ci->values_num);
  temp = (char **) realloc (m->data_sources,
      new_data_sources_num * sizeof (char *));
  if (temp == NULL)
  {
    ERROR ("`value' match: realloc failed.");
    return (-1);
  }
  m->data_sources = temp;

  /* Copy the strings, allocating memory as needed. */
  for (i = 0; i < ci->values_num; i++)
  {
    size_t j;

    /* If we get here, there better be memory for us to write to. */
    assert (m->data_sources_num < new_data_sources_num);

    j = m->data_sources_num;
    m->data_sources[j] = sstrdup (ci->values[i].value.string);
    if (m->data_sources[j] == NULL)
    {
      ERROR ("`value' match: sstrdup failed.");
      continue;
    }
    m->data_sources_num++;
  }

  return (0);
} /* }}} int mv_config_add_data_source */

static int mv_config_add_gauge (gauge_t *ret_value, /* {{{ */
    oconfig_item_t *ci)
{

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    ERROR ("`value' match: `%s' needs exactly one numeric argument.",
        ci->key);
    return (-1);
  }

  *ret_value = ci->values[0].value.number;

  return (0);
} /* }}} int mv_config_add_gauge */

static int mv_config_add_boolean (int *ret_value, /* {{{ */
    oconfig_item_t *ci)
{

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    ERROR ("`value' match: `%s' needs exactly one boolean argument.",
        ci->key);
    return (-1);
  }

  if (ci->values[0].value.boolean)
    *ret_value = 1;
  else
    *ret_value = 0;

  return (0);
} /* }}} int mv_config_add_boolean */

static int mv_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  mv_match_t *m;
  int status;
  int i;

  m = (mv_match_t *) malloc (sizeof (*m));
  if (m == NULL)
  {
    ERROR ("mv_create: malloc failed.");
    return (-ENOMEM);
  }
  memset (m, 0, sizeof (*m));

  m->min = NAN;
  m->max = NAN;
  m->invert = 0;
  m->satisfy = SATISFY_ALL;
  m->data_sources = NULL;
  m->data_sources_num = 0;

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Min", child->key) == 0)
      status = mv_config_add_gauge (&m->min, child);
    else if (strcasecmp ("Max", child->key) == 0)
      status = mv_config_add_gauge (&m->max, child);
    else if (strcasecmp ("Invert", child->key) == 0)
      status = mv_config_add_boolean (&m->invert, child);
    else if (strcasecmp ("Satisfy", child->key) == 0)
      status = mv_config_add_satisfy (m, child);
    else if (strcasecmp ("DataSource", child->key) == 0)
      status = mv_config_add_data_source (m, child);
    else
    {
      ERROR ("`value' match: The `%s' configuration option is not "
          "understood and will be ignored.", child->key);
      status = 0;
    }

    if (status != 0)
      break;
  }

  /* Additional sanity-checking */
  while (status == 0)
  {
    if (isnan (m->min) && isnan (m->max))
    {
      ERROR ("`value' match: Neither minimum nor maximum are defined. "
          "This match will be ignored.");
      status = -1;
    }

    break;
  }

  if (status != 0)
  {
    mv_free_match (m);
    return (status);
  }

  *user_data = m;
  return (0);
} /* }}} int mv_create */

static int mv_destroy (void **user_data) /* {{{ */
{
  if ((user_data != NULL) && (*user_data != NULL))
    mv_free_match (*user_data);
  return (0);
} /* }}} int mv_destroy */

static int mv_match (const data_set_t *ds, const value_list_t *vl, /* {{{ */
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  mv_match_t *m;
  gauge_t *values;
  int status;
  int i;

  if ((user_data == NULL) || (*user_data == NULL))
    return (-1);

  m = *user_data;

  values = uc_get_rate (ds, vl);
  if (values == NULL)
  {
    ERROR ("`value' match: Retrieving the current rate from the cache "
        "failed.");
    return (-1);
  }

  status = FC_MATCH_NO_MATCH;

  for (i = 0; i < ds->ds_num; i++)
  {
    int value_matches = 0;

    /* Check if this data source is relevant. */
    if (m->data_sources != NULL)
    {
      size_t j;

      for (j = 0; j < m->data_sources_num; j++)
        if (strcasecmp (ds->ds[i].name, m->data_sources[j]) == 0)
          break;

      /* No match, ignore this data source. */
      if (j >=  m->data_sources_num)
        continue;
    }

    DEBUG ("`value' match: current = %g; min = %g; max = %g; invert = %s;",
        values[i], m->min, m->max,
        m->invert ? "true" : "false");

    if ((!isnan (m->min) && (values[i] < m->min))
        || (!isnan (m->max) && (values[i] > m->max)))
      value_matches = 0;
    else
      value_matches = 1;

    if (m->invert)
    {
      if (value_matches)
        value_matches = 0;
      else
        value_matches = 1;
    }

    if (value_matches != 0)
    {
      status = FC_MATCH_MATCHES;
      if (m->satisfy == SATISFY_ANY)
        break;
    }
    else if (value_matches == 0)
    {
      status = FC_MATCH_NO_MATCH;
      if (m->satisfy == SATISFY_ALL)
        break;
    }
  } /* for (i = 0; i < ds->ds_num; i++) */

  free (values);
  return (status);
} /* }}} int mv_match */

void module_register (void)
{
  match_proc_t mproc;

  memset (&mproc, 0, sizeof (mproc));
  mproc.create  = mv_create;
  mproc.destroy = mv_destroy;
  mproc.match   = mv_match;
  fc_register_match ("value", mproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */

