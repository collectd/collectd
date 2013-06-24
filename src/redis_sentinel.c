/**
 * collectd - src/redis_sentinel.c, based on src/redis.c
 * Copyright (C) 2013       Daniel Mezzatto <danielm@buscape-inc.com>
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
 *   Daniel Mezzatto <danielm@buscape-inc.com>
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

#define REDIS_SENTINEL_DEF_HOSTNAME      "localhost"
#define REDIS_SENTINEL_DEF_HOST          "127.0.0.1"
#define REDIS_SENTINEL_DEF_PORT          (26379)
#define REDIS_SENTINEL_DEF_TIMEOUT       (2)
#define REDIS_SENTINEL_NODE_NAME_MAX     (64)

/* Redis Sentinel plugin configuration example:
 *
 * <Plugin redis_sentinel>
 *   <Node "mynode">
 *     Hostname "my_machine"
 *     Host "127.0.0.1"
 *     Port "26379"
 *     Timeout 2
 *   </Node>
 * </Plugin>
 */

struct redis_sentinel_node_s;
typedef struct redis_sentinel_node_s RedisSentinelNode;

struct redis_sentinel_node_s
{
  char name[REDIS_SENTINEL_NODE_NAME_MAX];
  char hostname[HOST_NAME_MAX];
  char host[HOST_NAME_MAX];
  int port;
  int timeout;
  redisContext *rc;
  RedisSentinelNode *next;
};

struct redis_sentinel_master_info_s;
typedef struct redis_sentinel_master_info_s RedisSentinelMasterInfo;

struct redis_sentinel_master_info_s
{
  char name[REDIS_SENTINEL_NODE_NAME_MAX];
  unsigned long long int status;
  unsigned long long int slaves;
  unsigned long long int sentinels;
};

struct redis_sentinel_info_s;
typedef struct redis_sentinel_info_s RedisSentinelInfo;

struct redis_sentinel_info_s
{
  unsigned long long int masters;
  unsigned long long int tilt;
  unsigned long long int running_scripts;
  unsigned long long int scripts_queue_length;
  RedisSentinelMasterInfo *mastersInfo;
};

static RedisSentinelNode *gRedisSentinelNodesHead = NULL;

static int redisSentinelNodeAdd(const RedisSentinelNode *rn)
{
  RedisSentinelNode *rn_copy;
  RedisSentinelNode *rn_ptr;

  // Check for duplicates first
  for(rn_ptr = gRedisSentinelNodesHead; rn_ptr; rn_ptr = rn_ptr->next)
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

  if(gRedisSentinelNodesHead == NULL)
    gRedisSentinelNodesHead = rn_copy;
  else
  {
    rn_ptr = gRedisSentinelNodesHead;

    while(rn_ptr->next)
      rn_ptr = rn_ptr->next;

    rn_ptr->next = rn_copy;
  }

  return 0;
}

static int redisSentinelConfigNode(oconfig_item_t *ci)
{
  RedisSentinelNode rn;
  int i;
  int status;
  oconfig_item_t *option;

  memset(&rn, 0, sizeof(rn));

  sstrncpy(rn.hostname, REDIS_SENTINEL_DEF_HOSTNAME, sizeof(rn.hostname));
  sstrncpy(rn.host, REDIS_SENTINEL_DEF_HOST, sizeof(rn.host));
  rn.port = REDIS_SENTINEL_DEF_PORT;
  rn.timeout = REDIS_SENTINEL_DEF_TIMEOUT;

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

    WARNING("redis plugin: Option '%s' not allowed inside a 'Node' block. I'll ignore this option.", option->key);
  }

  return redisSentinelNodeAdd(&rn);
}

static int redisSentinelConfig(oconfig_item_t *ci)
{
  int i;
  int status;
  oconfig_item_t *option;

  for(option = ci->children, i = 0; i < ci->children_num; ++i, ++option)
  {
    if(strcasecmp("Node", option->key) == 0)
    {
      if((status = redisSentinelConfigNode(option)))
        return status;

      continue;
    }

    WARNING("redis plugin: Option '%s' not allowed in redis configuration. It will be ignored.", option->key);
  }

  if(gRedisSentinelNodesHead == NULL)
  {
    ERROR("redis plugin: No valid node configuration could be found.");
    return ENOENT;
  }

  return 0;
}

#define redisSentinelSubmitDeclare(value_type) \
__attribute__((nonnull(2))) \
static void redisSentinelSubmit_ ## value_type(const char* hostname, const char *plugin_instance, const char *type, const char *type_instance, value_type ## _t value) \
{ \
  value_t values[1]; \
  value_list_t vl = VALUE_LIST_INIT; \
  values[0].value_type = value; \
  vl.values = values; \
  vl.values_len = 1; \
  sstrncpy(vl.host, hostname, sizeof(vl.host)); \
  sstrncpy(vl.plugin, type, sizeof(vl.plugin)); \
  if(plugin_instance) sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance)); \
  sstrncpy(vl.type, type, sizeof(vl.type)); \
  if(type_instance) sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance)); \
  plugin_dispatch_values(&vl); \
}

redisSentinelSubmitDeclare(gauge);
//redisSentinelSubmitDeclare(derive);

static void redisSentinelSubmit(const char* hostname, const char *name, const RedisSentinelInfo *info)
{
  unsigned int i;

  redisSentinelSubmit_gauge(hostname, name, "sentinel_masters", NULL, info->masters);
  redisSentinelSubmit_gauge(hostname, name, "sentinel_tilt", NULL, info->tilt);
  redisSentinelSubmit_gauge(hostname, name, "sentinel_running_scripts", NULL, info->running_scripts);
  redisSentinelSubmit_gauge(hostname, name, "sentinel_scripts_queue_length", NULL, info->scripts_queue_length);

  for(i = 0; i < info->masters; ++i)
  {
    redisSentinelSubmit_gauge(hostname, name, "sentinel_slaves", info->mastersInfo[i].name, info->mastersInfo[i].slaves);
    redisSentinelSubmit_gauge(hostname, name, "sentinel_sentinels", info->mastersInfo[i].name, info->mastersInfo[i].sentinels);
    redisSentinelSubmit_gauge(hostname, name, "sentinel_status", info->mastersInfo[i].name, info->mastersInfo[i].status);
  }

  if(info->mastersInfo)
    free(info->mastersInfo);
}

static int redisSentinelInit(void)
{
  RedisSentinelNode rn =
  {
    .name = "default",
    .hostname = REDIS_SENTINEL_DEF_HOSTNAME,
    .host = REDIS_SENTINEL_DEF_HOST,
    .port = REDIS_SENTINEL_DEF_PORT,
    .timeout = REDIS_SENTINEL_DEF_TIMEOUT,
    .rc = NULL,
    .next = NULL
  };

  if(gRedisSentinelNodesHead == NULL)
    redisSentinelNodeAdd(&rn);

  return 0;
}

// old gcc bug with c99 not declaring this function inside stdio.h
int vsscanf(const char *str, const char *format, va_list ap);

static void redisSentinelParseInfoLine(const char *info, const char *field, const char *format, ...)
{
  const char *str = strstr(info, field);

  if(str)
  {
    va_list ap;

    // also skip the ':'
    str += strlen(field) + 1;

    va_start(ap, format);
    vsscanf(str, format, ap);
    va_end(ap);
  }
}

static void redisSentinelGetInfo(const char *str, RedisSentinelInfo *info)
{
  unsigned int i;
  unsigned int j;
  const char *line;
  const char *field;
  char masterBuffer[16];

  memset(info, 0, sizeof(RedisSentinelInfo));

  redisSentinelParseInfoLine(str, "sentinel_masters", "%llu", &(info->masters));
  redisSentinelParseInfoLine(str, "sentinel_tilt", "%llu", &(info->tilt));
  redisSentinelParseInfoLine(str, "sentinel_running_scripts", "%llu", &(info->running_scripts));
  redisSentinelParseInfoLine(str, "sentinel_scripts_queue_length", "%llu", &(info->scripts_queue_length));

  if((info->mastersInfo = calloc(info->masters, sizeof(RedisSentinelMasterInfo))) == NULL)
    return;

  for(i = 0; i < info->masters; ++i)
  {
    snprintf(masterBuffer, sizeof(masterBuffer) - 1, "master%u", i);

    if((line = strstr(str, masterBuffer)) == NULL)
      continue;

    if((field = strstr(line, "name=")) == NULL)
      continue;
    field += 5;

    j = 0;
    while(*field != ',')
    {
      info->mastersInfo[i].name[j++] = *(field++);
    }

    if((field = strstr(line, "status=")) == NULL)
      continue;
    field += 7;

    if(field[0] == 'o' && field[1] == 'k')
      info->mastersInfo[i].status = 0;
    else if(field[0] == 's' && field[1] == 'd') // sdown - Subjective Down
      info->mastersInfo[i].status = 1;
    else if(field[0] == 'o' && field[1] == 'd') // odown - Objective Down
      info->mastersInfo[i].status = 2;
    else
      info->mastersInfo[i].status = 3;

    redisSentinelParseInfoLine(line, "slaves", "%llu", &(info->mastersInfo[i].slaves));
    redisSentinelParseInfoLine(line, "sentinels", "%llu", &(info->mastersInfo[i].sentinels));
  }
}

static int redisSentinelRead(void)
{
  RedisSentinelNode *rn;
  RedisSentinelInfo info;
  redisReply *rr;
  struct timeval tmout;

  for(rn = gRedisSentinelNodesHead; rn; rn = rn->next)
  {
    if(rn->rc == NULL)
    {
      tmout.tv_sec = rn->timeout;
      tmout.tv_usec = 0;

      DEBUG("redis plugin: connecting to node '%s' (%s:%d).", rn->name, rn->host, rn->port);

      if((rn->rc = redisConnectWithTimeout((char *)rn->host, rn->port, tmout)) == NULL)
      {
        ERROR("redis plugin: unable to connect to node '%s' (%s:%d).", rn->name, rn->host, rn->port);
        continue;
      }
    }

    DEBUG("redis plugin: querying info from node '%s' (%s:%d).", rn->name, rn->host, rn->port);

    if((rr = redisCommand(rn->rc, "INFO")) == NULL)
    {
      WARNING("redis plugin: unable to query info from node '%s'.", rn->name);
      redisFree(rn->rc);
      rn->rc = NULL;
      continue;
    }

    redisSentinelGetInfo(rr->str, &info);

    freeReplyObject(rr);

    redisSentinelSubmit(rn->hostname, rn->name, &info);
  }

  return 0;
}

void module_register(void)
{
  plugin_register_complex_config("redis_sentinel", redisSentinelConfig);
  plugin_register_init("redis_sentinel", redisSentinelInit);
  plugin_register_read("redis_sentinel", redisSentinelRead);
}
