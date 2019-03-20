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

#include "plugin.h"
#include "utils/common/common.h"

#include <hiredis/hiredis.h>
#include <sys/time.h>

#define REDIS_DEF_HOST "localhost"
#define REDIS_DEF_PASSWD ""
#define REDIS_DEF_PORT 6379
#define REDIS_DEF_TIMEOUT_SEC 2
#define REDIS_DEF_DB_COUNT 256
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
struct redis_query_s {
  char query[MAX_REDIS_QUERY];
  char type[DATA_MAX_NAME_LEN];
  char instance[DATA_MAX_NAME_LEN];
  int database;

  redis_query_t *next;
};

struct prev_s {
  derive_t keyspace_hits;
  derive_t keyspace_misses;
};
typedef struct prev_s prev_t;

struct redis_node_s;
typedef struct redis_node_s redis_node_t;
struct redis_node_s {
  char *name;
  char *host;
  char *socket;
  char *passwd;
  int port;
  struct timeval timeout;
  bool report_command_stats;
  bool report_cpu_usage;
  redisContext *redisContext;
  redis_query_t *queries;
  prev_t prev;

  redis_node_t *next;
};

static bool redis_have_instances;
static int redis_read(user_data_t *user_data);

static void redis_node_free(void *arg) {
  redis_node_t *rn = arg;
  if (rn == NULL)
    return;

  redis_query_t *rq = rn->queries;
  while (rq != NULL) {
    redis_query_t *next = rq->next;
    sfree(rq);
    rq = next;
  }

  if (rn->redisContext)
    redisFree(rn->redisContext);
  sfree(rn->name);
  sfree(rn->host);
  sfree(rn->socket);
  sfree(rn->passwd);
  sfree(rn);
} /* void redis_node_free */

static int redis_node_add(redis_node_t *rn) /* {{{ */
{
  DEBUG("redis plugin: Adding node \"%s\".", rn->name);

  /* Disable automatic generation of default instance in the init callback. */
  redis_have_instances = true;

  char cb_name[sizeof("redis/") + DATA_MAX_NAME_LEN];
  snprintf(cb_name, sizeof(cb_name), "redis/%s", rn->name);

  return plugin_register_complex_read(
      /* group = */ "redis",
      /* name      = */ cb_name,
      /* callback  = */ redis_read,
      /* interval  = */ 0,
      &(user_data_t){
          .data = rn, .free_func = redis_node_free,
      });
} /* }}} */

static redis_query_t *redis_config_query(oconfig_item_t *ci) /* {{{ */
{
  redis_query_t *rq;
  int status;

  rq = calloc(1, sizeof(*rq));
  if (rq == NULL) {
    ERROR("redis plugin: calloc failed adding redis_query.");
    return NULL;
  }
  status = cf_util_get_string_buffer(ci, rq->query, sizeof(rq->query));
  if (status != 0)
    goto err;

  /*
   * Default to a gauge type.
   */
  (void)strncpy(rq->type, "gauge", sizeof(rq->type));
  (void)sstrncpy(rq->instance, rq->query, sizeof(rq->instance));
  replace_special(rq->instance, sizeof(rq->instance));

  rq->database = 0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Type", option->key) == 0) {
      status = cf_util_get_string_buffer(option, rq->type, sizeof(rq->type));
    } else if (strcasecmp("Instance", option->key) == 0) {
      status =
          cf_util_get_string_buffer(option, rq->instance, sizeof(rq->instance));
    } else if (strcasecmp("Database", option->key) == 0) {
      status = cf_util_get_int(option, &rq->database);
      if (rq->database < 0) {
        WARNING("redis plugin: The \"Database\" option must be positive "
                "integer or zero");
        status = -1;
      }
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

static int redis_config_node(oconfig_item_t *ci) /* {{{ */
{
  redis_node_t *rn = calloc(1, sizeof(*rn));
  if (rn == NULL) {
    ERROR("redis plugin: calloc failed adding node.");
    return ENOMEM;
  }

  rn->port = REDIS_DEF_PORT;
  rn->timeout.tv_sec = REDIS_DEF_TIMEOUT_SEC;
  rn->report_cpu_usage = true;

  rn->host = strdup(REDIS_DEF_HOST);
  if (rn->host == NULL) {
    ERROR("redis plugin: strdup failed adding node.");
    sfree(rn);
    return ENOMEM;
  }

  int status = cf_util_get_string(ci, &rn->name);
  if (status != 0) {
    sfree(rn->host);
    sfree(rn);
    return status;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Host", option->key) == 0)
      status = cf_util_get_string(option, &rn->host);
    else if (strcasecmp("Port", option->key) == 0) {
      status = cf_util_get_port_number(option);
      if (status > 0) {
        rn->port = status;
        status = 0;
      }
    } else if (strcasecmp("Socket", option->key) == 0) {
      status = cf_util_get_string(option, &rn->socket);
    } else if (strcasecmp("Query", option->key) == 0) {
      redis_query_t *rq = redis_config_query(option);
      if (rq == NULL) {
        status = 1;
      } else {
        rq->next = rn->queries;
        rn->queries = rq;
      }
    } else if (strcasecmp("Timeout", option->key) == 0) {
      int timeout;
      status = cf_util_get_int(option, &timeout);
      if (status == 0) {
        rn->timeout.tv_usec = timeout * 1000;
        rn->timeout.tv_sec = rn->timeout.tv_usec / 1000000L;
        rn->timeout.tv_usec %= 1000000L;
      }
    } else if (strcasecmp("Password", option->key) == 0)
      status = cf_util_get_string(option, &rn->passwd);
    else if (strcasecmp("ReportCommandStats", option->key) == 0)
      status = cf_util_get_boolean(option, &rn->report_command_stats);
    else if (strcasecmp("ReportCpuUsage", option->key) == 0)
      status = cf_util_get_boolean(option, &rn->report_cpu_usage);
    else
      WARNING("redis plugin: Option `%s' not allowed inside a `Node' "
              "block. I'll ignore this option.",
              option->key);

    if (status != 0)
      break;
  }

  if (status != 0) {
    redis_node_free(rn);
    return status;
  }

  return redis_node_add(rn);
} /* }}} int redis_config_node */

static int redis_config(oconfig_item_t *ci) /* {{{ */
{
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Node", option->key) == 0)
      redis_config_node(option);
    else
      WARNING("redis plugin: Option `%s' not allowed in redis"
              " configuration. It will be ignored.",
              option->key);
  }

  return 0;
} /* }}} */

