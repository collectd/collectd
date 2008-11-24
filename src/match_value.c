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
#include "utils_cache.h"
#include "filter_chain.h"

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
};

/*
 * internal helper functions
 */
static void mv_free_match (mv_match_t *m) /* {{{ */
{
  if (m == NULL)
    return;

  free (m);
} /* }}} void mv_free_match */

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
    notification_meta_t **meta, void **user_data)
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

  status = FC_MATCH_MATCHES;
  for (i = 0; i < ds->ds_num; i++)
  {
    DEBUG ("`value' match: current = %g; min = %g; max = %g; invert = %s;",
        values[i], m->min, m->max,
        m->invert ? "true" : "false");

    if ((!isnan (m->min) && (values[i] < m->min))
        || (!isnan (m->max) && (values[i] > m->max)))
    {
      status = FC_MATCH_NO_MATCH;
      break;
    }
  }

  if (m->invert)
  {
    if (status == FC_MATCH_MATCHES)
      status = FC_MATCH_NO_MATCH;
    else
      status = FC_MATCH_MATCHES;
  }

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

