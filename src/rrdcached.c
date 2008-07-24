/**
 * collectd - src/rrdcached.c
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
#include "plugin.h"
#include "common.h"
#include "utils_rrdcreate.h"

#include <rrd_client.h>

/*
 * Private variables
 */
static const char *config_keys[] =
{
  "DaemonAddress",
  "DataDir",
  "CreateFiles"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char *datadir = NULL;
static char *daemon_address = NULL;
static int config_create_files = 1;
static rrdcreate_config_t rrdcreate_config =
{
	/* stepsize = */ 0,
	/* heartbeat = */ 0,
	/* rrarows = */ 1200,
	/* xff = */ 0.1,

	/* timespans = */ NULL,
	/* timespans_num = */ 0,

	/* consolidation_functions = */ NULL,
	/* consolidation_functions_num = */ 0
};

static int value_list_to_string (char *buffer, int buffer_len,
    const data_set_t *ds, const value_list_t *vl)
{
  int offset;
  int status;
  int i;

  assert (0 == strcmp (ds->type, vl->type));

  memset (buffer, '\0', buffer_len);

  status = ssnprintf (buffer, buffer_len, "%u", (unsigned int) vl->time);
  if ((status < 1) || (status >= buffer_len))
    return (-1);
  offset = status;

  for (i = 0; i < ds->ds_num; i++)
  {
    if ((ds->ds[i].type != DS_TYPE_COUNTER)
        && (ds->ds[i].type != DS_TYPE_GAUGE))
      return (-1);

    if (ds->ds[i].type == DS_TYPE_COUNTER)
    {
      status = ssnprintf (buffer + offset, buffer_len - offset,
          ":%llu", vl->values[i].counter);
    }
    else /* if (ds->ds[i].type == DS_TYPE_GAUGE) */
    {
      status = ssnprintf (buffer + offset, buffer_len - offset,
          ":%lf", vl->values[i].gauge);
    }

    if ((status < 1) || (status >= (buffer_len - offset)))
      return (-1);

    offset += status;
  } /* for ds->ds_num */

  return (0);
} /* int value_list_to_string */

static int value_list_to_filename (char *buffer, int buffer_len,
    const data_set_t *ds, const value_list_t *vl)
{
  int offset = 0;
  int status;

  assert (0 == strcmp (ds->type, vl->type));

  if (datadir != NULL)
  {
    status = ssnprintf (buffer + offset, buffer_len - offset,
        "%s/", datadir);
    if ((status < 1) || (status >= buffer_len - offset))
      return (-1);
    offset += status;
  }

  status = ssnprintf (buffer + offset, buffer_len - offset,
      "%s/", vl->host);
  if ((status < 1) || (status >= buffer_len - offset))
    return (-1);
  offset += status;

  if (strlen (vl->plugin_instance) > 0)
    status = ssnprintf (buffer + offset, buffer_len - offset,
        "%s-%s/", vl->plugin, vl->plugin_instance);
  else
    status = ssnprintf (buffer + offset, buffer_len - offset,
        "%s/", vl->plugin);
  if ((status < 1) || (status >= buffer_len - offset))
    return (-1);
  offset += status;

  if (strlen (vl->type_instance) > 0)
    status = ssnprintf (buffer + offset, buffer_len - offset,
        "%s-%s", vl->type, vl->type_instance);
  else
    status = ssnprintf (buffer + offset, buffer_len - offset,
        "%s", vl->type);
  if ((status < 1) || (status >= buffer_len - offset))
    return (-1);
  offset += status;

  strncpy (buffer + offset, ".rrd", buffer_len - offset);
  buffer[buffer_len - 1] = 0;

  return (0);
} /* int value_list_to_filename */

static int rc_config (const char *key, const char *value)
{
  if (strcasecmp ("DataDir", key) == 0)
  {
    if (datadir != NULL)
      free (datadir);
    datadir = strdup (value);
    if (datadir != NULL)
    {
      int len = strlen (datadir);
      while ((len > 0) && (datadir[len - 1] == '/'))
      {
        len--;
        datadir[len] = '\0';
      }
      if (len <= 0)
      {
        free (datadir);
        datadir = NULL;
      }
    }
  }
  else if (strcasecmp ("DaemonAddress", key) == 0)
  {
    sfree (daemon_address);
    daemon_address = strdup (value);
    if (daemon_address == NULL)
    {
      ERROR ("rrdcached plugin: strdup failed.");
      return (1);
    }
  }
  else if (strcasecmp ("CreateFiles", key) == 0)
  {
    if ((strcasecmp ("true", value) == 0)
        || (strcasecmp ("yes", value) == 0)
        || (strcasecmp ("on", value) == 0))
      config_create_files = 1;
    else
      config_create_files = 0;
  }
  else
  {
    return (-1);
  }
  return (0);
} /* int rc_config */

static int rc_write (const data_set_t *ds, const value_list_t *vl)
{
  char filename[512];
  char values[512];
  char *values_array[2];
  int status;

  if (daemon_address == NULL)
  {
    ERROR ("rrdcached plugin: daemon_address == NULL.");
    plugin_unregister_write ("rrdcached");
    return (-1);
  }

  if (strcmp (ds->type, vl->type) != 0)
  {
    ERROR ("rrdcached plugin: DS type does not match value list type");
    return (-1);
  }

  if (value_list_to_filename (filename, sizeof (filename), ds, vl) != 0)
  {
    ERROR ("rrdcached plugin: value_list_to_filename failed.");
    return (-1);
  }

  if (value_list_to_string (values, sizeof (values), ds, vl) != 0)
  {
    ERROR ("rrdcached plugin: value_list_to_string failed.");
    return (-1);
  }

  values_array[0] = values;
  values_array[1] = NULL;

  if (config_create_files != 0)
  {
    struct stat statbuf;

    status = stat (filename, &statbuf);
    if (status != 0)
    {
      if (errno != ENOENT)
      {
        char errbuf[1024];
        ERROR ("rrdcached plugin: stat (%s) failed: %s",
            filename, sstrerror (errno, errbuf, sizeof (errbuf)));
        return (-1);
      }

      status = cu_rrd_create_file (filename, ds, vl, &rrdcreate_config);
      if (status != 0)
      {
        ERROR ("rrdcached plugin: cu_rrd_create_file (%s) failed.",
            filename);
        return (-1);
      }
    }
  }

  status = rrdc_connect (daemon_address);
  if (status != 0)
  {
    ERROR ("rrdcached plugin: rrdc_connect (%s) failed with status %i.",
        daemon_address, status);
    return (-1);
  }

  status = rrdc_update (filename, /* values_num = */ 1, (void *) values_array);
  if (status != 0)
  {
    ERROR ("rrdcached plugin: rrdc_update (%s, [%s], 1) failed with "
        "status %i.",
        filename, values_array[0], status);
    return (-1);
  }

  return (0);
} /* int rc_write */

static int rc_shutdown (void)
{
  rrdc_disconnect ();
  return (0);
} /* int rc_shutdown */

void module_register (void)
{
  plugin_register_config ("rrdcached", rc_config,
      config_keys, config_keys_num);
  plugin_register_write ("rrdcached", rc_write);
  plugin_register_shutdown ("rrdcached", rc_shutdown);
} /* void module_register */

/*
 * vim: set sw=2 sts=2 et :
 */
