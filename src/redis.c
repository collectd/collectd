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
#include <sys/time.h>
#include <hiredis/hiredis.h>

#ifndef HOST_NAME_MAX
# define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#endif

#define REDIS_DEF_HOST   "localhost"
#define REDIS_DEF_PASSWD ""
#define REDIS_DEF_PORT    6379
#define REDIS_DEF_TIMEOUT 2000
#define MAX_REDIS_NODE_NAME 64
#define MAX_REDIS_QUERY_NAME 64
#define MAX_REDIS_COMMAND_LEN 256
#define MAX_REDIS_TYPE_LEN 256
#define MAX_REDIS_VAL_SIZE 256

/* Redis plugin configuration example:
 *
 * <Plugin redis>
 *   <Node "mynode">
 *     Host "localhost"
 *     Port "6379"
 *     Timeout 2
 *     Password "foobar"
 *     <Query "len-queue">
 *        Command "LLEN queue"
 *        Type "gauge"
 *     </Query>
 *   </Node>
 * </Plugin>
 */

struct redis_query_s;
typedef struct redis_query_s redis_query_t;
struct redis_query_s
{
  char name[MAX_REDIS_QUERY_NAME];
  char command[MAX_REDIS_COMMAND_LEN];
  char type[MAX_REDIS_TYPE_LEN];

  redis_query_t *next;
};

struct redis_node_s;
typedef struct redis_node_s redis_node_t;
struct redis_node_s
{
  char name[MAX_REDIS_NODE_NAME];
  char host[HOST_NAME_MAX];
  char passwd[HOST_NAME_MAX];
  int port;
  int timeout;
  redis_query_t *query;

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

static int redis_config_query_add (redis_node_t *rn, oconfig_item_t *ci) /* {{{ */
{
  redis_query_t *n;
  int status;
  int i;

  if ((n=malloc(sizeof(redis_query_t))) == NULL)
  {
    return (-1);
  }

  memset (n, 0, sizeof (redis_query_t));

  if ((status = cf_util_get_string_buffer (ci, n->name, sizeof (n->name))) != 0)
  {
    sfree(n);
    return (-1);
  }

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    if (strcasecmp ("Command", option->key) == 0)
    {
      if ((option->values_num != 1) || (option->values[0].type != OCONFIG_TYPE_STRING))
      {
        WARNING ("redis plugin: `Command' needs exactly one string argument.");
        continue;
      }
      status = cf_util_get_string_buffer (option, n->command, sizeof (n->command));
    }
    else if (strcasecmp ("Type", option->key) == 0)
    {
      if ((option->values_num != 1) || (option->values[0].type != OCONFIG_TYPE_STRING))
      {
        WARNING ("redis plugin: `Type' needs exactly one string argument.");
        continue;
      }
      status = cf_util_get_string_buffer (option, n->type, sizeof (n->type));
    }
    else
      WARNING ("redis plugin: Option `%s' not allowed inside a `Query' "
          "block. I'll ignore this option.", option->key);

    if (status != 0)
      break;
  }

  if (status != 0)
    return (status);

  if (rn->query == NULL)
  {
    rn->query = n;
  }
  else
  {
    redis_query_t *p;
    for(p=rn->query; p->next != NULL; p=p->next);
    p->next = n;
  }

  return (status);

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
    else if (strcasecmp ("Password", option->key) == 0)
      status = cf_util_get_string_buffer (option, rn.passwd, sizeof (rn.passwd));
    else if (strcasecmp ("Query", option->key) == 0)
    {
      status = redis_config_query_add(&rn, option);
    }
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
static void redis_submit (char *plugin_instance,
    const char *type, const char *type_instance,
    value_t value) /* {{{ */
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0] = value;

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
  redis_node_t rn = {
    .name = "default",
    .host = REDIS_DEF_HOST,
    .port = REDIS_DEF_PORT,
    .timeout = REDIS_DEF_TIMEOUT,
    .next = NULL
};

  if (nodes_head == NULL)
    redis_node_add (&rn);

  return (0);
} /* }}} int redis_init */

int redis_handle_info (char *node, char const *info_line, char const *type, char const *type_instance, char const *field_name, int ds_type) /* {{{ */
{
  char *str = strstr (info_line, field_name);
  static char buf[MAX_REDIS_VAL_SIZE];
  value_t val;
  if (str)
  {
    int i;

    str += strlen (field_name) + 1; /* also skip the ':' */
    for(i=0;(*str && (isdigit(*str) || *str == '.'));i++,str++)
      buf[i] = *str;
    buf[i] ='\0';

    if(parse_value (buf, &val, ds_type) == -1)
    {
      WARNING ("redis plugin: Unable to parse field `%s'.", field_name);
      return (-1);
    }

    redis_submit (node, type, type_instance, val);
    return (0);
  }
  return (-1);

} /* }}} int redis_handle_info */

