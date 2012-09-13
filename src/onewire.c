/**
 * collectd - src/owfs.c
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
 *   Florian octo Forster <octo at noris.net>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#include <owcapi.h>

#define OW_FAMILY_LENGTH 8
#define OW_FAMILY_MAX_FEATURES 2
struct ow_family_features_s
{
  char family[OW_FAMILY_LENGTH];
  struct
  {
    char filename[DATA_MAX_NAME_LEN];
    char type[DATA_MAX_NAME_LEN];
    char type_instance[DATA_MAX_NAME_LEN];
  } features[OW_FAMILY_MAX_FEATURES];
  size_t features_num;
};
typedef struct ow_family_features_s ow_family_features_t;

/* see http://owfs.sourceforge.net/ow_table.html for a list of families */
static ow_family_features_t ow_family_features[] =
{
  {
    /* family = */ "10.",
    {
      {
        /* filename = */ "temperature",
        /* type = */ "temperature",
        /* type_instance = */ ""
      }
    },
    /* features_num = */ 1
  }
};
static int ow_family_features_num = STATIC_ARRAY_SIZE (ow_family_features);

static char *device_g = NULL;
static cdtime_t ow_interval = 0;

static const char *config_keys[] =
{
  "Device",
  "IgnoreSelected",
  "Sensor",
  "Interval"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *sensor_list;

static int cow_load_config (const char *key, const char *value)
{
  if (sensor_list == NULL)
    sensor_list = ignorelist_create (1);

  if (strcasecmp (key, "Sensor") == 0)
  {
    if (ignorelist_add (sensor_list, value))
    {
      ERROR ("sensors plugin: "
          "Cannot add value to ignorelist.");
      return (1);
    }
  }
  else if (strcasecmp (key, "IgnoreSelected") == 0)
  {
    ignorelist_set_invert (sensor_list, 1);
    if (IS_TRUE (value))
      ignorelist_set_invert (sensor_list, 0);
  }
  else if (strcasecmp (key, "Device") == 0)
  {
    char *temp;
    temp = strdup (value);
    if (temp == NULL)
    {
      ERROR ("onewire plugin: strdup failed.");
      return (1);
    }
    sfree (device_g);
    device_g = temp;
  }
  else if (strcasecmp ("Interval", key) == 0)
  {
    double tmp;
    tmp = atof (value);
    if (tmp > 0.0)
      ow_interval = DOUBLE_TO_CDTIME_T (tmp);
    else
      ERROR ("onewire plugin: Invalid `Interval' setting: %s", value);
  }
  else
  {
    return (-1);
  }

  return (0);
}

static int cow_read_values (const char *path, const char *name,
    const ow_family_features_t *family_info)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  int success = 0;
  size_t i;

  if (sensor_list != NULL)
  {
    DEBUG ("onewire plugin: Checking ignorelist for `%s'", name);
    if (ignorelist_match (sensor_list, name) != 0)
      return 0;
  }

  vl.values = values;
  vl.values_len = 1;

  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "onewire", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, name, sizeof (vl.plugin_instance));

  for (i = 0; i < family_info->features_num; i++)
  {
    char *buffer;
    size_t buffer_size;
    int status;

    char file[4096];
    char *endptr;

    snprintf (file, sizeof (file), "%s/%s",
        path, family_info->features[i].filename);
    file[sizeof (file) - 1] = 0;

    buffer = NULL;
    buffer_size = 0;
    status = OW_get (file, &buffer, &buffer_size);
    if (status < 0)
    {
      ERROR ("onewire plugin: OW_get (%s/%s) failed. status = %#x;",
          path, family_info->features[i].filename, status);
      return (-1);
    }

    endptr = NULL;
    values[0].gauge = strtod (buffer, &endptr);
    if (endptr == NULL)
    {
      ERROR ("onewire plugin: Buffer is not a number: %s", buffer);
      status = -1;
      continue;
    }

    sstrncpy (vl.type, family_info->features[i].type, sizeof (vl.type));
    sstrncpy (vl.type_instance, family_info->features[i].type_instance,
        sizeof (vl.type_instance));

    plugin_dispatch_values (&vl);
    success++;

    free (buffer);
  } /* for (i = 0; i < features_num; i++) */

  return ((success > 0) ? 0 : -1);
} /* int cow_read_values */

