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
#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#endif

#define REDIS_DEF_HOSTNAME      "localhost"
#define REDIS_DEF_HOST          "127.0.0.1"
#define REDIS_DEF_PASSWD        ""
#define REDIS_DEF_PORT          (6379)
#define REDIS_DEF_TIMEOUT       (2000)
#define MAX_REDIS_NODE_NAME     (64)
#define MAX_REDIS_QUERY_NAME    (64)
#define MAX_REDIS_COMMAND_LEN   (256)
#define MAX_REDIS_TYPE_LEN      (256)

/* Redis plugin configuration example:
 *
 * <Plugin redis>
 *   GlobalData true
 *   PerHostData true
 *   <Node "mynode">
 *     Hostname "my_machine"
 *     Host "127.0.0.1"
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
typedef struct redis_query_s RedisQuery;

struct redis_query_s
{
  char name[MAX_REDIS_QUERY_NAME];
  char command[MAX_REDIS_COMMAND_LEN];
  char type[MAX_REDIS_TYPE_LEN];
  RedisQuery *next;
};

struct redis_node_s;
typedef struct redis_node_s RedisNode;

struct redis_node_s
{
  char name[MAX_REDIS_NODE_NAME];
  char hostname[HOST_NAME_MAX];
  char host[HOST_NAME_MAX];
  char passwd[HOST_NAME_MAX];
  int port;
  int timeout;
  RedisQuery *query;
  RedisNode *next;
};

struct redis_info_s;
typedef struct redis_info_s RedisInfo;

struct redis_info_s
{
  // #Server
  unsigned long long int uptime_in_seconds;

  // #Clients
  unsigned long long int connected_clients;
  unsigned long long int blocked_clients;
  unsigned long long int client_biggest_input_buf;
  unsigned long long int client_longest_output_list;

  // #Memory
  unsigned long long int used_memory;
  unsigned long long int used_memory_lua;
  unsigned long long int used_memory_peak;
  unsigned long long int used_memory_rss;
  double mem_fragmentation_ratio;

  // #Persistence
  unsigned long long int rdb_changes_since_last_save;
  unsigned long long int rdb_bgsave_in_progress;
  unsigned long long int rdb_last_bgsave_time_sec;
  unsigned long long int aof_last_rewrite_time_sec;
  unsigned long long int aof_rewrite_in_progress;

  // #Stats
  unsigned long long int total_connections_received;
  unsigned long long int total_commands_processed;
  unsigned long long int instantaneous_ops_per_sec;
  unsigned long long int rejected_connections;
  unsigned long long int expired_keys;
  unsigned long long int evicted_keys;
  unsigned long long int keyspace_hits;
  unsigned long long int keyspace_misses;
  unsigned long long int pubsub_patterns;
  unsigned long long int pubsub_channels;
  unsigned long long int latest_fork_usec;

  // #Replication
  unsigned long long int connected_slaves;

  // #CPU
  double used_cpu_sys;
  double used_cpu_user;
  double used_cpu_sys_children;
  double used_cpu_user_children;

  // #Keyspace
  unsigned long long int keys;
};

struct redis_host_s;
typedef struct redis_host_s RedisHost;

struct redis_host_s
{
  char hostname[HOST_NAME_MAX];
  RedisInfo info;
  RedisHost *next;
};

static RedisNode *gRedisNodesHead = NULL;
static RedisHost *gRedisHostsHead = NULL;
static _Bool gPerHostData = 0;
static _Bool gGlobalData = 0;

static int redisHostAdd(const RedisHost *rh)
{
  RedisHost *rh_copy;
  RedisHost *rh_ptr;

  // Check for duplicates first
  for(rh_ptr = gRedisHostsHead; rh_ptr; rh_ptr = rh_ptr->next)
    if(strcmp(rh->hostname, rh_ptr->hostname) == 0)
      break;

  if(rh_ptr)
    return 0;

  if((rh_copy = malloc(sizeof(*rh_copy))) == NULL)
  {
    ERROR("redis plugin: malloc failed adding redis_host to the tree.");
    return -1;
  }

  memcpy(rh_copy, rh, sizeof(*rh_copy));
  rh_copy->next = NULL;

  DEBUG("redis plugin: Adding host \"%s\".", rh->hostname);

  if(gRedisHostsHead == NULL)
    gRedisHostsHead = rh_copy;
  else
  {
    rh_ptr = gRedisHostsHead;

    while(rh_ptr->next)
      rh_ptr = rh_ptr->next;

    rh_ptr->next = rh_copy;
  }

  return 0;
}

static int redisNodeAdd(const RedisNode *rn)
{
  RedisNode *rn_copy;
  RedisNode *rn_ptr;

  // Check for duplicates first
  for(rn_ptr = gRedisNodesHead; rn_ptr; rn_ptr = rn_ptr->next)
    if(strcmp(rn->name, rn_ptr->name) == 0)
      break;

  if(rn_ptr)
  {
    ERROR("redis plugin: A node with the name '%s' already exists.", rn->name);
    return -1;
  }

  if((rn_copy = malloc(sizeof(*rn_copy))) == NULL)
  {
    ERROR("redis plugin: malloc failed adding redis_node to the tree.");
    return -1;
  }

  memcpy(rn_copy, rn, sizeof(*rn_copy));
  rn_copy->next = NULL;

  DEBUG("redis plugin: Adding node \"%s\".", rn->name);

  if(gRedisNodesHead == NULL)
    gRedisNodesHead = rn_copy;
  else
  {
    rn_ptr = gRedisNodesHead;

    while(rn_ptr->next)
      rn_ptr = rn_ptr->next;

    rn_ptr->next = rn_copy;
  }

  return 0;
}

static int redisConfigQueryAdd(RedisNode *rn, oconfig_item_t *ci)
{
  RedisQuery *n;
  oconfig_item_t *option;
  int status;
  int i;

  if((n = malloc(sizeof(RedisQuery))) == NULL)
    return -1;

  memset(n, 0, sizeof(RedisQuery));

  if((status = cf_util_get_string_buffer(ci, n->name, sizeof(n->name))) != 0)
  {
    sfree(n);
    return -1;
  }

  for(option = ci->children, i = 0; i < ci->children_num; ++i, ++option)
  {
    if(strcasecmp("Command", option->key) == 0)
    {
      if((option->values_num != 1) || (option->values[0].type != OCONFIG_TYPE_STRING))
      {
        WARNING("redis plugin: 'Command' needs exactly one string argument.");
        continue;
      }

      if((status = cf_util_get_string_buffer(option, n->command, sizeof(n->command))))
        return status;

      continue;
    }

    if(strcasecmp("Type", option->key) == 0)
    {
      if((option->values_num != 1) || (option->values[0].type != OCONFIG_TYPE_STRING))
      {
        WARNING("redis plugin: 'Type' needs exactly one string argument.");
        continue;
      }

      if((status = cf_util_get_string_buffer(option, n->type, sizeof(n->type))))
        return status;

      continue;
    }

    WARNING("redis plugin: Option '%s' not allowed inside a 'Query' block. I'll ignore this option.", option->key);
  }

  if(rn->query == NULL)
  {
    rn->query = n;
  }
  else
  {
    RedisQuery *p;
    for(p = rn->query; p->next; p = p->next);
    p->next = n;
  }

  return status;

}

static int redisConfigNode(oconfig_item_t *ci)
{
  RedisNode rn;
  int i;
  int status;
  oconfig_item_t *option;

  memset(&rn, 0, sizeof(rn));

  sstrncpy(rn.hostname, REDIS_DEF_HOSTNAME, sizeof(rn.hostname));
  sstrncpy(rn.host, REDIS_DEF_HOST, sizeof(rn.host));
  rn.port = REDIS_DEF_PORT;
  rn.timeout = REDIS_DEF_TIMEOUT;

  if((status = cf_util_get_string_buffer(ci, rn.name, sizeof(rn.name))))
    return status;

  for(option = ci->children, i = 0; i < ci->children_num; ++i, ++option)
  {
    if(strcasecmp("Host", option->key) == 0)
    {
      if((status = cf_util_get_string_buffer(option, rn.host, sizeof(rn.host))))
        return status;

      continue;
    }

    if(strcasecmp("Hostname", option->key) == 0)
    {
      if((status = cf_util_get_string_buffer(option, rn.hostname, sizeof(rn.hostname))))
        return status;

      continue;
    }

    if(strcasecmp("Port", option->key) == 0)
    {
      if((status = cf_util_get_port_number(option)) <= 0)
        return status;

      rn.port = status;

      continue;
    }

    if(strcasecmp("Timeout", option->key) == 0)
    {
      if((status = cf_util_get_int(option, &rn.timeout)))
        return status;

      continue;
    }

    if(strcasecmp("Password", option->key) == 0)
    {
      if((status = cf_util_get_string_buffer(option, rn.passwd, sizeof(rn.passwd))))
        return status;

      continue;
    }

    if(strcasecmp("Query", option->key) == 0)
    {
      if((status = redisConfigQueryAdd(&rn, option)))
        return status;

      continue;
    }

    WARNING("redis plugin: Option '%s' not allowed inside a 'Node' block. I'll ignore this option.", option->key);
  }

  if(gPerHostData)
  {
    RedisHost host;

    memset(&host, 0, sizeof(RedisHost));

    sstrncpy(host.hostname, rn.hostname, sizeof(host.hostname));

    redisHostAdd(&host);
  }

  return redisNodeAdd(&rn);
}

static int redisConfig(oconfig_item_t *ci)
{
  int i;
  int status;
  oconfig_item_t *option;

  for(option = ci->children, i = 0; i < ci->children_num; ++i, ++option)
  {
    if(strcasecmp("GlobalData", option->key) == 0)
    {
      if((status = cf_util_get_boolean(option, &gGlobalData)))
        return status;

      continue;
    }

    if(strcasecmp("PerHostData", option->key) == 0)
    {
      if((status = cf_util_get_boolean(option, &gPerHostData)))
        return status;

      continue;
    }

    if(strcasecmp("Node", option->key) == 0)
    {
      if((status = redisConfigNode(option)))
        return status;

      continue;
    }

    WARNING("redis plugin: Option '%s' not allowed in redis configuration. It will be ignored.", option->key);
  }

  if(gRedisNodesHead == NULL)
  {
    ERROR("redis plugin: No valid node configuration could be found.");
    return ENOENT;
  }

  return 0;
}

__attribute__((nonnull(2)))
static void redisSubmitGauge(const char* hostname, const char *plugin_instance, const char *type, const char *type_instance, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;

  sstrncpy(vl.host, hostname, sizeof(vl.host));

  sstrncpy(vl.plugin, type, sizeof(vl.plugin));

  if(plugin_instance)
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));

  sstrncpy(vl.type, type, sizeof(vl.type));

  if(type_instance)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

__attribute__((nonnull(2)))
static void redisSubmitDerive(const char* hostname, const char *plugin_instance, const char *type, const char *type_instance, derive_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].derive = value;

  vl.values = values;
  vl.values_len = 1;

  sstrncpy(vl.host, hostname, sizeof(vl.host));

  sstrncpy(vl.plugin, type, sizeof(vl.plugin));

  if(plugin_instance)
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));

  sstrncpy(vl.type, type, sizeof(vl.type));

  if(type_instance)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void redisSubmit(const char* hostname, const char *name, const RedisInfo *info)
{
  // #Server
  redisSubmitDerive(hostname, name, "uptime", NULL, info->uptime_in_seconds);

  // #Clients
  redisSubmitGauge(hostname, name, "connected_clients", NULL, info->connected_clients);
  redisSubmitGauge(hostname, name, "blocked_clients", NULL, info->blocked_clients);
  redisSubmitGauge(hostname, name, "client_biggest_input_buf", NULL, info->client_biggest_input_buf);
  redisSubmitGauge(hostname, name, "client_longest_output_list", NULL, info->client_longest_output_list);

  // #Memory
  redisSubmitGauge(hostname, name, "memory", "used_memory", info->used_memory);
  redisSubmitGauge(hostname, name, "memory", "used_memory_lua", info->used_memory_lua);
  redisSubmitGauge(hostname, name, "memory", "used_memory_peak", info->used_memory_peak);
  redisSubmitGauge(hostname, name, "memory", "used_memory_rss", info->used_memory_rss);
  redisSubmitGauge(hostname, name, "mem_fragmentation_ratio", NULL, info->mem_fragmentation_ratio);

  // #Persistence
  redisSubmitGauge(hostname, name, "rdb_changes_since_last_save", NULL, info->rdb_changes_since_last_save);
  redisSubmitGauge(hostname, name, "rdb_bgsave_in_progress", NULL, info->rdb_bgsave_in_progress);
  redisSubmitGauge(hostname, name, "rdb_last_bgsave_time_sec", NULL, info->rdb_last_bgsave_time_sec);
  redisSubmitGauge(hostname, name, "aof_last_rewrite_time_sec", NULL, info->aof_last_rewrite_time_sec);
  redisSubmitGauge(hostname, name, "aof_rewrite_in_progress", NULL, info->aof_rewrite_in_progress);

  // #Stats
  redisSubmitDerive(hostname, name, "total_connections_received", NULL, info->total_connections_received);
  redisSubmitDerive(hostname, name, "total_commands_processed", NULL, info->total_commands_processed);
  redisSubmitGauge(hostname, name, "instantaneous_ops_per_sec", NULL, info->instantaneous_ops_per_sec);
  redisSubmitGauge(hostname, name, "rejected_connections", NULL, info->rejected_connections);
  redisSubmitGauge(hostname, name, "expired_keys", NULL, info->expired_keys);
  redisSubmitGauge(hostname, name, "evicted_keys", NULL, info->evicted_keys);
  redisSubmitGauge(hostname, name, "keyspace_hits", NULL, info->keyspace_hits);
  redisSubmitGauge(hostname, name, "keyspace_misses", NULL, info->keyspace_misses);
  redisSubmitGauge(hostname, name, "pubsub", "patterns", info->pubsub_patterns);
  redisSubmitGauge(hostname, name, "pubsub", "channels", info->pubsub_channels);
  redisSubmitGauge(hostname, name, "latest_fork_usec", NULL, info->latest_fork_usec);

  // #Replication
  redisSubmitGauge(hostname, name, "connected_slaves", NULL, info->connected_slaves);

  // #CPU
  redisSubmitGauge(hostname, name, "used_cpu_sys", NULL, info->used_cpu_sys);
  redisSubmitGauge(hostname, name, "used_cpu_user", NULL, info->used_cpu_user);
  redisSubmitGauge(hostname, name, "used_cpu_sys_children", NULL, info->used_cpu_sys_children);
  redisSubmitGauge(hostname, name, "used_cpu_user_children", NULL, info->used_cpu_user_children);

  // #Keyspace
  redisSubmitGauge(hostname, name, "keys", NULL, info->keys);
}

static int redisInit(void)
{
  RedisNode rn =
  {
    .name = "default",
    .hostname = REDIS_DEF_HOSTNAME,
    .host = REDIS_DEF_HOST,
    .port = REDIS_DEF_PORT,
    .timeout = REDIS_DEF_TIMEOUT,
    .next = NULL
  };

  if(gRedisNodesHead == NULL)
    redisNodeAdd(&rn);

  return 0;
}

static void redisParseInfoLine(const char *info, const char *field, const char *format, void *storage)
{
  char *str = strstr(info, field);

  if(str)
  {
    // also skip the ':'
    str += strlen(field) + 1;

    sscanf(str, format, storage);
  }
}

static void redisGetInfo(const char *str, RedisInfo *info)
{
  memset(info, 0, sizeof(RedisInfo));

  // #Server
  redisParseInfoLine(str, "uptime_in_seconds", "%lu", &(info->uptime_in_seconds));

  // #Clients
  redisParseInfoLine(str, "connected_clients", "%llu", &(info->connected_clients));
  redisParseInfoLine(str, "blocked_clients", "%llu", &(info->blocked_clients));
  redisParseInfoLine(str, "client_biggest_input_buf", "%llu", &(info->client_biggest_input_buf));
  redisParseInfoLine(str, "client_longest_output_list", "%llu", &(info->client_longest_output_list));

  // #Memory
  redisParseInfoLine(str, "used_memory", "%llu", &(info->used_memory));
  redisParseInfoLine(str, "used_memory_lua", "%llu", &(info->used_memory_lua));
  redisParseInfoLine(str, "used_memory_peak", "%llu", &(info->used_memory_peak));
  redisParseInfoLine(str, "used_memory_rss", "%llu", &(info->used_memory_rss));
  redisParseInfoLine(str, "mem_fragmentation_ratio", "%lf", &(info->mem_fragmentation_ratio));

  // #Persistence
  redisParseInfoLine(str, "rdb_changes_since_last_save", "%llu", &(info->rdb_changes_since_last_save));
  redisParseInfoLine(str, "rdb_bgsave_in_progress", "%llu", &(info->rdb_bgsave_in_progress));

  redisParseInfoLine(str, "rdb_last_bgsave_time_sec", "%llu", &(info->rdb_last_bgsave_time_sec));
  if(info->rdb_last_bgsave_time_sec == -1)
    info->rdb_last_bgsave_time_sec = 0;

  redisParseInfoLine(str, "aof_last_rewrite_time_sec", "%llu", &(info->aof_last_rewrite_time_sec));
  if(info->aof_last_rewrite_time_sec == -1)
    info->aof_last_rewrite_time_sec = 0;

  redisParseInfoLine(str, "aof_rewrite_in_progress", "%llu", &(info->aof_rewrite_in_progress));

  // #Stats
  redisParseInfoLine(str, "total_connections_received", "%llu", &(info->total_connections_received));
  redisParseInfoLine(str, "total_commands_processed", "%llu", &(info->total_commands_processed));
  redisParseInfoLine(str, "instantaneous_ops_per_sec", "%llu", &(info->instantaneous_ops_per_sec));
  redisParseInfoLine(str, "rejected_connections", "%llu", &(info->rejected_connections));
  redisParseInfoLine(str, "expired_keys", "%llu", &(info->expired_keys));
  redisParseInfoLine(str, "evicted_keys", "%llu", &(info->evicted_keys));
  redisParseInfoLine(str, "keyspace_hits", "%llu", &(info->keyspace_hits));
  redisParseInfoLine(str, "keyspace_misses", "%llu", &(info->keyspace_misses));
  redisParseInfoLine(str, "pubsub_patterns", "%llu", &(info->pubsub_patterns));
  redisParseInfoLine(str, "pubsub_channels", "%llu", &(info->pubsub_channels));
  redisParseInfoLine(str, "latest_fork_usec", "%llu", &(info->latest_fork_usec));

  // #Replication
  redisParseInfoLine(str, "connected_slaves", "%llu", &(info->connected_slaves));

  // #CPU
  redisParseInfoLine(str, "used_cpu_sys", "%lf", &(info->used_cpu_sys));
  redisParseInfoLine(str, "used_cpu_user", "%lf", &(info->used_cpu_user));
  redisParseInfoLine(str, "used_cpu_sys_children", "%lf", &(info->used_cpu_sys_children));
  redisParseInfoLine(str, "used_cpu_user_children", "%lf", &(info->used_cpu_user_children));

  // #Keyspace
  redisParseInfoLine(str, "db0:keys", "%llu", &(info->keys));
}

static void sumInfo(RedisInfo *dst, const RedisInfo *src)
{
  // #Server
  dst->uptime_in_seconds += src->uptime_in_seconds;

  // #Clients
  dst->connected_clients += src->connected_clients;
  dst->blocked_clients += src->blocked_clients;
  dst->client_biggest_input_buf += src->client_biggest_input_buf;
  dst->client_longest_output_list += src->client_longest_output_list;

  // #Memory
  dst->used_memory += src->used_memory;
  dst->used_memory_lua += src->used_memory_lua;
  dst->used_memory_peak += src->used_memory_peak;
  dst->used_memory_rss += src->used_memory_rss;
  dst->mem_fragmentation_ratio += src->mem_fragmentation_ratio;

  // #Persistence
  dst->rdb_changes_since_last_save += src->rdb_changes_since_last_save;
  dst->rdb_bgsave_in_progress += src->rdb_bgsave_in_progress;
  dst->rdb_last_bgsave_time_sec += src->rdb_last_bgsave_time_sec;
  dst->aof_last_rewrite_time_sec += src->aof_last_rewrite_time_sec;
  dst->aof_rewrite_in_progress += src->aof_rewrite_in_progress;

  // #Stats
  dst->total_connections_received += src->total_connections_received;
  dst->total_commands_processed += src->total_commands_processed;
  dst->instantaneous_ops_per_sec += src->instantaneous_ops_per_sec;
  dst->rejected_connections += src->rejected_connections;
  dst->expired_keys += src->expired_keys;
  dst->evicted_keys += src->evicted_keys;
  dst->keyspace_hits += src->keyspace_hits;
  dst->keyspace_misses += src->keyspace_misses;
  dst->pubsub_patterns += src->pubsub_patterns;
  dst->pubsub_channels += src->pubsub_channels;
  dst->latest_fork_usec += src->latest_fork_usec;

  // #Replication
  dst->connected_slaves += src->connected_slaves;

  // #CPU
  dst->used_cpu_sys += src->used_cpu_sys;
  dst->used_cpu_user += src->used_cpu_user;
  dst->used_cpu_sys_children += src->used_cpu_sys_children;
  dst->used_cpu_user_children += src->used_cpu_user_children;

  // #Keyspace
  dst->keys += src->keys;
}

static int readCustomQueries(const RedisNode *rn, redisContext *rc)
{
  int i;
  RedisQuery *query;
  redisReply *rr = NULL;
  const data_set_t *ds;
  value_list_t vl = VALUE_LIST_INIT;

  for(query = rn->query; query; query = query->next)
  {
    if(rr)
    {
      freeReplyObject(rr);
      rr = NULL;
    }

    if((rr = redisCommand(rc, query->command)) == NULL)
    {
      WARNING("redis plugin: unable to execute query '%s' on node '%s'.", query->name, rn->name);
      continue;
    }

    if(rr->type != REDIS_REPLY_INTEGER)
    {
      WARNING("redis plugin: unable to get reply for query '%s' on node '%s', integer expected.", query->name, rn->name);
      continue;
    }

    DEBUG("Get data from query '%s' executing '%s' on node '%s'.", query->name, query->command, rn->name);

    if((ds = plugin_get_ds(query->type)) == NULL)
    {
      ERROR("redis plugin: DataSet '%s' not defined.", query->type);
      continue;
    }

    if(ds->ds_num != 1)
    {
      ERROR("redis plugin: DataSet '%s' requires %i values, but config talks about %i", query->type, ds->ds_num, 1);
      continue;
    }

    vl.values_len = ds->ds_num;
    vl.values = (value_t *)malloc(sizeof(value_t) * vl.values_len);

    if(vl.values == NULL)
      continue;

    for(i = 0; i < vl.values_len; ++i)
    {
      if(ds->ds[i].type == DS_TYPE_COUNTER)
        vl.values[i].counter = rr->integer;
      else
        vl.values[i].gauge = rr->integer;
    }

    sstrncpy(vl.host, rn->hostname, sizeof(vl.host));
    sstrncpy(vl.plugin, "redis-query", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, rn->name, sizeof(vl.plugin));
    sstrncpy(vl.type, query->type, sizeof(vl.type));
    sstrncpy(vl.type_instance, query->name, sizeof(vl.type));

    plugin_dispatch_values(&vl);
  }

  if(rr)
  {
    freeReplyObject(rr);
    rr = NULL;
  }
  
  return 0;
}

static int redisRead(void)
{
  const RedisNode *rn;
  RedisInfo info;
  RedisInfo infoGlobal;
  RedisHost *rh;
  redisContext *rc = NULL;
  redisReply *rr = NULL;
  struct timeval tmout;

  if(gGlobalData)
    memset(&infoGlobal, 0, sizeof(RedisInfo));

  if(gPerHostData)
    for(rh = gRedisHostsHead; rh; rh = rh->next)
      memset(&rh->info, 0, sizeof(RedisInfo));

  for(rn = gRedisNodesHead; rn; rn = rn->next)
  {
    if(rr)
    {
      freeReplyObject(rr);
      rr = NULL;
    }

    if(rc)
    {
      redisFree(rc);
      rc = NULL;
    }

    tmout.tv_sec = rn->timeout;
    tmout.tv_usec = 0;

    DEBUG("redis plugin: querying info from node '%s' (%s:%d).", rn->name, rn->host, rn->port);

    if((rc = redisConnectWithTimeout((char *)rn->host, rn->port, tmout)) == NULL)
    {
      ERROR("redis plugin: unable to connect to node '%s' (%s:%d).", rn->name, rn->host, rn->port);
      continue;
    }

    if(strlen(rn->passwd) > 0)
    {
      DEBUG("redis plugin: authenticanting node '%s' passwd(%s).", rn->name, rn->passwd);

      if((rr = redisCommand(rc, "AUTH %s", rn->passwd)) == NULL || rr->type != REDIS_REPLY_STATUS)
      {
        WARNING("redis plugin: unable to authenticate on node '%s'.", rn->name);
        continue;
      }
    }

    if((rr = redisCommand(rc, "INFO")) == NULL)
    {
      WARNING("redis plugin: unable to connect to node '%s'.", rn->name);
      continue;
    }

    redisGetInfo(rr->str, &info);
    
    redisSubmit(rn->hostname, rn->name, &info);

    readCustomQueries(rn, rc);

    if(gGlobalData)
      sumInfo(&infoGlobal, &info);

    if(gPerHostData)
    {
      for(rh = gRedisHostsHead; rh; rh = rh->next)
      {
        if(strcmp(rn->hostname, rh->hostname) == 0)
        {
          sumInfo(&rh->info, &info);
          break;
        }
      }
    }
  }

  if(rr)
  {
    freeReplyObject(rr);
    rr = NULL;
  }

  if(rc)
  {
    redisFree(rc);
    rc = NULL;
  }

  if(gGlobalData)
    redisSubmit("Global", "Global", &infoGlobal);

  if(gPerHostData)
    for(rh = gRedisHostsHead; rh; rh = rh->next)
      redisSubmit(rh->hostname, "all", &rh->info);

  return 0;
}

void module_register(void)
{
  plugin_register_complex_config("redis", redisConfig);
  plugin_register_init("redis", redisInit);
  plugin_register_read("redis", redisRead);
}
