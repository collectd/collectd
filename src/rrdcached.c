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

#undef HAVE_CONFIG_H
#include <rrd.h>
#include <rrd_client.h>

/*
 * Private variables
 */
static char *datadir = NULL;
static char *daemon_address = NULL;
static int config_create_files = 1;
static int config_collect_stats = 1;
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

/*
 * Prototypes.
 */
static int rc_write (const data_set_t *ds, const value_list_t *vl,
    user_data_t __attribute__((unused)) *user_data);
static int rc_flush (__attribute__((unused)) cdtime_t timeout,
    const char *identifier, __attribute__((unused)) user_data_t *ud);

static int value_list_to_string (char *buffer, int buffer_len,
    const data_set_t *ds, const value_list_t *vl)
{
  int offset;
  int status;
  int i;
  time_t t;

  assert (0 == strcmp (ds->type, vl->type));

  memset (buffer, '\0', buffer_len);

  t = CDTIME_T_TO_TIME_T (vl->time);
  status = ssnprintf (buffer, buffer_len, "%lu", (unsigned long) t);
  if ((status < 1) || (status >= buffer_len))
    return (-1);
  offset = status;

  for (i = 0; i < ds->ds_num; i++)
  {
    if ((ds->ds[i].type != DS_TYPE_COUNTER)
        && (ds->ds[i].type != DS_TYPE_GAUGE)
	&& (ds->ds[i].type != DS_TYPE_DERIVE)
	&& (ds->ds[i].type != DS_TYPE_ABSOLUTE))
      return (-1);

    if (ds->ds[i].type == DS_TYPE_COUNTER)
    {
      status = ssnprintf (buffer + offset, buffer_len - offset,
          ":%llu", vl->values[i].counter);
    }
    else if (ds->ds[i].type == DS_TYPE_GAUGE) 
    {
      status = ssnprintf (buffer + offset, buffer_len - offset,
          ":%f", vl->values[i].gauge);
    }
    else if (ds->ds[i].type == DS_TYPE_DERIVE) {
      status = ssnprintf (buffer + offset, buffer_len - offset,
	  ":%"PRIi64, vl->values[i].derive);
    }
    else /* if (ds->ds[i].type == DS_TYPE_ABSOLUTE) */ {
      status = ssnprintf (buffer + offset, buffer_len - offset,
	  ":%"PRIu64, vl->values[i].absolute);
 
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

static const char *config_get_string (oconfig_item_t *ci)
{
  if ((ci->children_num != 0) || (ci->values_num != 1)
      || ((ci->values[0].type != OCONFIG_TYPE_STRING)
        && (ci->values[0].type != OCONFIG_TYPE_BOOLEAN)))
  {
    ERROR ("rrdcached plugin: %s expects a single string argument.",
        ci->key);
    return (NULL);
  }

  if (ci->values[0].type == OCONFIG_TYPE_BOOLEAN) {
    if (ci->values[0].value.boolean)
      return "true";
    else
      return "false";
  }
  return (ci->values[0].value.string);
} /* const char *config_get_string */

static int rc_config (oconfig_item_t *ci)
{
  int i;

  for (i = 0; i < ci->children_num; ++i) {
    const char *key = ci->children[i].key;
    const char *value = config_get_string (ci->children + i);

    if (value == NULL) /* config_get_strings prints error message */
      continue;

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
        continue;
      }
    }
    else if (strcasecmp ("CreateFiles", key) == 0)
    {
      if (IS_FALSE (value))
        config_create_files = 0;
      else
        config_create_files = 1;
    }
    else if (strcasecmp ("CollectStatistics", key) == 0)
    {
      if (IS_FALSE (value))
        config_collect_stats = 0;
      else
        config_collect_stats = 1;
    }
    else
    {
      WARNING ("rrdcached plugin: Ignoring invalid option %s.", key);
      continue;
    }
  }

  if (daemon_address != NULL) {
    plugin_register_write ("rrdcached", rc_write, /* user_data = */ NULL);
    plugin_register_flush ("rrdcached", rc_flush, /* user_data = */ NULL);
  }
  return (0);
} /* int rc_config */

static int rc_read (void)
{
  int status;
  rrdc_stats_t *head;
  rrdc_stats_t *ptr;

  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  if (daemon_address == NULL)
    return (-1);

  if (config_collect_stats == 0)
    return (-1);

  vl.values = values;
  vl.values_len = 1;

  if ((strncmp ("unix:", daemon_address, strlen ("unix:")) == 0)
      || (daemon_address[0] == '/'))
    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  else
    sstrncpy (vl.host, daemon_address, sizeof (vl.host));
  sstrncpy (vl.plugin, "rrdcached", sizeof (vl.plugin));

  head = NULL;
  status = rrdc_stats_get (&head);
  if (status != 0)
  {
    ERROR ("rrdcached plugin: rrdc_stats_get failed with status %i.", status);
    return (-1);
  }

  for (ptr = head; ptr != NULL; ptr = ptr->next)
  {
    if (ptr->type == RRDC_STATS_TYPE_GAUGE)
      values[0].gauge = (gauge_t) ptr->value.gauge;
    else if (ptr->type == RRDC_STATS_TYPE_COUNTER)
      values[0].counter = (counter_t) ptr->value.counter;
    else
      continue;

    if (strcasecmp ("QueueLength", ptr->name) == 0)
    {
      sstrncpy (vl.type, "queue_length", sizeof (vl.type));
      sstrncpy (vl.type_instance, "", sizeof (vl.type_instance));
    }
    else if (strcasecmp ("UpdatesWritten", ptr->name) == 0)
    {
      sstrncpy (vl.type, "operations", sizeof (vl.type));
      sstrncpy (vl.type_instance, "write-updates", sizeof (vl.type_instance));
    }
    else if (strcasecmp ("DataSetsWritten", ptr->name) == 0)
    {
      sstrncpy (vl.type, "operations", sizeof (vl.type));
      sstrncpy (vl.type_instance, "write-data_sets",
          sizeof (vl.type_instance));
    }
    else if (strcasecmp ("TreeNodesNumber", ptr->name) == 0)
    {
      sstrncpy (vl.type, "gauge", sizeof (vl.type));
      sstrncpy (vl.type_instance, "tree_nodes", sizeof (vl.type_instance));
    }
    else if (strcasecmp ("TreeDepth", ptr->name) == 0)
    {
      sstrncpy (vl.type, "gauge", sizeof (vl.type));
      sstrncpy (vl.type_instance, "tree_depth", sizeof (vl.type_instance));
    }
    else if (strcasecmp ("FlushesReceived", ptr->name) == 0)
    {
      sstrncpy (vl.type, "operations", sizeof (vl.type));
      sstrncpy (vl.type_instance, "receive-flush", sizeof (vl.type_instance));
    }
    else if (strcasecmp ("JournalBytes", ptr->name) == 0)
    {
      sstrncpy (vl.type, "counter", sizeof (vl.type));
      sstrncpy (vl.type_instance, "journal-bytes", sizeof (vl.type_instance));
    }
    else if (strcasecmp ("JournalRotate", ptr->name) == 0)
    {
      sstrncpy (vl.type, "counter", sizeof (vl.type));
      sstrncpy (vl.type_instance, "journal-rotates", sizeof (vl.type_instance));
    }
    else if (strcasecmp ("UpdatesReceived", ptr->name) == 0)
    {
      sstrncpy (vl.type, "operations", sizeof (vl.type));
      sstrncpy (vl.type_instance, "receive-update", sizeof (vl.type_instance));
    }
    else
    {
      DEBUG ("rrdcached plugin: rc_read: Unknown statistic `%s'.", ptr->name);
      continue;
    }

    plugin_dispatch_values (&vl);
  } /* for (ptr = head; ptr != NULL; ptr = ptr->next) */

  rrdc_stats_free (head);

  return (0);
} /* int rc_read */

static int rc_init (void)
{
  if (config_collect_stats != 0)
    plugin_register_read ("rrdcached", rc_read);

  return (0);
} /* int rc_init */

static int rc_write (const data_set_t *ds, const value_list_t *vl,
    user_data_t __attribute__((unused)) *user_data)
{
  char filename[PATH_MAX];
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

static int rc_flush (__attribute__((unused)) cdtime_t timeout, /* {{{ */
    const char *identifier,
    __attribute__((unused)) user_data_t *ud)
{
  char filename[PATH_MAX + 1];
  int status;

  if (identifier == NULL)
    return (EINVAL);

  if (datadir != NULL)
    ssnprintf (filename, sizeof (filename), "%s/%s.rrd", datadir, identifier);
  else
    ssnprintf (filename, sizeof (filename), "%s.rrd", identifier);

  status = rrdc_connect (daemon_address);
  if (status != 0)
  {
    ERROR ("rrdcached plugin: rrdc_connect (%s) failed with status %i.",
        daemon_address, status);
    return (-1);
  }

  status = rrdc_flush (filename);
  if (status != 0)
  {
    ERROR ("rrdcached plugin: rrdc_flush (%s) failed with status %i.",
        filename, status);
    return (-1);
  }
  DEBUG ("rrdcached plugin: rrdc_flush (%s): Success.", filename);

  return (0);
} /* }}} int rc_flush */

static int rc_shutdown (void)
{
  rrdc_disconnect ();
  return (0);
} /* int rc_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("rrdcached", rc_config);
  plugin_register_init ("rrdcached", rc_init);
  plugin_register_shutdown ("rrdcached", rc_shutdown);
} /* void module_register */

/*
 * vim: set sw=2 sts=2 et :
 */
