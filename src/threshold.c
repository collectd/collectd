/**
 * collectd - src/threshold.c
 * Copyright (C) 2007-2010  Florian Forster
 * Copyright (C) 2008-2009  Sebastian Harl
 * Copyright (C) 2009       Andrés J. Díaz
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
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 *   Andrés J. Díaz <ajdiaz at connectical.com>
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
#define UT_FLAG_PERCENTAGE 0x04
#define UT_FLAG_INTERESTING 0x08
#define UT_FLAG_PERSIST_OK 0x10
typedef struct threshold_s
{
  char host[DATA_MAX_NAME_LEN];
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
  char data_source[DATA_MAX_NAME_LEN];
  gauge_t warning_min;
  gauge_t warning_max;
  gauge_t failure_min;
  gauge_t failure_max;
  gauge_t hysteresis;
  unsigned int flags;
  int hits;
  struct threshold_s *next;
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
 */
/*
 * threshold_t *threshold_get
 *
 * Retrieve one specific threshold configuration. For looking up a threshold
 * matching a value_list_t, see "threshold_search" below. Returns NULL if the
 * specified threshold doesn't exist.
 */
static threshold_t *threshold_get (const char *hostname,
    const char *plugin, const char *plugin_instance,
    const char *type, const char *type_instance)
{ /* {{{ */
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
} /* }}} threshold_t *threshold_get */

/*
 * int ut_threshold_add
 *
 * Adds a threshold configuration to the list of thresholds. The threshold_t
 * structure is copied and may be destroyed after this call. Returns zero on
 * success, non-zero otherwise.
 */
static int ut_threshold_add (const threshold_t *th)
{ /* {{{ */
  char name[6 * DATA_MAX_NAME_LEN];
  char *name_copy;
  threshold_t *th_copy;
  threshold_t *th_ptr;
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
  th_ptr = NULL;

  DEBUG ("ut_threshold_add: Adding entry `%s'", name);

  pthread_mutex_lock (&threshold_lock);

  th_ptr = threshold_get (th->host, th->plugin, th->plugin_instance,
      th->type, th->type_instance);

  while ((th_ptr != NULL) && (th_ptr->next != NULL))
    th_ptr = th_ptr->next;

  if (th_ptr == NULL) /* no such threshold yet */
  {
    status = c_avl_insert (threshold_tree, name_copy, th_copy);
  }
  else /* th_ptr points to the last threshold in the list */
  {
    th_ptr->next = th_copy;
    /* name_copy isn't needed */
    sfree (name_copy);
  }

  pthread_mutex_unlock (&threshold_lock);

  if (status != 0)
  {
    ERROR ("ut_threshold_add: c_avl_insert (%s) failed.", name);
    sfree (name_copy);
    sfree (th_copy);
  }

  return (status);
} /* }}} int ut_threshold_add */

/* 
 * threshold_t *threshold_search
 *
 * Searches for a threshold configuration using all the possible variations of
 * "Host", "Plugin" and "Type" blocks. Returns NULL if no threshold could be
 * found.
 * XXX: This is likely the least efficient function in collectd.
 */
static threshold_t *threshold_search (const value_list_t *vl)
{ /* {{{ */
  threshold_t *th;

  if ((th = threshold_get (vl->host, vl->plugin, vl->plugin_instance,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, vl->plugin, vl->plugin_instance,
	  vl->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, vl->plugin, NULL,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, vl->plugin, NULL,
	  vl->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, "", NULL,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, "", NULL,
	  vl->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, vl->plugin_instance,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, vl->plugin_instance,
	  vl->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, NULL,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, NULL,
	  vl->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get ("", "", NULL,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get ("", "", NULL,
	  vl->type, NULL)) != NULL)
    return (th);

  return (NULL);
} /* }}} threshold_t *threshold_search */

/*
 * Configuration
 * =============
 * The following approximately two hundred functions are used to handle the
 * configuration and fill the threshold list.
 * {{{ */
static int ut_config_type_datasource (threshold_t *th, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("threshold values: The `DataSource' option needs exactly one "
	"string argument.");
    return (-1);
  }

  sstrncpy (th->data_source, ci->values[0].value.string,
      sizeof (th->data_source));

  return (0);
} /* int ut_config_type_datasource */

static int ut_config_type_instance (threshold_t *th, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("threshold values: The `Instance' option needs exactly one "
	"string argument.");
    return (-1);
  }

  sstrncpy (th->type_instance, ci->values[0].value.string,
      sizeof (th->type_instance));

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
    th->warning_max = ci->values[0].value.number;
  else
    th->failure_max = ci->values[0].value.number;

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

static int ut_config_type_hits (threshold_t *th, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    WARNING ("threshold values: The `%s' option needs exactly one "
      "number argument.", ci->key);
    return (-1);
  }

  th->hits = ci->values[0].value.number;

  return (0);
} /* int ut_config_type_hits */