static int redis_read (void) /* {{{ */
{
  redis_node_t *rn;
  redis_query_t *query;

  const data_set_t *ds;
  value_list_t vl = VALUE_LIST_INIT;
  value_t val;


  for (rn = nodes_head; rn != NULL; rn = rn->next)
  {
    redisContext *rh;
    redisReply   *rr;

    struct timeval tmout;

    tmout.tv_sec = rn->timeout;
    tmout.tv_usec = 0;

    DEBUG ("redis plugin: querying info from node `%s' (%s:%d).", rn->name, rn->host, rn->port);

    rh = redisConnectWithTimeout ((char *)rn->host, rn->port, tmout);
    if (rh == NULL)
    {
      ERROR ("redis plugin: unable to connect to node `%s' (%s:%d).", rn->name, rn->host, rn->port);
      continue;
    }

    if (strlen (rn->passwd) > 0)
    {
      DEBUG ("redis plugin: authenticanting node `%s' passwd(%s).", rn->name, rn->passwd);
      rr = redisCommand (rh, "AUTH %s", rn->passwd);

      if (rr == NULL || rr->type != 5)
      {
        WARNING ("redis plugin: unable to authenticate on node `%s'.", rn->name);
        redisFree (rh);
        continue;
      }
    }

    if ((rr = redisCommand(rh, "INFO")) == NULL)
    {
      WARNING ("redis plugin: unable to connect to node `%s'.", rn->name);
      redisFree (rh);
      continue;
    }

    redis_handle_info (rn->name, rr->str, "uptime", NULL, "uptime_in_seconds", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "connections", "clients", "connected_clients", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "connections", "slaves", "connected_slaves", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "blocked_clients", NULL, "blocked_clients", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "memory", NULL, "used_memory", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "changes_since_last_save", NULL, "changes_since_last_save", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "operations", NULL, "total_commands_processed", DS_TYPE_DERIVE);
    redis_handle_info (rn->name, rr->str, "expired_keys", NULL, "expired_keys", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "pubsub", "patterns", "pubsub_patterns", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "pubsub", "channels", "pubsub_channels", DS_TYPE_GAUGE);

    /* Read custom queries */
    for (query=rn->query; query!=NULL; query=query->next)
    {
      if ((rr = redisCommand (rh, query->command)) == NULL)
      {
        WARNING ("redis plugin: unable to execute query `%s' on node `%s'.", query->name, rn->name);
        continue;
      }

      if(rr->type != REDIS_REPLY_INTEGER)
      {
        WARNING ("redis plugin: unable to get reply for query `%s' on node `%s', integer expected.", query->name, rn->name);
        continue;
      }
      DEBUG("Get data from query `%s' executing `%s' on node `%s'.", query->name, query->command, rn->name);


      ds = plugin_get_ds (query->type);
      if (!ds)
      {
        ERROR ("redis plugin: DataSet `%s' not defined.", query->type);
        continue;
      }

      if (ds->ds_num != 1)
      {
        ERROR ("redis plugin: DataSet `%s' requires %i values, but config talks about %i",
            query->type, ds->ds_num, 1);
        continue;
      }

      vl.values_len = ds->ds_num;
      vl.values = &val;

      if (ds->ds[0].type == DS_TYPE_COUNTER)
        vl.values[0].counter = rr->integer;
      else if (ds->ds[0].type == DS_TYPE_DERIVE)
        vl.values[0].derive = rr->integer;
      else if (ds->ds[0].type == DS_TYPE_ABSOLUTE)
        vl.values[0].absolute = rr->integer;
      else if (ds->ds[0].type == DS_TYPE_GAUGE)
        vl.values[0].gauge = rr->integer;
      else
      {
        ERROR ("redis plugin: Unknown type `%i' for datasource.", ds->ds[0].type);
        continue;
      }

      sstrncpy (vl.host, hostname_g, sizeof (vl.host));
      sstrncpy (vl.plugin, "redis", sizeof (vl.plugin));
      sstrncpy (vl.plugin_instance, rn->name, sizeof (vl.plugin));
      strncat (vl.plugin_instance, "-query", sizeof (vl.plugin));
      sstrncpy (vl.type, query->type, sizeof (vl.type));
      sstrncpy (vl.type_instance, query->name, sizeof (vl.type));

      plugin_dispatch_values(&vl);

    }

    redisFree (rh);
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
