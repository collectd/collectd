/**
 * collectd - src/write_mongodb.c
 * Copyright (C) 2010-2012  Florian Forster
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
 *   Florian Forster <ff at octo.it>
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

  int connected;

  _Bool store_rates;

  mongo conn[1];
  pthread_mutex_t lock;
};
typedef struct wm_node_s wm_node_t;

/*
 * Functions
 */
static int wm_write (const data_set_t *ds, /* {{{ */
    const value_list_t *vl,
    user_data_t *ud)
{
  wm_node_t *node = ud->data;
  char collection_name[512];
  int status;
  int i;
  bson record;

  gauge_t *rates = NULL;
  if (node->store_rates)
  {
    rates = uc_get_rate (ds, vl);
    if (rates == NULL)
    {
      ERROR ("write_mongodb plugin: uc_get_rate() failed.");
      return (-1);
    }
  }

  ssnprintf(collection_name, sizeof (collection_name), "collectd.%s", vl->plugin);

  bson_init(&record);
  bson_append_date (&record, "time", (bson_date_t) CDTIME_T_TO_MS (vl->time));
  bson_append_string (&record, "host", vl->host);
  bson_append_string (&record, "plugin_instance", vl->plugin_instance);
  bson_append_string (&record, "type", vl->type);
  bson_append_string (&record, "type_instance", vl->type_instance);

  for (i = 0; i < ds->ds_num; i++)
  {
    if (ds->ds[i].type == DS_TYPE_GAUGE)
      bson_append_double(&record, ds->ds[i].name, vl->values[i].gauge);
    else if (node->store_rates)
      bson_append_double(&record, ds->ds[i].name, (double) rates[i]);
    else if (ds->ds[i].type == DS_TYPE_COUNTER)
      bson_append_long(&record, ds->ds[i].name, vl->values[i].counter);
    else if (ds->ds[i].type == DS_TYPE_DERIVE)
      bson_append_long(&record, ds->ds[i].name, vl->values[i].derive);
    else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
      bson_append_long(&record, ds->ds[i].name, vl->values[i].absolute);
    else
      assert (23 == 42);
  }
  /* We must finish the record, other wise the insert will fail */
  bson_finish(&record);

  sfree (rates);

  pthread_mutex_lock (&node->lock);

  if (node->connected == 0)
  {
    status = mongo_connect(node->conn, node->host, node->port);
    if (status != MONGO_OK) {
      ERROR ("write_mongodb plugin: Connecting to host \"%s\" (port %i) failed.",
          (node->host != NULL) ? node->host : "localhost",
          (node->port != 0) ? node->port : MONGO_DEFAULT_PORT);
      mongo_destroy(node->conn);
      pthread_mutex_unlock (&node->lock);
      return (-1);
    }

    if (node->timeout > 0) {
      status = mongo_set_op_timeout (node->conn, node->timeout);
      if (status != MONGO_OK) {
        WARNING ("write_mongodb plugin: mongo_set_op_timeout(%i) failed: %s",
            node->timeout, node->conn->errstr);
      }
    }

    node->connected = 1;
  }

  /* Assert if the connection has been established */
  assert (node->connected == 1);

  DEBUG ( "write_mongodb plugin: writing record");
  /* bson_print(&record); */

  status = mongo_insert(node->conn,collection_name,&record);

  if(status != MONGO_OK)
  {
    ERROR ( "write_mongodb plugin: error inserting record: %d", node->conn->err);
    if (node->conn->err == MONGO_BSON_INVALID)
      ERROR ("write_mongodb plugin: %s", node->conn->errstr);
    else if (record.err)
      ERROR ("write_mongodb plugin: %s", record.errstr);
  }

  pthread_mutex_unlock (&node->lock);
  /* free our resource as not to leak memory */
  bson_destroy(&record);

  return (0);
} /* }}} int wm_write */

static void wm_config_free (void *ptr) /* {{{ */
{
  wm_node_t *node = ptr;

  if (node == NULL)
    return;

  if (node->connected != 0)
  {
    mongo_destroy(node->conn);
    node->connected = 0;
  }

  sfree (node->host);
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
  node->host = NULL;
  node->store_rates = 1;
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
    else
      WARNING ("write_mongodb plugin: Ignoring unknown config option \"%s\".",
          child->key);

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status == 0)
  {
    char cb_name[DATA_MAX_NAME_LEN];
    user_data_t ud;

    ssnprintf (cb_name, sizeof (cb_name), "write_mongodb/%s", node->name);

    ud.data = node;
    ud.free_func = wm_config_free;

    status = plugin_register_write (cb_name, wm_write, &ud);
    INFO ("write_mongodb plugin: registered write plugin %s %d",cb_name,status);
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
