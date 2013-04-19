/**
 * collectd - src/twemproxy.c, based on src/redis.c
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <json/json.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

/*
Twemproxy plugin configuration example:

<Plugin twemproxy>
  PerPoolData true
  PerHostData true
  <Node "mynode">
    Host "127.0.0.1"
  </Node>
</Plugin>
*/

#define isJSONField(key, field) (strncasecmp(key, field, sizeof(field) - 1) == 0)

#define TWEMPROXY_STATS_PORT    (22222)
#define TWEMPROXY_DEF_HOST      "127.0.0.1"
#define MAX_TWEMPROXY_NAME      (64)

struct twemproxy_node_s;
typedef struct twemproxy_node_s TwemproxyNode;

struct twemproxy_node_s
{
  char name[MAX_TWEMPROXY_NAME];
  char host[HOST_NAME_MAX];
  TwemproxyNode *next;
};

struct twemproxy_instance_s;
typedef struct twemproxy_instance_s TwemproxyInstance;

struct twemproxy_instance_s
{
  char name[MAX_TWEMPROXY_NAME];
  long int server_eof;
  long int server_err;
  long int server_timedout;
  long int server_connections;
  long int requests;
  long int request_bytes;
  long int responses;
  long int response_bytes;
  long int in_queue;
  long int in_queue_bytes;
  long int out_queue;
  long int out_queue_bytes;
  TwemproxyInstance *next;
};

struct twemproxy_pool_s;
typedef struct twemproxy_pool_s TwemproxyPool;

struct twemproxy_pool_s
{
  char name[MAX_TWEMPROXY_NAME];
  long int client_eof;
  long int client_err;
  long int client_connections;
  long int server_ejects;
  long int forward_error;
  long int fragments;
  TwemproxyInstance *head;
  TwemproxyPool *next;
};

struct twemproxy_stats_s;
typedef struct twemproxy_stats_s TwemproxyStats;

struct twemproxy_stats_s
{
  char source[HOST_NAME_MAX];
  time_t uptime;
  TwemproxyPool *head;
};

static TwemproxyNode *gTwemproxyNodesHead = NULL;
static _Bool gPerPoolData = 0;
static _Bool gPerHostData = 0;

static int twemproxyNodeAdd(const TwemproxyNode *tn)
{
  TwemproxyNode *tn_copy;
  TwemproxyNode *tn_ptr;

  // Check for duplicates first
  for(tn_ptr = gTwemproxyNodesHead; tn_ptr; tn_ptr = tn_ptr->next)
    if(strcmp(tn->name, tn_ptr->name) == 0)
      break;

  if(tn_ptr)
  {
    ERROR("twemproxy plugin: A node with the name '%s' already exists.", tn->name);
    return -1;
  }

  if((tn_copy = malloc(sizeof(*tn_copy))) == NULL)
  {
    ERROR("twemproxy plugin: malloc failed adding TwemproxyNode to the tree.");
    return -1;
  }

  memcpy(tn_copy, tn, sizeof(*tn_copy));
  tn_copy->next = NULL;

  DEBUG("twemproxy plugin: Adding node \"%s\".", tn->name);

  if(gTwemproxyNodesHead == NULL)
    gTwemproxyNodesHead = tn_copy;
  else
  {
    tn_ptr = gTwemproxyNodesHead;

    while(tn_ptr->next)
      tn_ptr = tn_ptr->next;

    tn_ptr->next = tn_copy;
  }

  return 0;
}

static int twemproxyPoolAdd(TwemproxyStats *stats, const TwemproxyPool *pn)
{
  TwemproxyPool *pn_copy;
  TwemproxyPool *pn_ptr;

  if((pn_copy = malloc(sizeof(*pn_copy))) == NULL)
  {
    ERROR("twemproxy plugin: malloc failed adding pool to the tree.");
    return -1;
  }

  memcpy(pn_copy, pn, sizeof(*pn_copy));
  pn_copy->next = NULL;

  DEBUG("twemproxy plugin: Adding pool \"%s\".", pn->name);

  if(stats->head == NULL)
    stats->head = pn_copy;
  else
  {
    pn_ptr = stats->head;

    while(pn_ptr->next)
      pn_ptr = pn_ptr->next;

    pn_ptr->next = pn_copy;
  }

  return 0;
}
static int twemproxyInstanceAdd(TwemproxyPool *pool, const TwemproxyInstance *in)
{
  TwemproxyInstance *in_copy;
  TwemproxyInstance *in_ptr;

  if((in_copy = malloc(sizeof(*in_copy))) == NULL)
  {
    ERROR("twemproxy plugin: malloc failed adding instance to the tree.");
    return -1;
  }

  memcpy(in_copy, in, sizeof(*in_copy));
  in_copy->next = NULL;

  DEBUG("twemproxy plugin: Adding instance \"%s\".", in->name);

  if(pool->head == NULL)
    pool->head = in_copy;
  else
  {
    in_ptr = pool->head;

    while(in_ptr->next)
      in_ptr = in_ptr->next;

    in_ptr->next = in_copy;
  }

  return 0;
}

