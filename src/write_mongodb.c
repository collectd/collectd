/**
 * collectd - src/write_mongodb.c
 * Copyright (C) 2010-2013  Florian Forster
 * Copyright (C) 2010       Akkarit Sangpetch
 * Copyright (C) 2012       Chris Lundquist
 * Copyright (C) 2014       Amim Knabben
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
 *   Amim Knabben <amim.knabben at gmail.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_cache.h"

#include <pthread.h>

#include <bson.h>
#include <mongoc.h>

struct wm_node_s
{
  char name[DATA_MAX_NAME_LEN];
  int timeout;

  /* Authentication information */
  char *db;
  char *col;
  char *uri;

  mongoc_client_t *client;

  _Bool store_rates;

  pthread_mutex_t lock;
};
typedef struct wm_node_s wm_node_t;

/*
 * Functions
 */
static bson_t *wm_create_bson (const data_set_t *ds, /* {{{ */
    const value_list_t *vl,
    _Bool store_rates)
{
  int i;
  gauge_t *rates;
  bson_t values, dstypes, dsnames;

  if (store_rates)
  {
    rates = uc_get_rate(ds, vl);
    if (rates == NULL)
    {
      ERROR ("write_mongodb plugin: uc_get_rate() failed.");
      return (NULL);
    }
  } else {
    rates = NULL;
  }

  bson_t *doc = bson_new ();

  BSON_APPEND_DATE_TIME (doc, "time",  CDTIME_T_TO_DOUBLE (vl->time));
  BSON_APPEND_UTF8 (doc, "host", vl->host);
  BSON_APPEND_UTF8 (doc, "plugin", vl->plugin);
  BSON_APPEND_UTF8 (doc, "plugin_instance", vl->plugin_instance);
  BSON_APPEND_UTF8 (doc, "type", vl->type);
  BSON_APPEND_UTF8 (doc, "type_instance", vl->type_instance);

  bson_append_array_begin (doc, "values", -1, &values); /* {{{ */
  for (i = 0; i < ds->ds_num; i++)
  {
    char key[16];
    ssnprintf (key, sizeof (key), "%i", i);

    if (ds->ds[i].type == DS_TYPE_GAUGE)
      bson_append_double(&values, key, -1, vl->values[i].gauge);
    else if (store_rates)
      bson_append_double(&values, key, -1, (double) rates[i]);
    else if (ds->ds[i].type == DS_TYPE_COUNTER)
      bson_append_double(&values, key, -1, vl->values[i].counter);
    else if (ds->ds[i].type == DS_TYPE_DERIVE) 
      bson_append_double(&values, key, -1, vl->values[i].derive);
    else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
      bson_append_double(&values, key, -1, vl->values[i].absolute);
    else
      assert (23 == 42);
  }
  bson_append_array_end (doc, &values); /* }}} values */

  bson_append_array_begin (doc, "dstypes", -1, &dstypes); /* {{{ */
  for (i = 0; i < ds->ds_num; i++)
  {
    char key[16];
    ssnprintf (key, sizeof (key), "%i", i);
    if (store_rates)
      bson_append_utf8 (&dstypes, key, -1, "gauge", -1);
    else 
      bson_append_utf8 (&dstypes, key, -1, DS_TYPE_TO_STRING (ds->ds[i].type), -1);
  }
  bson_append_array_end (doc, &dstypes); /* }}} dstypes */

  bson_append_array_begin (doc, "dsnames", -1, &dsnames); /* {{{ */
  for (i = 0; i < ds->ds_num; i++)
  {
    char key[16];

    ssnprintf (key, sizeof (key), "%i", i);
    bson_append_utf8 (&dsnames, key, -1, ds->ds[i].name, -1);
  }
  bson_append_array_end (doc, &dsnames); /* }}} dsnames */

  sfree (rates);
  return (doc);
} /* }}} bson *wm_create_bson */

static int wm_write (const data_set_t *ds, /* {{{ */
    const value_list_t *vl,
    user_data_t *ud)
{
  wm_node_t *node = ud->data;
  mongoc_collection_t *collection;
  bson_t *bson_record;
  bson_error_t error;

  bson_record = wm_create_bson (ds, vl, node->store_rates);
  if (!bson_record) 
    return (ENOMEM);

  pthread_mutex_lock (&node->lock);

  if (node->client == NULL) 
  {
    INFO ("write_mongodb plugin: Connecting to [%s]", node->uri);
    node->client = mongoc_client_new (node->uri);
    if (!node->client) {
      mongoc_client_destroy(node->client);
      pthread_mutex_unlock (&node->lock);
      return (-1);
    }
  }

  collection = mongoc_client_get_collection (node->client, node->db, node->col);
  if (!mongoc_collection_insert (collection, MONGOC_INSERT_NONE, bson_record, NULL, &error)) {
    ERROR ( "write_mongodb plugin: error inserting record: %s", error.message );
    mongoc_client_destroy(node->client);
    pthread_mutex_unlock (&node->lock);
    return (-1);
  }

  pthread_mutex_unlock (&node->lock);

  /* free our resource as not to leak memory */
  bson_destroy (bson_record);

  return (0);
} /* }}} int wm_write */

static void wm_config_free (void *ptr) /* {{{ */
{
  wm_node_t *node = ptr;

  if (node == NULL)
    return;

  if (!node->client)
    mongoc_client_destroy(node->client);

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
  mongoc_init ();
  node->uri= NULL;
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

    if (strcasecmp ("URI", child->key) == 0)
      status = cf_util_get_string (child, &node->uri);
    else if (strcasecmp ("Timeout", child->key) == 0)
      status = cf_util_get_int (child, &node->timeout);
   else if (strcasecmp ("Database", child->key) == 0)
      status = cf_util_get_string (child, &node->db);
   else if (strcasecmp ("Collection", child->key) == 0)
      status = cf_util_get_string (child, &node->col);
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
