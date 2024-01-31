/**
 * collectd - src/write_redis.c
 * Copyright (C) 2010-2024  Florian Forster
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

#include "daemon/plugin.h"
#include "daemon/utils_cache.h"
#include "utils/common/common.h"
#include "utils/format_json/format_json.h"

#include <hiredis/hiredis.h>
#include <sys/time.h>

static int const default_port = 6379;
static cdtime_t const default_timeout = TIME_T_TO_CDTIME_T(1);

struct wr_node_s;
typedef struct wr_node_s wr_node_t;
struct wr_node_s {
  char name[DATA_MAX_NAME_LEN];

  char *host;
  int port;
  cdtime_t timeout;
  char *prefix;
  int database;
  cdtime_t retention;
  bool store_rates;

  int (*reconnect)(wr_node_t *node);
  void (*disconnect)(wr_node_t *node);
  int (*execute)(wr_node_t *node, int argc, char const **argv);

  redisContext *conn;
  pthread_mutex_t lock;
};

/*
 * Functions
 */
static void disconnect(wr_node_t *node) {
  if (node->conn == NULL) {
    return;
  }

  redisFree(node->conn);
  node->conn = NULL;
}

static int reconnect(wr_node_t *node) {
  if (node->conn != NULL) {
    return 0;
  }

  node->conn = redisConnectWithTimeout(node->host, node->port,
                                       CDTIME_T_TO_TIMEVAL(node->timeout));
  if (node->conn == NULL) {
    ERROR("write_redis plugin: Connecting to host \"%s\" (port %i) failed: "
          "Unknown reason",
          (node->host != NULL) ? node->host : "localhost",
          (node->port != 0) ? node->port : default_port);
    return ENOTCONN;
  }
  if (node->conn->err) {
    ERROR("write_redis plugin: Connecting to host \"%s\" (port %i) failed: %s",
          (node->host != NULL) ? node->host : "localhost",
          (node->port != 0) ? node->port : default_port, node->conn->errstr);
    disconnect(node);
    return ENOTCONN;
  }

  redisReply *rr = redisCommand(node->conn, "SELECT %d", node->database);
  if (rr == NULL) {
    ERROR("write_redis plugin: Command \"SELECT %d\" failed: %s",
          node->database, node->conn->errstr);
    disconnect(node);
    return ENOTCONN;
  }

  freeReplyObject(rr);
  return 0;
}

static void print_execute_error(int argc, char const **argv, char const *msg) {
  strbuf_t cmd = STRBUF_CREATE;
  for (int i = 0; i < argc; i++) {
    if (i != 0) {
      strbuf_print(&cmd, " ");
    }
    strbuf_print(&cmd, argv[i]);
  }

  ERROR("write_redis plugin: Command \"%s\" failed: %s", cmd.ptr, msg);

  STRBUF_DESTROY(cmd);
}

static int execute(wr_node_t *node, int argc, char const **argv) {
  redisReply *rr = redisCommandArgv(node->conn, argc, argv, NULL);
  if (rr == NULL) {
    print_execute_error(argc, argv, node->conn->errstr);
    return (node->conn->err != 0) ? node->conn->err : -1;
  }
  if (rr->type == REDIS_REPLY_ERROR) {
    print_execute_error(argc, argv, rr->str);
    freeReplyObject(rr);
    return -1;
  }

  freeReplyObject(rr);
  return 0;
}

static int add_resource_to_global_set(wr_node_t *node, char const *id) {
  strbuf_t key = STRBUF_CREATE;
  if (node->prefix != NULL) {
    strbuf_print(&key, node->prefix);
  }
  strbuf_print(&key, "resources");

  char const *cmd[] = {"SADD", key.ptr, id};
  int err = node->execute(node, STATIC_ARRAY_SIZE(cmd), cmd);

  STRBUF_DESTROY(key);
  return err;
}

static int add_metric_to_resource(wr_node_t *node, char const *resource_id,
                                  char const *metric_id) {
  char const *cmd[] = {"SADD", resource_id, metric_id};
  return node->execute(node, STATIC_ARRAY_SIZE(cmd), cmd);
}

static int ts_add(wr_node_t *node, metric_t const *m, char const *id) {
  strbuf_t m_time = STRBUF_CREATE;
  strbuf_printf(&m_time, "%" PRIu64, CDTIME_T_TO_MS(m->time));

  strbuf_t value = STRBUF_CREATE;
  int err = format_values(&value, m, node->store_rates);
  if (err != 0) {
    ERROR("write_redis plugin: format_values failed: %s", STRERROR(err));
    goto cleanup;
  }

  // format_values returns a string with "<timestamp>:<value>"; create a new
  // pointer that points to "<value>" directly.
  char const *value_ptr = strchr(value.ptr, ':');
  if (value_ptr == NULL) {
    ERROR("write_redis plugin: format_values returned \"%s\", want string "
          "containing ':'",
          value.ptr);
    goto cleanup;
  }
  value_ptr++;

  // RedisTimeSeries doesn't handle NANs.
  if (strcmp("nan", value_ptr) == 0) {
    goto cleanup;
  }

  char const *cmd[] = {"TS.ADD", id, m_time.ptr, value_ptr};
  err = node->execute(node, STATIC_ARRAY_SIZE(cmd), cmd);

cleanup:
  STRBUF_DESTROY(value);
  STRBUF_DESTROY(m_time);
  return err;
}