/* Forward declaration so the recursion below works */
static int cow_read_bus (const char *path);

/*
 * cow_read_ds2409
 *
 * Handles:
 * - DS2409 - MicroLAN Coupler
 */
static int cow_read_ds2409 (const char *path)
{
  char subpath[4096];
  int status;

  status = ssnprintf (subpath, sizeof (subpath), "%s/main", path);
  if ((status > 0) && (status < sizeof (subpath)))
    cow_read_bus (subpath);

  status = ssnprintf (subpath, sizeof (subpath), "%s/aux", path);
  if ((status > 0) && (status < sizeof (subpath)))
    cow_read_bus (subpath);

  return (0);
} /* int cow_read_ds2409 */

static int cow_read_bus (const char *path)
{
  char *buffer;
  size_t buffer_size;
  int status;

  char *buffer_ptr;
  char *dummy;
  char *saveptr;
  char subpath[4096];

  status = OW_get (path, &buffer, &buffer_size);
  if (status < 0)
  {
    ERROR ("onewire plugin: OW_get (%s) failed. status = %#x;",
        path, status);
    return (-1);
  }
  DEBUG ("onewire plugin: OW_get (%s) returned: %s",
      path, buffer);

  dummy = buffer;
  saveptr = NULL;
  while ((buffer_ptr = strtok_r (dummy, ",/", &saveptr)) != NULL)
  {
    int i;

    dummy = NULL;

    if (strcmp ("/", path) == 0)
      status = ssnprintf (subpath, sizeof (subpath), "/%s", buffer_ptr);
    else
      status = ssnprintf (subpath, sizeof (subpath), "%s/%s",
          path, buffer_ptr);
    if ((status <= 0) || (status >= sizeof (subpath)))
      continue;

    for (i = 0; i < ow_family_features_num; i++)
    {
      if (strncmp (ow_family_features[i].family, buffer_ptr,
            strlen (ow_family_features[i].family)) != 0)
        continue;

      cow_read_values (subpath,
          buffer_ptr + strlen (ow_family_features[i].family),
          ow_family_features + i);
      break;
    }
    if (i < ow_family_features_num)
      continue;

    /* DS2409 */
    if (strncmp ("1F.", buffer_ptr, strlen ("1F.")) == 0)
    {
      cow_read_ds2409 (subpath);
      continue;
    }
  } /* while (strtok_r) */

  free (buffer);
  return (0);
} /* int cow_read_bus */

static int cow_read (user_data_t *ud __attribute__((unused)))
{
  return (cow_read_bus ("/"));
} /* int cow_read */

static int cow_shutdown (void)
{
  OW_finish ();
  ignorelist_free (sensor_list);
  return (0);
} /* int cow_shutdown */

static int cow_init (void)
{
  int status;
  struct timespec cb_interval;

  if (device_g == NULL)
  {
    ERROR ("onewire plugin: cow_init: No device configured.");
    return (-1);
  }

  status = (int) OW_init (device_g);
  if (status != 0)
  {
    ERROR ("onewire plugin: OW_init(%s) failed: %i.", device_g, status);
    return (1);
  }

  CDTIME_T_TO_TIMESPEC (ow_interval, &cb_interval);

  plugin_register_complex_read (/* group = */ NULL, "onewire", cow_read,
      (ow_interval != 0) ? &cb_interval : NULL,
      /* user data = */ NULL);
  plugin_register_shutdown ("onewire", cow_shutdown);

  return (0);
} /* int cow_init */

void module_register (void)
{
  plugin_register_init ("onewire", cow_init);
  plugin_register_config ("onewire", cow_load_config,
    config_keys, config_keys_num);
}

/* vim: set sw=2 sts=2 ts=8 et fdm=marker cindent : */
