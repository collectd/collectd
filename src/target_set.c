/**
 * collectd - src/target_set.c
 * Copyright (C) 2008       Florian Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
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

static int ts_config_add_string (char **dest, /* {{{ */
    const oconfig_item_t *ci, int may_be_empty)
{
  char *tmp = NULL;
  int status;

  status = cf_util_get_string (ci, &tmp);
  if (status != 0)
    return (status);

  if (!may_be_empty && (strlen (tmp) == 0))
  {
    ERROR ("Target `set': The `%s' option does not accept empty strings.",
        ci->key);
    sfree (tmp);
    return (-1);
  }

  *dest = tmp;
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
      ERROR ("Target `set': You need to set at least one of `Host', "
          "`Plugin', `PluginInstance' or `TypeInstance'.");
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

