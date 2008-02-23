/*
 * collectd - src/utils_logtail.c
 * Copyright (C) 2007-2008  C-Ware, Inc.
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
 *   Luke Heberling <lukeh at c-ware.com>
 *
 * Description:
 *   Encapsulates useful code to plugins which must parse a log file.
 */

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_tail.h"
#include "utils_llist.h"
#include "utils_avltree.h"

struct logtail_instance_s
{
  char *name;
  cu_tail_t *tail;
  c_avl_tree_t *tree;
  llist_t *list;
  uint cache_size;
  unsigned long *counters;
};
typedef struct logtail_instance_s logtail_instance_t;

static void submit (const char *plugin, const char *plugin_instance,
    const char *name, value_t value)
{
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[1];

  values[0] = value;

  vl.values = values;
  vl.values_len = 1;
  vl.time = time (NULL);
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, plugin, sizeof (vl.plugin));
  sstrncpy (vl.type_instance, "", sizeof (vl.type_instance));
  sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));

  plugin_dispatch_values (name, &vl);
} /* static void submit */

static int destroy_instance (logtail_instance_t *inst)
{
  if (inst == NULL)
    return (-1);

  sfree (inst->name);
  if (inst->tail != NULL)
  {
    cu_tail_destroy (inst->tail);
    inst->tail = NULL;
  }
  if (inst->tree != NULL)
  {
    c_avl_destroy (inst->tree);
    inst->tree = NULL;
  }
  assert ((inst->list == NULL) || (llist_size (inst->list) == 0));
  if (inst->list != NULL)
  {
    llist_destroy (inst->list);
    inst->list = NULL;
  }

  sfree (inst->counters);
  sfree (inst);

  return (0);
} /* int destroy_instance */

int logtail_term (llist_t **instances)
{
  llentry_t *entry;
  llentry_t *prev;

  llentry_t *lentry;
  llentry_t *lprev;

  logtail_instance_t *instance;

  if (*instances != NULL)
  {
    entry = llist_head (*instances);
    while (entry)
    {
      prev = entry;
      entry = entry->next;

      instance = prev->value;
      if (instance->list != NULL)
      {
	lentry = llist_head (instance->list);
	while (lentry)
	{
	  lprev = lentry;
	  lentry = lentry->next;
	  if (lprev->key != NULL)
	    free (lprev->key);
	  if (lprev->value != NULL)
	    free (lprev->value);
	  llist_remove (instance->list, lprev);
	  llentry_destroy (lprev);
	}
      }

      llist_remove (*instances, prev);
      llentry_destroy (prev);
      destroy_instance (instance);
    }

    llist_destroy (*instances);
    *instances = NULL;
  }

  return (0);
} /* int logtail_term */

int logtail_init (llist_t **instances)
{
  if (*instances == NULL)
    *instances = llist_create();

  return (*instances == NULL);
} /* int logtail_init */

int logtail_read (llist_t **instances, tailfunc *func, char *plugin,
    char **counter_instances)
{
  llentry_t *entry;
  char buffer[2048];
  int status;
  int i;

  for (entry = llist_head (*instances); entry != NULL; entry = entry->next )
  {
    logtail_instance_t *instance = (logtail_instance_t *) entry->value;

    status = cu_tail_read (instance->tail, buffer, sizeof (buffer),
	func, instance);
    if (status != 0)
      continue;

    for (i = 0; counter_instances[i] != NULL; i++)
    {
      char *name = counter_instances[i];
      value_t value;
      
      value.counter = (counter_t) instance->counters[i];

      submit (plugin, instance->name, name, value);
    }
  }

  return (0);
} /* int logtail_read */

int logtail_config (llist_t **instances, oconfig_item_t *ci, char *plugin,
    char **names, char *default_file, int default_cache_size)
{
  int counterslen = 0;
  logtail_instance_t *instance;

  llentry_t *entry;
  char *tail_file;

  oconfig_item_t *gchild;
  int gchildren;

  oconfig_item_t *child = ci->children;
  int children = ci->children_num;

  while (*(names++) != NULL)
    counterslen += sizeof (unsigned long);

  if (*instances == NULL)
  {
    *instances = llist_create();
    if (*instances == NULL)
      return 1;
  }


  for (; children; --children, ++child)
  {
    tail_file = NULL;

    if (strcmp (child->key, "Instance") != 0)
    {
      WARNING ("%s plugin: Ignoring unknown"
	  " config option `%s'.", plugin, child->key);
      continue;
    }

    if ((child->values_num != 1) ||
	(child->values[0].type != OCONFIG_TYPE_STRING))
    {
      WARNING ("%s plugin: `Instance' needs exactly"
	  " one string argument.", plugin);
      continue;
    }

    instance = malloc (sizeof (logtail_instance_t));
    if (instance == NULL)
    {
      ERROR ("%s plugin: `malloc' failed.", plugin);
      return 1;
    }
    memset (instance, '\0', sizeof (logtail_instance_t));

    instance->counters = malloc (counterslen);
    if (instance->counters == NULL)
    {
      ERROR ("%s plugin: `malloc' failed.", plugin);
      destroy_instance (instance);
      return 1;
    }
    memset (instance->counters, '\0', counterslen);

    instance->name = strdup (child->values[0].value.string);
    if (instance->name == NULL)
    {
      ERROR ("%s plugin: `strdup' failed.", plugin);
      destroy_instance (instance);
      return 1;
    }

    instance->list = llist_create();
    if (instance->list == NULL)
    {
      ERROR ("%s plugin: `llist_create' failed.", plugin);
      destroy_instance (instance);
      return 1;
    }

    instance->tree = c_avl_create ((void *)strcmp);
    if (instance->tree == NULL)
    {
      ERROR ("%s plugin: `c_avl_create' failed.", plugin);
      destroy_instance (instance);
      return 1;
    }

    entry = llentry_create (instance->name, instance);
    if (entry == NULL)
    {
      ERROR ("%s plugin: `llentry_create' failed.", plugin);
      destroy_instance (instance);
      return 1;
    }

    gchild = child->children;
    gchildren = child->children_num;

    for (; gchildren; --gchildren, ++gchild)
    {
      if (strcmp (gchild->key, "LogFile") == 0)
      {
	if (gchild->values_num != 1 || 
	    gchild->values[0].type != OCONFIG_TYPE_STRING)
	{
	  WARNING ("%s plugin: config option `%s'"
	      " should have exactly one string value.",
	      plugin, gchild->key);
	  continue;
	}
	if (tail_file != NULL)
	{
	  WARNING ("%s plugin: ignoring extraneous"
	      " `LogFile' config option.", plugin);
	  continue;
	}
	tail_file = gchild->values[0].value.string;
      }
      else if (strcmp (gchild->key, "CacheSize") == 0)
      {
	if (gchild->values_num != 1 
	    || gchild->values[0].type != OCONFIG_TYPE_NUMBER)
	{
	  WARNING ("%s plugin: config option `%s'"
	      " should have exactly one numerical value.",
	      plugin, gchild->key);
	  continue;
	}
	if (instance->cache_size)
	{
	  WARNING ("%s plugin: ignoring extraneous"
	      " `CacheSize' config option.", plugin);
	  continue;
	}
	instance->cache_size = gchild->values[0].value.number;
      }
      else
      {
	WARNING ("%s plugin: Ignoring unknown config option"
	    " `%s'.", plugin, gchild->key);
	continue;
      }

      if (gchild->children_num)
      {
	WARNING ("%s plugin: config option `%s' should not"
	    " have children.", plugin, gchild->key);
      }
    }

    if (tail_file == NULL)
      tail_file = default_file;
    instance->tail = cu_tail_create (tail_file);
    if (instance->tail == NULL)
    {
      ERROR ("%s plugin: `cu_tail_create' failed.", plugin);
      destroy_instance (instance);

      llentry_destroy (entry);
      return 1;
    }

    if (instance->cache_size == 0)
      instance->cache_size = default_cache_size;

    llist_append (*instances, entry);
  }

  return 0;
} /* int logtail_config */

unsigned long *logtail_counters (logtail_instance_t *instance)
{
  return instance->counters;
} /* unsigned log *logtail_counters */

int logtail_cache (logtail_instance_t *instance, char *plugin, char *key, void **data, int len)
{
  llentry_t *entry = NULL;

  if (c_avl_get (instance->tree, key, (void*)&entry) == 0)
  {
    *data = entry->value;
    return (0);
  }

  if ((key = strdup (key)) == NULL)
  {
    ERROR ("%s plugin: `strdup' failed.", plugin);
    return (0);
  }

  if (data != NULL && (*data = malloc (len)) == NULL)
  {
    ERROR ("%s plugin: `malloc' failed.", plugin);
    free (key);
    return (0);
  }

  if (data != NULL)
    memset (*data, '\0', len);

  entry = llentry_create (key, data == NULL ? NULL : *data);
  if (entry == NULL)
  {
    ERROR ("%s plugin: `llentry_create' failed.", plugin);
    free (key);
    if (data !=NULL)
      free (*data);
    return (0);
  }

  if (c_avl_insert (instance->tree, key, entry) != 0)
  {
    ERROR ("%s plugin: `c_avl_insert' failed.", plugin);
    llentry_destroy (entry);
    free (key);
    if (data != NULL)
      free (*data);
    return (0);
  }

  llist_prepend (instance->list, entry);

  while (llist_size (instance->list) > instance->cache_size &&
      (entry = llist_tail (instance->list)) != NULL )
  {
    c_avl_remove (instance->tree, entry->key, NULL, NULL);
    llist_remove (instance->list, entry);
    free (entry->key);
    if (entry->value != NULL)
      free (entry->value);
    llentry_destroy (entry);
  }

  return (1);
}

void logtail_decache (logtail_instance_t *instance, char *key)
{
  llentry_t *entry = NULL;
  if (c_avl_remove (instance->tree, key, NULL, (void*)&entry))
    return;

  llist_remove (instance->list, entry);
  free (entry->key);
  if (entry->value != NULL)
    free (entry->value);

  llentry_destroy (entry);
}

/* vim: set sw=2 sts=2 ts=8 : */