__attribute__((nonnull(2))) static void
redis_submit(const char *plugin_instance, const char *type,
             const char *type_instance, value_t value) /* {{{ */
{
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &value;
  vl.values_len = 1;
  sstrncpy(vl.plugin, "redis", sizeof(vl.plugin));
  if (plugin_instance != NULL)
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* }}} */

__attribute__((nonnull(2))) static void
redis_submit2(const char *plugin_instance, const char *type,
              const char *type_instance, value_t value0,
              value_t value1) /* {{{ */
{
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {value0, value1};

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);

  sstrncpy(vl.plugin, "redis", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));

  if (plugin_instance != NULL)
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));

  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* }}} */

static int redis_init(void) /* {{{ */
{
  if (redis_have_instances)
    return 0;

  redis_node_t *rn = calloc(1, sizeof(*rn));
  if (rn == NULL)
    return ENOMEM;

  rn->port = REDIS_DEF_PORT;
  rn->timeout.tv_sec = REDIS_DEF_TIMEOUT_SEC;

  rn->name = strdup("default");
  rn->host = strdup(REDIS_DEF_HOST);

  if (rn->name == NULL || rn->host == NULL) {
    sfree(rn->name);
    sfree(rn->host);
    sfree(rn);
    return ENOMEM;
  }

  return redis_node_add(rn);
} /* }}} int redis_init */

static void *c_redisCommand(redis_node_t *rn, const char *format, ...) {
  redisContext *c = rn->redisContext;

  if (c == NULL)
    return NULL;

  va_list ap;
  va_start(ap, format);
  void *reply = redisvCommand(c, format, ap);
  va_end(ap);

  if (reply == NULL) {
    ERROR("redis plugin: Connection error: %s", c->errstr);
    redisFree(rn->redisContext);
    rn->redisContext = NULL;
  }

  return reply;
} /* void c_redisCommand */

static int redis_get_info_value(char const *info_line, char const *field_name,
                                int ds_type, value_t *val) {
  char *str = strstr(info_line, field_name);
  static char buf[MAX_REDIS_VAL_SIZE];
  if (str) {
    int i;

    str += strlen(field_name) + 1; /* also skip the ':' */
    for (i = 0; (*str && (isdigit((unsigned char)*str) || *str == '.'));
         i++, str++)
      buf[i] = *str;
    buf[i] = '\0';

    if (parse_value(buf, val, ds_type) == -1) {
      WARNING("redis plugin: Unable to parse field `%s'.", field_name);
      return -1;
    }

    return 0;
  }
  return -1;
} /* int redis_get_info_value */

static int redis_handle_info(char *node, char const *info_line,
                             char const *type, char const *type_instance,
                             char const *field_name, int ds_type) /* {{{ */
{
  value_t val;
  if (redis_get_info_value(info_line, field_name, ds_type, &val) != 0)
    return -1;

  redis_submit(node, type, type_instance, val);
  return 0;
} /* }}} int redis_handle_info */

static int redis_handle_query(redis_node_t *rn, redis_query_t *rq) /* {{{ */
{
  redisReply *rr;
  const data_set_t *ds;
  value_t val;

  ds = plugin_get_ds(rq->type);
  if (!ds) {
    ERROR("redis plugin: DS type `%s' not defined.", rq->type);
    return -1;
  }

  if (ds->ds_num != 1) {
    ERROR("redis plugin: DS type `%s' has too many datasources. This is not "
          "supported currently.",
          rq->type);
    return -1;
  }

  if ((rr = c_redisCommand(rn, "SELECT %d", rq->database)) == NULL) {
    WARNING("redis plugin: unable to switch to database `%d' on node `%s'.",
            rq->database, rn->name);
    return -1;
  }

  if ((rr = c_redisCommand(rn, rq->query)) == NULL) {
    WARNING("redis plugin: unable to carry out query `%s'.", rq->query);
    return -1;
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
    if (parse_value(rr->str, &val, ds->ds[0].type) == -1) {
      WARNING("redis plugin: Query `%s': Unable to parse value.", rq->query);
      freeReplyObject(rr);
      return -1;
    }
    break;
  case REDIS_REPLY_ERROR:
    WARNING("redis plugin: Query `%s' failed: %s.", rq->query, rr->str);
    freeReplyObject(rr);
    return -1;
  case REDIS_REPLY_ARRAY:
    WARNING("redis plugin: Query `%s' should return string or integer. Arrays "
            "are not supported.",
            rq->query);
    freeReplyObject(rr);
    return -1;
  default:
    WARNING("redis plugin: Query `%s': Cannot coerce redis type (%i).",
            rq->query, rr->type);
    freeReplyObject(rr);
    return -1;
  }

  redis_submit(rn->name, rq->type,
               (strlen(rq->instance) > 0) ? rq->instance : NULL, val);
  freeReplyObject(rr);
  return 0;
} /* }}} int redis_handle_query */

static int redis_db_stats(const char *node, char const *info_line) /* {{{ */
{
  /* redis_db_stats parses and dispatches Redis database statistics,
   * currently the number of keys for each database.
   * info_line needs to have the following format:
   *   db0:keys=4,expires=0,avg_ttl=0
   */

  for (int db = 0; db < REDIS_DEF_DB_COUNT; db++) {
    static char buf[MAX_REDIS_VAL_SIZE];
    static char field_name[12];
    static char db_id[4];
    value_t val;
    char *str;
    int i;

    snprintf(field_name, sizeof(field_name), "db%d:keys=", db);

    str = strstr(info_line, field_name);
    if (!str)
      continue;

    str += strlen(field_name);
    for (i = 0; (*str && isdigit((int)*str)); i++, str++)
      buf[i] = *str;
    buf[i] = '\0';

    if (parse_value(buf, &val, DS_TYPE_GAUGE) != 0) {
      WARNING("redis plugin: Unable to parse field `%s'.", field_name);
      return -1;
    }

    snprintf(db_id, sizeof(db_id), "%d", db);
    redis_submit(node, "records", db_id, val);
  }
  return 0;

} /* }}} int redis_db_stats */

static void redis_cpu_usage(const char *node, char const *info_line) {
  while (42) {
    value_t rusage_user;
    value_t rusage_syst;

    if (redis_get_info_value(info_line, "used_cpu_user", DS_TYPE_GAUGE,
                             &rusage_user) != 0)
      break;

    if (redis_get_info_value(info_line, "used_cpu_sys", DS_TYPE_GAUGE,
                             &rusage_syst) != 0)
      break;

    redis_submit2(node, "ps_cputime", "daemon",
                  (value_t){.derive = rusage_user.gauge * 1000000},
                  (value_t){.derive = rusage_syst.gauge * 1000000});
    break;
  }

  while (42) {
    value_t rusage_user;
    value_t rusage_syst;

    if (redis_get_info_value(info_line, "used_cpu_user_children", DS_TYPE_GAUGE,
                             &rusage_user) != 0)
      break;

    if (redis_get_info_value(info_line, "used_cpu_sys_children", DS_TYPE_GAUGE,
                             &rusage_syst) != 0)
      break;

    redis_submit2(node, "ps_cputime", "children",
                  (value_t){.derive = rusage_user.gauge * 1000000},
                  (value_t){.derive = rusage_syst.gauge * 1000000});
    break;
  }
} /* void redis_cpu_usage */

static gauge_t calculate_ratio_percent(derive_t part1, derive_t part2,
                                       derive_t *prev1, derive_t *prev2) {
  if ((*prev1 == 0) || (*prev2 == 0) || (part1 < *prev1) || (part2 < *prev2)) {
    *prev1 = part1;
    *prev2 = part2;
    return NAN;
  }

  derive_t num = part1 - *prev1;
  derive_t denom = part2 - *prev2 + num;

  *prev1 = part1;
  *prev2 = part2;

  if (denom == 0)
    return NAN;

  if (num == 0)
    return 0;

  return 100.0 * (gauge_t)num / (gauge_t)denom;
} /* gauge_t calculate_ratio_percent */

static void redis_keyspace_usage(redis_node_t *rn, char const *info_line) {
  value_t hits, misses;

  if (redis_get_info_value(info_line, "keyspace_hits", DS_TYPE_DERIVE, &hits) !=
      0)
    return;

  if (redis_get_info_value(info_line, "keyspace_misses", DS_TYPE_DERIVE,
                           &misses) != 0)
    return;

  redis_submit(rn->name, "cache_result", "hits", hits);
  redis_submit(rn->name, "cache_result", "misses", misses);

  prev_t *prev = &rn->prev;
  gauge_t ratio = calculate_ratio_percent(
      hits.derive, misses.derive, &prev->keyspace_hits, &prev->keyspace_misses);
  redis_submit(rn->name, "percent", "hitratio", (value_t){.gauge = ratio});

} /* void redis_keyspace_usage */

static void redis_check_connection(redis_node_t *rn) {
  if (rn->redisContext)
    return;

  redisContext *rh;
  if (rn->socket != NULL)
    rh = redisConnectUnixWithTimeout(rn->socket, rn->timeout);
  else
    rh = redisConnectWithTimeout(rn->host, rn->port, rn->timeout);

  if (rh == NULL) {
    ERROR("redis plugin: can't allocate redis context");
    return;
  }
  if (rh->err) {
    if (rn->socket)
      ERROR("redis plugin: unable to connect to node `%s' (%s): %s.", rn->name,
            rn->socket, rh->errstr);
    else
      ERROR("redis plugin: unable to connect to node `%s' (%s:%d): %s.",
            rn->name, rn->host, rn->port, rh->errstr);
    redisFree(rh);
    return;
  }

  rn->redisContext = rh;

  if (rn->passwd) {
    redisReply *rr;

    DEBUG("redis plugin: authenticating node `%s' passwd(%s).", rn->name,
          rn->passwd);

    if ((rr = c_redisCommand(rn, "AUTH %s", rn->passwd)) == NULL) {
      WARNING("redis plugin: unable to authenticate on node `%s'.", rn->name);
      return;
    }

    if (rr->type != REDIS_REPLY_STATUS) {
      WARNING("redis plugin: invalid authentication on node `%s'.", rn->name);
      freeReplyObject(rr);
      redisFree(rn->redisContext);
      rn->redisContext = NULL;
      return;
    }

    freeReplyObject(rr);
  }
  return;
} /* void redis_check_connection */

static void redis_read_server_info(redis_node_t *rn) {
  redisReply *rr;

  if ((rr = c_redisCommand(rn, "INFO")) == NULL) {
    WARNING("redis plugin: unable to get INFO from node `%s'.", rn->name);
    return;
  }

  redis_handle_info(rn->name, rr->str, "uptime", NULL, "uptime_in_seconds",
                    DS_TYPE_GAUGE);
  redis_handle_info(rn->name, rr->str, "current_connections", "clients",
                    "connected_clients", DS_TYPE_GAUGE);
  redis_handle_info(rn->name, rr->str, "blocked_clients", NULL,
                    "blocked_clients", DS_TYPE_GAUGE);
  redis_handle_info(rn->name, rr->str, "memory", NULL, "used_memory",
                    DS_TYPE_GAUGE);
  redis_handle_info(rn->name, rr->str, "memory_lua", NULL, "used_memory_lua",
                    DS_TYPE_GAUGE);
  /* changes_since_last_save: Deprecated in redis version 2.6 and above */
  redis_handle_info(rn->name, rr->str, "volatile_changes", NULL,
                    "changes_since_last_save", DS_TYPE_GAUGE);
  redis_handle_info(rn->name, rr->str, "total_connections", NULL,
                    "total_connections_received", DS_TYPE_DERIVE);
  redis_handle_info(rn->name, rr->str, "total_operations", NULL,
                    "total_commands_processed", DS_TYPE_DERIVE);
  redis_handle_info(rn->name, rr->str, "expired_keys", NULL, "expired_keys",
                    DS_TYPE_DERIVE);
  redis_handle_info(rn->name, rr->str, "evicted_keys", NULL, "evicted_keys",
                    DS_TYPE_DERIVE);
  redis_handle_info(rn->name, rr->str, "pubsub", "channels", "pubsub_channels",
                    DS_TYPE_GAUGE);
  redis_handle_info(rn->name, rr->str, "pubsub", "patterns", "pubsub_patterns",
                    DS_TYPE_GAUGE);
  redis_handle_info(rn->name, rr->str, "current_connections", "slaves",
                    "connected_slaves", DS_TYPE_GAUGE);
  redis_handle_info(rn->name, rr->str, "total_bytes", "input",
                    "total_net_input_bytes", DS_TYPE_DERIVE);
  redis_handle_info(rn->name, rr->str, "total_bytes", "output",
                    "total_net_output_bytes", DS_TYPE_DERIVE);

  redis_keyspace_usage(rn, rr->str);

  redis_db_stats(rn->name, rr->str);

  if (rn->report_cpu_usage)
    redis_cpu_usage(rn->name, rr->str);

  freeReplyObject(rr);
} /* void redis_read_server_info */

static void redis_read_command_stats(redis_node_t *rn) {
  redisReply *rr;

  if ((rr = c_redisCommand(rn, "INFO commandstats")) == NULL) {
    WARNING("redis plugin: node `%s': unable to get `INFO commandstats'.",
            rn->name);
    return;
  }

  if (rr->type != REDIS_REPLY_STRING) {
    WARNING("redis plugin: node `%s' `INFO commandstats' returned unsupported "
            "redis type %i.",
            rn->name, rr->type);
    freeReplyObject(rr);
    return;
  }

  char *command;
  char *line;
  char *ptr = rr->str;
  char *saveptr = NULL;
  while ((line = strtok_r(ptr, "\n\r", &saveptr)) != NULL) {
    ptr = NULL;

    if (line[0] == '#')
      continue;

    /* command name */
    if (strstr(line, "cmdstat_") != line) {
      ERROR("redis plugin: not found 'cmdstat_' prefix in line '%s'", line);
      continue;
    }

    char *values = strstr(line, ":");
    if (values == NULL) {
      ERROR("redis plugin: not found ':' separator in line '%s'", line);
      continue;
    }

    /* Null-terminate command token */
    values[0] = '\0';
    command = line + strlen("cmdstat_");
    values++;

    /* parse values */
    /* cmdstat_publish:calls=20795774,usec=111039258,usec_per_call=5.34 */
    char *field;
    char *saveptr_field = NULL;
    while ((field = strtok_r(values, "=", &saveptr_field)) != NULL) {
      values = NULL;

      const char *type;
      /* only these are supported */
      if (strcmp(field, "calls") == 0)
        type = "commands";
      else if (strcmp(field, "usec") == 0)
        type = "redis_command_cputime";
      else
        continue;

      if ((field = strtok_r(NULL, ",", &saveptr_field)) == NULL)
        continue;

      char *endptr = NULL;
      errno = 0;
      derive_t value = strtoll(field, &endptr, 0);

      if ((endptr == field) || (errno != 0))
        continue;

      redis_submit(rn->name, type, command, (value_t){.derive = value});
    }
  }
  freeReplyObject(rr);
} /* void redis_read_command_stats */

static int redis_read(user_data_t *user_data) /* {{{ */
{
  redis_node_t *rn = user_data->data;

#if COLLECT_DEBUG
  if (rn->socket)
    DEBUG("redis plugin: querying info from node `%s' (%s).", rn->name,
          rn->socket);
  else
    DEBUG("redis plugin: querying info from node `%s' (%s:%d).", rn->name,
          rn->host, rn->port);
#endif

  redis_check_connection(rn);

  if (!rn->redisContext) /* no connection */
    return -1;

  redis_read_server_info(rn);

  if (!rn->redisContext) /* connection lost */
    return -1;

  if (rn->report_command_stats) {
    redis_read_command_stats(rn);

    if (!rn->redisContext) /* connection lost */
      return -1;
  }

  for (redis_query_t *rq = rn->queries; rq != NULL; rq = rq->next) {
    redis_handle_query(rn, rq);
    if (!rn->redisContext) /* connection lost */
      return -1;
  }

  return 0;
}
/* }}} */

void module_register(void) /* {{{ */
{
  plugin_register_complex_config("redis", redis_config);
  plugin_register_init("redis", redis_init);
}
/* }}} */
