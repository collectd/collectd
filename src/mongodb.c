/**
 * collectd - src/mongo.c
 * Copyright (C) 2010 Ryan Cox
 * Copyright (C) 2012 Florian Forster
 * Copyright (C) 2013 John (J5) Palmieri
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
 *   Ryan Cox <ryan.a.cox at gmail.com>
 *   Florian Forster <octo at collectd.org>
 *   John (J5) Palmieri <j5 at stackdriver.com>
 *   Corey Kosak <kosak at google.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_llist.h"

#include <errno.h>

#include <mongoc.h>
#include <bson.h>

static const char this_plugin_name[] = "mongodb";

typedef struct {
  char *hostname;
  char *server_uri;
  _Bool prefer_secondary_query;
} context_t;

static context_t *context_create(const char *hostname, const char *server_uri,
                                 _Bool prefer_secondary_query) {
  context_t *result = calloc(1, sizeof(*result));
  if (result == NULL) {
    ERROR("mongodb plugin: calloc failed.");
    return NULL;
  }
  result->hostname = strdup(hostname);
  result->server_uri = strdup(server_uri);
  result->prefer_secondary_query = prefer_secondary_query;
  return result;
}

static void context_destroy(context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  sfree(ctx->server_uri);
  sfree(ctx->hostname);
  sfree(ctx);
}

// Gets a value from the bson_iter (which can be in one of a few different
// numeric formats) and add it to the appropriate slot (indicated by ds_type)
// of the value_t indicated by *result. This is basically a
// demultiplexing/multiplexing problem. Caller needs to initialize '*sum' to
// some reasonable initial value (like zero). 'scale' is given as a fraction
// in order to avoid awkward conversions to/from double which can lose precision
// in the int64 and uint64 cases (because double has only 53 bits of precision).
static int mg_try_sum_value(const bson_iter_t *iter, int ds_type,
    int scaleNum, int scaleDenom, value_t *sum) {
  // The bson value can be one of a few types. Our strategy for dealing with
  // this is to store the value we extract in the variable of the most
  // appropriate type, then copy that value to the other variables. Then we
  // select the appropriate one of those three variables according to 'ds_type'.
  int64_t int64_value;
  uint64_t uint64_value;
  double double_value;

  // The bson value can be one of a few types. First store the in the variable of
  // the most appropriate type and then copy to the others.
  switch (bson_iter_type(iter)) {
    case BSON_TYPE_INT32:
      int64_value = bson_iter_int32(iter) * (int64_t)scaleNum / scaleDenom;
      uint64_value = int64_value;
      double_value = int64_value;
      break;

    case BSON_TYPE_INT64:
      int64_value = bson_iter_int64(iter) * scaleNum / scaleDenom;
      uint64_value = int64_value;
      double_value = int64_value;
      break;

    case BSON_TYPE_DOUBLE:
      double_value = bson_iter_double(iter) * scaleNum / scaleDenom;
      int64_value = double_value;
      uint64_value = double_value;
      break;

    default:
      ERROR("mongodb plugin: unrecognized iter type %d.", bson_iter_type(iter));
      return -1;
  }

  // Now sum into the appropriate field of the result union.
  switch (ds_type) {
    case DS_TYPE_COUNTER:
      sum->counter += uint64_value;
      break;

    case DS_TYPE_GAUGE:
      sum->gauge += double_value;
      break;

    case DS_TYPE_DERIVE:
      sum->derive += int64_value;
      break;

    case DS_TYPE_ABSOLUTE:
      sum->absolute += uint64_value;
      break;

    default:
      ERROR("mongodb plugin: unrecognized ds_type %d.", ds_type);
      return -1;
  }
  return 0;
}

// Add all of the values of the document to 'sum'. Caller needs to initialize
// '*sum' to some reasonable initial value (like zero).
static int mg_try_sum_document(const bson_iter_t *doc, int ds_type,
    int scaleNum, int scaleDenom, value_t *sum) {
  bson_iter_t iter;
  if (!bson_iter_recurse(doc, &iter)) {
    ERROR("mongodb plugin: couldn't recurse into document.");
    return -1;
  }

  while (bson_iter_next(&iter)) {
    if (mg_try_sum_value(&iter, ds_type, scaleNum, scaleDenom, sum) != 0) {
      ERROR("mongodb plugin: Failed to parse value from document. Key is %s.",
          bson_iter_key(&iter));
      return -1;
    }
  }
  return 0;
}

typedef struct {
  _Bool sum_document;
  const char *key;
  const char *type;
  const char *type_instance;
  int ds_type;
  // "scale" is broken up into numerator and denominator, in order to avoid
  // loss of precision when doing math on 64 bit integers (the idea is to avoid
  // converting to doubles if we don't have to).
  int scaleNum;
  int scaleDenom;
} parse_info_t;

static parse_info_t server_parse_infos[] = {
    { 0, "opcounters.insert", "total_operations", "insert", DS_TYPE_DERIVE, 1, 1 },
    { 0, "opcounters.query", "total_operations", "query", DS_TYPE_DERIVE, 1, 1 },
    { 0, "opcounters.update", "total_operations", "update", DS_TYPE_DERIVE, 1, 1 },
    { 0, "opcounters.delete", "total_operations", "delete", DS_TYPE_DERIVE, 1, 1 },
    { 0, "opcounters.getmore", "total_operations", "getmore", DS_TYPE_DERIVE, 1, 1},
    { 0, "opcounters.command", "total_operations", "command", DS_TYPE_DERIVE, 1, 1},

    { 0, "mem.mapped", "memory", "mapped", DS_TYPE_GAUGE, 1 << 20, 1 },
    { 0, "mem.resident", "memory", "resident", DS_TYPE_GAUGE, 1 << 20, 1},
    { 0, "mem.virtual", "memory", "virtual", DS_TYPE_GAUGE, 1 << 20, 1 },

    { 0, "connections.current", "current_connections", NULL, DS_TYPE_GAUGE, 1, 1},

    // This mapping depends on which version of Mongo is runinng. One of them
    // will succeed and the other will fail.
    // pre-Mongo 3.0
    { 0, "globalLock.lockTime", "total_time_in_ms", "global_lock_held",
        DS_TYPE_DERIVE, 1, 1 },
    // post-Mongo 3.0
    { 1, "locks.Global.timeAcquiringMicros", "total_time_in_ms", "global_lock_held",
        DS_TYPE_DERIVE, 1, 1000 },
};

static parse_info_t db_parse_infos[] = {
    { 0, "collections", "gauge", "collections", DS_TYPE_GAUGE, 1, 1 },
    { 0, "objects", "gauge", "objects", DS_TYPE_GAUGE, 1, 1 },
    { 0, "numExtents", "gauge", "num_extents", DS_TYPE_GAUGE, 1, 1 },
    { 0, "indexes", "gauge", "indexes", DS_TYPE_GAUGE, 1, 1 },
    { 0, "dataSize", "bytes", "data", DS_TYPE_GAUGE, 1, 1 },
    { 0, "storageSize", "bytes", "storage", DS_TYPE_GAUGE, 1, 1 },
    { 0, "indexSize", "bytes", "index", DS_TYPE_GAUGE, 1, 1 }
};

// Fill in a few values and submit the value_t.
static int mg_submit_helper(value_t *value, cdtime_t now, cdtime_t interval,
    const char *type, const char *plugin_instance, const char *type_instance) {
    value_list_t vl = {
        .values = value,
        .values_len = 1,
        .time = now,
        .interval = interval
    };
    sstrncpy(vl.host, hostname_g, sizeof(vl.host));
    sstrncpy(vl.plugin, this_plugin_name, sizeof(vl.plugin));
    sstrncpy(vl.type, type, sizeof(vl.type));
    if (plugin_instance != NULL) {
      sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
    }
    if (type_instance != NULL) {
      sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));
    }

    if (plugin_dispatch_values(&vl) != 0) {
      ERROR("mongodb plugin: plugin_dispatch_values failed.");
      return -1;
    }
    return 0;
}

static int mg_parse_and_submit(
    const bson_t *status, const char *plugin_instance,
    const parse_info_t *infos, size_t num_infos) {
  cdtime_t now = cdtime();
  cdtime_t interval = plugin_get_interval();

  size_t i;
  for (i = 0; i < num_infos; ++i) {
    bson_iter_t iter;
    bson_iter_t sub_iter;
    const parse_info_t *ip = &infos[i];

    if (!bson_iter_init(&iter, status)) {
      ERROR("mongodb plugin: bson_iter_init failed.");
      return -1;
    }

    if (!bson_iter_find_descendant(&iter, ip->key, &sub_iter)) {
      DEBUG("mongodb plugin: key %s not found.", ip->key);
      continue;
    }

    value_t value;
    memset(&value, 0, sizeof(value));
    int result;
    if (ip->sum_document) {
      result = mg_try_sum_document(&sub_iter, ip->ds_type, ip->scaleNum,
          ip->scaleDenom, &value);
    } else {
      result = mg_try_sum_value(&sub_iter, ip->ds_type, ip->scaleNum,
          ip->scaleDenom, &value);
    }
    if (result != 0) {
      ERROR("mongodb plugin: Error getting value for key %s.", ip->key);
      return -1;
    }

    if (mg_submit_helper(&value, now, interval, ip->type, plugin_instance,
        ip->type_instance) != 0) {
      ERROR("mongodb plugin: mg_submit_helper failed on key %s.",
            ip->key);
      return -1;
    }
  }
  return 0;
}

/*
 * Read statistics from the mongo database `db_name`.
 */