static void twemproxyInstanceFree(TwemproxyInstance *in)
{
  TwemproxyInstance* aux;
  TwemproxyInstance* current = in;

  while(current)
  {
    aux = current;
    current = current->next;

    free(aux);
  }
}

static void twemproxyPoolFree(TwemproxyPool *pn)
{
  TwemproxyPool* aux;
  TwemproxyPool* current = pn;

  while(current)
  {
    aux = current;
    current = current->next;

    twemproxyInstanceFree(aux->head);

    free(aux);
  }
}

static int twemproxyInit(void)
{
  TwemproxyNode tn =
  {
    .name = "default",
    .host = TWEMPROXY_DEF_HOST,
    .next = NULL
  };

  if(gTwemproxyNodesHead == NULL)
    twemproxyNodeAdd(&tn);

  return 0;
}

#define twemproxySubmitDeclare(value_type) \
__attribute__((nonnull(2))) \
static void twemproxySubmit_ ## value_type(const char* hostname, const char *plugin_instance, const char *type, const char *type_instance, value_type ## _t value) \
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

twemproxySubmitDeclare(gauge);
twemproxySubmitDeclare(derive);

static int parseTwemproxyStats(TwemproxyStats *stats, const char* const buffer)
{
  int ret = 0;
  json_object* respJson;
  TwemproxyPool pool;
  TwemproxyInstance instance;

  if((respJson = json_tokener_parse(buffer)) == NULL)
  {
    ERROR("twemproxy plugin: error parsing JSON");
    return 1;
  }

  memset(stats, 0, sizeof(TwemproxyStats));

  json_object_object_foreach(respJson, poolLevelKey, poolLevelVal)
  {
    if(json_object_get_type(poolLevelVal) != json_type_object)
    {
      if(isJSONField(poolLevelKey, "source"))
      {
        if(json_object_get_type(poolLevelVal) != json_type_string)
        {
          ERROR("twemproxy plugin: unexpected object type for source");
          ret = 1;
          goto end;
        }

        DEBUG("twemproxy plugin: source %s", json_object_get_string(poolLevelVal));

        sstrncpy(stats->source, json_object_get_string(poolLevelVal), HOST_NAME_MAX);

        continue;
      }

      if(isJSONField(poolLevelKey, "uptime"))
      {
        if(json_object_get_type(poolLevelVal) != json_type_int)
        {
          ERROR("twemproxy plugin: unexpected object type for uptime");
          ret = 1;
          goto end;
          ;
        }

        DEBUG("twemproxy plugin: uptime %ld", json_object_get_int64(poolLevelVal));

        stats->uptime = json_object_get_int64(poolLevelVal);

        continue;
      }

      continue;
    }

    DEBUG("twemproxy plugin: pool %s", poolLevelKey);

    memset(&pool, 0, sizeof(TwemproxyPool));

    sstrncpy(pool.name, poolLevelKey, MAX_TWEMPROXY_NAME);

    json_object_object_foreach(poolLevelVal, nodeLevelKey, nodeLevelVal)
    {
      if(json_object_get_type(nodeLevelVal) != json_type_object)
      {
        if(json_object_get_type(nodeLevelVal) != json_type_int)
        {
          ERROR("twemproxy plugin: unexpected object type for pool data");
          ret = 1;
          goto end;
          ;
        }

        if(isJSONField(nodeLevelKey, "client_eof"))
        {
          DEBUG("twemproxy plugin: \tclient_eof: %ld", json_object_get_int64(nodeLevelVal));

          pool.client_eof = json_object_get_int64(nodeLevelVal);

          continue;
        }

        if(isJSONField(nodeLevelKey, "client_err"))
        {
          DEBUG("twemproxy plugin: \tclient_err: %ld", json_object_get_int64(nodeLevelVal));

          pool.client_err = json_object_get_int64(nodeLevelVal);

          continue;
        }

        if(isJSONField(nodeLevelKey, "client_connections"))
        {
          DEBUG("twemproxy plugin: \tclient_connections: %ld", json_object_get_int64(nodeLevelVal));

          pool.client_connections = json_object_get_int64(nodeLevelVal);

          continue;
        }

        if(isJSONField(nodeLevelKey, "server_ejects"))
        {
          DEBUG("twemproxy plugin: \tserver_ejects: %ld", json_object_get_int64(nodeLevelVal));

          pool.server_ejects = json_object_get_int64(nodeLevelVal);

          continue;
        }

        if(isJSONField(nodeLevelKey, "forward_error"))
        {
          DEBUG("twemproxy plugin: \tforward_error: %ld", json_object_get_int64(nodeLevelVal));

          pool.forward_error = json_object_get_int64(nodeLevelVal);

          continue;
        }

        if(isJSONField(nodeLevelKey, "fragments"))
        {
          DEBUG("twemproxy plugin: \tfragments: %ld", json_object_get_int64(nodeLevelVal));

          pool.fragments = json_object_get_int64(nodeLevelVal);

          continue;
        }

        continue;
      }

      DEBUG("twemproxy plugin: \tnode %s", nodeLevelKey);

      memset(&instance, 0, sizeof(TwemproxyInstance));

      sstrncpy(instance.name, nodeLevelKey, MAX_TWEMPROXY_NAME);

      json_object_object_foreach(nodeLevelVal, instanceLevelKey, instanceLevelVal)
      {
        if(json_object_get_type(instanceLevelVal) != json_type_int)
        {
          ERROR("twemproxy plugin: unexpected object type for node data");
          ret = 1;
          goto end;
        }

        if(isJSONField(instanceLevelKey, "server_eof"))
        {
          DEBUG("twemproxy plugin: \t\tserver_eof: %ld", json_object_get_int64(instanceLevelVal));

          instance.server_eof = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "server_err"))
        {
          DEBUG("twemproxy plugin: \t\tserver_err: %ld", json_object_get_int64(instanceLevelVal));

          instance.server_err = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "server_timedout"))
        {
          DEBUG("twemproxy plugin: \t\tserver_timedout: %ld", json_object_get_int64(instanceLevelVal));

          instance.server_timedout = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "server_connections"))
        {
          DEBUG("twemproxy plugin: \t\tserver_connections: %ld", json_object_get_int64(instanceLevelVal));

          instance.server_connections = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "requests"))
        {
          DEBUG("twemproxy plugin: \t\trequests: %ld", json_object_get_int64(instanceLevelVal));

          instance.requests = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "request_bytes"))
        {
          DEBUG("twemproxy plugin: \t\trequest_bytes: %ld", json_object_get_int64(instanceLevelVal));

          instance.request_bytes = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "responses"))
        {
          DEBUG("twemproxy plugin: \t\tresponses: %ld", json_object_get_int64(instanceLevelVal));

          instance.responses = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "response_bytes"))
        {
          DEBUG("twemproxy plugin: \t\tresponse_bytes: %ld", json_object_get_int64(instanceLevelVal));

          instance.response_bytes = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "in_queue"))
        {
          DEBUG("twemproxy plugin: \t\tin_queue: %ld", json_object_get_int64(instanceLevelVal));

          instance.in_queue = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "in_queue_bytes"))
        {
          DEBUG("twemproxy plugin: \t\tin_queue_bytes: %ld", json_object_get_int64(instanceLevelVal));

          instance.in_queue_bytes = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "out_queue"))
        {
          DEBUG("twemproxy plugin: \t\tout_queue: %ld", json_object_get_int64(instanceLevelVal));

          instance.out_queue = json_object_get_int64(instanceLevelVal);

          continue;
        }

        if(isJSONField(instanceLevelKey, "out_queue_bytes"))
        {
          DEBUG("twemproxy plugin: \t\tout_queue_bytes: %ld", json_object_get_int64(instanceLevelVal));

          instance.out_queue_bytes = json_object_get_int64(instanceLevelVal);

          continue;
        }
      }

      if(twemproxyInstanceAdd(&pool, &instance))
      {
        ret = 1;
        goto end;
      }
    }

    if(twemproxyPoolAdd(stats, &pool))
    {
      ret = 1;
      goto end;
    }
  }