static int ut_config_type_hysteresis (threshold_t *th, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    WARNING ("threshold values: The `%s' option needs exactly one "
      "number argument.", ci->key);
    return (-1);
  }

  th->hysteresis = ci->values[0].value.number;

  return (0);
} /* int ut_config_type_hysteresis */

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
  sstrncpy (th.type, ci->values[0].value.string, sizeof (th.type));

  th.warning_min = NAN;
  th.warning_max = NAN;
  th.failure_min = NAN;
  th.failure_max = NAN;
  th.hits = 0;
  th.hysteresis = 0;
  th.flags = UT_FLAG_INTERESTING; /* interesting by default */

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    status = 0;

    if (strcasecmp ("Instance", option->key) == 0)
      status = ut_config_type_instance (&th, option);
    else if (strcasecmp ("DataSource", option->key) == 0)
      status = ut_config_type_datasource (&th, option);
    else if ((strcasecmp ("WarningMax", option->key) == 0)
	|| (strcasecmp ("FailureMax", option->key) == 0))
      status = ut_config_type_max (&th, option);
    else if ((strcasecmp ("WarningMin", option->key) == 0)
	|| (strcasecmp ("FailureMin", option->key) == 0))
      status = ut_config_type_min (&th, option);
    else if (strcasecmp ("Interesting", option->key) == 0)
      status = cf_util_get_flag (option, &th.flags, UT_FLAG_INTERESTING);
    else if (strcasecmp ("Invert", option->key) == 0)
      status = cf_util_get_flag (option, &th.flags, UT_FLAG_INVERT);
    else if (strcasecmp ("Persist", option->key) == 0)
      status = cf_util_get_flag (option, &th.flags, UT_FLAG_PERSIST);
    else if (strcasecmp ("PersistOK", option->key) == 0)
      status = cf_util_get_flag (option, &th.flags, UT_FLAG_PERSIST_OK);
    else if (strcasecmp ("Percentage", option->key) == 0)
      status = cf_util_get_flag (option, &th.flags, UT_FLAG_PERCENTAGE);
    else if (strcasecmp ("Hits", option->key) == 0)
      status = ut_config_type_hits (&th, option);
    else if (strcasecmp ("Hysteresis", option->key) == 0)
      status = ut_config_type_hysteresis (&th, option);
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

  sstrncpy (th->plugin_instance, ci->values[0].value.string,
      sizeof (th->plugin_instance));

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
  sstrncpy (th.plugin, ci->values[0].value.string, sizeof (th.plugin));

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
  sstrncpy (th.host, ci->values[0].value.string, sizeof (th.host));

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
/*
 * End of the functions used to configure threshold values.
 */
/* }}} */

/*
 * int ut_report_state
 *
 * Checks if the `state' differs from the old state and creates a notification
 * if appropriate.
 * Does not fail.
 */
