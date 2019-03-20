/**
 * collectd - src/write_redis.c
 * Copyright (C) 2010-2015  Florian Forster
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
 *   Florian Forster <ff at octo.it>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <hiredis/hiredis.h>
#include <sys/time.h>

#ifndef REDIS_DEFAULT_PREFIX
#define REDIS_DEFAULT_PREFIX "collectd/"
#endif

struct wr_node_s {
  char name[DATA_MAX_NAME_LEN];

  char *host;
  int port;
  struct timeval timeout;
  char *prefix;
  int database;
  int max_set_size;
  int max_set_duration;
  bool store_rates;

  redisContext *conn;
  pthread_mutex_t lock;
};
typedef struct wr_node_s wr_node_t;

/*
 * Functions
 */
static int wr_write(const data_set_t *ds, /* {{{ */
                    const value_list_t *vl, user_data_t *ud) {
  wr_node_t *node = ud->data;
  char ident[512];
  char key[512];
  char value[512] = {0};
  char time[24];
  size_t value_size;
  char *value_ptr;
  int status;
  redisReply *rr;

  status = FORMAT_VL(ident, sizeof(ident), vl);
  if (status != 0)
    return status;
  snprintf(key, sizeof(key), "%s%s",
           (node->prefix != NULL) ? node->prefix : REDIS_DEFAULT_PREFIX, ident);
  snprintf(time, sizeof(time), "%.9f", CDTIME_T_TO_DOUBLE(vl->time));

  value_size = sizeof(value);
  value_ptr = &value[0];
  status = format_values(value_ptr, value_size, ds, vl, node->store_rates);
  if (status != 0)
    return status;

  pthread_mutex_lock(&node->lock);

  if (node->conn == NULL) {
    node->conn =
        redisConnectWithTimeout((char *)node->host, node->port, node->timeout);
    if (node->conn == NULL) {
      ERROR("write_redis plugin: Connecting to host \"%s\" (port %i) failed: "
            "Unknown reason",
            (node->host != NULL) ? node->host : "localhost",
            (node->port != 0) ? node->port : 6379);
      pthread_mutex_unlock(&node->lock);
      return -1;
    } else if (node->conn->err) {
      ERROR(
          "write_redis plugin: Connecting to host \"%s\" (port %i) failed: %s",
          (node->host != NULL) ? node->host : "localhost",
          (node->port != 0) ? node->port : 6379, node->conn->errstr);
      pthread_mutex_unlock(&node->lock);
      return -1;
    }

    rr = redisCommand(node->conn, "SELECT %d", node->database);
    if (rr == NULL)
      WARNING("SELECT command error. database:%d message:%s", node->database,
              node->conn->errstr);
    else
      freeReplyObject(rr);
  }

  rr = redisCommand(node->conn, "ZADD %s %s %s", key, time, value);
  if (rr == NULL)
    WARNING("ZADD command error. key:%s message:%s", key, node->conn->errstr);
  else
    freeReplyObject(rr);

  if (node->max_set_size >= 0) {
    rr = redisCommand(node->conn, "ZREMRANGEBYRANK %s %d %d", key, 0,
                      (-1 * node->max_set_size) - 1);
    if (rr == NULL)
      WARNING("ZREMRANGEBYRANK command error. key:%s message:%s", key,
              node->conn->errstr);
    else
      freeReplyObject(rr);
  }

  if (node->max_set_duration > 0) {
    /*
     * remove element, scored less than 'current-max_set_duration'
     * '(...' indicates 'less than' in redis CLI.
     */
    rr = redisCommand(node->conn, "ZREMRANGEBYSCORE %s -1 (%.9f", key,
                      (CDTIME_T_TO_DOUBLE(vl->time) - node->max_set_duration));
    if (rr == NULL)
      WARNING("ZREMRANGEBYSCORE command error. key:%s message:%s", key,
              node->conn->errstr);
    else
      freeReplyObject(rr);
  }

  /* TODO(octo): This is more overhead than necessary. Use the cache and
   * metadata to determine if it is a new metric and call SADD only once for
   * each metric. */
  rr = redisCommand(
      node->conn, "SADD %svalues %s",
      (node->prefix != NULL) ? node->prefix : REDIS_DEFAULT_PREFIX, ident);
  if (rr == NULL)
    WARNING("SADD command error. ident:%s message:%s", ident,
            node->conn->errstr);
  else
    freeReplyObject(rr);

  pthread_mutex_unlock(&node->lock);

  return 0;
} /* }}} int wr_write */