end:
  json_object_put(respJson);

  return ret;
}

static void twemproxySubmitPool(const char* nodename, const char *name, const TwemproxyPool *pool)
{
  twemproxySubmit_gauge(nodename, name, "nc_client_eof", NULL, pool->client_eof);
  twemproxySubmit_gauge(nodename, name, "nc_client_err", NULL, pool->client_err);
  twemproxySubmit_gauge(nodename, name, "nc_client_connections", NULL, pool->client_connections);
  twemproxySubmit_gauge(nodename, name, "nc_server_ejects", NULL, pool->server_ejects);
  twemproxySubmit_gauge(nodename, name, "nc_forward_error", NULL, pool->forward_error);
  twemproxySubmit_gauge(nodename, name, "nc_fragments", NULL, pool->fragments);
}

static void twemproxySubmitInstance(const char* nodename, const char *name, const TwemproxyInstance *instance)
{
  if(instance->requests == 0)
    return;

  twemproxySubmit_gauge(nodename, name, "nc_server_eof", NULL, instance->server_eof);
  twemproxySubmit_gauge(nodename, name, "nc_server_err", NULL, instance->server_err);
  twemproxySubmit_gauge(nodename, name, "nc_server_timedout", NULL, instance->server_timedout);
  twemproxySubmit_gauge(nodename, name, "nc_server_connections", NULL, instance->server_connections);
  twemproxySubmit_derive(nodename, name, "nc_requests", "requests", instance->requests);
  twemproxySubmit_derive(nodename, name, "nc_requests", "responses", instance->responses);
  twemproxySubmit_derive(nodename, name, "nc_request_bytes", "request_bytes", instance->request_bytes);
  twemproxySubmit_derive(nodename, name, "nc_request_bytes", "response_bytes", instance->response_bytes);
  twemproxySubmit_derive(nodename, name, "nc_queue", "in_queue", instance->in_queue);
  twemproxySubmit_derive(nodename, name, "nc_queue", "out_queue", instance->out_queue);
  twemproxySubmit_derive(nodename, name, "nc_queue_bytes", "in_queue_bytes", instance->in_queue_bytes);
  twemproxySubmit_derive(nodename, name, "nc_queue_bytes", "out_queue_bytes", instance->out_queue_bytes);
}

