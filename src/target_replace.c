/**
 * collectd - src/target_replace.c
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
#include "utils_subst.h"

#include <regex.h>

struct tr_action_s;
typedef struct tr_action_s tr_action_t;
struct tr_action_s
{
  regex_t re;
  char *replacement;
  int may_be_empty;

  tr_action_t *next;
};

struct tr_data_s
{
  tr_action_t *host;
  tr_action_t *plugin;
  tr_action_t *plugin_instance;
  /* tr_action_t *type; */
  tr_action_t *type_instance;
};
typedef struct tr_data_s tr_data_t;

static char *tr_strdup (const char *orig) /* {{{ */
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
} /* }}} char *tr_strdup */

static void tr_action_destroy (tr_action_t *act) /* {{{ */
{
  if (act == NULL)
    return;

  regfree (&act->re);
  sfree (act->replacement);

  if (act->next != NULL)
    tr_action_destroy (act->next);

  sfree (act);
} /* }}} void tr_action_destroy */

static int tr_config_add_action (tr_action_t **dest, /* {{{ */
    const oconfig_item_t *ci, int may_be_empty)
{
  tr_action_t *act;
  int status;

  if (dest == NULL)
    return (-EINVAL);

  if ((ci->values_num != 2)
      || (ci->values[0].type != OCONFIG_TYPE_STRING)
      || (ci->values[1].type != OCONFIG_TYPE_STRING))
  {
    ERROR ("Target `replace': The `%s' option requires exactly two string "
        "arguments.", ci->key);
    return (-1);
  }

  act = (tr_action_t *) malloc (sizeof (*act));
  if (act == NULL)
  {
    ERROR ("tr_config_add_action: malloc failed.");
    return (-ENOMEM);
  }
  memset (act, 0, sizeof (*act));

  act->replacement = NULL;
  act->may_be_empty = may_be_empty;

  status = regcomp (&act->re, ci->values[0].value.string, REG_EXTENDED);
  if (status != 0)
  {
    char errbuf[1024] = "";

    /* regerror assures null termination. */
    regerror (status, &act->re, errbuf, sizeof (errbuf));
    ERROR ("Target `replace': Compiling the regular expression `%s' "
        "failed: %s.",
        ci->values[0].value.string, errbuf);
    sfree (act);
    return (-EINVAL);
  }

  act->replacement = tr_strdup (ci->values[1].value.string);
  if (act->replacement == NULL)
  {
    ERROR ("tr_config_add_action: tr_strdup failed.");
    regfree (&act->re);
    sfree (act);
    return (-ENOMEM);
  }

  /* Insert action at end of list. */
  if (*dest == NULL)
    *dest = act;
  else
  {
    tr_action_t *prev;

    prev = *dest;
    while (prev->next != NULL)
      prev = prev->next;

    prev->next = act;
  }

  return (0);
} /* }}} int tr_config_add_action */

static int tr_action_invoke (tr_action_t *act_head, /* {{{ */
    char *buffer_in, size_t buffer_in_size, int may_be_empty)
{
  tr_action_t *act;
  int status;
  char buffer[DATA_MAX_NAME_LEN];
  regmatch_t matches[8];

  if (act_head == NULL)
    return (-EINVAL);

  sstrncpy (buffer, buffer_in, sizeof (buffer));
  memset (matches, 0, sizeof (matches));

  DEBUG ("target_replace plugin: tr_action_invoke: <- buffer = %s;", buffer);

  for (act = act_head; act != NULL; act = act->next)
  {
    char temp[DATA_MAX_NAME_LEN];
    char *subst_status;

    status = regexec (&act->re, buffer,
        STATIC_ARRAY_SIZE (matches), matches,
        /* flags = */ 0);
    if (status == REG_NOMATCH)
      continue;
    else if (status != 0)
    {
      char errbuf[1024] = "";

      regerror (status, &act->re, errbuf, sizeof (errbuf));
      ERROR ("Target `replace': Executing a regular expression failed: %s.",
          errbuf);
      continue;
    }

    subst_status = subst (temp, sizeof (temp), buffer,
        matches[0].rm_so, matches[0].rm_eo, act->replacement);
    if (subst_status == NULL)
    {
      ERROR ("Target `replace': subst (buffer = %s, start = %zu, end = %zu, "
          "replacement = %s) failed.",
          buffer, (size_t) matches[0].rm_so, (size_t) matches[0].rm_eo,
          act->replacement);
      continue;
    }
    sstrncpy (buffer, temp, sizeof (buffer));

    DEBUG ("target_replace plugin: tr_action_invoke: -- buffer = %s;", buffer);
  } /* for (act = act_head; act != NULL; act = act->next) */

  if ((may_be_empty == 0) && (buffer[0] == 0))
  {
    WARNING ("Target `replace': Replacement resulted in an empty string, "
        "which is not allowed for this buffer (`host' or `plugin').");
    return (0);
  }

  DEBUG ("target_replace plugin: tr_action_invoke: -> buffer = %s;", buffer);
  sstrncpy (buffer_in, buffer, buffer_in_size);

  return (0);
} /* }}} int tr_action_invoke */