static void wr_config_free(void *ptr) /* {{{ */
{
  wr_node_t *node = ptr;

  if (node == NULL)
    return;

  if (node->conn != NULL) {
    redisFree(node->conn);
    node->conn = NULL;
  }

  sfree(node->host);
  sfree(node);
} /* }}} void wr_config_free */

static int wr_config_node(oconfig_item_t *ci) /* {{{ */
{
  wr_node_t *node;
  int timeout;
  int status;

  node = calloc(1, sizeof(*node));
  if (node == NULL)
    return ENOMEM;
  node->host = NULL;
  node->port = 0;
  node->timeout.tv_sec = 1;
  node->timeout.tv_usec = 0;
  node->conn = NULL;
  node->prefix = NULL;
  node->database = 0;
  node->max_set_size = -1;
  node->max_set_duration = -1;
  node->store_rates = true;
  pthread_mutex_init(&node->lock, /* attr = */ NULL);

  status = cf_util_get_string_buffer(ci, node->name, sizeof(node->name));
  if (status != 0) {
    sfree(node);
    return status;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Host", child->key) == 0)
      status = cf_util_get_string(child, &node->host);
    else if (strcasecmp("Port", child->key) == 0) {
      status = cf_util_get_port_number(child);
      if (status > 0) {
        node->port = status;
        status = 0;
      }
    } else if (strcasecmp("Timeout", child->key) == 0) {
      status = cf_util_get_int(child, &timeout);
      if (status == 0) {
        node->timeout.tv_usec = timeout * 1000;
        node->timeout.tv_sec = node->timeout.tv_usec / 1000000L;
        node->timeout.tv_usec %= 1000000L;
      }
    } else if (strcasecmp("Prefix", child->key) == 0) {
      status = cf_util_get_string(child, &node->prefix);
    } else if (strcasecmp("Database", child->key) == 0) {
      status = cf_util_get_int(child, &node->database);
    } else if (strcasecmp("MaxSetSize", child->key) == 0) {
      status = cf_util_get_int(child, &node->max_set_size);
    } else if (strcasecmp("MaxSetDuration", child->key) == 0) {
      status = cf_util_get_int(child, &node->max_set_duration);
    } else if (strcasecmp("StoreRates", child->key) == 0) {
      status = cf_util_get_boolean(child, &node->store_rates);
    } else
      WARNING("write_redis plugin: Ignoring unknown config option \"%s\".",
              child->key);

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status == 0) {
    char cb_name[sizeof("write_redis/") + DATA_MAX_NAME_LEN];

    snprintf(cb_name, sizeof(cb_name), "write_redis/%s", node->name);

    status =
        plugin_register_write(cb_name, wr_write,
                              &(user_data_t){
                                  .data = node, .free_func = wr_config_free,
                              });
  }

  if (status != 0)
    wr_config_free(node);

  return status;
} /* }}} int wr_config_node */

static int wr_config(oconfig_item_t *ci) /* {{{ */
{
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Node", child->key) == 0)
      wr_config_node(child);
    else
      WARNING("write_redis plugin: Ignoring unknown "
              "configuration option \"%s\" at top level.",
              child->key);
  }

  return 0;
} /* }}} int wr_config */

void module_register(void) {
  plugin_register_complex_config("write_redis", wr_config);
}
