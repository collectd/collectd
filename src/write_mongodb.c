/**
 * collectd - src/write_mongodb.c
 * Copyright (C) 2010-2013  Florian Forster
 * Copyright (C) 2010       Akkarit Sangpetch
 * Copyright (C) 2012       Chris Lundquist
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
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_cache.h"

#include <pthread.h>

#if HAVE_STDINT_H
# define MONGO_HAVE_STDINT 1
#else
# define MONGO_USE_LONG_LONG_INT 1
#endif
#include <mongo.h>

struct wm_node_s
{
  char name[DATA_MAX_NAME_LEN];

  char *host;
  int port;
  int timeout;

  /* Authentication information */
  char *db;
  char *user;
  char *passwd;

  _Bool store_rates;

  int max_size;
  int max_doc;
  char *db_name;
  char *collection_name;

  _Bool write_data_set;
  _Bool write_notification;

  mongo conn[1];
  pthread_mutex_t lock;
};
typedef struct wm_node_s wm_node_t;

/*
 * Functions
 */
static bson *wm_create_bson_notification (const notification_t *notification) /* {{{ */
{
  bson *ret;
  notification_meta_t * meta = NULL;

  ret = bson_alloc ();
  if (ret == NULL)
  {
    ERROR ("write_mongodb plugin: bson_alloc failed.");
    return (NULL);
  }

  /* Prepare bson doc */
  bson_init (ret);
  /* Add header */
  bson_append_date (ret, "time", (bson_date_t) CDTIME_T_TO_MS (notification->time));
  bson_append_string (ret, "host", notification->host);
  bson_append_string (ret, "plugin", notification->plugin);
  bson_append_string (ret, "plugin_instance", notification->plugin_instance);
  bson_append_string (ret, "type", notification->type);
  bson_append_string (ret, "type_instance", notification->type_instance);
  bson_append_int (ret, "severity", notification->severity);
  bson_append_string (ret, "message", notification->message);

  /* Add meta */
  bson_append_start_object (ret, "meta"); /* {{{ */
  meta = notification->meta;

  while (meta)
  {
    switch (meta->type)
    {
      case NM_TYPE_STRING:
        bson_append_string (ret, meta->name, meta->nm_value.nm_string);
        break;
      case NM_TYPE_SIGNED_INT:
        bson_append_long (ret, meta->name, (int64_t)meta->nm_value.nm_signed_int);
        break;
      case NM_TYPE_UNSIGNED_INT:
        bson_append_long (ret, meta->name, (int64_t)meta->nm_value.nm_signed_int);
        break;
      case NM_TYPE_DOUBLE:
        bson_append_double (ret, meta->name, meta->nm_value.nm_double);
        break;
      case NM_TYPE_BOOLEAN:
        bson_append_bool (ret, meta->name, meta->nm_value.nm_boolean);
        break;
      default:
        WARNING ("write_mongodb plugin: Ignoring unknown notification meta type %s(%i).",
            meta->name, meta->type);
        break;
    }
    meta = meta->next;
  };

  bson_append_finish_object (ret); /* }}} meta */

  bson_finish (ret);

  return (ret);
} /* }}} */

