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
  char *tsdb_prefix;
  char *tsdb_tags;
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
  free (data->tsdb_prefix);
  free (data->tsdb_tags);
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
  data->tsdb_prefix = NULL;
  data->tsdb_tags = NULL;

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
    else if (strcasecmp ("TSDBPrefix", child->key) == 0)
      status = ts_config_add_string (&data->tsdb_prefix, child,
          /* may be empty = */ 1);
    else if (strcasecmp ("TSDBTags", child->key) == 0)
      status = ts_config_add_string (&data->tsdb_tags, child,
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
        && (data->type_instance == NULL)
        && (data->tsdb_prefix == NULL)
        && (data->tsdb_tags == NULL))
    {
      ERROR ("Target `set': You need to set at least one of `Host', "
          "`Plugin', `PluginInstance', `TypeInstance', 'TSDBPrefix' "
          "or 'TSDBTags'.");
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

static int ts_opentsdb_tagger (value_list_t *vl, ts_data_t *data) /* {{{ */
{
  int status;
  char *old_tags = NULL;
  char new_tags[1024];
  const char *meta_tsdb = "tsdb_tags";

  if (data->tsdb_tags != NULL) {
    if (vl->meta == NULL)
      vl->meta = meta_data_create ();

    if (meta_data_exists (vl->meta, meta_tsdb)) {
        status = meta_data_get_string ( vl->meta, meta_tsdb, &old_tags );
        if (status < 0) {
          sfree (old_tags);
          return status;
        }

        ssnprintf ( new_tags, sizeof (new_tags), "%s %s", old_tags,
                    data->tsdb_tags);

        status = meta_data_add_string ( vl->meta, meta_tsdb, new_tags);
        if (status < 0) {
          sfree (old_tags);
          return status;
        }

    } else {
      status = meta_data_add_string ( vl->meta, meta_tsdb, data->tsdb_tags);
      if (status < 0)
        return status;
    }
  }

  sfree (old_tags);
  return 0;
} /* }}} int ts_opentsdb_tagger */

static int ts_opentsdb_prefix (value_list_t *vl, ts_data_t *data) /* {{{ */
{
  int status;
  const char *meta_tsdb = "tsdb_prefix";

  if (data->tsdb_prefix != NULL) {
    if (vl->meta == NULL)
      vl->meta = meta_data_create ();

    status = meta_data_add_string ( vl->meta, meta_tsdb, data->tsdb_prefix);
    if (status < 0)
      return status;
  }

  return 0;
} /* }}} int ts_opentsdb_prefix */

static int ts_invoke (const data_set_t *ds, value_list_t *vl, /* {{{ */
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  int status;
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

  status = ts_opentsdb_tagger (vl, data);
  if (status < 0)
    return status;

  status = ts_opentsdb_prefix (vl, data);
  if (status < 0)
    return status;

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

