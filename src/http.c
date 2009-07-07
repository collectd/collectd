/**
 * collectd - src/http.c
 * Copyright (C) 2007-2009  Florian octo Forster
 * Copyright (C) 2009       Doug MacEachern
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
 *   Doug MacEachern <dougm@hyperic.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "utils_cache.h"
#include "utils_parse_option.h"

/*
 * Private variables
 */
static const char *config_keys[] =
{
  "Location",
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char *location   = NULL;

static int value_list_to_string (char *buffer, int buffer_len,
    const data_set_t *ds, const value_list_t *vl)
{
  int offset = 0;
  int status;
  int i;
  gauge_t *rates = NULL;

  assert (0 == strcmp (ds->type, vl->type));

  memset (buffer, '\0', buffer_len);

  for (i = 0; i < ds->ds_num; i++)
  {
    if (i > 0) 
    {
      status = ssnprintf (buffer + offset,
          buffer_len - offset,
          ":");
      offset += status;
    }
    if ((ds->ds[i].type != DS_TYPE_COUNTER)
        && (ds->ds[i].type != DS_TYPE_GAUGE))
      return (-1);

    if (ds->ds[i].type == DS_TYPE_COUNTER)
    {
      if (rates == NULL)
        rates = uc_get_rate (ds, vl);
      if (rates == NULL)
      {
        WARNING ("http plugin: "
            "uc_get_rate failed.");
        return (-1);
      }
      if (isnan(rates[i]))
      {
        /* dont output */
        return (-1);
      }
      status = ssnprintf (buffer + offset,
          buffer_len - offset,
          "%lf", rates[i]);
    }
    else /* if (ds->ds[i].type == DS_TYPE_GAUGE) */
    {
      status = ssnprintf (buffer + offset, buffer_len - offset,
          "%lf", vl->values[i].gauge);
    }

    if ((status < 1) || (status >= (buffer_len - offset)))
    {
      sfree (rates);
      return (-1);
    }

    offset += status;
  } /* for ds->ds_num */

  sfree (rates);
  return (0);
} /* int value_list_to_string */

static int value_list_to_timestamp (char *buffer, int buffer_len,
    const data_set_t *ds, const value_list_t *vl)
{
  int offset = 0;
  int status;

  assert (0 == strcmp (ds->type, vl->type));

  memset (buffer, '\0', buffer_len);

  status = ssnprintf (buffer, buffer_len, "%u", (unsigned int) vl->time);
  if ((status < 1) || (status >= buffer_len))
    return (-1);
  offset = status;

  return (0);
} /* int value_list_to_timestamp */

static int value_list_to_metric_name (char *buffer, int buffer_len,
    const data_set_t *ds, const value_list_t *vl)
{
  int offset = 0;
  int status;

  assert (0 == strcmp (ds->type, vl->type));

  /* hostname */
  status = ssnprintf (buffer + offset, buffer_len - offset,
      "%s", vl->host);
  if ((status < 1) || (status >= buffer_len - offset))
    return (-1);
  offset += status;

  /* plugin */
  status = ssnprintf (buffer + offset, buffer_len - offset,
      ",%s", vl->plugin);
  if ((status < 1) || (status >= buffer_len - offset))
    return (-1);
  offset += status;

  /* plugin_instance */
  if (strlen (vl->plugin_instance) > 0)
  {
    status = ssnprintf (buffer + offset, buffer_len - offset,
        ",%s", vl->plugin_instance);
    if ((status < 1) || (status >= buffer_len - offset))
      return (-1);
    offset += status;
  }

  /* type (if its the same as plugin, don't bother repeating it */
  if (0 != strcmp (vl->type, vl->plugin)) 
  {
    status = ssnprintf (buffer + offset, buffer_len - offset,
        ",%s", vl->type);
    if ((status < 1) || (status >= buffer_len - offset))
      return (-1);
    offset += status;
  }

  /* type_instance */
  if (strlen (vl->type_instance) > 0)
  {
    status = ssnprintf (buffer + offset, buffer_len - offset,
        ",%s", vl->type_instance);
    if ((status < 1) || (status >= buffer_len - offset))
      return (-1);
    offset += status;
  }

  return (0);
} /* int value_list_to_metric_name */

static int http_config (const char *key, const char *value)
{
  if (strcasecmp ("Location", key) == 0)
  {
    if (location != NULL)
      free (location);
    location = strdup (value);
    if (location != NULL)
    {
      int len = strlen (location);
      while ((len > 0) && (location[len - 1] == '/'))
      {
        len--;
        location[len] = '\0';
      }
      if (len <= 0)
      {
        free (location);
        location = NULL;
      }
    }
  }
  else
  {
    return (-1);
  }
  return (0);
} /* int http_config */

static int http_write (const data_set_t *ds, const value_list_t *vl,
    user_data_t __attribute__((unused)) *user_data)
{
  char         metric_name[512];
  char         values[512];
  char         timestamp[512];

  if (0 != strcmp (ds->type, vl->type)) {
    ERROR ("http plugin: DS type does not match value list type");
    return -1;
  }

  if (value_list_to_metric_name (metric_name, sizeof (metric_name), ds, vl) != 0)
    return (-1);

  DEBUG ("http plugin: http_write: metric_name = %s;", metric_name);

  if (value_list_to_timestamp (timestamp, sizeof (timestamp), ds, vl) != 0)
    return (-1);

  if (value_list_to_string (values, sizeof (values), ds, vl) != 0)
    return (-1);

  escape_string (metric_name, sizeof (metric_name));

  fprintf (stdout,
      "\"%s\",%s,%s\n",
      metric_name, timestamp, values);
  return (0);

} /* int http_write */

void module_register (void)
{
  plugin_register_config ("http", http_config,
      config_keys, config_keys_num);
  plugin_register_write ("http", http_write, /* user_data = */ NULL);
} /* void module_register */