static int ts_create(wr_node_t *node, metric_t const *m,
                     char const *metric_id) {
  strbuf_t retention = STRBUF_CREATE;
  strbuf_printf(&retention, "%" PRIu64, CDTIME_T_TO_MS(node->retention));

  size_t cmd_len = 11;
  size_t cmd_cap = cmd_len + 2 * m->label.num;
  char const *cmd[cmd_cap];
  memset(cmd, 0, sizeof(cmd));
  cmd[0] = "TS.CREATE";
  cmd[1] = metric_id;
  cmd[2] = "RETENTION";
  cmd[3] = retention.ptr;
  cmd[4] = "ENCODING";
  cmd[5] = "COMPRESSED";
  cmd[6] = "DUPLICATE_POLICY";
  cmd[7] = "FIRST";
  cmd[8] = "LABELS";
  cmd[9] = "metric.name";
  cmd[10] = m->family->name;

  for (size_t i = 0; i < m->label.num; i++) {
    assert(cmd_len + 2 <= cmd_cap);
    cmd[cmd_len] = m->label.ptr[i].name;
    cmd[cmd_len + 1] = m->label.ptr[i].value;
    cmd_len += 2;
  }

  int err = node->execute(node, (int)cmd_len, cmd);

  STRBUF_DESTROY(retention);
  return err;
}

static int write_metric(wr_node_t *node, char const *resource_id,
                        metric_t const *m, bool is_new) {
  strbuf_t id = STRBUF_CREATE;
  if (node->prefix != NULL) {
    strbuf_print(&id, node->prefix);
  }
  strbuf_print(&id, "metric/");
  int err = format_json_metric_identity(&id, m);
  if (err != 0) {
    ERROR("write_redis plugin: Formatting metric identity failed: %s",
          STRERROR(err));
    return err;
  }

  if (is_new) {
    // An error is returned even if the existing and new keys are equal
    // => ignore
    (void)ts_create(node, m, id.ptr);
  }

  err = ts_add(node, m, id.ptr);
  if (err != 0) {
    goto cleanup;
  }

  if (is_new) {
    err = add_metric_to_resource(node, resource_id, id.ptr);
    if (err != 0) {
      goto cleanup;
    }
  }

cleanup:
  STRBUF_DESTROY(id);
  return err;
}

static bool metric_is_new(metric_t const *m) {
  uc_first_metric_result_t first = uc_first_metric(m);
  if (first.err != 0) {
    ERROR("write_redis plugin: uc_get_first failed: %s", STRERROR(first.err));
    return true;
  }

  return m->time == first.time;
}

static int wr_write(metric_family_t const *fam, user_data_t *ud) {
  wr_node_t *node = ud->data;

  strbuf_t resource_id = STRBUF_CREATE;
  if (node->prefix != NULL) {
    strbuf_print(&resource_id, node->prefix);
  }
  strbuf_print(&resource_id, "resource/");
  int err = format_json_label_set(&resource_id, fam->resource);
  if (err != 0) {
    STRBUF_DESTROY(resource_id);
    return err;
  }

  // determine whether metrics are new before grabbing the log.
  bool all_new = true;
  bool is_new[fam->metric.num];
  memset(is_new, 0, sizeof(is_new));

  for (size_t i = 0; i < fam->metric.num; i++) {
    is_new[i] = metric_is_new(fam->metric.ptr + i);
    all_new = all_new && is_new[i];
  }

  pthread_mutex_lock(&node->lock);

  err = node->reconnect(node);
  if (err != 0) {
    goto cleanup;
  }

  for (size_t i = 0; i < fam->metric.num; i++) {
    int err =
        write_metric(node, resource_id.ptr, fam->metric.ptr + i, is_new[i]);
    if (err != 0) {
      WARNING("write_redis plugin: write_metric failed with %s, aborting at "
              "index %zu",
              STRERROR(err), i);
      continue;
    }
  }

  if (all_new) {
    err = add_resource_to_global_set(node, resource_id.ptr);
    if (err != 0) {
      goto cleanup;
    }
  }

cleanup:
  pthread_mutex_unlock(&node->lock);
  STRBUF_DESTROY(resource_id);
  return err;
}

static void wr_config_free(void *ptr) /* {{{ */
{
  wr_node_t *node = ptr;
  if (node == NULL)
    return;

  node->disconnect(node);
  pthread_mutex_destroy(&node->lock);

  sfree(node->host);
  sfree(node->prefix);
  sfree(node);
} /* }}} void wr_config_free */

static int wr_config_node(oconfig_item_t *ci) /* {{{ */
{
  wr_node_t *node = calloc(1, sizeof(*node));
  if (node == NULL)
    return ENOMEM;

  *node = (wr_node_t){
      .store_rates = true,
      .port = default_port,
      .timeout = default_timeout,

      .reconnect = reconnect,
      .disconnect = disconnect,
      .execute = execute,
  };
  pthread_mutex_init(&node->lock, /* attr = */ NULL);

  int status = cf_util_get_string_buffer(ci, node->name, sizeof(node->name));
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
      status = cf_util_get_cdtime(child, &node->timeout);
    } else if (strcasecmp("Prefix", child->key) == 0) {
      status = cf_util_get_string(child, &node->prefix);
    } else if (strcasecmp("Database", child->key) == 0) {
      status = cf_util_get_int(child, &node->database);
    } else if (strcasecmp("Retention", child->key) == 0) {
      status = cf_util_get_cdtime(child, &node->retention);
    } else if (strcasecmp("StoreRates", child->key) == 0) {
      status = cf_util_get_boolean(child, &node->store_rates);
    } else
      WARNING("write_redis plugin: Ignoring unknown config option \"%s\".",
              child->key);

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status == 0) {
    strbuf_t cb_name = STRBUF_CREATE;
    strbuf_printf(&cb_name, "write_redis/%s", node->name);

    status = plugin_register_write(cb_name.ptr, wr_write,
                                   &(user_data_t){
                                       .data = node,
                                       .free_func = wr_config_free,
                                   });
    STRBUF_DESTROY(cb_name);
  }

  if (status != 0) {
    wr_config_free(node);
  }

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