static void sumPool(TwemproxyPool *dst, const TwemproxyPool *src)
{
  dst->client_eof += src->client_eof;
  dst->client_err += src->client_err;
  dst->client_connections += src->client_connections;
  dst->server_ejects += src->server_ejects;
  dst->forward_error += src->forward_error;
  dst->fragments += src->fragments;
}

static void sumInstance(TwemproxyInstance *dst, const TwemproxyInstance *src)
{
  dst->server_eof += src->server_eof;
  dst->server_err += src->server_err;
  dst->server_timedout += src->server_timedout;
  dst->server_connections += src->server_connections;
  dst->requests += src->requests;
  dst->responses += src->responses;
  dst->request_bytes += src->request_bytes;
  dst->response_bytes += src->response_bytes;
  dst->in_queue += src->in_queue;
  dst->out_queue += src->out_queue;
  dst->in_queue_bytes += src->in_queue_bytes;
  dst->out_queue_bytes += src->out_queue_bytes;
}

static int twemproxyRead(void)
{
  int s;
  int status;
  int nread;
  int posBuffer;
  TwemproxyStats stats;
  TwemproxyPool *pool;
  TwemproxyInstance *instance;
  TwemproxyPool poolTotal;
  TwemproxyInstance instanceTotal;
  TwemproxyInstance instanceGlobal;
  const TwemproxyNode *tn;
  char buffer[128 * 1024];
  struct sockaddr_in serv_addr;
  struct hostent *server;
  char name[MAX_TWEMPROXY_NAME * 2];

  memset(&stats, 0, sizeof(TwemproxyStats));

  for(tn = gTwemproxyNodesHead; tn; tn = tn->next)
  {
    DEBUG("twemproxy plugin: connecting to %s (%s)", tn->host, tn->name);

    if((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      ERROR("twemproxy plugin: error openning socket");
      return 1;
    }

    if((server = gethostbyname(tn->host)) == NULL)
    {
      ERROR("twemproxy plugin: no such host '%s'", tn->host);
      goto error;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));

    memcpy(&serv_addr.sin_addr, server->h_addr, server->h_length);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TWEMPROXY_STATS_PORT);

    if(connect(s, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
      ERROR("twemproxy plugin: error connecting to '%s' (%s)", tn->host, strerror(errno));
      goto error;
    }

    posBuffer = 0;

    do
    {
      nread = read(s, buffer + posBuffer, sizeof(buffer) - posBuffer);

      if(nread == -1 && errno != EAGAIN && errno != EINTR)
      {
        ERROR("twemproxy plugin: reading from '%s': %s", tn->host, strerror(errno));
        goto error;
      }

      if(nread > 0)
        posBuffer += nread;

    }
    while(nread > 0);

    status = parseTwemproxyStats(&stats, buffer);

    close(s);

    memset(&poolTotal, 0, sizeof(TwemproxyPool));
    memset(&instanceGlobal, 0, sizeof(TwemproxyInstance));

    pool = stats.head;

    while(pool)
    {
      twemproxySubmitPool(tn->name, pool->name, pool);

      memset(&instanceTotal, 0, sizeof(TwemproxyInstance));

      instance = pool->head;

      while(instance)
      {
        twemproxySubmitInstance(tn->name, instance->name, instance);

        sumInstance(&instanceTotal, instance);

        instance = instance->next;
      }

      if(gPerPoolData)
      {
        snprintf(name, sizeof(name) - 1, "%s-instance-all", pool->name);

        twemproxySubmitInstance(tn->name, name, &instanceTotal);
      }

      sumPool(&poolTotal, pool);
      sumInstance(&instanceGlobal, &instanceTotal);

      pool = pool->next;
    }

    if(gPerHostData)
    {
      twemproxySubmitPool(tn->name, "pool-all", &poolTotal);
      twemproxySubmitInstance(tn->name, "instance-all", &instanceGlobal);
    }

    twemproxyPoolFree(stats.head);

    if(status)
      return 1;
  }

  return 0;

error:
  close(s);
  return 1;
}