static int mg_process_database(
    const context_t *ctx, mongoc_client_t *client, const char *db_name) {
  bson_t *request = NULL;
  bson_t reply = BSON_INITIALIZER;
  bson_error_t error;
  int result = -1;  // Pessimistically assume failure.

  request = BCON_NEW("dbStats", BCON_INT32(1),
                     "scale", BCON_INT32(1));

  if (!mongoc_client_command_simple(client, db_name, request,
                                    NULL, &reply, &error)) {
    ERROR("mongodb plugin: dbStats command failed: %s.", error.message);
    goto leave;
  }

  if (mg_parse_and_submit(&reply, db_name, db_parse_infos,
                          STATIC_ARRAY_SIZE(db_parse_infos)) != 0) {
    ERROR("mongodb plugin: mg_parse_and_submit(db) failed.");
    goto leave;
  }

  result = 0;  // Success!

 leave:
  bson_destroy(&reply);
  bson_destroy(request);
  return result;
}

// This code is identical to the method "mongoc_client_get_database_names"
// in the Mongo driver, except that it doesn't filter out the database
// called "local". This allows us to correctly enumerate and gather stats
// from all databases when there is a "local" database (the normal case),
// and when there is not (as in the sharding case).
static char **
mg_get_database_names (mongoc_client_t *client, bson_error_t *error)
{
   bson_iter_t iter;
   const char *name;
   char **ret = NULL;
   int i = 0;
   mongoc_cursor_t *cursor;
   const bson_t *doc;

   BSON_ASSERT (client);

   cursor = mongoc_client_find_databases (client, error);

   while (mongoc_cursor_next (cursor, &doc)) {
      if (bson_iter_init (&iter, doc) &&
          bson_iter_find (&iter, "name") &&
          BSON_ITER_HOLDS_UTF8 (&iter) &&
          (name = bson_iter_utf8 (&iter, NULL))) {
            ret = (char **)bson_realloc (ret, sizeof(char*) * (i + 2));
            ret [i] = bson_strdup (name);
            ret [++i] = NULL;
         }
   }

   if (!ret && !mongoc_cursor_error (cursor, error)) {
      ret = (char **)bson_malloc0 (sizeof (void*));
   }

   mongoc_cursor_destroy (cursor);

   return ret;
}

/**
 * Read the data from the MongoDB server.
 */
static int mg_read(user_data_t *user_data) {
  context_t *ctx = user_data->data;

  mongoc_client_t *client = NULL;
  bson_t server_reply = BSON_INITIALIZER;
  bson_error_t error;
  char **databases = NULL;
  int result = -1;  // Pessimistically assume failure.

  // Make a connection to the database.
  client = mongoc_client_new(ctx->server_uri);
  if (client == NULL) {
    ERROR("mongodb plugin: mongoc_client_new failed.");
    goto leave;
  }

  // Get the server status, parse it, and upload the response.
  if (!mongoc_client_get_server_status(client, NULL, &server_reply,
                                       &error)) {
    ERROR("mongodb plugin: mongoc_client_get_server_status failed: %s.",
          error.message);
    goto leave;
  }

  if (mg_parse_and_submit(&server_reply, NULL, server_parse_infos,
                          STATIC_ARRAY_SIZE(server_parse_infos)) != 0) {
    ERROR("mongodb plugin: mg_parse_and_submit(server) failed.");
    goto leave;
  }

  // Get the list of databases, then process each database.
  databases = mg_get_database_names(client, &error);
  if (databases == NULL) {
    ERROR("mongodb plugin: mongoc_client_get_database_names failed: %s.",
          error.message);
    goto leave;
  }
  int i;
  for (i = 0; databases[i] != NULL; ++i) {
    if (mg_process_database(ctx, client, databases[i]) != 0) {
      // If there's an error, maybe it's only on one of the databases.
      ERROR("mongodb plugin: mg_process_database '%s' failed."
          " Continuing anyway...", databases[i]);
    }
  }
  result = 0;  // Success!

 leave:
  bson_strfreev (databases);
  bson_destroy(&server_reply);
  mongoc_client_destroy(client);
  return result;
}

/*
 * Initialize the mongoc driver.
 */
static int mg_init() {
  mongoc_init();
  return 0;
}

/*
 * Shut down the mongoc driver.
 */
static int mg_shutdown() {
  mongoc_cleanup();
  return 0;
}