static bson *wm_create_bson_data_set (const data_set_t *ds, /* {{{ */
    const value_list_t *vl,
    _Bool store_rates)
{
  bson *ret;
  gauge_t *rates;
  int i;

  ret = bson_alloc ();
  if (ret == NULL)
  {
    ERROR ("write_mongodb plugin: bson_alloc failed.");
    return (NULL);
  }

  if (store_rates)
  {
    rates = uc_get_rate (ds, vl);
    if (rates == NULL)
    {
      ERROR ("write_mongodb plugin: uc_get_rate() failed.");
      return (NULL);
    }
  }
  else
  {
    rates = NULL;
  }

  bson_init (ret);
  bson_append_date (ret, "time", (bson_date_t) CDTIME_T_TO_MS (vl->time));
  bson_append_string (ret, "host", vl->host);
  bson_append_string (ret, "plugin", vl->plugin);
  bson_append_string (ret, "plugin_instance", vl->plugin_instance);
  bson_append_string (ret, "type", vl->type);
  bson_append_string (ret, "type_instance", vl->type_instance);

  bson_append_start_array (ret, "values"); /* {{{ */
  for (i = 0; i < ds->ds_num; i++)
  {
    char key[16];

    ssnprintf (key, sizeof (key), "%i", i);

    if (ds->ds[i].type == DS_TYPE_GAUGE)
      bson_append_double(ret, key, vl->values[i].gauge);
    else if (store_rates)
      bson_append_double(ret, key, (double) rates[i]);
    else if (ds->ds[i].type == DS_TYPE_COUNTER)
      bson_append_long(ret, key, vl->values[i].counter);
    else if (ds->ds[i].type == DS_TYPE_DERIVE)
      bson_append_long(ret, key, vl->values[i].derive);
    else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
      bson_append_long(ret, key, vl->values[i].absolute);
    else
      assert (23 == 42);
  }
  bson_append_finish_array (ret); /* }}} values */

  bson_append_start_array (ret, "dstypes"); /* {{{ */
  for (i = 0; i < ds->ds_num; i++)
  {
    char key[16];

    ssnprintf (key, sizeof (key), "%i", i);

    if (store_rates)
      bson_append_string (ret, key, "gauge");
    else
      bson_append_string (ret, key, DS_TYPE_TO_STRING (ds->ds[i].type));
  }
  bson_append_finish_array (ret); /* }}} dstypes */

  bson_append_start_array (ret, "dsnames"); /* {{{ */
  for (i = 0; i < ds->ds_num; i++)
  {
    char key[16];

    ssnprintf (key, sizeof (key), "%i", i);
    bson_append_string (ret, key, ds->ds[i].name);
  }
  bson_append_finish_array (ret); /* }}} dsnames */

  bson_finish (ret);

  sfree (rates);
  return (ret);
} /* }}} bson *wm_create_bson */

static void wm_check_collection (wm_node_t *node, /* {{{ */
    const char *coll_name)
{
    /* If max_size is set then create a capped collection */
    if (node->max_size) {
      /* Check if collection exists:
       * db['system.namespaces'].find({'name':'nsc.collectd'})
       * {
       *     "name" : "nsc.collectd",
       *     "options" : {
       *                 "capped" : true,
       *                  "size" : 100096
       *                }
       *      }
       *
       * */
      int status;
      bson query;
      bson response;
      char ns[512];
      char collection_ns[512];

      strcpy (ns, node->db_name);
      strcat (ns, ".system.namespaces");
      strcpy (collection_ns, node->db_name);
      strcat (collection_ns, ".");
      strcat (collection_ns, coll_name);

      bson_init (&query);
      bson_append_start_object (&query, "$query");
      bson_append_string (&query, "name", collection_ns);
      bson_append_finish_object (&query);
      bson_finish ( &query );

      status = mongo_find_one (node->conn, ns, &query, NULL, &response);
      bson_destroy (&query);

      if (status == MONGO_OK) {
        /* Collection already exists */
        bson_iterator i, item;
        bson_bool_t is_capped = 0;

        /* get "options.capped" value */
        if (bson_find (&i, &response, "options")) {
          bson options;
          bson_iterator_subobject_init (&i, &options, 0);
          if(bson_find (&item, &options, "capped")) {
            is_capped = bson_iterator_bool (&item);
          }

          /* cleanup */
          bson_destroy (&options);
        }

        if (!is_capped) {
          // TODO: Need to convert to capped
          ERROR ("write_mongodb plugin: Collection %s exists but is not capped.",
              collection_ns);
        }else{
          DEBUG ("write_mongodb plugin: Collection %s exists and is capped.",
              collection_ns);
        }
      }else{
        // Collection does not exists, create
        status = mongo_create_capped_collection (node->conn, node->db_name, coll_name, node->max_size, node->max_doc, NULL);
        if (status != MONGO_OK)
        {
          ERROR ("write_mongodb plugin: Create capped collection %s.%s(%i/%i) failed %i.", 
              node->db_name, coll_name, node->max_size, node->max_doc, status);
        }else{
          INFO ("write_mongodb plugin: Create capped collection %s.%s(%i/%i) succeed.",
              node->db_name, coll_name, node->max_size, node->max_doc);
        }
      }
      bson_destroy (&response);
    }
} /* }}} void wm_check_collection */

static int wm_write_mongo (wm_node_t *node, const char *plugin, bson *bson_record)
{/*{{{*/
  char collection_name[512];
  const char * coll_name = NULL;
  int status;

  /* build collection name */
  strcpy (collection_name, node->db_name);
  strcat (collection_name, ".");
  if (node->collection_name) {
    strcat (collection_name, node->collection_name);
    coll_name = node->collection_name;
  }else{
    strcat (collection_name, plugin);
    coll_name = plugin;
  }

  pthread_mutex_lock (&node->lock);

  if (!mongo_is_connected (node->conn))
  {
    INFO ("write_mongodb plugin: Connecting to [%s]:%i",
        (node->host != NULL) ? node->host : "localhost",
        (node->port != 0) ? node->port : MONGO_DEFAULT_PORT);
    status = mongo_connect (node->conn, node->host, node->port);
    if (status != MONGO_OK) {
      ERROR ("write_mongodb plugin: Connecting to [%s]:%i failed.",
          (node->host != NULL) ? node->host : "localhost",
          (node->port != 0) ? node->port : MONGO_DEFAULT_PORT);
      mongo_destroy (node->conn);
      pthread_mutex_unlock (&node->lock);
      return (-1);
    }

    if ((node->db != NULL) && (node->user != NULL) && (node->passwd != NULL))
    {
      status = mongo_cmd_authenticate (node->conn,
          node->db, node->user, node->passwd);
      if (status != MONGO_OK)
      {
        ERROR ("write_mongodb plugin: Authenticating to [%s]%i for database "
            "\"%s\" as user \"%s\" failed.",
          (node->host != NULL) ? node->host : "localhost",
          (node->port != 0) ? node->port : MONGO_DEFAULT_PORT,
          node->db, node->user);
        mongo_destroy (node->conn);
        pthread_mutex_unlock (&node->lock);
        return (-1);
      }
    }

    if (node->timeout > 0) {
      status = mongo_set_op_timeout (node->conn, node->timeout);
      if (status != MONGO_OK) {
        WARNING ("write_mongodb plugin: mongo_set_op_timeout(%i) failed: %s",
            node->timeout, node->conn->errstr);
      }
    }
  }

  /* Assert if the connection has been established */
  assert (mongo_is_connected (node->conn));

  /* Check collection */
  wm_check_collection (node, coll_name);

  #if MONGO_MINOR >= 6
    /* There was an API change in 0.6.0 as linked below */
    /* https://github.com/mongodb/mongo-c-driver/blob/master/HISTORY.md */
    status = mongo_insert (node->conn, collection_name, bson_record, NULL);
  #else
    status = mongo_insert (node->conn, collection_name, bson_record);
  #endif

  if (status != MONGO_OK)
  {
    ERROR ( "write_mongodb plugin: error inserting record: %d", node->conn->err);
    if (node->conn->err != MONGO_BSON_INVALID)
      ERROR ("write_mongodb plugin: %s", node->conn->errstr);
    else
      ERROR ("write_mongodb plugin: Invalid BSON structure, error = %#x",
          (unsigned int) bson_record->err);

    /* Disconnect except on data errors. */
    if ((node->conn->err != MONGO_BSON_INVALID)
        && (node->conn->err != MONGO_BSON_NOT_FINISHED))
      mongo_destroy (node->conn);
  }

  pthread_mutex_unlock (&node->lock);

  return (0);

}/*}}}*/

static int wm_write_notification (const notification_t *notification, /*{{{*/
		user_data_t *ud)
{
  wm_node_t *node = ud->data;
  bson *bson_record;
  int status;

  bson_record = wm_create_bson_notification (notification);
  if (bson_record == NULL)
    return (ENOMEM);

  status = wm_write_mongo (node, notification->plugin, bson_record);

  /* free our resource as not to leak memory */
  bson_destroy (bson_record);
  bson_dealloc (bson_record);

  return (status);

} /*}}}*/

static int wm_write_data_set (const data_set_t *ds, /* {{{ */
    const value_list_t *vl,
    user_data_t *ud)
{
  wm_node_t *node = ud->data;
  bson *bson_record;
  int status;

  bson_record = wm_create_bson_data_set (ds, vl, node->store_rates);
  if (bson_record == NULL)
    return (ENOMEM);

  status = wm_write_mongo (node, vl->plugin, bson_record);

  /* free our resource as not to leak memory */
  bson_destroy (bson_record);
  bson_dealloc (bson_record);

  return (status);
} /* }}} int wm_write */

static void wm_config_free (void *ptr) /* {{{ */
{
  wm_node_t *node = ptr;

  if (node == NULL)
    return;

  if (mongo_is_connected (node->conn))
    mongo_destroy (node->conn);

  sfree (node->host);
  sfree (node->db_name);
  sfree (node->collection_name);
  sfree (node);
} /* }}} void wm_config_free */

static int wm_config_node (oconfig_item_t *ci) /* {{{ */
{
  wm_node_t *node;
  int status;
  int i;

  node = malloc (sizeof (*node));
  if (node == NULL)
    return (ENOMEM);
  memset (node, 0, sizeof (*node));
  mongo_init (node->conn);
  node->host = NULL;
  node->store_rates = 1;
  node->write_data_set = 1;
  pthread_mutex_init (&node->lock, /* attr = */ NULL);

  status = cf_util_get_string_buffer (ci, node->name, sizeof (node->name));

  if (status != 0)
  {
    sfree (node);
    return (status);
  }

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Host", child->key) == 0)
      status = cf_util_get_string (child, &node->host);
    else if (strcasecmp ("Port", child->key) == 0)
    {
      status = cf_util_get_port_number (child);
      if (status > 0)
      {
        node->port = status;
        status = 0;
      }
    }
    else if (strcasecmp ("Timeout", child->key) == 0)
      status = cf_util_get_int (child, &node->timeout);
    else if (strcasecmp ("StoreRates", child->key) == 0)
      status = cf_util_get_boolean (child, &node->store_rates);
    else if (strcasecmp ("Database", child->key) == 0)
      status = cf_util_get_string (child, &node->db);
    else if (strcasecmp ("User", child->key) == 0)
      status = cf_util_get_string (child, &node->user);
    else if (strcasecmp ("Password", child->key) == 0)
      status = cf_util_get_string (child, &node->passwd);
    else if (strcasecmp ("DbName", child->key) == 0)
      status = cf_util_get_string (child, &node->db_name);
    else if (strcasecmp ("CollectionName", child->key) == 0)
      status = cf_util_get_string (child, &node->collection_name);
    else if (strcasecmp ("MaxSize", child->key) == 0)
      status = cf_util_get_int (child, &node->max_size);
    else if (strcasecmp ("MaxDoc", child->key) == 0)
      status = cf_util_get_int (child, &node->max_doc);
    else if (strcasecmp ("WriteDataSet", child->key) == 0)
      status = cf_util_get_boolean (child, &node->write_data_set);
    else if (strcasecmp ("WriteNotification", child->key) == 0)
      status = cf_util_get_boolean (child, &node->write_notification);
    else
      WARNING ("write_mongodb plugin: Ignoring unknown config option \"%s\".",
          child->key);

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if ((node->db != NULL) || (node->user != NULL) || (node->passwd != NULL))
  {
    if ((node->db == NULL) || (node->user == NULL) || (node->passwd == NULL))
    {
      WARNING ("write_mongodb plugin: Authentication requires the "
          "\"Database\", \"User\" and \"Password\" options to be specified, "
          "but at last one of them is missing. Authentication will NOT be "
          "used.");
      sfree (node->db);
      sfree (node->user);
      sfree (node->passwd);
    }
  }
  /* Set db_name if not specified */
  if (NULL == node->db_name) {
    node->db_name = malloc (strlen("collectd") + 1);
    strcpy (node->db_name, "collectd");
  }

  if (status == 0)
  {
    char cb_name[DATA_MAX_NAME_LEN];
    user_data_t ud;

    ssnprintf (cb_name, sizeof (cb_name), "write_mongodb/%s", node->name);

    ud.data = node;
    ud.free_func = wm_config_free;

    /* Write data set ? */
    if (status == 0 && node->write_data_set)
    {
      status = plugin_register_write (cb_name, wm_write_data_set, &ud);
      INFO ("write_mongodb plugin: registered write plugin %s %d",cb_name,status);
    }
    /* Write notification ? */
    if (status == 0 && node->write_notification)
    {
      status = plugin_register_notification (cb_name, wm_write_notification, &ud);
      INFO ("write_mongodb plugin: registered notification plugin %s %d",cb_name,status);
    }
  }

  if (status != 0)
    wm_config_free (node);

  return (status);
} /* }}} int wm_config_node */

static int wm_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Node", child->key) == 0)
      wm_config_node (child);
    else
      WARNING ("write_mongodb plugin: Ignoring unknown "
          "configuration option \"%s\" at top level.", child->key);
  }

  return (0);
} /* }}} int wm_config */

void module_register (void)
{
  plugin_register_complex_config ("write_mongodb", wm_config);
}

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
