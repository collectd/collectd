/**
 * collectd - src/entropy.c
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
#include "configfile.h"
#include "utils_ignorelist.h"

/*
 * Variables
 */
static ignorelist_t *il_host   = NULL;
static ignorelist_t *il_plugin = NULL;
static ignorelist_t *il_type   = NULL;

static const char *config_keys[] =
{
  "IgnoreHost",
  "IgnorePlugin",
  "IgnoreType"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/*
 * Functions
 */
static int ignorelist_add_create (ignorelist_t **il_ptr, const char *entry)
{
  ignorelist_t *il;
  int status;

  il = *il_ptr;

  if (il == NULL)
  {
    il = ignorelist_create (/* ignore = */ 0);
    if (il == NULL)
    {
      ERROR ("filter_ignore plugin: ignorelist_create failed.");
      return (-1);
    }
    *il_ptr = il;
  }

  status = ignorelist_add (il, entry);
  if (status != 0)
  {
    ERROR ("filter_ignore plugin: ignorelist_add failed with error %i.",
        status);
    return (status);
  }

  return (0);
} /* int ignorelist_add_create */

static int fi_config (const char *key, const char *value)
{
  int status;

  status = 0;

  if (strcasecmp ("IgnoreHost", key) == 0)
    status = ignorelist_add_create (&il_host, value);
  else if (strcasecmp ("IgnorePlugin", key) == 0)
    status = ignorelist_add_create (&il_plugin, value);
  else if (strcasecmp ("IgnoreType", key) == 0)
    status = ignorelist_add_create (&il_type, value);
  else
    return (-1);
    
  if (status < 0)
    status = status * (-1);

  return (status);
} /* int fi_config */

static int fi_filter (const data_set_t *ds, value_list_t *vl)
{
  int status;

  if (il_host != NULL)
  {
    status = ignorelist_match (il_host, vl->host);
    if (status != 0)
      return (FILTER_IGNORE);
  }

  if (il_plugin != NULL)
  {
    char buffer[2 * DATA_MAX_NAME_LEN];

    if (vl->plugin_instance[0] == 0)
      sstrncpy (buffer, vl->plugin, sizeof (buffer));
    else
      ssnprintf (buffer, sizeof (buffer), "%s-%s",
          vl->plugin, vl->plugin_instance);

    status = ignorelist_match (il_plugin, buffer);
    if (status != 0)
      return (FILTER_IGNORE);
  }

  if (il_type != NULL)
  {
    char buffer[2 * DATA_MAX_NAME_LEN];

    if (vl->type_instance[0] == 0)
      sstrncpy (buffer, vl->type, sizeof (buffer));
    else
      ssnprintf (buffer, sizeof (buffer), "%s-%s",
          vl->type, vl->type_instance);

    status = ignorelist_match (il_type, buffer);
    if (status != 0)
      return (FILTER_IGNORE);
  }

  return (0);
} /* int fi_filter */

void module_register (void)
{
  plugin_register_config ("filter_ignore", fi_config,
      config_keys, config_keys_num);
  plugin_register_filter ("filter_ignore", fi_filter);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
