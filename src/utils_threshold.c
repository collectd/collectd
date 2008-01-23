/**
 * collectd - src/utils_threshold.c
 * Copyright (C) 2007,2008  Florian octo Forster
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
 * Author:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_avltree.h"
#include "utils_cache.h"

#include <assert.h>
#include <pthread.h>

/*
 * Private data structures
 * {{{ */
#define UT_FLAG_INVERT  0x01
#define UT_FLAG_PERSIST 0x02

typedef struct threshold_s
{
  char host[DATA_MAX_NAME_LEN];
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
  gauge_t warning_min;
  gauge_t warning_max;
  gauge_t failure_min;
  gauge_t failure_max;
  int flags;
} threshold_t;
/* }}} */

/*
 * Private (static) variables
 * {{{ */
static c_avl_tree_t   *threshold_tree = NULL;
static pthread_mutex_t threshold_lock = PTHREAD_MUTEX_INITIALIZER;
/* }}} */

/*
 * Threshold management
 * ====================
 * The following functions add, delete, search, etc. configured thresholds to
 * the underlying AVL trees.
 * {{{ */
static int ut_threshold_add (const threshold_t *th)
{
  char name[6 * DATA_MAX_NAME_LEN];
  char *name_copy;
  threshold_t *th_copy;
  int status = 0;

  if (format_name (name, sizeof (name), th->host,
	th->plugin, th->plugin_instance,
	th->type, th->type_instance) != 0)
  {
    ERROR ("ut_threshold_add: format_name failed.");
    return (-1);
  }

  name_copy = strdup (name);
  if (name_copy == NULL)
  {
    ERROR ("ut_threshold_add: strdup failed.");
    return (-1);
  }

  th_copy = (threshold_t *) malloc (sizeof (threshold_t));
  if (th_copy == NULL)
  {
    sfree (name_copy);
    ERROR ("ut_threshold_add: malloc failed.");
    return (-1);
  }
  memcpy (th_copy, th, sizeof (threshold_t));

  DEBUG ("ut_threshold_add: Adding entry `%s'", name);

  pthread_mutex_lock (&threshold_lock);
  status = c_avl_insert (threshold_tree, name_copy, th_copy);
  pthread_mutex_unlock (&threshold_lock);

  if (status != 0)
  {
    ERROR ("ut_threshold_add: c_avl_insert (%s) failed.", name);
    sfree (name_copy);
    sfree (th_copy);
  }

  return (status);
} /* int ut_threshold_add */
/*
 * End of the threshold management functions
 * }}} */

/*
 * Configuration
 * =============
 * The following approximately two hundred functions are used to handle the
 * configuration and fill the threshold list.
 * {{{ */
static int ut_config_type_instance (threshold_t *th, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("threshold values: The `Instance' option needs exactly one "
	"string argument.");
    return (-1);
  }

  strncpy (th->type_instance, ci->values[0].value.string,
      sizeof (th->type_instance));
  th->type_instance[sizeof (th->type_instance) - 1] = '\0';

  return (0);
} /* int ut_config_type_instance */

static int ut_config_type_max (threshold_t *th, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    WARNING ("threshold values: The `%s' option needs exactly one "
	"number argument.", ci->key);
    return (-1);
  }

  if (strcasecmp (ci->key, "WarningMax") == 0)
    th->warning_min = ci->values[0].value.number;
  else
    th->failure_min = ci->values[0].value.number;

  return (0);
} /* int ut_config_type_max */

static int ut_config_type_min (threshold_t *th, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    WARNING ("threshold values: The `%s' option needs exactly one "
	"number argument.", ci->key);
    return (-1);
  }

  if (strcasecmp (ci->key, "WarningMin") == 0)
    th->warning_min = ci->values[0].value.number;
  else
    th->failure_min = ci->values[0].value.number;

  return (0);
} /* int ut_config_type_min */

static int ut_config_type_invert (threshold_t *th, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    WARNING ("threshold values: The `Invert' option needs exactly one "
	"boolean argument.");
    return (-1);
  }

  if (ci->values[0].value.boolean)
    th->flags |= UT_FLAG_INVERT;
  else
    th->flags &= ~UT_FLAG_INVERT;

  return (0);
} /* int ut_config_type_invert */

