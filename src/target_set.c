/**
 * collectd - src/target_set.c
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

struct ts_data_s
{
  char *host;
  char *plugin;
  char *plugin_instance;
  /* char *type; */
  char *type_instance;
};
typedef struct ts_data_s ts_data_t;

static char *ts_strdup (const char *orig) /* {{{ */
{
  size_t sz;
  char *dest;

  if (orig == NULL)
    return (NULL);

  sz = strlen (orig) + 1;
  dest = (char *) malloc (sz);
  if (dest == NULL)
    return (NULL);

  memcpy (dest, orig, sz);

  return (dest);
} /* }}} char *ts_strdup */

static int ts_config_add_string (char **dest, /* {{{ */
    const oconfig_item_t *ci, int may_be_empty)
{
  char *temp;

  if (dest == NULL)
    return (-EINVAL);

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    ERROR ("Target `set': The `%s' option requires exactly one string "
        "argument.", ci->key);
    return (-1);
  }

  if ((!may_be_empty) && (ci->values[0].value.string[0] == 0))
  {
    ERROR ("Target `set': The `%s' option does not accept empty strings.",
        ci->key);
    return (-1);
  }

  temp = ts_strdup (ci->values[0].value.string);
  if (temp == NULL)
  {
    ERROR ("ts_config_add_string: ts_strdup failed.");
    return (-1);
  }

  free (*dest);
  *dest = temp;

  return (0);
} /* }}} int ts_config_add_string */

static int ts_destroy (void **user_data) /* {{{ */
{
  ts_data_t *data;

  if (user_data == NULL)
    return (-EINVAL);

  data = *user_data;
  if (data == NULL)
    return (0);

  free (data->host);
  free (data->plugin);
  free (data->plugin_instance);
  /* free (data->type); */
  free (data->type_instance);
  free (data);

  return (0);
} /* }}} int ts_destroy */

static int ts_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  ts_data_t *data;
  int status;
  int i;

  data = (ts_data_t *) malloc (sizeof (*data));
  if (data == NULL)
  {
    ERROR ("ts_create: malloc failed.");
    return (-ENOMEM);
  }
  memset (data, 0, sizeof (*data));

  data->host = NULL;
  data->plugin = NULL;
  data->plugin_instance = NULL;
  /* data->type = NULL; */
  data->type_instance = NULL;

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if ((strcasecmp ("Host", child->key) == 0)
        || (strcasecmp ("Hostname", child->key) == 0))
      status = ts_config_add_string (&data->host, child,
          /* may be empty = */ 0);
    else if (strcasecmp ("Plugin", child->key) == 0)
      status = ts_config_add_string (&data->plugin, child,
          /* may be empty = */ 0);
    else if (strcasecmp ("PluginInstance", child->key) == 0)
      status = ts_config_add_string (&data->plugin_instance, child,
          /* may be empty = */ 1);
#if 0
    else if (strcasecmp ("Type", child->key) == 0)
      status = ts_config_add_string (&data->type, child,
          /* may be empty = */ 0);
#endif
    else if (strcasecmp ("TypeInstance", child->key) == 0)
      status = ts_config_add_string (&data->type_instance, child,
          /* may be empty = */ 1);
    else
    {
      ERROR ("Target `set': The `%s' configuration option is not understood "
          "and will be ignored.", child->key);
      status = 0;
    }

    if (status != 0)
      break;
  }

  /* Additional sanity-checking */
  while (status == 0)
  {
    if ((data->host == NULL)
        && (data->plugin == NULL)
        && (data->plugin_instance == NULL)
        /* && (data->type == NULL) */
        && (data->type_instance == NULL))
    {
      ERROR ("Target `set': You need to set at lease one of `Host', "
          "`Plugin', `PluginInstance', `Type', or `TypeInstance'.");
      status = -1;
    }

    break;
  }

  if (status != 0)
  {
    ts_destroy ((void *) &data);
    return (status);
  }

  *user_data = data;
  return (0);
} /* }}} int ts_create */

static int ts_invoke (const data_set_t *ds, value_list_t *vl, /* {{{ */
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  ts_data_t *data;

  if ((ds == NULL) || (vl == NULL) || (user_data == NULL))
    return (-EINVAL);

  data = *user_data;
  if (data == NULL)
  {
    ERROR ("Target `set': Invoke: `data' is NULL.");
    return (-EINVAL);
  }

#define SET_FIELD(f) if (data->f != NULL) { sstrncpy (vl->f, data->f, sizeof (vl->f)); }
  SET_FIELD (host);
  SET_FIELD (plugin);
  SET_FIELD (plugin_instance);
  /* SET_FIELD (type); */
  SET_FIELD (type_instance);

  return (FC_TARGET_CONTINUE);
} /* }}} int ts_invoke */

void module_register (void)
{
	target_proc_t tproc;

	memset (&tproc, 0, sizeof (tproc));
	tproc.create  = ts_create;
	tproc.destroy = ts_destroy;
	tproc.invoke  = ts_invoke;
	fc_register_target ("set", tproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */

