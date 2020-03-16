/**
 * collectd - src/mongodb.c
 * Copyright (C) 2020 Pavel Rochnyak
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
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
 *   Pavel Rochnyak  <pavel2000 ngs.ru>
 **/

#include "collectd.h"

#include "configfile.h"
#include "plugin.h"
#include "utils/common/common.h"

#include <mongoc.h>

#define MONGO_DEF_HOST "localhost"
#define MONGO_DEF_DB "admin"

struct mdb_node_status_metric_s;
typedef struct mdb_node_status_metric_s mdb_node_status_metric_t;

struct mdb_node_status_metric_s {
  char *path;
  char *type;
  char *type_instance;
  char *plugin_instance;

  int ds_type; // data_source_t->type

  mdb_node_status_metric_t *next;
};

struct mdb_node_s {
  char name[DATA_MAX_NAME_LEN];
  mongoc_client_t *client;
  char *reportHost;

  bool notifyRoleChanges; // configuration flag
  bool repl_ismaster;     // previous state
  bool repl_issecondary;  // previous state
  bool repl_hasState;     // has previous state

  bool reportTotalSize;
  bool reportDBSize;
  bool reportDBStorageStats;
  bool reportDB_hideAdminDB;
  bool reportDB_asPluginInstance;

  mdb_node_status_metric_t *status_metrics;
  bool status_metrics_init;
};
typedef struct mdb_node_s mdb_node_t;

void mdb_logger(mongoc_log_level_t log_level, const char *log_domain,
                const char *message, void *user_data) {
  switch (log_level) {
  case MONGOC_LOG_LEVEL_ERROR:
  case MONGOC_LOG_LEVEL_CRITICAL:
    ERROR("mongodb plugin: %s: %s", log_domain, message);
    break;
  case MONGOC_LOG_LEVEL_WARNING:
    WARNING("mongodb plugin: %s: %s", log_domain, message);
    break;
  case MONGOC_LOG_LEVEL_MESSAGE:
    NOTICE("mongodb plugin: %s: %s", log_domain, message);
    break;
  case MONGOC_LOG_LEVEL_INFO:
    INFO("mongodb plugin: %s: %s", log_domain, message);
    break;
  case MONGOC_LOG_LEVEL_DEBUG:
  case MONGOC_LOG_LEVEL_TRACE:
    break;
  }
}

#ifndef BSON_ITER_HOLDS_NUMBER
#define BSON_ITER_HOLDS_NUMBER(iter)                                           \
  (BSON_ITER_HOLDS_INT32(iter) || BSON_ITER_HOLDS_INT64(iter) ||               \
   BSON_ITER_HOLDS_DOUBLE(iter))
#endif

void mdb_notify(const mdb_node_t *node, const char *message, int severity) {
  notification_t n = {0};

  if (node->reportHost != NULL)
    sstrncpy(n.host, node->reportHost, sizeof(n.host));
  else
    sstrncpy(n.host, hostname_g, sizeof(n.host));

  n.time = cdtime();
  n.severity = severity;
  sstrncpy(n.plugin, "mongodb", sizeof(n.plugin));
  ssnprintf(n.message, sizeof(n.message), message, node->name);

  plugin_dispatch_notification(&n);
}

void mdb_notifyRoleChanges(mdb_node_t *node, const bson_t *reply) {
  bson_iter_t iter;
  bson_iter_t sub_iter;
  if (bson_iter_init(&iter, reply) &&
      bson_iter_find_descendant(&iter, "repl.ismaster", &sub_iter) &&
      BSON_ITER_HOLDS_BOOL(&sub_iter)) {
    bool ismaster = bson_iter_as_bool(&sub_iter);
    if (node->repl_ismaster && !ismaster) {
      if (node->repl_hasState) {
        mdb_notify(node, "Node %s lost master status.", NOTIF_WARNING);
      }
      node->repl_ismaster = false;
    } else if (ismaster && !node->repl_ismaster) {
      if (node->repl_hasState) {
        mdb_notify(node, "Node %s becomes master member.", NOTIF_OKAY);
      }
      node->repl_ismaster = true;
    }
  } else {
    WARNING("mongodb plugin: Node `%s`: Unable to find `repl.ismaster` field.",
            node->name);
  }

  if (bson_iter_init(&iter, reply) &&
      bson_iter_find_descendant(&iter, "repl.secondary", &sub_iter) &&
      BSON_ITER_HOLDS_BOOL(&sub_iter)) {
    bool issecondary = bson_iter_as_bool(&sub_iter);
    if (node->repl_issecondary && !issecondary) {
      if (node->repl_hasState) {
        mdb_notify(node, "Node %s lost secondary status.", NOTIF_WARNING);
      }
      node->repl_issecondary = false;
    } else if (issecondary && !node->repl_issecondary) {
      if (node->repl_hasState) {
        mdb_notify(node, "Node %s becomes secondary member.", NOTIF_OKAY);
      }
      node->repl_issecondary = true;
    }
  } else {
    WARNING("mongodb plugin: Node `%s`: Unable to find `repl.secondary` field.",
            node->name);
  }
  node->repl_hasState = true;
}

static void submit(const mdb_node_t *node, const char *plugin_instance,
                   const char *type, const char *type_instance, value_t *values,
                   size_t values_len) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = values_len;

  if (node->reportHost != NULL)
    sstrncpy(vl.host, node->reportHost, sizeof(vl.host));

  sstrncpy(vl.plugin, "mongodb", sizeof(vl.plugin));
  if (plugin_instance != NULL)
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));

  sstrncpy(vl.type, type, sizeof(vl.type));

  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

/* Chooses where to put database name, in plugin_instance or in type_instance,
 * depending on ReportDB_AsPluginInstance option */
static void submit_db(const mdb_node_t *node, const char *dbName,
                      const char *type, value_t *values, size_t values_len) {

  submit(node, node->reportDB_asPluginInstance ? dbName : NULL, type,
         node->reportDB_asPluginInstance ? NULL : dbName, values, values_len);
}

static void handle_asserts(const mdb_node_t *node, bson_iter_t *doc_iter) {
  while (bson_iter_next(doc_iter)) {
    const char *subkey = bson_iter_key(doc_iter);

    // Starting in MongoDB 4.0, the field always returns zero.
    if (strcmp(subkey, "warning") == 0)
      continue;
    // The number of times that the rollover counters have rolled over since
    // start. Not useful.
    if (strcmp(subkey, "rollovers") == 0)
      continue;

    int64_t value = bson_iter_as_int64(doc_iter);
    submit(node, NULL, "asserts", subkey, &(value_t){.derive = value}, 1);
  }
}

static void handle_backgroundFlushing(const mdb_node_t *node,
                                      bson_iter_t *doc_iter) {
  while (bson_iter_next(doc_iter)) {
    const char *subkey = bson_iter_key(doc_iter);

    if (strcmp(subkey, "flushes") == 0) {
      // The number of times the database has flushed all writes to disk.
      int64_t requests = bson_iter_as_int64(doc_iter);
      submit(node, NULL, "flushes", NULL, &(value_t){.derive = requests}, 1);
    } else if (strcmp(subkey, "total_ms") == 0) {
      // The total number of milliseconds (ms) that the mongod processes have
      // spent writing data to disk.
      int64_t requests = bson_iter_as_int64(doc_iter) / 1000;
      submit(node, NULL, "flush_time", NULL, &(value_t){.derive = requests}, 1);
    }
  }
}

static void handle_connections(const mdb_node_t *node, bson_iter_t *doc_iter) {
  while (bson_iter_next(doc_iter)) {
    const char *subkey = bson_iter_key(doc_iter);

    if (strcmp(subkey, "current") == 0) {
      // The number of incoming connections from clients to the database server
      int64_t requests = bson_iter_as_int64(doc_iter);
      submit(node, NULL, "current_connections", "total",
             &(value_t){.gauge = requests}, 1);
    } else if (strcmp(subkey, "active") == 0) {
      // connections.active - New in version 4.0.7.
      // Active client connections refers to client connections that currently
      // have operations in progress.
      int64_t requests = bson_iter_as_int64(doc_iter);
      submit(node, NULL, "current_connections", "active",
             &(value_t){.gauge = requests}, 1);
    } else if (strcmp(subkey, "totalCreated") == 0) {
      // Count of all incoming connections created to the server.
      int64_t requests = bson_iter_as_int64(doc_iter);
      submit(node, NULL, "connections", NULL, &(value_t){.derive = requests},
             1);
    }
  }
}

static void handle_network(const mdb_node_t *node, bson_iter_t *doc_iter) {
  int64_t rx = 0;
  int64_t tx = 0;
  while (bson_iter_next(doc_iter)) {
    const char *subkey = bson_iter_key(doc_iter);

    if (strcmp(subkey, "bytesIn") == 0) {
      rx = bson_iter_as_int64(doc_iter);
    } else if (strcmp(subkey, "bytesOut") == 0) {
      tx = bson_iter_as_int64(doc_iter);
    } else if (strcmp(subkey, "numRequests") == 0) {
      int64_t requests = bson_iter_as_int64(doc_iter);
      submit(node, NULL, "total_requests", NULL, &(value_t){.derive = requests},
             1);
    }
  }

  value_t values[] = {
      {.derive = rx},
      {.derive = tx},
  };
  submit(node, NULL, "io_octets", NULL, values, STATIC_ARRAY_SIZE(values));
}

static void handle_opcounters(const mdb_node_t *node, bson_iter_t *doc_iter) {
  while (bson_iter_next(doc_iter)) {
    const char *subkey = bson_iter_key(doc_iter);
    int64_t value = bson_iter_as_int64(doc_iter);

    submit(node, NULL, "operations", subkey, &(value_t){.derive = value}, 1);
  }
}

static void handle_opcountersRepl(const mdb_node_t *node,
                                  bson_iter_t *doc_iter) {
  while (bson_iter_next(doc_iter)) {
    const char *subkey = bson_iter_key(doc_iter);
    int64_t value = bson_iter_as_int64(doc_iter);

    submit(node, NULL, "mongodb_replops", subkey, &(value_t){.derive = value},
           1);
  }
}

static void handle_mem(const mdb_node_t *node, bson_iter_t *doc_iter) {
  while (bson_iter_next(doc_iter)) {
    const char *subkey = bson_iter_key(doc_iter);

    if ((strcmp(subkey, "resident") != 0) && (strcmp(subkey, "virtual") != 0))
      continue;

    int64_t value = bson_iter_as_int64(doc_iter); // Value in MiB
    submit(node, NULL, "memory", subkey, &(value_t){.gauge = value * 1048576},
           1);
  }
}

static void handle_metrics_document(const mdb_node_t *node,
                                    bson_iter_t *doc_iter) {
  // reflects document access and modification patterns.
  while (bson_iter_next(doc_iter)) {
    const char *subkey = bson_iter_key(doc_iter);
    int64_t value = bson_iter_as_int64(doc_iter);

    submit(node, NULL, "mongodb_documents", subkey, &(value_t){.derive = value},
           1);
  }
}

void mdb_node_status_metric_free(mdb_node_status_metric_t *metric) {
  assert(metric->next == NULL);
  sfree(metric->path);
  sfree(metric->type);
  sfree(metric->type_instance);
  sfree(metric->plugin_instance);
  sfree(metric);
}

static void mdb_init_customMetrics(mdb_node_t *node) {
  mdb_node_status_metric_t *prev = NULL;
  mdb_node_status_metric_t *metric = node->status_metrics;

  while (metric != NULL) {
    const data_set_t *ds = plugin_get_ds(metric->type);

    if (ds && (ds->ds_num == 1)) {
      // Save required data
      metric->ds_type = ds->ds->type;
      // Prepare variables for possible deletion case
      prev = metric;
      // Go to next metric
      metric = metric->next;
      continue;
    }

    if (!ds) {
      ERROR("mongodb plugin: Type `%s`, specified for path `%s` in "
            "`ServerStatusMetric` option, not defined.",
            metric->type, metric->path);
    } else {
      ERROR("mongodb plugin: Type `%s`, specified for path `%s` in "
            "`ServerStatusMetric` option, should have one data source, but %zu "
            "found.",
            metric->type, metric->path, ds->ds_num);
    }

    // Remove metric from linked list
    mdb_node_status_metric_t *next = metric->next;
    metric->next = NULL;

    mdb_node_status_metric_free(metric);

    metric = next;

    if (prev == NULL) {
      node->status_metrics = metric;
    } else {
      prev->next = metric;
    }
  }
}

static void mdb_get_serverStatus_customMetrics(mdb_node_t *node,
                                               const bson_t *reply) {
  if (!node->status_metrics_init) {
    mdb_init_customMetrics(node);
    node->status_metrics_init = true;
  }

  mdb_node_status_metric_t *metric = node->status_metrics;
  while (metric != NULL) {
    bson_iter_t iter, sub_iter;
    if (bson_iter_init(&iter, reply) &&
        bson_iter_find_descendant(&iter, metric->path, &sub_iter)) {
      // Key found
      if (BSON_ITER_HOLDS_NUMBER(&sub_iter) ||
          BSON_ITER_HOLDS_BOOL(&sub_iter)) {
        value_t value;

        switch (metric->ds_type) {
        case DS_TYPE_COUNTER:
          value.counter = bson_iter_as_int64(&sub_iter);
          break;
        case DS_TYPE_GAUGE:
          value.gauge = bson_iter_as_int64(&sub_iter);
          break;
        case DS_TYPE_DERIVE:
          value.derive = bson_iter_as_int64(&sub_iter);
          break;
        case DS_TYPE_ABSOLUTE:
          value.absolute = bson_iter_as_int64(&sub_iter);
        }
        submit(node, metric->plugin_instance, metric->type,
               metric->type_instance, &value, 1);
      } else {
        WARNING("mongodb plugin: Node `%s`: Key `%s` of `serverStatus` "
                "document has unsupported type %d.",
                node->name, metric->path, bson_iter_type(&sub_iter));
      }
    } else { // Key not found
      WARNING("mongodb plugin: Node `%s`: Key `%s` not found in `serverStatus` "
              "document.",
              node->name, metric->path);
    }

    metric = metric->next;
  }
}

static int mdb_run_serverStatus(mdb_node_t *node) {
  bson_t *command = BCON_NEW("serverStatus", BCON_INT32(1));

  bson_t reply;
  bson_error_t error;
  bool ret = mongoc_client_command_simple(node->client, MONGO_DEF_DB, command,
                                          NULL, &reply, &error);
  if (!ret) {
    ERROR("mongodb plugin: Node `%s`: Failed to run `serverStatus`: %s.",
          node->name, error.message);
    bson_destroy(command);
    return -1;
  }

  bson_iter_t iter;
  if (!(bson_iter_init_find(&iter, &reply, "ok") &&
        (BSON_ITER_HOLDS_NUMBER(&iter)) && (bson_iter_as_int64(&iter) == 1))) {
    ERROR("mongodb plugin: Node `%s`: Command `serverStatus` return 'failed' "
          "status.",
          node->name);
    bson_destroy(&reply);
    bson_destroy(command);
    return -1;
  }

  if (node->status_metrics)
    mdb_get_serverStatus_customMetrics(node, &reply);

  if (node->notifyRoleChanges)
    mdb_notifyRoleChanges(node, &reply);

  bson_iter_t sub_iter;
  if (bson_iter_init(&iter, &reply)) {
    while (bson_iter_next(&iter)) {
      const char *ikey = bson_iter_key(&iter);

      if (strcmp(ikey, "uptime") == 0) {
        int64_t uptime = bson_iter_as_int64(&iter);
        submit(node, NULL, "uptime", NULL, &(value_t){.gauge = uptime}, 1);
        continue;
      }

      if (!BSON_ITER_HOLDS_DOCUMENT(&iter))
        continue;

      if ((strcmp(ikey, "asserts") == 0) && bson_iter_recurse(&iter, &sub_iter))
        handle_asserts(node, &sub_iter);
      else if ((strcmp(ikey, "backgroundFlushing") == 0) &&
               bson_iter_recurse(&iter, &sub_iter))
        // information only appears for instances that use the MMAPv1 storage
        // engine. Starting in version 4.2, MongoDB removes the deprecated
        // MMAPv1 storage engine.
        handle_backgroundFlushing(node, &sub_iter);
      else if ((strcmp(ikey, "connections") == 0) &&
               bson_iter_recurse(&iter, &sub_iter))
        handle_connections(node, &sub_iter);
      else if ((strcmp(ikey, "network") == 0) &&
               bson_iter_recurse(&iter, &sub_iter))
        handle_network(node, &sub_iter);
      else if ((strcmp(ikey, "opcounters") == 0) &&
               bson_iter_recurse(&iter, &sub_iter))
        handle_opcounters(node, &sub_iter);
      else if ((strcmp(ikey, "opcountersRepl") == 0) &&
               bson_iter_recurse(&iter, &sub_iter))
        handle_opcountersRepl(node, &sub_iter);
      else if ((strcmp(ikey, "mem") == 0) &&
               bson_iter_recurse(&iter, &sub_iter))
        handle_mem(node, &sub_iter);
      // TODO: support "transactions"
      // (no recent version mongo instance available for tests)
      // else if (strcmp(ikey, "transactions") == 0)
      // Available on mongod in 3.6.3+ and on mongos in 4.2+.
      // Most of metrics available on mongod in 4.0.2+ and mongos in 4.2.1+
      //  handle_transactions(&iter);
      else if ((strcmp(ikey, "metrics") == 0) &&
               bson_iter_recurse(&iter, &sub_iter)) {
        bson_iter_t doc_iter;
        if (!bson_iter_find_descendant(&sub_iter, "document", &doc_iter) ||
            !BSON_ITER_HOLDS_DOCUMENT(&doc_iter) ||
            !bson_iter_recurse(&doc_iter, &sub_iter)) {
          WARNING(
              "mongodb plugin: Node `%s`: Not found `document` in `metrics`.",
              node->name);
          continue;
        }
        handle_metrics_document(node, &sub_iter);
      }
    }
  }

  bson_destroy(&reply);
  bson_destroy(command);

  return 0;
}

static int mdb_run_dbStats(mdb_node_t *node, const char *dbName) {
  bson_t reply;
  bson_error_t error;
  bson_t *command = BCON_NEW("dbstats", BCON_INT32(1));
  bool ret = mongoc_client_command_simple(node->client, dbName, command, NULL,
                                          &reply, &error);
  if (!ret) {
    ERROR("mongodb plugin: Node `%s`, database `%s`: Failed to run `dbstats`: "
          "%s.",
          node->name, dbName, error.message);
    bson_destroy(command);
    return -1;
  }

  bson_iter_t iter;
  if (!(bson_iter_init_find(&iter, &reply, "ok") &&
        (BSON_ITER_HOLDS_NUMBER(&iter)) && (bson_iter_as_int64(&iter) == 1))) {
    ERROR("mongodb plugin: Node `%s`, database `%s`: Command `dbstats` return "
          "'failed' status.",
          node->name, dbName);
    bson_destroy(&reply);
    bson_destroy(command);
    return -1;
  }

  if (bson_iter_init(&iter, &reply)) {
    while (bson_iter_next(&iter)) {
      if (!BSON_ITER_HOLDS_NUMBER(&iter))
        continue;

      const char *subkey = bson_iter_key(&iter);
      int64_t value = bson_iter_as_int64(&iter);

      if (strcmp(subkey, "collections") == 0)
        submit_db(node, dbName, "mongodb_collections",
                  &(value_t){.gauge = value}, 1);
      else if (strcmp(subkey, "objects") == 0)
        submit_db(node, dbName, "mongodb_objects", &(value_t){.gauge = value},
                  1);
      else if (strcmp(subkey, "dataSize") == 0)
        submit_db(node, dbName, "mongodb_datasize", &(value_t){.gauge = value},
                  1);
      else if (strcmp(subkey, "storageSize") == 0)
        submit_db(node, dbName, "mongodb_storagesize",
                  &(value_t){.gauge = value}, 1);
      else if (strcmp(subkey, "numExtents") == 0)
        submit_db(node, dbName, "mongodb_extents", &(value_t){.gauge = value},
                  1);
      else if (strcmp(subkey, "indexes") == 0)
        submit_db(node, dbName, "mongodb_indexes", &(value_t){.gauge = value},
                  1);
      else if (strcmp(subkey, "indexSize") == 0)
        submit_db(node, dbName, "mongodb_indexsize", &(value_t){.gauge = value},
                  1);
    }
  }

  bson_destroy(&reply);
  bson_destroy(command);
  return 0;
}

static int mdb_run_listDatabases(mdb_node_t *node) {
  bson_t *command = BCON_NEW("listDatabases", BCON_INT32(1));

  bson_t reply;
  bson_error_t error;
  bool ret = mongoc_client_command_simple(node->client, MONGO_DEF_DB, command,
                                          NULL, &reply, &error);
  if (!ret) {
    ERROR("mongodb plugin: Node `%s`: Failed to run `listDatabases`: %s.",
          node->name, error.message);
    bson_destroy(command);
    return -1;
  }

  bson_iter_t iter;
  if (!(bson_iter_init_find(&iter, &reply, "ok") &&
        (BSON_ITER_HOLDS_NUMBER(&iter)) && (bson_iter_as_int64(&iter) == 1))) {
    ERROR("mongodb plugin: Node `%s`: Command `listDatabases` return 'failed' "
          "status.",
          node->name);
    bson_destroy(&reply);
    bson_destroy(command);
    return -1;
  }

  if (node->reportTotalSize) {
    if (bson_iter_init_find(&iter, &reply, "totalSize") &&
        BSON_ITER_HOLDS_NUMBER(&iter)) {
      int64_t value = bson_iter_as_int64(&iter);
      submit(node, NULL, "mongodb_totalsize", NULL, &(value_t){.gauge = value},
             1);
    } else {
      WARNING("mongodb plugin: Node `%s`: Command `listDatabases` returns "
              "unsupported 'totalSize' field.",
              node->name);
    }
  }

  if (node->reportDBSize || node->reportDBStorageStats) {
    bson_iter_t array_iter;
    if (bson_iter_init_find(&iter, &reply, "databases") &&
        BSON_ITER_HOLDS_ARRAY(&iter) && bson_iter_recurse(&iter, &array_iter)) {
      while (bson_iter_next(&array_iter) &&
             BSON_ITER_HOLDS_DOCUMENT(&array_iter)) {

        bson_iter_t elem_iter;
        if (bson_iter_recurse(&array_iter, &elem_iter) &&
            bson_iter_find(&elem_iter, "empty") &&
            BSON_ITER_HOLDS_BOOL(&elem_iter)) {
          bool empty = bson_iter_as_bool(&elem_iter);
          if (empty)
            continue; // Skip empty databases
        } else {
          WARNING(
              "mongodb plugin: Node `%s`: Unable to find databases.X.empty in "
              "`listDatabases` output.",
              node->name);
          continue;
        }

        const char *dbName = NULL;
        if (bson_iter_recurse(&array_iter, &elem_iter) &&
            bson_iter_find(&elem_iter, "name") &&
            BSON_ITER_HOLDS_UTF8(&elem_iter)) {
          dbName = bson_iter_utf8(&elem_iter, NULL);
        } else {
          WARNING(
              "mongodb plugin: Node `%s`: Unable to find databases.X.name in "
              "`listDatabases` output.",
              node->name);
          continue;
        }

        if (node->reportDB_hideAdminDB && (strcmp(dbName, "admin") == 0))
          continue;

        if (node->reportDBSize) {
          if (bson_iter_recurse(&array_iter, &elem_iter) &&
              bson_iter_find(&elem_iter, "sizeOnDisk") &&
              BSON_ITER_HOLDS_NUMBER(&elem_iter)) {
            int64_t sizeOnDisk = bson_iter_as_int64(&elem_iter);

            submit_db(node, dbName, "mongodb_filesize",
                      &(value_t){.gauge = sizeOnDisk}, 1);
          } else {
            WARNING("mongodb plugin: Node `%s`: Unable to find "
                    "databases.X.sizeOnDisk in `listDatabases` output.",
                    node->name);
            continue;
          }
        }
        if (node->reportDBStorageStats) {
          mdb_run_dbStats(node, dbName);
        }
      }
    } else {
      WARNING("mongodb plugin: Node `%s`: Command `listDatabases` returns "
              "unsupported 'databases' field.",
              node->name);
    }
  }

  bson_destroy(&reply);
  bson_destroy(command);

  return 0;
}

static int mdb_read_node(user_data_t *user_data) {
  mdb_node_t *node = user_data->data;

  assert(node->name != NULL);
  assert(node->client != NULL);

  int status = mdb_run_serverStatus(node);
  if (status != 0)
    return status;

  if (node->reportTotalSize || node->reportDBSize || node->reportDBStorageStats)
    status = mdb_run_listDatabases(node);

  if (status != 0)
    return status;

  return 0;
}

static void mdb_node_free(void *ptr) {
  mdb_node_t *node = ptr;

  if (node == NULL)
    return;

  if (node->client)
    mongoc_client_destroy(node->client);

  mdb_node_status_metric_t *metric = node->status_metrics;
  while (metric != NULL) {
    mdb_node_status_metric_t *next = metric->next;
    metric->next = NULL;

    mdb_node_status_metric_free(metric);

    metric = next;
  }

  sfree(node->reportHost);
  sfree(node);
}

static int mdb_percent_encode(char **str) {
  if ((str == NULL) || (*str == NULL))
    return -EINVAL;

  const size_t len = strlen(*str);
  size_t i = 0, cnt = 0;
  while (i < len) {
    const char octet = (*str)[i++];

    switch (octet) {
    case '@':
    case '&':
    case ':':
    case '/':
    case '%':
      cnt++;
      break;
    default:
      break;
    }
  }

  if (cnt == 0)
    return 0;

  char *newstr = realloc(*str, len + 2 * cnt + 1); // include trailing \0
  if (newstr == NULL)
    return -ENOMEM;

  *str = newstr;

  i = len;
  size_t j = len + 2 * cnt;

  while (j > i) {
    const char octet = (*str)[i--];

    int16_t code = 0;
    switch (octet) {
    case '@':
      code = 0x3430; // "40"
      break;
    case '&':
      code = 0x3236; // "26"
      break;
    case ':':
      code = 0x3341; // "3A"
      break;
    case '/':
      code = 0x3246; // "2F"
      break;
    case '%':
      code = 0x3235; // "25"
      break;
    default:
      break;
    }

    if (code) {
      (*str)[j--] = code & 0xFF;
      (*str)[j--] = code >> 8;
      (*str)[j--] = 0x25; // "%"
    } else {
      (*str)[j--] = octet;
    }
  }

  return 0;
}

static int mdb_node_add_custom_metric(oconfig_item_t *ci, mdb_node_t *node) {

  if ((ci->values_num < 2) || (ci->values_num > 4) ||
      (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING) ||
      ((ci->values_num >= 3) && (ci->values[2].type != OCONFIG_TYPE_STRING)) ||
      ((ci->values_num >= 4) && (ci->values[3].type != OCONFIG_TYPE_STRING))) {
    ERROR("mongodb plugin: The `ServerStatusMetric` option requires two, three "
          "or four string arguments.");
    return -1;
  }

  if (strlen(ci->values[0].value.string) == 0) {
    ERROR("mongodb plugin: The `ServerStatusMetric` option `path` argument "
          "cannot be empty.");
    return -1;
  }

  if (strlen(ci->values[1].value.string) == 0) {
    ERROR("mongodb plugin: The `ServerStatusMetric` option `type` argument "
          "cannot be empty.");
    return -1;
  }

  mdb_node_status_metric_t *metric = calloc(1, sizeof(*metric));
  if (metric == NULL) {
    ERROR("mongodb plugin: mdb_node_add_custom_metric: calloc failed.");
    return -1;
  }

  metric->path = strdup(ci->values[0].value.string);
  if (metric->path == NULL) {
    ERROR("mongodb plugin: mdb_node_add_custom_metric: strdup failed.");
    sfree(metric);
    return -1;
  }

  metric->type = strdup(ci->values[1].value.string);
  if (metric->path == NULL) {
    ERROR("mongodb plugin: mdb_node_add_custom_metric: strdup failed.");
    sfree(metric->path);
    sfree(metric);
    return -1;
  }

  if ((ci->values_num > 2) && (strlen(ci->values[2].value.string) > 0)) {
    metric->type_instance = strdup(ci->values[2].value.string);
    if (metric->type_instance == NULL) {
      ERROR("mongodb plugin: mdb_node_add_custom_metric: strdup failed.");
      sfree(metric->path);
      sfree(metric->type);
      sfree(metric);
      return -1;
    }
  }

  if ((ci->values_num > 3) && (strlen(ci->values[3].value.string) > 0)) {
    metric->plugin_instance = strdup(ci->values[3].value.string);
    if (metric->plugin_instance == NULL) {
      ERROR("mongodb plugin: mdb_node_add_custom_metric: strdup failed.");
      sfree(metric->type_instance);
      sfree(metric->path);
      sfree(metric->type);
      sfree(metric);
      return -1;
    }
  }

  metric->next = node->status_metrics;
  node->status_metrics = metric;

  return 0;
}

static int mdb_config_node(oconfig_item_t *ci) {
  mdb_node_t *node = calloc(1, sizeof(*node));
  if (node == NULL) {
    ERROR("mongodb plugin: calloc failed.");
    return ENOMEM;
  }

  int status = cf_util_get_string_buffer(ci, node->name, sizeof(node->name));
  if (status != 0) {
    sfree(node);
    return status;
  }

  char *uri = NULL;
  char *host = NULL;
  int port = 0;
  char *user = NULL;
  char *passwd = NULL;
  char *db = NULL;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("URI", child->key) == 0)
      status = cf_util_get_string(child, &uri);
    else if (strcasecmp("Host", child->key) == 0)
      status = cf_util_get_string(child, &host);
    else if (strcasecmp("Port", child->key) == 0) {
      status = cf_util_get_port_number(child);
      if (status > 0) {
        port = status;
        status = 0;
      }
    } else if (strcasecmp("User", child->key) == 0) {
      status = cf_util_get_string(child, &user);
      // If the username or password includes the at sign @, colon :,
      // slash /, or the percent sign % character, we need to use percent
      // encoding.
      if (status == 0)
        status = mdb_percent_encode(&user);
    } else if (strcasecmp("Password", child->key) == 0) {
      status = cf_util_get_string(child, &passwd);
      if (status == 0)
        status = mdb_percent_encode(&passwd);
    } else if (strcasecmp("Database", child->key) == 0)
      status = cf_util_get_string(child, &db);
    else if (strcasecmp("ReportHost", child->key) == 0)
      status = cf_util_get_string(child, &node->reportHost);
    // Notifications
    else if (strcasecmp("NotifyRoleChanges", child->key) == 0)
      status = cf_util_get_boolean(child, &node->notifyRoleChanges);
    // Report options
    else if (strcasecmp("ReportTotalSize", child->key) == 0)
      status = cf_util_get_boolean(child, &node->reportTotalSize);
    else if (strcasecmp("ReportDBSize", child->key) == 0)
      status = cf_util_get_boolean(child, &node->reportDBSize);
    else if (strcasecmp("ReportDBStorageStats", child->key) == 0)
      status = cf_util_get_boolean(child, &node->reportDBStorageStats);
    else if (strcasecmp("ReportDB_HideAdminDB", child->key) == 0)
      status = cf_util_get_boolean(child, &node->reportDB_hideAdminDB);
    else if (strcasecmp("ReportDB_AsPluginInstance", child->key) == 0)
      status = cf_util_get_boolean(child, &node->reportDB_asPluginInstance);
    // Custom Server Status metric
    else if (strcasecmp("ServerStatusMetric", child->key) == 0)
      status = mdb_node_add_custom_metric(child, node);
    else
      WARNING("mongodb plugin: Ignoring unknown config option \"%s\".",
              child->key);

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status != 0)
    goto err;

  if (uri != NULL) {
    if ((host != NULL) || (port != 0) || (user != NULL) || (passwd != NULL) ||
        (db != NULL)) {
      ERROR("mongodb plugin: Option `URI` could not be used together with "
            "`Host`, `Port`, `User`, `Password` or `Database`.");
      status = -1;
      goto err;
    }
  } else { // uri == NULL
    if ((user == NULL) || (passwd == NULL)) {
      ERROR("mongodb plugin: Authentication option `User` or `Password` is "
            "missing.");
      status = -1;
      goto err;
    }

    uri = ssnprintf_alloc("mongodb://%s:%s@%s:%d/?authSource=%s", user, passwd,
                          (host != NULL) ? host : MONGO_DEF_HOST,
                          (port > 0) ? port : MONGOC_DEFAULT_PORT,
                          (db != NULL) ? db : MONGO_DEF_DB);
    if (uri == NULL) {
      ERROR("mongodb plugin: Not enough memory to assemble connection uri.");
      status = ENOMEM;
      goto err;
    }
  }

#if MONGOC_CHECK_VERSION(1, 7, 0)
  bson_error_t error;
  mongoc_uri_t *mongoc_uri = mongoc_uri_new_with_error(uri, &error);
  if (!mongoc_uri) {
    ERROR("mongodb plugin: Failed to parse URI `%s`, error: `%s`.", uri,
          error.message);
    status = -1;
    goto err;
  }
#else
  mongoc_uri_t *mongoc_uri = mongoc_uri_new(uri);
  if (!mongoc_uri) {
    ERROR("mongodb plugin: Failed to parse URI `%s`.", uri);
    status = -1;
    goto err;
  }
#endif

  node->client = mongoc_client_new_from_uri(mongoc_uri);
  if (!node->client) {
    ERROR("mongodb plugin: Failed to create client.");
    mongoc_uri_destroy(mongoc_uri);
    status = -1;
    goto err;
  }
  mongoc_client_set_error_api(node->client, 2);

  char cb_name[sizeof("mongodb/") + sizeof(node->name)];
  snprintf(cb_name, sizeof(cb_name), "mongodb/%s", node->name);

  status = plugin_register_complex_read("mongodb", cb_name, mdb_read_node, 0,
                                        &(user_data_t){
                                            .data = node,
                                            .free_func = mdb_node_free,
                                        });

  sfree(uri);
  sfree(host);
  sfree(user);
  sfree(passwd);
  sfree(db);

  return status;

err:
  sfree(uri);
  sfree(host);
  sfree(user);
  sfree(passwd);
  sfree(db);
  mdb_node_free(node);

  return status;
}

static int mdb_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Node", child->key) == 0) {
      int status = mdb_config_node(child);
      if (status != 0) {
        ERROR("mongodb plugin: Failed to handle configuration.");
        return status;
      }
    } else
      WARNING("mongodb plugin: Ignoring unknown configuration block `%s`.",
              child->key);
  }

  return 0;
}

static int mdb_init(void) {
  mongoc_init();
  mongoc_log_set_handler(mdb_logger, NULL);
  return 0;
}

void module_register(void) {
  plugin_register_init("mongodb", mdb_init);
  plugin_register_complex_config("mongodb", mdb_config);
}