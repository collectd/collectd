/**
 * collectd - src/write_redis.c
 * Copyright (C) 2010  Florian Forster
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
#include "common.h"
#include "configfile.h"

#include <pthread.h>
#include <credis.h>

struct wr_node_s
{
  char name[DATA_MAX_NAME_LEN];

  char *host;
  int port;
  int timeout;

  REDIS conn;
  pthread_mutex_t lock;
};
typedef struct wr_node_s wr_node_t;

/*
 * Functions
 */
static int wr_write (const data_set_t *ds, /* {{{ */
    const value_list_t *vl,
    user_data_t *ud)
{
  wr_node_t *node = ud->data;
  char ident[512];
  char key[512];
  char value[512];
  size_t value_size;
  char *value_ptr;
  int status;
  int i;

  status = FORMAT_VL (ident, sizeof (ident), vl);
  if (status != 0)
    return (status);
  ssnprintf (key, sizeof (key), "collectd/%s", ident);

  memset (value, 0, sizeof (value));
  value_size = sizeof (value);
  value_ptr = &value[0];

#define APPEND(...) do {                                             \
  status = snprintf (value_ptr, value_size, __VA_ARGS__);            \
  if (((size_t) status) > value_size)                                \
  {                                                                  \
    value_ptr += value_size;                                         \
    value_size = 0;                                                  \
  }                                                                  \
  else                                                               \
  {                                                                  \
    value_ptr += status;                                             \
    value_size -= status;                                            \
  }                                                                  \
} while (0)

  APPEND ("%lu", (unsigned long) vl->time);
  for (i = 0; i < ds->ds_num; i++)
  {
    if (ds->ds[i].type == DS_TYPE_COUNTER)
      APPEND ("%llu", vl->values[i].counter);
    else if (ds->ds[i].type == DS_TYPE_GAUGE)
      APPEND ("%g", vl->values[i].gauge);
    else if (ds->ds[i].type == DS_TYPE_DERIVE)
      APPEND ("%"PRIi64, vl->values[i].derive);
    else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
      APPEND ("%"PRIu64, vl->values[i].absolute);
    else
      assert (23 == 42);
  }

#undef APPEND

  pthread_mutex_lock (&node->lock);

  if (node->conn == NULL)
  {
    node->conn = credis_connect (node->host, node->port, node->timeout);
    if (node->conn == NULL)
    {
      ERROR ("write_redis plugin: Connecting to host \"%s\" (port %i) failed.",
          (node->host != NULL) ? node->host : "localhost",
          (node->port != 0) ? node->port : 6379);
      pthread_mutex_unlock (&node->lock);
      return (-1);
    }
  }

  /* "credis_zadd" doesn't handle a NULL pointer gracefully, so I'd rather
   * have a meaningful assertion message than a normal segmentation fault. */
  assert (node->conn != NULL);
  status = credis_zadd (node->conn, key, (double) vl->time, value);

  credis_sadd (node->conn, "collectd/values", ident);

  pthread_mutex_unlock (&node->lock);

  return (0);
} /* }}} int wr_write */

static void wr_config_free (void *ptr) /* {{{ */
{
  wr_node_t *node = ptr;

  if (node == NULL)
    return;

  if (node->conn != NULL)
  {
    credis_close (node->conn);
    node->conn = NULL;
  }

  sfree (node->host);
  sfree (node);
} /* }}} void wr_config_free */

static int wr_config_node (oconfig_item_t *ci) /* {{{ */
{
  wr_node_t *node;
  int status;
  int i;

  node = malloc (sizeof (*node));
  if (node == NULL)
    return (ENOMEM);
  memset (node, 0, sizeof (*node));
  node->host = NULL;
  node->port = 0;
  node->timeout = 1000;
  node->conn = NULL;
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
    else
      WARNING ("write_redis plugin: Ignoring unknown config option \"%s\".",
          child->key);

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status == 0)
  {
    char cb_name[DATA_MAX_NAME_LEN];
    user_data_t ud;

    ssnprintf (cb_name, sizeof (cb_name), "write_redis/%s", node->name);

    ud.data = node;
    ud.free_func = wr_config_free;

    status = plugin_register_write (cb_name, wr_write, &ud);
  }

  if (status != 0)
    wr_config_free (node);

  return (status);
} /* }}} int wr_config_node */

static int wr_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Node", child->key) == 0)
      wr_config_node (child);
    else
      WARNING ("write_redis plugin: Ignoring unknown "
          "configuration option \"%s\" at top level.", child->key);
  }

  return (0);
} /* }}} int wr_config */

void module_register (void)
{
  plugin_register_complex_config ("write_redis", wr_config);
}

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