static int mg_make_uri(char *buffer, size_t buffer_size,
                       const char *hostname, int port,
                       const char *user, const char *password) {
  char auth[256];
  auth[0] = 0;
  if (user != NULL) {
    int result = snprintf(auth, sizeof(auth), "%s:%s@", user, password);
    if (result < 0 || result >= sizeof(auth)) {
      ERROR("mongodb plugin: no space in buffer for user/password");
      return -1;
    }
  }

  const char *uri_hostname = hostname != NULL ? hostname : "localhost";

  int result = snprintf(buffer, buffer_size, "mongodb://%s%s:%d/admin",
                       auth, uri_hostname, port);
  if (result < 0 || result >= buffer_size) {
    ERROR("mongodb plugin: buffer not big enough to build URI.");
    return -1;
  }
  return 0;
}

/*
 * Read the configuration. If successful, register a read callback.
 */
static int mg_config(oconfig_item_t *ci) {
  char *hostname = NULL;
  int port = MONGOC_DEFAULT_PORT;
  char *user = NULL;
  char *password = NULL;
  _Bool prefer_secondary_query = 0;
  context_t *ctx = NULL;
  int result = -1;  // Pessimistically assume failure.

  int parse_errors = 0;

  int i;
  for (i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *child = &ci->children[i];

    const char *error_template =
        "mongodb plugin: Error parsing \"%s\" in config.";

    if (strcasecmp("Host", child->key) == 0) {
      if (cf_util_get_string(child, &hostname) != 0) {
        ERROR(error_template, "Host");
        ++parse_errors;
        continue;
      }
    } else if (strcasecmp("Port", child->key) == 0) {
      char *portString = NULL;
      if (cf_util_get_string(child, &portString) != 0) {
        ERROR(error_template, "Port");
        ++parse_errors;
        continue;
      }
      port = service_name_to_port_number(portString);
      sfree(portString);
      if (port <= 0) {
        ERROR(error_template, "Port");
        ++parse_errors;
        continue;
      }
    } else if (strcasecmp("User", child->key) == 0) {
      if (cf_util_get_string(child, &user) != 0) {
        ERROR(error_template, "User");
        ++parse_errors;
        continue;
      }
    } else if (strcasecmp("Password", child->key) == 0) {
      if (cf_util_get_string(child, &password) != 0) {
        ERROR(error_template, "Password");
        ++parse_errors;
        continue;
      }
    } else if (strcasecmp("PreferSecondaryQuery", child->key) == 0) {
      if (cf_util_get_boolean(child, &prefer_secondary_query) != 0) {
        ERROR(error_template, "PreferSecondaryQuery");
        ++parse_errors;
        continue;
      }
    } else if (strcasecmp("AllowSecondaryQuery", child->key) == 0) {
      WARNING("mongodb plugin: config option 'AllowSecondaryQuery' is"
          " deprecated. Use 'PreferSecondaryQuery' instead.");
      if (cf_util_get_boolean(child, &prefer_secondary_query) != 0) {
        ERROR(error_template, "AllowSecondaryQuery");
        ++parse_errors;
        continue;
      }
    } else {
      ERROR("mongodb plugin: unrecognized key \"%s\" in config.",
            child->key);
      ++parse_errors;
    }
  }
  if (parse_errors > 0) {
    goto leave;
  }
  if ((user == NULL && password != NULL) || (user != NULL && password == NULL)){
    ERROR("mongodb plugin: User and Password in the config either need to both"
        " be specified or both be unspecified.");
    goto leave;
  }

  char uri[1024];
  if (mg_make_uri(uri, sizeof(uri), hostname, port, user, password) != 0) {
    ERROR("mongodb plugin: mg_make_uri failed");
    goto leave;
  }

  const char *stats_hostname = hostname != NULL ? hostname : hostname_g;
  ctx = context_create(stats_hostname, uri, prefer_secondary_query);
  if (ctx == NULL) {
    ERROR("mongodb plugin: context_create failed.");
    goto leave;
  }

  user_data_t user_data = {
      .data = ctx,
      .free_func = (void(*)(void*))&context_destroy
  };

  if (plugin_register_complex_read(NULL, this_plugin_name,
                                   &mg_read, 0, &user_data) != 0) {
    ERROR("mongodb plugin: plugin_register_complex_read failed.");
    goto leave;
  }

  ctx = NULL;  // Owned by plugin system now.
  result = 0;  // Success!

 leave:
  context_destroy(ctx);
  sfree(password);
  sfree(user);
  sfree(hostname);
  return result;
}

/* Register this module with collectd */
void module_register(void)
{
  if (plugin_register_init(this_plugin_name, &mg_init) != 0) {
    ERROR("mongodb plugin: plugin_register_init failed.");
    return;
  }
  if (plugin_register_complex_config(this_plugin_name, &mg_config) != 0) {
    ERROR("mongodb plugin: plugin_register_complex_config failed.");
    return;
  }
  if (plugin_register_shutdown (this_plugin_name, &mg_shutdown) != 0) {
    ERROR("mongodb plugin: plugin_register_shutdown failed.");
    return;
  }
}