static int ut_report_state (const data_set_t *ds,
    const value_list_t *vl,
    const threshold_t *th,
    const gauge_t *values,
    int ds_index,
    int state)
{ /* {{{ */
  int state_old;
  notification_t n;

  char *buf;
  size_t bufsize;

  int status;

  /* Check if hits matched */
  if ( (th->hits != 0) )
  {
    int hits = uc_get_hits(ds,vl);
    /* STATE_OKAY resets hits unless PERSIST_OK flag is set. Hits resets if
     * threshold is hit. */
    if ( ( (state == STATE_OKAY) && ((th->flags & UT_FLAG_PERSIST_OK) == 0) ) || (hits > th->hits) )
    {
        DEBUG("ut_report_state: reset uc_get_hits = 0");
        uc_set_hits(ds,vl,0); /* reset hit counter and notify */
    } else {
      DEBUG("ut_report_state: th->hits = %d, uc_get_hits = %d",th->hits,uc_get_hits(ds,vl));
      (void) uc_inc_hits(ds,vl,1); /* increase hit counter */
      return (0);
    }
  } /* end check hits */

  state_old = uc_get_state (ds, vl);

  /* If the state didn't change, report if `persistent' is specified. If the
   * state is `okay', then only report if `persist_ok` flag is set. */
  if (state == state_old)
  {
    if ((th->flags & UT_FLAG_PERSIST) == 0)
      return (0);
    else if ( (state == STATE_OKAY) && ((th->flags & UT_FLAG_PERSIST_OK) == 0) )
      return (0);
  }

  if (state != state_old)
    uc_set_state (ds, vl, state);

  NOTIFICATION_INIT_VL (&n, vl);

  buf = n.message;
  bufsize = sizeof (n.message);

  if (state == STATE_OKAY)
    n.severity = NOTIF_OKAY;
  else if (state == STATE_WARNING)
    n.severity = NOTIF_WARNING;
  else
    n.severity = NOTIF_FAILURE;

  n.time = vl->time;

  status = ssnprintf (buf, bufsize, "Host %s, plugin %s",
      vl->host, vl->plugin);
  buf += status;
  bufsize -= status;

  if (vl->plugin_instance[0] != '\0')
  {
    status = ssnprintf (buf, bufsize, " (instance %s)",
	vl->plugin_instance);
    buf += status;
    bufsize -= status;
  }

  status = ssnprintf (buf, bufsize, " type %s", vl->type);
  buf += status;
  bufsize -= status;

  if (vl->type_instance[0] != '\0')
  {
    status = ssnprintf (buf, bufsize, " (instance %s)",
	vl->type_instance);
    buf += status;
    bufsize -= status;
  }

  plugin_notification_meta_add_string (&n, "DataSource",
      ds->ds[ds_index].name);
  plugin_notification_meta_add_double (&n, "CurrentValue", values[ds_index]);
  plugin_notification_meta_add_double (&n, "WarningMin", th->warning_min);
  plugin_notification_meta_add_double (&n, "WarningMax", th->warning_max);
  plugin_notification_meta_add_double (&n, "FailureMin", th->failure_min);
  plugin_notification_meta_add_double (&n, "FailureMax", th->failure_max);

  /* Send an okay notification */
  if (state == STATE_OKAY)
  {
    if (state_old == STATE_MISSING)
      status = ssnprintf (buf, bufsize,
          ": Value is no longer missing.");
    else
      status = ssnprintf (buf, bufsize,
          ": All data sources are within range again.");
    buf += status;
    bufsize -= status;
  }
  else
  {
    double min;
    double max;

    min = (state == STATE_ERROR) ? th->failure_min : th->warning_min;
    max = (state == STATE_ERROR) ? th->failure_max : th->warning_max;

    if (th->flags & UT_FLAG_INVERT)
    {
      if (!isnan (min) && !isnan (max))
      {
        status = ssnprintf (buf, bufsize, ": Data source \"%s\" is currently "
            "%f. That is within the %s region of %f%s and %f%s.",
            ds->ds[ds_index].name, values[ds_index],
            (state == STATE_ERROR) ? "failure" : "warning",
            min, ((th->flags & UT_FLAG_PERCENTAGE) != 0) ? "%" : "",
            max, ((th->flags & UT_FLAG_PERCENTAGE) != 0) ? "%" : "");
      }
      else
      {
	status = ssnprintf (buf, bufsize, ": Data source \"%s\" is currently "
	    "%f. That is %s the %s threshold of %f%s.",
	    ds->ds[ds_index].name, values[ds_index],
	    isnan (min) ? "below" : "above",
	    (state == STATE_ERROR) ? "failure" : "warning",
	    isnan (min) ? max : min,
	    ((th->flags & UT_FLAG_PERCENTAGE) != 0) ? "%" : "");
      }
    }
    else if (th->flags & UT_FLAG_PERCENTAGE)
    {
      gauge_t value;
      gauge_t sum;
      int i;

      sum = 0.0;
      for (i = 0; i < vl->values_len; i++)
      {
        if (isnan (values[i]))
          continue;

        sum += values[i];
      }

      if (sum == 0.0)
        value = NAN;
      else
        value = 100.0 * values[ds_index] / sum;

      status = ssnprintf (buf, bufsize, ": Data source \"%s\" is currently "
          "%g (%.2f%%). That is %s the %s threshold of %.2f%%.",
          ds->ds[ds_index].name, values[ds_index], value,
          (value < min) ? "below" : "above",
          (state == STATE_ERROR) ? "failure" : "warning",
          (value < min) ? min : max);
    }
    else /* is not inverted */
    {
      status = ssnprintf (buf, bufsize, ": Data source \"%s\" is currently "
	  "%f. That is %s the %s threshold of %f.",
	  ds->ds[ds_index].name, values[ds_index],
	  (values[ds_index] < min) ? "below" : "above",
	  (state == STATE_ERROR) ? "failure" : "warning",
	  (values[ds_index] < min) ? min : max);
    }
    buf += status;
    bufsize -= status;
  }

  plugin_dispatch_notification (&n);

  plugin_notification_meta_free (n.meta);
  return (0);
} /* }}} int ut_report_state */

