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

#include <pthread.h>
#include <credis.h>

#define REDIS_DEF_HOST   "localhost"
#define REDIS_DEF_PORT    6379
#define REDIS_DEF_TIMEOUT 2000
#define MAX_REDIS_NODE_NAME 64

/* Redis plugin configuration example:
 *
 * <Plugin redis>
 *   <Node "mynode">
 *     Host "localhost"
 *     Port "6379"
 *     Timeout 2000
 *   </Node>
 * </Plugin>
 */

struct redis_node_s;
typedef struct redis_node_s redis_node_t;
struct redis_node_s
{
  char name[MAX_REDIS_NODE_NAME];
  char host[HOST_NAME_MAX];
  int port;
  int timeout;

  redis_node_t *next;
};

static redis_node_t *nodes_head = NULL;

static int redis_node_add (const redis_node_t *rn) /* {{{ */
{
  redis_node_t *rn_copy;
  redis_node_t *rn_ptr;

  /* Check for duplicates first */
  for (rn_ptr = nodes_head; rn_ptr != NULL; rn_ptr = rn_ptr->next)
    if (strcmp (rn->name, rn_ptr->name) == 0)
      break;

  if (rn_ptr != NULL)
  {
    ERROR ("redis plugin: A node with the name `%s' already exists.",
        rn->name);
    return (-1);
  }

  rn_copy = malloc (sizeof (*rn_copy));
  if (rn_copy == NULL)
  {
    ERROR ("redis plugin: malloc failed adding redis_node to the tree.");
    return (-1);
  }

  memcpy (rn_copy, rn, sizeof (*rn_copy));
  rn_copy->next = NULL;

  DEBUG ("redis plugin: Adding node \"%s\".", rn->name);

  if (nodes_head == NULL)
    nodes_head = rn_copy;
  else
  {
    rn_ptr = nodes_head;
    while (rn_ptr->next != NULL)
      rn_ptr = rn_ptr->next;
    rn_ptr->next = rn_copy;
  }

  return (0);
} /* }}} */

static int redis_config_node (oconfig_item_t *ci) /* {{{ */
{
  redis_node_t rn;
  int i;
  int status;

  memset (&rn, 0, sizeof (rn));
  sstrncpy (rn.host, REDIS_DEF_HOST, sizeof (rn.host));
  rn.port = REDIS_DEF_PORT;
  rn.timeout = REDIS_DEF_TIMEOUT;

  status = cf_util_get_string_buffer (ci, rn.name, sizeof (rn.name));
  if (status != 0)
    return (status);

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Host", option->key) == 0)
      status = cf_util_get_string_buffer (option, rn.host, sizeof (rn.host));
    else if (strcasecmp ("Port", option->key) == 0)
    {
      status = cf_util_get_port_number (option);
      if (status > 0)
      {
        rn.port = status;
        status = 0;
      }
    }
    else if (strcasecmp ("Timeout", option->key) == 0)
      status = cf_util_get_int (option, &rn.timeout);
    else
      WARNING ("redis plugin: Option `%s' not allowed inside a `Node' "
          "block. I'll ignore this option.", option->key);

    if (status != 0)
      break;
  }

  if (status != 0)
    return (status);

  return (redis_node_add (&rn));
} /* }}} int redis_config_node */

static int redis_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Node", option->key) == 0)
      redis_config_node (option);
    else
      WARNING ("redis plugin: Option `%s' not allowed in redis"
          " configuration. It will be ignored.", option->key);
  }

  if (nodes_head == NULL)
  {
    ERROR ("redis plugin: No valid node configuration could be found.");
    return (ENOENT);
  }

  return (0);
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
static void redis_submit_d (char *plugin_instance,
    const char *type, const char *type_instance,
    derive_t value) /* {{{ */
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].derive = value;

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

static int redis_init (void) /* {{{ */
{
  redis_node_t rn = { "default", REDIS_DEF_HOST, REDIS_DEF_PORT,
    REDIS_DEF_TIMEOUT, /* next = */ NULL };

  if (nodes_head == NULL)
    redis_node_add (&rn);

  return (0);
} /* }}} int redis_init */

static int redis_read (void) /* {{{ */
{
  redis_node_t *rn;

  for (rn = nodes_head; rn != NULL; rn = rn->next)
  {
    REDIS rh;
    REDIS_INFO info;

    int status;

    DEBUG ("redis plugin: querying info from node `%s' (%s:%d).", rn->name, rn->host, rn->port);

    rh = credis_connect (rn->host, rn->port, rn->timeout);
    if (rh == NULL)
    {
      ERROR ("redis plugin: unable to connect to node `%s' (%s:%d).", rn->name, rn->host, rn->port);
      continue;
    }

    memset (&info, 0, sizeof (info));
    status = credis_info (rh, &info);
    if (status != 0)
    {
      WARNING ("redis plugin: unable to get info from node `%s'.", rn->name);
      credis_close (rh);
      continue;
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

    redis_submit_g (rn->name, "current_connections", "clients", info.connected_clients);
    redis_submit_g (rn->name, "current_connections", "slaves", info.connected_slaves);
    redis_submit_g (rn->name, "memory", "used", info.used_memory);
    redis_submit_g (rn->name, "volatile_changes", NULL, info.changes_since_last_save);
    redis_submit_d (rn->name, "total_connections", NULL, info.total_connections_received);
    redis_submit_d (rn->name, "total_operations", NULL, info.total_commands_processed);

    credis_close (rh);
  }

  return 0;
}
/* }}} */

void module_register (void) /* {{{ */
{
  plugin_register_complex_config ("redis", redis_config);
  plugin_register_init ("redis", redis_init);
  plugin_register_read ("redis", redis_read);
  /* TODO: plugin_register_write: one redis list per value id with
   * X elements */
}
/* }}} */

/* vim: set sw=2 sts=2 et fdm=marker : */