static int ut_config_type_persist (threshold_t *th, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    WARNING ("threshold values: The `Persist' option needs exactly one "
	"boolean argument.");
    return (-1);
  }

  if (ci->values[0].value.boolean)
    th->flags |= UT_FLAG_PERSIST;
  else
    th->flags &= ~UT_FLAG_PERSIST;

  return (0);
} /* int ut_config_type_persist */

static int ut_config_type (const threshold_t *th_orig, oconfig_item_t *ci)
{
  int i;
  threshold_t th;
  int status = 0;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("threshold values: The `Type' block needs exactly one string "
	"argument.");
    return (-1);
  }

  if (ci->children_num < 1)
  {
    WARNING ("threshold values: The `Type' block needs at least one option.");
    return (-1);
  }

  memcpy (&th, th_orig, sizeof (th));
  strncpy (th.type, ci->values[0].value.string, sizeof (th.type));
  th.type[sizeof (th.type) - 1] = '\0';

  th.warning_min = NAN;
  th.warning_max = NAN;
  th.failure_min = NAN;
  th.failure_max = NAN;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    status = 0;

    if (strcasecmp ("Instance", option->key) == 0)
      status = ut_config_type_instance (&th, option);
    else if ((strcasecmp ("WarningMax", option->key) == 0)
	|| (strcasecmp ("FailureMax", option->key) == 0))
      status = ut_config_type_max (&th, option);
    else if ((strcasecmp ("WarningMin", option->key) == 0)
	|| (strcasecmp ("FailureMin", option->key) == 0))
      status = ut_config_type_min (&th, option);
    else if (strcasecmp ("Invert", option->key) == 0)
      status = ut_config_type_invert (&th, option);
    else if (strcasecmp ("Persist", option->key) == 0)
      status = ut_config_type_persist (&th, option);
    else
    {
      WARNING ("threshold values: Option `%s' not allowed inside a `Type' "
	  "block.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status == 0)
  {
    status = ut_threshold_add (&th);
  }

  return (status);
} /* int ut_config_type */

static int ut_config_plugin_instance (threshold_t *th, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("threshold values: The `Instance' option needs exactly one "
	"string argument.");
    return (-1);
  }

  strncpy (th->plugin_instance, ci->values[0].value.string,
      sizeof (th->plugin_instance));
  th->plugin_instance[sizeof (th->plugin_instance) - 1] = '\0';

  return (0);
} /* int ut_config_plugin_instance */