/*
 * int ut_check_one_data_source
 *
 * Checks one data source against the given threshold configuration. If the
 * `DataSource' option is set in the threshold, and the name does NOT match,
 * `okay' is returned. If the threshold does match, its failure and warning
 * min and max values are checked and `failure' or `warning' is returned if
 * appropriate.
 * Does not fail.
 */
static int ut_check_one_data_source (const data_set_t *ds,
    const value_list_t __attribute__((unused)) *vl,
    const threshold_t *th,
    const gauge_t *values,
    int ds_index)
{ /* {{{ */
  const char *ds_name;
  int is_warning = 0;
  int is_failure = 0;
  int prev_state = STATE_OKAY;

  /* check if this threshold applies to this data source */
  if (ds != NULL)
  {
    ds_name = ds->ds[ds_index].name;
    if ((th->data_source[0] != 0)
	&& (strcmp (ds_name, th->data_source) != 0))
      return (STATE_OKAY);
  }

  if ((th->flags & UT_FLAG_INVERT) != 0)
  {
    is_warning--;
    is_failure--;
  }

  /* XXX: This is an experimental code, not optimized, not fast, not reliable,
   * and probably, do not work as you expect. Enjoy! :D */
  if ( (th->hysteresis > 0) && ((prev_state = uc_get_state(ds,vl)) != STATE_OKAY) )
  {
    switch(prev_state)
    {
      case STATE_ERROR:
	if ( (!isnan (th->failure_min) && ((th->failure_min + th->hysteresis) < values[ds_index])) ||
	     (!isnan (th->failure_max) && ((th->failure_max - th->hysteresis) > values[ds_index])) )
	  return (STATE_OKAY);
	else
	  is_failure++;
      case STATE_WARNING:
	if ( (!isnan (th->warning_min) && ((th->warning_min + th->hysteresis) < values[ds_index])) ||
	     (!isnan (th->warning_max) && ((th->warning_max - th->hysteresis) > values[ds_index])) )
	  return (STATE_OKAY);
	else
	  is_warning++;
     }
  }
  else { /* no hysteresis */
    if ((!isnan (th->failure_min) && (th->failure_min > values[ds_index]))
	|| (!isnan (th->failure_max) && (th->failure_max < values[ds_index])))
      is_failure++;

    if ((!isnan (th->warning_min) && (th->warning_min > values[ds_index]))
	|| (!isnan (th->warning_max) && (th->warning_max < values[ds_index])))
      is_warning++;
 }

  if (is_failure != 0)
    return (STATE_ERROR);

  if (is_warning != 0)
    return (STATE_WARNING);

  return (STATE_OKAY);
} /* }}} int ut_check_one_data_source */

/*
 * int ut_check_one_threshold
 *
 * Checks all data sources of a value list against the given threshold, using
 * the ut_check_one_data_source function above. Returns the worst status,
 * which is `okay' if nothing has failed.
 * Returns less than zero if the data set doesn't have any data sources.
 */
static int ut_check_one_threshold (const data_set_t *ds,
    const value_list_t *vl,
    const threshold_t *th,
    const gauge_t *values,
    int *ret_ds_index)
{ /* {{{ */
  int ret = -1;
  int ds_index = -1;
  int i;
  gauge_t values_copy[ds->ds_num];

  memcpy (values_copy, values, sizeof (values_copy));

  if ((th->flags & UT_FLAG_PERCENTAGE) != 0)
  {
    int num = 0;
    gauge_t sum=0.0;

    if (ds->ds_num == 1)
    {
      WARNING ("ut_check_one_threshold: The %s type has only one data "
          "source, but you have configured to check this as a percentage. "
          "That doesn't make much sense, because the percentage will always "
          "be 100%%!", ds->type);
    }

    /* Prepare `sum' and `num'. */
    for (i = 0; i < ds->ds_num; i++)
      if (!isnan (values[i]))
      {
        num++;
	sum += values[i];
      }

    if ((num == 0) /* All data sources are undefined. */
        || (sum == 0.0)) /* Sum is zero, cannot calculate percentage. */
    {
      for (i = 0; i < ds->ds_num; i++)
        values_copy[i] = NAN;
    }
    else /* We can actually calculate the percentage. */
    {
      for (i = 0; i < ds->ds_num; i++)
        values_copy[i] = 100.0 * values[i] / sum;
    }
  } /* if (UT_FLAG_PERCENTAGE) */

  for (i = 0; i < ds->ds_num; i++)
  {
    int status;

    status = ut_check_one_data_source (ds, vl, th, values_copy, i);
    if (ret < status)
    {
      ret = status;
      ds_index = i;
    }
  } /* for (ds->ds_num) */

  if (ret_ds_index != NULL)
    *ret_ds_index = ds_index;

  return (ret);
} /* }}} int ut_check_one_threshold */

/*
 * int ut_check_threshold
 *
 * Gets a list of matching thresholds and searches for the worst status by one
 * of the thresholds. Then reports that status using the ut_report_state
 * function above. 
 * Returns zero on success and if no threshold has been configured. Returns
 * less than zero on failure.
 */
static int ut_check_threshold (const data_set_t *ds, const value_list_t *vl,
    __attribute__((unused)) user_data_t *ud)
{ /* {{{ */
  threshold_t *th;
  gauge_t *values;
  int status;

  int worst_state = -1;
  threshold_t *worst_th = NULL;
  int worst_ds_index = -1;

  if (threshold_tree == NULL)
    return (0);

  /* Is this lock really necessary? So far, thresholds are only inserted at
   * startup. -octo */
  pthread_mutex_lock (&threshold_lock);
  th = threshold_search (vl);
  pthread_mutex_unlock (&threshold_lock);
  if (th == NULL)
    return (0);

  DEBUG ("ut_check_threshold: Found matching threshold(s)");

  values = uc_get_rate (ds, vl);
  if (values == NULL)
    return (0);

  while (th != NULL)
  {
    int ds_index = -1;

    status = ut_check_one_threshold (ds, vl, th, values, &ds_index);
    if (status < 0)
    {
      ERROR ("ut_check_threshold: ut_check_one_threshold failed.");
      sfree (values);
      return (-1);
    }

    if (worst_state < status)
    {
      worst_state = status;
      worst_th = th;
      worst_ds_index = ds_index;
    }

    th = th->next;
  } /* while (th) */

  status = ut_report_state (ds, vl, worst_th, values,
      worst_ds_index, worst_state);
  if (status != 0)
  {
    ERROR ("ut_check_threshold: ut_report_state failed.");
    sfree (values);
    return (-1);
  }

  sfree (values);

  return (0);
} /* }}} int ut_check_threshold */

/*
 * int ut_missing
 *
 * This function is called whenever a value goes "missing".
 */
static int ut_missing (const value_list_t *vl,
    __attribute__((unused)) user_data_t *ud)
{ /* {{{ */
  threshold_t *th;
  cdtime_t missing_time;
  char identifier[6 * DATA_MAX_NAME_LEN];
  notification_t n;

  /* dispatch notifications for "interesting" values only */
  if (threshold_tree == NULL)
    return (0);

  th = threshold_search (vl);
  if (th == NULL)
    return (0);

  missing_time = cdtime () - vl->time;
  FORMAT_VL (identifier, sizeof (identifier), vl);

  NOTIFICATION_INIT_VL (&n, vl);
  ssnprintf (n.message, sizeof (n.message),
      "%s has not been updated for %.3f seconds.",
      identifier, CDTIME_T_TO_DOUBLE (missing_time));

  plugin_dispatch_notification (&n);

  return (0);
} /* }}} int ut_missing */

int ut_config (oconfig_item_t *ci)
{ /* {{{ */
  int i;
  int status = 0;

  threshold_t th;

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

  th.hits = 0;
  th.hysteresis = 0;
  th.flags = UT_FLAG_INTERESTING; /* interesting by default */
    
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

  if (c_avl_size (threshold_tree) > 0) {
    plugin_register_missing ("threshold", ut_missing,
        /* user data = */ NULL);
    plugin_register_write ("threshold", ut_check_threshold,
        /* user data = */ NULL);
  }

  return (status);
} /* }}} int um_config */

void module_register (void)
{
  plugin_register_complex_config ("threshold", ut_config);
}

/* vim: set sw=2 ts=8 sts=2 tw=78 et fdm=marker : */