static int tr_destroy (void **user_data) /* {{{ */
{
  tr_data_t *data;

  if (user_data == NULL)
    return (-EINVAL);

  data = *user_data;
  if (data == NULL)
    return (0);

  tr_action_destroy (data->host);
  tr_action_destroy (data->plugin);
  tr_action_destroy (data->plugin_instance);
  /* tr_action_destroy (data->type); */
  tr_action_destroy (data->type_instance);
  sfree (data);

  return (0);
} /* }}} int tr_destroy */

static int tr_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  tr_data_t *data;
  int status;
  int i;

  data = (tr_data_t *) malloc (sizeof (*data));
  if (data == NULL)
  {
    ERROR ("tr_create: malloc failed.");
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
      status = tr_config_add_action (&data->host, child,
          /* may be empty = */ 0);
    else if (strcasecmp ("Plugin", child->key) == 0)
      status = tr_config_add_action (&data->plugin, child,
          /* may be empty = */ 0);
    else if (strcasecmp ("PluginInstance", child->key) == 0)
      status = tr_config_add_action (&data->plugin_instance, child,
          /* may be empty = */ 1);
#if 0
    else if (strcasecmp ("Type", child->key) == 0)
      status = tr_config_add_action (&data->type, child,
          /* may be empty = */ 0);
#endif
    else if (strcasecmp ("TypeInstance", child->key) == 0)
      status = tr_config_add_action (&data->type_instance, child,
          /* may be empty = */ 1);
    else
    {
      ERROR ("Target `replace': The `%s' configuration option is not understood "
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
      ERROR ("Target `replace': You need to set at lease one of `Host', "
          "`Plugin', `PluginInstance', `Type', or `TypeInstance'.");
      status = -1;
    }

    break;
  }

  if (status != 0)
  {
    tr_destroy ((void *) &data);
    return (status);
  }

  *user_data = data;
  return (0);
} /* }}} int tr_create */

static int tr_invoke (const data_set_t *ds, value_list_t *vl, /* {{{ */
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  tr_data_t *data;

  if ((ds == NULL) || (vl == NULL) || (user_data == NULL))
    return (-EINVAL);

  data = *user_data;
  if (data == NULL)
  {
    ERROR ("Target `replace': Invoke: `data' is NULL.");
    return (-EINVAL);
  }

#define HANDLE_FIELD(f,e) \
  if (data->f != NULL) \
    tr_action_invoke (data->f, vl->f, sizeof (vl->f), e)
  HANDLE_FIELD (host, 0);
  HANDLE_FIELD (plugin, 0);
  HANDLE_FIELD (plugin_instance, 1);
  /* HANDLE_FIELD (type); */
  HANDLE_FIELD (type_instance, 1);

  return (FC_TARGET_CONTINUE);
} /* }}} int tr_invoke */

void module_register (void)
{
	target_proc_t tproc;

	memset (&tproc, 0, sizeof (tproc));
	tproc.create  = tr_create;
	tproc.destroy = tr_destroy;
	tproc.invoke  = tr_invoke;
	fc_register_target ("replace", tproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */

