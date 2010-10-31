/**
 * collectd - src/target_notification.c
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

#include "collectd.h"
#include "common.h"
#include "filter_chain.h"
#include "utils_cache.h"
#include "utils_subst.h"

struct tn_data_s
{
  int severity;
  char *message;
};
typedef struct tn_data_s tn_data_t;

static int tn_config_add_severity (tn_data_t *data, /* {{{ */
    const oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    ERROR ("Target `notification': The `%s' option requires exactly one string "
        "argument.", ci->key);
    return (-1);
  }

  if ((strcasecmp ("FAILURE", ci->values[0].value.string) == 0)
      || (strcasecmp ("CRITICAL", ci->values[0].value.string) == 0))
    data->severity = NOTIF_FAILURE;
  else if ((strcasecmp ("WARNING", ci->values[0].value.string) == 0)
      || (strcasecmp ("WARN", ci->values[0].value.string) == 0))
    data->severity = NOTIF_WARNING;
  else if (strcasecmp ("OKAY", ci->values[0].value.string) == 0)
    data->severity = NOTIF_OKAY;
  else
  {
    WARNING ("Target `notification': Unknown severity `%s'. "
        "Will use `FAILURE' instead.",
        ci->values[0].value.string);
    data->severity = NOTIF_FAILURE;
  }

  return (0);
} /* }}} int tn_config_add_severity */

static int tn_config_add_string (char **dest, /* {{{ */
    const oconfig_item_t *ci)
{
  char *temp;

  if (dest == NULL)
    return (-EINVAL);

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    ERROR ("Target `notification': The `%s' option requires exactly one string "
        "argument.", ci->key);
    return (-1);
  }

  if (ci->values[0].value.string[0] == 0)
  {
    ERROR ("Target `notification': The `%s' option does not accept empty strings.",
        ci->key);
    return (-1);
  }

  temp = sstrdup (ci->values[0].value.string);
  if (temp == NULL)
  {
    ERROR ("tn_config_add_string: sstrdup failed.");
    return (-1);
  }

  free (*dest);
  *dest = temp;

  return (0);
} /* }}} int tn_config_add_string */

static int tn_destroy (void **user_data) /* {{{ */
{
  tn_data_t *data;

  if (user_data == NULL)
    return (-EINVAL);

  data = *user_data;
  if (data == NULL)
    return (0);

  sfree (data->message);
  sfree (data);

  return (0);
} /* }}} int tn_destroy */

static int tn_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  tn_data_t *data;
  int status;
  int i;

  data = (tn_data_t *) malloc (sizeof (*data));
  if (data == NULL)
  {
    ERROR ("tn_create: malloc failed.");
    return (-ENOMEM);
  }
  memset (data, 0, sizeof (*data));

  data->message = NULL;
  data->severity = 0;

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Message", child->key) == 0)
      status = tn_config_add_string (&data->message, child);
    else if (strcasecmp ("Severity", child->key) == 0)
      status = tn_config_add_severity (data, child);
    else
    {
      ERROR ("Target `notification': The `%s' configuration option is not understood "
          "and will be ignored.", child->key);
      status = 0;
    }

    if (status != 0)
      break;
  }

  /* Additional sanity-checking */
  while (status == 0)
  {
    if ((data->severity != NOTIF_FAILURE)
        && (data->severity != NOTIF_WARNING)
        && (data->severity != NOTIF_OKAY))
    {
      DEBUG ("Target `notification': Setting "
          "the default severity `WARNING'.");
      data->severity = NOTIF_WARNING;
    }

    if (data->message == NULL)
    {
      ERROR ("Target `notification': No `Message' option has been specified. "
          "Without it, the `Notification' target is useless.");
      status = -1;
    }

    break;
  }

  if (status != 0)
  {
    tn_destroy ((void *) data);
    return (status);
  }

  *user_data = data;
  return (0);
} /* }}} int tn_create */

static int tn_invoke (const data_set_t *ds, value_list_t *vl, /* {{{ */
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  tn_data_t *data;
  notification_t n;
  char temp[NOTIF_MAX_MSG_LEN];

  gauge_t *rates;
  int rates_failed;

  int i;

  if ((ds == NULL) || (vl == NULL) || (user_data == NULL))
    return (-EINVAL);

  data = *user_data;
  if (data == NULL)
  {
    ERROR ("Target `notification': Invoke: `data' is NULL.");
    return (-EINVAL);
  }

  /* Initialize the structure. */
  memset (&n, 0, sizeof (n));
  n.severity = data->severity;
  n.time = cdtime ();
  sstrncpy (n.message, data->message, sizeof (n.message));
  sstrncpy (n.host, vl->host, sizeof (n.host));
  sstrncpy (n.plugin, vl->plugin, sizeof (n.plugin));
  sstrncpy (n.plugin_instance, vl->plugin_instance,
      sizeof (n.plugin_instance));
  sstrncpy (n.type, vl->type, sizeof (n.type));
  sstrncpy (n.type_instance, vl->type_instance,
      sizeof (n.type_instance));
  n.meta = NULL;

#define REPLACE_FIELD(t,v) \
  if (subst_string (temp, sizeof (temp), n.message, t, v) != NULL) \
    sstrncpy (n.message, temp, sizeof (n.message));
  REPLACE_FIELD ("%{host}", n.host);
  REPLACE_FIELD ("%{plugin}", n.plugin);
  REPLACE_FIELD ("%{plugin_instance}", n.plugin_instance);
  REPLACE_FIELD ("%{type}", n.type);
  REPLACE_FIELD ("%{type_instance}", n.type_instance);

  rates_failed = 0;
  rates = NULL;
  for (i = 0; i < ds->ds_num; i++)
  {
    char template[DATA_MAX_NAME_LEN];
    char value_str[DATA_MAX_NAME_LEN];

    ssnprintf (template, sizeof (template), "%%{ds:%s}", ds->ds[i].name);

    if (ds->ds[i].type != DS_TYPE_GAUGE)
    {
      if ((rates == NULL) && (rates_failed == 0))
      {
        rates = uc_get_rate (ds, vl);
        if (rates == NULL)
          rates_failed = 1;
      }
    }

    /* If this is a gauge value, use the current value. */
    if (ds->ds[i].type == DS_TYPE_GAUGE)
      ssnprintf (value_str, sizeof (value_str),
          "%g", (double) vl->values[i].gauge);
    /* If it's a counter, try to use the current rate. This may fail, if the
     * value has been renamed. */
    else if (rates != NULL)
      ssnprintf (value_str, sizeof (value_str),
          "%g", (double) rates[i]);
    /* Since we don't know any better, use the string `unknown'. */
    else
      sstrncpy (value_str, "unknown", sizeof (value_str));

    REPLACE_FIELD (template, value_str);
  }
  sfree (rates);

  plugin_dispatch_notification (&n);

  return (FC_TARGET_CONTINUE);
} /* }}} int tn_invoke */

void module_register (void)
{
	target_proc_t tproc;

	memset (&tproc, 0, sizeof (tproc));
	tproc.create  = tn_create;
	tproc.destroy = tn_destroy;
	tproc.invoke  = tn_invoke;
	fc_register_target ("notification", tproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */

