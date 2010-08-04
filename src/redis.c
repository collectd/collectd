/**
 * collectd - src/redis.c, based on src/memcached.c
 * Copyright (C) 2010       Andrés J. Díaz <ajdiaz@connectical.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Andrés J. Díaz <ajdiaz@connectical.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_avltree.h"

#include <pthread.h>
#include <credis.h>

#define REDIS_DEF_HOST "127.0.0.1"
#define REDIS_DEF_PORT 6379
#define MAX_REDIS_NODE_NAME 64

/* Redis plugin configuration example:
 *
 * <Plugin redis>
 *   <Node mynode>
 *     Host localhost
 *     Port 6379
 *     Timeout 2000
 *   </Node>
 * </Plugin>
 */

static c_avl_tree_t *redis_tree = NULL;
static pthread_mutex_t redis_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct redis_node_s {
  char name[MAX_REDIS_NODE_NAME];
  char host[HOST_NAME_MAX];
  int port;
  int timeout;
} redis_node_t;

static int redis_config_node (redis_node_t *rn, oconfig_item_t *ci) /* {{{ */
{
  int i;
  int status = 0;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("redis plugin: The `Node' block needs exactly one string "
        "argument.");
    return (-1);
  }

  if (ci->children_num < 1)
  {
    WARNING ("redis plugin: The `Node' block needs at least one option.");
    return (-1);
  }

  sstrncpy (rn->name, ci->values[0].value.string, sizeof (rn->name));

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    status = 0;

    if (strcasecmp ("Host", option->key) == 0)
      status = cf_util_get_string_buffer (option, rn->host, HOST_NAME_MAX);
    else if (strcasecmp ("Port", option->key) == 0)
      status = rn->port = cf_util_get_port_number (option);
    else if (strcasecmp ("Timeout", option->key) == 0)
      status = cf_util_get_int (option, &rn->timeout);
    else
    {
      WARNING ("redis plugin: Option `%s' not allowed inside a `Node' "
          "block.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  return (status);
} /* }}} */

static redis_node_t *redis_node_get (const char *name, redis_node_t *rn) /* {{{ */
{
  if (c_avl_get (redis_tree, name, (void *) rn) == 0)
    return (rn);
  else
    return (NULL);
} /* }}} */

static int redis_node_add (const redis_node_t *rn) /* {{{ */
{
  int status;
  redis_node_t *rn_copy = NULL;
  redis_node_t *rn_ptr;
  redis_node_t  rn_get;

  rn_copy = (redis_node_t *) malloc (sizeof (redis_node_t));
  if (rn_copy == NULL)
  {
    sfree (rn_copy);
    ERROR ("redis plugin: malloc failed adding redis_node to the tree.");
    return (-1);
  }
  memcpy (rn_copy, rn, sizeof (redis_node_t));
  if (*rn_copy->name == '\0')
  {
    (void) strncpy(rn_copy->name, "default", MAX_REDIS_NODE_NAME); /* in theory never fails */
  }

  DEBUG ("redis plugin: adding entry `%s' to the tree.", rn_copy->name);

  pthread_mutex_lock (&redis_lock);

  if ( (rn_ptr = redis_node_get (rn_copy->name, &rn_get)) != NULL )
  {
    WARNING ("redis plugin: the node `%s' override a previous node with same node.", rn_copy->name);
  }

  status = c_avl_insert (redis_tree, rn_copy->name, rn_copy);
  pthread_mutex_unlock (&redis_lock);

  if (status != 0)
  {
    ERROR ("redis plugin: c_avl_insert (%s) failed adding noew node.", rn_copy->name);
    sfree (rn_copy);
    return (-1);
  }

  return (status);
} /* }}} */

static int redis_config (oconfig_item_t *ci) /* {{{ */
{
  int status;
  int i;

  redis_node_t rn = {
    .name = "",
    .host = "",
    .port = REDIS_DEF_PORT,
    .timeout = 2000
  };

  if (redis_tree == NULL)
  {
    redis_tree = c_avl_create ((void *) strcmp);
    if (redis_tree == NULL)
    {
      ERROR ("redis plugin: c_avl_create failed reading config.");
      return (-1);
    }
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Node", option->key) == 0)
    {
      if ( (status = redis_config_node (&rn, option)) == 0 )
        status = redis_node_add (&rn);
    }
    else if (strcasecmp ("Host", option->key) == 0)
      status = cf_util_get_string_buffer (option, rn.host, HOST_NAME_MAX);
    else if (strcasecmp ("Port", option->key) == 0)
      status = rn.port = cf_util_get_port_number (option);
    else if (strcasecmp ("Timeout", option->key) == 0)
      status = cf_util_get_int (option, &rn.timeout);
    else
    {
      WARNING ("redis plugin: Option `%s' not allowed in redis"
          " configuration.", option->key);
      status = -1;
    }


    if (status != 0)
      break;
  }

  if ( status == 0 && *rn.name != '\0') {
    status = redis_node_add (&rn);
  }

  return (status);
} /* }}} */

  __attribute__ ((nonnull(2)))
static void redis_submit_g (char *plugin_instance,
    const char *type, const char *type_instance,
    gauge_t value) /* {{{ */
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "redis", sizeof (vl.plugin));
  if (plugin_instance != NULL)
    sstrncpy (vl.plugin_instance, plugin_instance,
        sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));
  if (type_instance != NULL)
    sstrncpy (vl.type_instance, type_instance,
        sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* }}} */

  __attribute__ ((nonnull(2)))
static void redis_submit_c (char *plugin_instance,
    const char *type, const char *type_instance,
    counter_t value) /* {{{ */
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].counter = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "redis", sizeof (vl.plugin));
  if (plugin_instance != NULL)
    sstrncpy (vl.plugin_instance, plugin_instance,
        sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));
  if (type_instance != NULL)
    sstrncpy (vl.type_instance, type_instance,
        sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* }}} */

static int redis_read (void) /* {{{ */
{
  REDIS rh;
  REDIS_INFO info;

  char key[64];
  int status;
  c_avl_iterator_t *iter;
  redis_node_t *rn;

  status = -1;
  if ( (iter = c_avl_get_iterator (redis_tree)) == NULL )
  {
    ERROR ("redis plugin: unable to iterate redis tree.");
    return (-1);
  }

  while (c_avl_iterator_next (iter, (void *) &key, (void *) &rn) == 0)
  {
    DEBUG ("redis plugin: querying info from node `%s'.", rn->name);

    if ( (rh = credis_connect (rn->host, rn->port, rn->timeout)) == NULL )
    {
      ERROR ("redis plugin: unable to connect to node `%s' (%s:%d).", rn->name, rn->host, rn->port);
      status = -1;
      break;
    }

    if ( (status = credis_info (rh, &info)) == -1 )
    {
      WARNING ("redis plugin: unable to get info from node `%s'.", rn->name);
      credis_close (rh);
      break;
    }

    /* typedef struct _cr_info {
     *   char redis_version[CREDIS_VERSION_STRING_SIZE];
     *   int bgsave_in_progress;
     *   int connected_clients;
     *   int connected_slaves;
     *   unsigned int used_memory;
     *   long long changes_since_last_save;
     *   int last_save_time;
     *   long long total_connections_received;
     *   long long total_commands_processed;
     *   int uptime_in_seconds;
     *   int uptime_in_days;
     *   int role;
     * } REDIS_INFO; */

    DEBUG ("redis plugin: received info from node `%s': connected_clients = %d; "
        "connected_slaves = %d; used_memory = %lu; changes_since_last_save = %lld; "
        "bgsave_in_progress = %d; total_connections_received = %lld; "
        "total_commands_processed = %lld; uptime_in_seconds = %ld", rn->name,
        info.connected_clients, info.connected_slaves, info.used_memory,
        info.changes_since_last_save, info.bgsave_in_progress,
        info.total_connections_received, info.total_commands_processed,
        info.uptime_in_seconds);

    redis_submit_g (rn->name, "connected_clients", NULL, info.connected_clients);
    redis_submit_g (rn->name, "connected_slaves", NULL, info.connected_slaves);
    redis_submit_g (rn->name, "used_memory", NULL, info.used_memory);
    redis_submit_g (rn->name, "changes_since_last_save", NULL, info.changes_since_last_save);
    redis_submit_g (rn->name, "bgsave_in_progress", NULL, info.bgsave_in_progress);
    redis_submit_c (rn->name, "total_connections_received", NULL, info.total_connections_received);
    redis_submit_c (rn->name, "total_commands_processed", NULL, info.total_commands_processed);
    redis_submit_c (rn->name, "uptime_in_seconds", NULL, info.uptime_in_seconds);

    credis_close (rh);
    status = 0;
  }

  c_avl_iterator_destroy(iter);
  if ( status != 0 )
  {
    return (-1);
  }

  return 0;
}
/* }}} */

void module_register (void) /* {{{ */
{
  plugin_register_complex_config ("redis", redis_config);
  plugin_register_read ("redis", redis_read);
  /* TODO: plugin_register_write: one redis list per value id with
   * X elements */
}
/* }}} */

/* vim: set sw=2 sts=2 et fdm=marker : */
