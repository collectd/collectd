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
#define MAX_REDIS_PASSWD_LENGTH 512
#define MAX_REDIS_VAL_SIZE 256
#define MAX_REDIS_QUERY 2048

/* Redis plugin configuration example:
 *
 * <Plugin redis>
 *   <Node "mynode">
 *     Host "localhost"
 *     Port "6379"
 *     Timeout 2
 *     Password "foobar"
 *   </Node>
 * </Plugin>
 */

struct redis_query_s;
typedef struct redis_query_s redis_query_t;
struct redis_query_s
{
    char query[MAX_REDIS_QUERY];
    char type[DATA_MAX_NAME_LEN];
    char instance[DATA_MAX_NAME_LEN];
    redis_query_t *next;
};

struct redis_node_s;
typedef struct redis_node_s redis_node_t;
struct redis_node_s
{
  char name[MAX_REDIS_NODE_NAME];
  char host[HOST_NAME_MAX];
  char passwd[MAX_REDIS_PASSWD_LENGTH];
  int port;
  struct timeval timeout;
  redis_query_t *queries;

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

static redis_query_t *redis_config_query (oconfig_item_t *ci) /* {{{ */
{
    redis_query_t *rq;
    int status;
    int i;

    rq = calloc(1, sizeof(*rq));
    if (rq == NULL) {
        ERROR("redis plugin: calloca failed adding redis_query.");
        return NULL;
    }
    status = cf_util_get_string_buffer(ci, rq->query, sizeof(rq->query));
    if (status != 0)
        goto err;

    /*
     * Default to a gauge type.
     */
    (void)strncpy(rq->type, "gauge", sizeof(rq->type));
    (void)strncpy(rq->instance, rq->query, sizeof(rq->instance));
    replace_special(rq->instance, sizeof(rq->instance));

    for (i = 0; i < ci->children_num; i++) {
        oconfig_item_t *option = ci->children + i;

        if (strcasecmp("Type", option->key) == 0) {
            status = cf_util_get_string_buffer(option, rq->type, sizeof(rq->type));
        } else if (strcasecmp("Instance", option->key) == 0) {
            status = cf_util_get_string_buffer(option, rq->instance, sizeof(rq->instance));
        } else {
            WARNING("redis plugin: unknown configuration option: %s", option->key);
            status = -1;
        }
        if (status != 0)
            goto err;
    }
    return rq;
 err:
    free(rq);
    return NULL;
} /* }}} */

static int redis_config_node (oconfig_item_t *ci) /* {{{ */
{
  redis_node_t rn;
  redis_query_t *rq;
  int i;
  int status;
  int timeout;

  memset (&rn, 0, sizeof (rn));
  sstrncpy (rn.host, REDIS_DEF_HOST, sizeof (rn.host));
  rn.port = REDIS_DEF_PORT;
  rn.timeout.tv_usec = REDIS_DEF_TIMEOUT;
  rn.queries = NULL;

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
    else if (strcasecmp ("Query", option->key) == 0)
    {
      rq = redis_config_query(option);
      if (rq == NULL) {
          status =1;
      } else {
          rq->next = rn.queries;
          rn.queries = rq;
      }
    }
    else if (strcasecmp ("Timeout", option->key) == 0)
    {
      status = cf_util_get_int (option, &timeout);
      if (status == 0) rn.timeout.tv_usec = timeout;
    }
    else if (strcasecmp ("Password", option->key) == 0)
      status = cf_util_get_string_buffer (option, rn.passwd, sizeof (rn.passwd));
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
    .timeout.tv_sec = 0,
    .timeout.tv_usec = REDIS_DEF_TIMEOUT,
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

int redis_handle_query (redisContext *rh, redis_node_t *rn, redis_query_t *rq) /* {{{ */
{
    redisReply *rr;
    const data_set_t *ds;
    value_t val;

    ds = plugin_get_ds (rq->type);
    if (!ds) {
        ERROR ("redis plugin: DataSet `%s' not defined.", rq->type);
        return (-1);
    }

    if (ds->ds_num != 1) {
        ERROR ("redis plugin: DS `%s' has too many types.", rq->type);
        return (-1);
    }

    if ((rr = redisCommand(rh, rq->query)) == NULL) {
        WARNING("redis plugin: unable to carry out query `%s'.", rq->query);
        return (-1);
    }

    switch (rr->type) {
    case REDIS_REPLY_INTEGER:
        switch (ds->ds[0].type) {
        case DS_TYPE_COUNTER:
            val.counter = (counter_t)rr->integer;
            break;
        case DS_TYPE_GAUGE:
            val.gauge = (gauge_t)rr->integer;
            break;
        case DS_TYPE_DERIVE:
            val.gauge = (derive_t)rr->integer;
            break;
        case DS_TYPE_ABSOLUTE:
            val.gauge = (absolute_t)rr->integer;
            break;
        }
        break;
    case REDIS_REPLY_STRING:
        if (parse_value (rr->str, &val, ds->ds[0].type) == -1) {
            WARNING("redis plugin: Unable to parse field `%s'.", rq->type);
            freeReplyObject (rr);
            return (-1);
        }
        break;
    default:
        WARNING("redis plugin: Cannot coerce redis type.");
        freeReplyObject(rr);
        return (-1);
    }

    redis_submit(rn->name, rq->type, (strlen(rq->instance) >0)?rq->instance:NULL, val);
    freeReplyObject (rr);
    return 0;
} /* }}} int redis_handle_info */

static int redis_read (void) /* {{{ */
{
  redis_node_t *rn;
  redis_query_t *rq;

  for (rn = nodes_head; rn != NULL; rn = rn->next)
  {
    redisContext *rh;
    redisReply   *rr;

    DEBUG ("redis plugin: querying info from node `%s' (%s:%d).", rn->name, rn->host, rn->port);

    rh = redisConnectWithTimeout ((char *)rn->host, rn->port, rn->timeout);
    if (rh == NULL)
    {
      ERROR ("redis plugin: unable to connect to node `%s' (%s:%d).", rn->name, rn->host, rn->port);
      continue;
    }

    if (strlen (rn->passwd) > 0)
    {
      DEBUG ("redis plugin: authenticanting node `%s' passwd(%s).", rn->name, rn->passwd);
      rr = redisCommand (rh, "AUTH %s", rn->passwd);

      if (rr == NULL || rr->type != REDIS_REPLY_STATUS)
      {
        WARNING ("redis plugin: unable to authenticate on node `%s'.", rn->name);
        if (rr != NULL)
          freeReplyObject (rr);

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
    redis_handle_info (rn->name, rr->str, "current_connections", "clients", "connected_clients", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "blocked_clients", NULL, "blocked_clients", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "memory", NULL, "used_memory", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "memory_lua", NULL, "used_memory_lua", DS_TYPE_GAUGE);
    /* changes_since_last_save: Deprecated in redis version 2.6 and above */
    redis_handle_info (rn->name, rr->str, "volatile_changes", NULL, "changes_since_last_save", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "total_connections", NULL, "total_connections_received", DS_TYPE_DERIVE);
    redis_handle_info (rn->name, rr->str, "total_operations", NULL, "total_commands_processed", DS_TYPE_DERIVE);
    redis_handle_info (rn->name, rr->str, "expired_keys", NULL, "expired_keys", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "pubsub", "channels", "pubsub_channels", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "pubsub", "patterns", "pubsub_patterns", DS_TYPE_GAUGE);
    redis_handle_info (rn->name, rr->str, "current_connections", "slaves", "connected_slaves", DS_TYPE_GAUGE);

    freeReplyObject (rr);

    for (rq = rn->queries; rq != NULL; rq = rq->next)
        redis_handle_query(rh, rn, rq);

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
