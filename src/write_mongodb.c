/**
 * collectd - src/write_mongodb.c
 * Copyright (C) 2010-2013  Florian Forster
 * Copyright (C) 2010       Akkarit Sangpetch
 * Copyright (C) 2012       Chris Lundquist
 * Copyright (C) 2017       Saikrishna Arcot
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
 *   Florian Forster <octo at collectd.org>
 *   Akkarit Sangpetch <asangpet at andrew.cmu.edu>
 *   Chris Lundquist <clundquist at bluebox.net>
 *   Saikrishna Arcot <saiarcot895 at gmail.com>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_cache.h"

#include <mongoc.h>

struct wm_node_s {
  char name[DATA_MAX_NAME_LEN];

  char *host;
  int port;
  int timeout;

  /* Authentication information */
  char *db;
  char *user;
  char *passwd;

  _Bool store_rates;
  _Bool connected;

  mongoc_client_t *client;
  mongoc_database_t *database;
  pthread_mutex_t lock;
};
typedef struct wm_node_s wm_node_t;

/*
 * Functions
 */
static bson_t *wm_create_bson(const data_set_t *ds, /* {{{ */
                              const value_list_t *vl, _Bool store_rates) {
  bson_t *ret;
  bson_t subarray;
  gauge_t *rates;

  ret = bson_new();
  if (!ret) {
    ERROR("write_mongodb plugin: bson_new failed.");
    return NULL;
  }

  if (store_rates) {
    rates = uc_get_rate(ds, vl);
    if (rates == NULL) {
      ERROR("write_mongodb plugin: uc_get_rate() failed.");
      bson_destroy(ret);
      return NULL;
    }
  } else {
    rates = NULL;
  }

  BSON_APPEND_DATE_TIME(ret, "timestamp", CDTIME_T_TO_MS(vl->time));
  BSON_APPEND_UTF8(ret, "host", vl->host);
  BSON_APPEND_UTF8(ret, "plugin", vl->plugin);
  BSON_APPEND_UTF8(ret, "plugin_instance", vl->plugin_instance);
  BSON_APPEND_UTF8(ret, "type", vl->type);
  BSON_APPEND_UTF8(ret, "type_instance", vl->type_instance);

  BSON_APPEND_ARRAY_BEGIN(ret, "values", &subarray); /* {{{ */
  for (size_t i = 0; i < ds->ds_num; i++) {
    char key[16];

    snprintf(key, sizeof(key), "%" PRIsz, i);

    if (ds->ds[i].type == DS_TYPE_GAUGE)
      BSON_APPEND_DOUBLE(&subarray, key, vl->values[i].gauge);
    else if (store_rates)
      BSON_APPEND_DOUBLE(&subarray, key, (double)rates[i]);
    else if (ds->ds[i].type == DS_TYPE_COUNTER)
      BSON_APPEND_INT64(&subarray, key, vl->values[i].counter);
    else if (ds->ds[i].type == DS_TYPE_DERIVE)
      BSON_APPEND_INT64(&subarray, key, vl->values[i].derive);
    else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
      BSON_APPEND_INT64(&subarray, key, vl->values[i].absolute);
    else {
      ERROR("write_mongodb plugin: Unknown ds_type %d for index %" PRIsz,
            ds->ds[i].type, i);
      bson_destroy(ret);
      return NULL;
    }
  }
  bson_append_array_end(ret, &subarray); /* }}} values */

  BSON_APPEND_ARRAY_BEGIN(ret, "dstypes", &subarray); /* {{{ */
  for (size_t i = 0; i < ds->ds_num; i++) {
    char key[16];

    snprintf(key, sizeof(key), "%" PRIsz, i);

    if (store_rates)
      BSON_APPEND_UTF8(&subarray, key, "gauge");
    else
      BSON_APPEND_UTF8(&subarray, key, DS_TYPE_TO_STRING(ds->ds[i].type));
  }
  bson_append_array_end(ret, &subarray); /* }}} dstypes */

  BSON_APPEND_ARRAY_BEGIN(ret, "dsnames", &subarray); /* {{{ */
  for (size_t i = 0; i < ds->ds_num; i++) {
    char key[16];

    snprintf(key, sizeof(key), "%" PRIsz, i);
    BSON_APPEND_UTF8(&subarray, key, ds->ds[i].name);
  }
  bson_append_array_end(ret, &subarray); /* }}} dsnames */

  sfree(rates);

  size_t error_location;
  if (!bson_validate(ret, BSON_VALIDATE_UTF8, &error_location)) {
    ERROR("write_mongodb plugin: Error in generated BSON document "
          "at byte %" PRIsz,
          error_location);
    bson_destroy(ret);
    return NULL;
  }

  return ret;
} /* }}} bson *wm_create_bson */

static int wm_initialize(wm_node_t *node) /* {{{ */
{
  char *uri;

  if (node->connected)
    return 0;

  INFO("write_mongodb plugin: Connecting to [%s]:%d", node->host, node->port);

  if ((node->db != NULL) && (node->user != NULL) && (node->passwd != NULL)) {
    uri = ssnprintf_alloc("mongodb://%s:%s@%s:%d/?authSource=%s", node->user,
                          node->passwd, node->host, node->port, node->db);
    if (uri == NULL) {
      ERROR("write_mongodb plugin: Not enough memory to assemble "
            "authentication string.");
      mongoc_client_destroy(node->client);
      node->client = NULL;
      node->connected = 0;
      return -1;
    }

    node->client = mongoc_client_new(uri);
    if (!node->client) {
      ERROR("write_mongodb plugin: Authenticating to [%s]:%d for database "
            "\"%s\" as user \"%s\" failed.",
            node->host, node->port, node->db, node->user);
      node->connected = 0;
      sfree(uri);
      return -1;
    }
  } else {
    uri = ssnprintf_alloc("mongodb://%s:%d", node->host, node->port);
    if (uri == NULL) {
      ERROR("write_mongodb plugin: Not enough memory to assemble "
            "authentication string.");
      mongoc_client_destroy(node->client);
      node->client = NULL;
      node->connected = 0;
      return -1;
    }

    node->client = mongoc_client_new(uri);
    if (!node->client) {
      ERROR("write_mongodb plugin: Connecting to [%s]:%d failed.", node->host,
            node->port);
      node->connected = 0;
      sfree(uri);
      return -1;
    }
    sfree(uri);
  }

  node->database = mongoc_client_get_database(node->client, "collectd");
  if (!node->database) {
    ERROR("write_mongodb plugin: error creating/getting database");
    mongoc_client_destroy(node->client);
    node->client = NULL;
    node->connected = 0;
    return -1;
  }

  node->connected = 1;
  return 0;
} /* }}} int wm_initialize */

static int wm_write(const data_set_t *ds, /* {{{ */
                    const value_list_t *vl, user_data_t *ud) {
  wm_node_t *node = ud->data;
  mongoc_collection_t *collection = NULL;
  bson_t *bson_record;
  bson_error_t error;
  int status;

  bson_record = wm_create_bson(ds, vl, node->store_rates);
  if (!bson_record) {
    ERROR("write_mongodb plugin: error making insert bson");
    return -1;
  }

  pthread_mutex_lock(&node->lock);
  if (wm_initialize(node) < 0) {
    ERROR("write_mongodb plugin: error making connection to server");
    pthread_mutex_unlock(&node->lock);
    bson_destroy(bson_record);
    return -1;
  }

  collection =
      mongoc_client_get_collection(node->client, "collectd", vl->plugin);
  if (!collection) {
    ERROR("write_mongodb plugin: error creating/getting collection");
    mongoc_database_destroy(node->database);
    mongoc_client_destroy(node->client);
    node->database = NULL;
    node->client = NULL;
    node->connected = 0;
    pthread_mutex_unlock(&node->lock);
    bson_destroy(bson_record);
    return -1;
  }

  status = mongoc_collection_insert(collection, MONGOC_INSERT_NONE, bson_record,
                                    NULL, &error);

  if (!status) {
    ERROR("write_mongodb plugin: error inserting record: %s", error.message);
    mongoc_database_destroy(node->database);
    mongoc_client_destroy(node->client);
    node->database = NULL;
    node->client = NULL;
    node->connected = 0;
    pthread_mutex_unlock(&node->lock);
    bson_destroy(bson_record);
    mongoc_collection_destroy(collection);
    return -1;
  }

  /* free our resource as not to leak memory */
  mongoc_collection_destroy(collection);

  pthread_mutex_unlock(&node->lock);

  bson_destroy(bson_record);

  return 0;
} /* }}} int wm_write */