static int ut_config_plugin (const threshold_t *th_orig, oconfig_item_t *ci)
{
  int i;
  threshold_t th;
  int status = 0;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("threshold values: The `Plugin' block needs exactly one string "
	"argument.");
    return (-1);
  }

  if (ci->children_num < 1)
  {
    WARNING ("threshold values: The `Plugin' block needs at least one nested "
	"block.");
    return (-1);
  }

  memcpy (&th, th_orig, sizeof (th));
  strncpy (th.plugin, ci->values[0].value.string, sizeof (th.plugin));
  th.plugin[sizeof (th.plugin) - 1] = '\0';

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    status = 0;

    if (strcasecmp ("Type", option->key) == 0)
      status = ut_config_type (&th, option);
    else if (strcasecmp ("Instance", option->key) == 0)
      status = ut_config_plugin_instance (&th, option);
    else
    {
      WARNING ("threshold values: Option `%s' not allowed inside a `Plugin' "
	  "block.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  return (status);
} /* int ut_config_plugin */

static int ut_config_host (const threshold_t *th_orig, oconfig_item_t *ci)
{
  int i;
  threshold_t th;
  int status = 0;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("threshold values: The `Host' block needs exactly one string "
	"argument.");
    return (-1);
  }

  if (ci->children_num < 1)
  {
    WARNING ("threshold values: The `Host' block needs at least one nested "
	"block.");
    return (-1);
  }

  memcpy (&th, th_orig, sizeof (th));
  strncpy (th.host, ci->values[0].value.string, sizeof (th.host));
  th.host[sizeof (th.host) - 1] = '\0';

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    status = 0;

    if (strcasecmp ("Type", option->key) == 0)
      status = ut_config_type (&th, option);
    else if (strcasecmp ("Plugin", option->key) == 0)
      status = ut_config_plugin (&th, option);
    else
    {
      WARNING ("threshold values: Option `%s' not allowed inside a `Host' "
	  "block.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  return (status);
} /* int ut_config_host */

int ut_config (const oconfig_item_t *ci)
{
  int i;
  int status = 0;

  threshold_t th;

  if (ci->values_num != 0)
  {
    ERROR ("threshold values: The `Threshold' block may not have any "
	"arguments.");
    return (-1);
  }

  if (threshold_tree == NULL)
  {
    threshold_tree = c_avl_create ((void *) strcmp);
    if (threshold_tree == NULL)
    {
      ERROR ("ut_config: c_avl_create failed.");
      return (-1);
    }
  }

  memset (&th, '\0', sizeof (th));
  th.warning_min = NAN;
  th.warning_max = NAN;
  th.failure_min = NAN;
  th.failure_max = NAN;
    
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    status = 0;

    if (strcasecmp ("Type", option->key) == 0)
      status = ut_config_type (&th, option);
    else if (strcasecmp ("Plugin", option->key) == 0)
      status = ut_config_plugin (&th, option);
    else if (strcasecmp ("Host", option->key) == 0)
      status = ut_config_host (&th, option);
    else
    {
      WARNING ("threshold values: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  return (status);
} /* int um_config */
/*
 * End of the functions used to configure threshold values.
 */
/* }}} */

static threshold_t *threshold_get (const char *hostname,
    const char *plugin, const char *plugin_instance,
    const char *type, const char *type_instance)
{
  char name[6 * DATA_MAX_NAME_LEN];
  threshold_t *th = NULL;

  format_name (name, sizeof (name),
      (hostname == NULL) ? "" : hostname,
      (plugin == NULL) ? "" : plugin, plugin_instance,
      (type == NULL) ? "" : type, type_instance);
  name[sizeof (name) - 1] = '\0';

  if (c_avl_get (threshold_tree, name, (void *) &th) == 0)
    return (th);
  else
    return (NULL);
} /* threshold_t *threshold_get */

static threshold_t *threshold_search (const data_set_t *ds,
    const value_list_t *vl)
{
  threshold_t *th;

  if ((th = threshold_get (vl->host, vl->plugin, vl->plugin_instance,
	  ds->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, vl->plugin, vl->plugin_instance,
	  ds->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, vl->plugin, NULL,
	  ds->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, vl->plugin, NULL,
	  ds->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, "", NULL,
	  ds->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, "", NULL,
	  ds->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, vl->plugin_instance,
	  ds->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, vl->plugin_instance,
	  ds->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, NULL,
	  ds->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, NULL,
	  ds->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get ("", "", NULL,
	  ds->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get ("", "", NULL,
	  ds->type, NULL)) != NULL)
    return (th);

  return (NULL);
} /* threshold_t *threshold_search */

int ut_check_threshold (const data_set_t *ds, const value_list_t *vl)
{
  threshold_t *th;
  gauge_t *values;
  int i;

  if (threshold_tree == NULL)
    return (0);
  /* Is this lock really necessary? So far, thresholds are only inserted at
   * startup. -octo */
  pthread_mutex_lock (&threshold_lock);
  th = threshold_search (ds, vl);
  pthread_mutex_unlock (&threshold_lock);
  if (th == NULL)
    return (0);

  DEBUG ("ut_check_threshold: Found matching threshold");

  values = uc_get_rate (ds, vl);
  if (values == NULL)
    return (0);

  for (i = 0; i < ds->ds_num; i++)
  {
    int is_inverted = 0;
    int is_warning = 0;
    int is_failure = 0;

    if ((th->flags & UT_FLAG_INVERT) != 0)
      is_inverted = 1;
    if ((!isnan (th->failure_min) && (th->failure_min > values[i]))
	|| (!isnan (th->failure_max) && (th->failure_max < values[i])))
      is_failure = is_inverted - 1;
    if ((!isnan (th->warning_min) && (th->warning_min > values[i]))
	|| (!isnan (th->warning_max) && (th->warning_max < values[i])))
      is_warning = is_inverted - 1;

    if ((is_failure != 0) || (is_warning != 0))
    {
      notification_t n;
      char *buf;
      size_t bufsize;
      int status;

      double min;
      double max;

      min = (is_failure != 0) ? th->failure_min : th->warning_min;
      max = (is_failure != 0) ? th->failure_max : th->warning_max;

      DEBUG ("ut_check_threshold: ds[%s]: %lf <= !%lf <= %lf (invert: %s)",
	  ds->ds[i].name, min, values[i], max,
	  is_inverted ? "true" : "false");

      /* Copy the associative members */
      NOTIFICATION_INIT_VL (&n, vl, ds);

      n.severity = (is_failure != 0) ? NOTIF_FAILURE : NOTIF_WARNING;
      n.time = vl->time;

      buf = n.message;
      bufsize = sizeof (n.message);

      status = snprintf (buf, bufsize, "Host %s, plugin %s",
	  vl->host, vl->plugin);
      buf += status;
      bufsize -= status;

      if (vl->plugin_instance[0] != '\0')
      {
	status = snprintf (buf, bufsize, " (instance %s)",
	    vl->plugin_instance);
	buf += status;
	bufsize -= status;
      }

      status = snprintf (buf, bufsize, " type %s", ds->type);
      buf += status;
      bufsize -= status;

      if (vl->type_instance[0] != '\0')
      {
	status = snprintf (buf, bufsize, " (instance %s)",
	    vl->type_instance);
	buf += status;
	bufsize -= status;
      }

      if (is_inverted)
      {
	if (!isnan (min) && !isnan (max))
	{
	  status = snprintf (buf, bufsize, ": Data source \"%s\" is currently "
	      "%f. That is within the %s region of %f and %f.",
	      ds->ds[i].name, values[i],
	      (is_failure != 0) ? "failure" : "warning",
	      min, min);
	}
	else
	{
	  status = snprintf (buf, bufsize, ": Data source \"%s\" is currently "
	      "%f. That is %s the %s threshold of %f.",
	      ds->ds[i].name, values[i],
	      isnan (min) ? "below" : "above",
	      (is_failure != 0) ? "failure" : "warning",
	      isnan (min) ? max : min);
	}
      }
      else /* (!is_inverted) */
      {
	status = snprintf (buf, bufsize, ": Data source \"%s\" is currently "
	    "%f. That is %s the %s threshold of %f.",
	    ds->ds[i].name, values[i],
	    (values[i] < min) ? "below" : "above",
	    (is_failure != 0) ? "failure" : "warning",
	    (values[i] < min) ? min : max);
      }
      buf += status;
      bufsize -= status;

      plugin_dispatch_notification (&n);
    }
  } /* for (i = 0; i < ds->ds_num; i++) */

  sfree (values);

  return (0);
} /* int ut_check_threshold */

int ut_check_interesting (const char *name)
{
  char *name_copy = NULL;
  char *host = NULL;
  char *plugin = NULL;
  char *plugin_instance = NULL;
  char *type = NULL;
  char *type_instance = NULL;
  int status;
  data_set_t ds;
  value_list_t vl;
  threshold_t *th;

  /* If there is no tree nothing is interesting. */
  if (threshold_tree == NULL)
    return (0);

  name_copy = strdup (name);
  if (name_copy == NULL)
  {
    ERROR ("ut_check_interesting: strdup failed.");
    return (-1);
  }

  status = parse_identifier (name_copy, &host,
      &plugin, &plugin_instance, &type, &type_instance);
  if (status != 0)
  {
    ERROR ("ut_check_interesting: parse_identifier failed.");
    return (-1);
  }

  memset (&ds, '\0', sizeof (ds));
  memset (&vl, '\0', sizeof (vl));

  strncpy (vl.host, host, sizeof (vl.host));
  vl.host[sizeof (vl.host) - 1] = '\0';
  strncpy (vl.plugin, plugin, sizeof (vl.plugin));
  vl.plugin[sizeof (vl.plugin) - 1] = '\0';
  if (plugin_instance != NULL)
  {
    strncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
    vl.plugin_instance[sizeof (vl.plugin_instance) - 1] = '\0';
  }
  strncpy (ds.type, type, sizeof (ds.type));
  ds.type[sizeof (ds.type) - 1] = '\0';
  if (type_instance != NULL)
  {
    strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));
    vl.type_instance[sizeof (vl.type_instance) - 1] = '\0';
  }

  sfree (name_copy);
  host = plugin = plugin_instance = type = type_instance = NULL;

  th = threshold_search (&ds, &vl);
  if (th == NULL)
    return (0);
  if ((th->flags & UT_FLAG_PERSIST) == 0)
    return (1);
  return (2);
} /* int ut_check_interesting */

/* vim: set sw=2 ts=8 sts=2 tw=78 fdm=marker : */