static int twemproxyConfigNode(oconfig_item_t *ci)
{
  TwemproxyNode tn;
  int i;
  int status;
  oconfig_item_t *option;

  memset(&tn, 0, sizeof(tn));

  sstrncpy(tn.host, TWEMPROXY_DEF_HOST, sizeof(tn.host));

  if((status = cf_util_get_string_buffer(ci, tn.name, sizeof(tn.name))))
    return status;

  for(option = ci->children, i = 0; i < ci->children_num; ++i, ++option)
  {
    if(strcasecmp("Host", option->key) == 0)
    {
      if((status = cf_util_get_string_buffer(option, tn.host, sizeof(tn.host))))
        return status;

      continue;
    }

    WARNING("twemproxy plugin: Option '%s' not allowed inside a 'Node' block. I'll ignore this option.", option->key);
  }

  return twemproxyNodeAdd(&tn);
}

static int twemproxyConfig(oconfig_item_t *ci)
{
  int i;
  int status;
  oconfig_item_t *option;

  for(option = ci->children, i = 0; i < ci->children_num; ++i, ++option)
  {
    if(strcasecmp("Node", option->key) == 0)
    {
      if((status = twemproxyConfigNode(option)))
        return status;

      continue;
    }

    if(strcasecmp("PerPoolData", option->key) == 0)
    {
      if((status = cf_util_get_boolean(option, &gPerPoolData)))
        return status;

      continue;
    }

    if(strcasecmp("PerHostData", option->key) == 0)
    {
      if((status = cf_util_get_boolean(option, &gPerHostData)))
        return status;

      continue;
    }

    WARNING("redis plugin: Option '%s' not allowed in redis configuration. It will be ignored.", option->key);
  }

  if(gTwemproxyNodesHead == NULL)
  {
    ERROR("twemproxy plugin: No valid node configuration could be found.");
    return ENOENT;
  }

  return 0;
}

void module_register(void)
{
  plugin_register_complex_config("twemproxy", twemproxyConfig);
  plugin_register_init("twemproxy", twemproxyInit);
  plugin_register_read("twemproxy", twemproxyRead);
}
