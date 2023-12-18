/**
 * collectd - src/daemon/data_set.c
 * Copyright (C) 2005-2023  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 *   Manoj Srivastava <srivasta at google.com>
 **/

#include "daemon/data_set.h"
#include "daemon/plugin.h"
#include "utils/avltree/avltree.h"
#include "utils/common/common.h"

#ifdef WIN32
#define EXPORT __declspec(dllexport)
#include <sys/stat.h>
#include <unistd.h>
#else
#define EXPORT
#endif

static c_avl_tree_t *data_sets;

EXPORT int plugin_register_data_set(const data_set_t *ds) {
  data_set_t *ds_copy;

  if ((data_sets != NULL) && (c_avl_get(data_sets, ds->type, NULL) == 0)) {
    NOTICE("Replacing DS `%s' with another version.", ds->type);
    plugin_unregister_data_set(ds->type);
  } else if (data_sets == NULL) {
    data_sets = c_avl_create((int (*)(const void *, const void *))strcmp);
    if (data_sets == NULL)
      return -1;
  }

  ds_copy = malloc(sizeof(*ds_copy));
  if (ds_copy == NULL)
    return -1;
  memcpy(ds_copy, ds, sizeof(data_set_t));

  ds_copy->ds = malloc(sizeof(*ds_copy->ds) * ds->ds_num);
  if (ds_copy->ds == NULL) {
    sfree(ds_copy);
    return -1;
  }

  for (size_t i = 0; i < ds->ds_num; i++)
    memcpy(ds_copy->ds + i, ds->ds + i, sizeof(data_source_t));

  return c_avl_insert(data_sets, (void *)ds_copy->type, (void *)ds_copy);
} /* int plugin_register_data_set */

EXPORT int plugin_unregister_data_set(const char *name) {
  data_set_t *ds;

  if (data_sets == NULL)
    return -1;

  if (c_avl_remove(data_sets, name, NULL, (void *)&ds) != 0)
    return -1;

  sfree(ds->ds);
  sfree(ds);

  return 0;
} /* int plugin_unregister_data_set */

EXPORT const data_set_t *plugin_get_ds(const char *name) {
  data_set_t *ds;

  if (data_sets == NULL) {
    P_ERROR("plugin_get_ds: No data sets are defined yet.");
    return NULL;
  }

  if (c_avl_get(data_sets, name, (void *)&ds) != 0) {
    DEBUG("No such dataset registered: %s", name);
    return NULL;
  }

  return ds;
} /* data_set_t *plugin_get_ds */

void plugin_free_data_sets(void) {
  void *key;
  void *value;

  if (data_sets == NULL)
    return;

  while (c_avl_pick(data_sets, &key, &value) == 0) {
    data_set_t *ds = value;
    /* key is a pointer to ds->type */

    sfree(ds->ds);
    sfree(ds);
  }

  c_avl_destroy(data_sets);
  data_sets = NULL;
} /* void plugin_free_data_sets */