static void wm_config_free(void *ptr) /* {{{ */
{
  wm_node_t *node = ptr;

  if (node == NULL)
    return;

  mongoc_database_destroy(node->database);
  mongoc_client_destroy(node->client);
  node->database = NULL;
  node->client = NULL;
  node->connected = 0;

  sfree(node->host);
  sfree(node);
} /* }}} void wm_config_free */

static int wm_config_node(oconfig_item_t *ci) /* {{{ */
{
  wm_node_t *node;
  int status;

  node = calloc(1, sizeof(*node));
  if (node == NULL)
    return ENOMEM;
  mongoc_init();
  node->host = strdup("localhost");
  if (node->host == NULL) {
    sfree(node);
    return ENOMEM;
  }
  node->port = MONGOC_DEFAULT_PORT;
  node->store_rates = 1;
  pthread_mutex_init(&node->lock, /* attr = */ NULL);

  status = cf_util_get_string_buffer(ci, node->name, sizeof(node->name));

  if (status != 0) {
    sfree(node->host);
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
    } else if (strcasecmp("Timeout", child->key) == 0)
      status = cf_util_get_int(child, &node->timeout);
    else if (strcasecmp("StoreRates", child->key) == 0)
      status = cf_util_get_boolean(child, &node->store_rates);
    else if (strcasecmp("Database", child->key) == 0)
      status = cf_util_get_string(child, &node->db);
    else if (strcasecmp("User", child->key) == 0)
      status = cf_util_get_string(child, &node->user);
    else if (strcasecmp("Password", child->key) == 0)
      status = cf_util_get_string(child, &node->passwd);
    else
      WARNING("write_mongodb plugin: Ignoring unknown config option \"%s\".",
              child->key);

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if ((node->db != NULL) || (node->user != NULL) || (node->passwd != NULL)) {
    if ((node->db == NULL) || (node->user == NULL) || (node->passwd == NULL)) {
      WARNING(
          "write_mongodb plugin: Authentication requires the "
          "\"Database\", \"User\" and \"Password\" options to be specified, "
          "but at last one of them is missing. Authentication will NOT be "
          "used.");
      sfree(node->db);
      sfree(node->user);
      sfree(node->passwd);
    }
  }

  if (status == 0) {
    char cb_name[sizeof("write_mongodb/") + DATA_MAX_NAME_LEN];

    snprintf(cb_name, sizeof(cb_name), "write_mongodb/%s", node->name);

    status =
        plugin_register_write(cb_name, wm_write,
                              &(user_data_t){
                                  .data = node, .free_func = wm_config_free,
                              });
    INFO("write_mongodb plugin: registered write plugin %s %d", cb_name,
         status);
  }

  if (status != 0)
    wm_config_free(node);

  return status;
} /* }}} int wm_config_node */

static int wm_config(oconfig_item_t *ci) /* {{{ */
{
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Node", child->key) == 0)
      wm_config_node(child);
    else
      WARNING("write_mongodb plugin: Ignoring unknown "
              "configuration option \"%s\" at top level.",
              child->key);
  }

  return 0;
} /* }}} int wm_config */

void module_register(void) {
  plugin_register_complex_config("write_mongodb", wm_config);
}
