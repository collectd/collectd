/*
 * collectd - src/wmi.c
 * Copyright (c) 2018  Google LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "configfile.h"
#include "common.h"
#include "plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <wbemidl.h>
#include <windows.h>

#include "utils_wmi.h"

#define log_err(...) ERROR("wmi: " __VA_ARGS__)
#define log_warn(...) WARNING("wmi: " __VA_ARGS__)

typedef struct wmi_metric_s {
  char *type;
  char *instance;
  char *values_from;
} wmi_metric_t;
LIST_DEF_TYPE(wmi_metric_t);
void wmi_metric_free(wmi_metric_t *m);

typedef struct wmi_query_s {
  char *statement;
  char *instance_prefix;
  char *instances_from;
  LIST_TYPE(wmi_metric_t) *metrics;
} wmi_query_t;
LIST_DEF_TYPE(wmi_query_t);
void wmi_query_free(wmi_query_t *q);

static LIST_TYPE(wmi_query_t) *queries_g;
static wmi_connection_t *wmi;

static wmi_metric_t *config_get_metric(oconfig_item_t *ci) {
  int i;
  char *instance = NULL;
  char *type = NULL;
  char *values_from = NULL;
  wmi_metric_t *metric = NULL;

  assert(strcasecmp("Metric", ci->key) == 0);

  for (i = 0; i < ci->children_num; i++) {
    oconfig_item_t *c = &ci->children[i];
    if (strcasecmp("Instance", c->key) == 0) {
      cf_util_get_string(c, &instance);
    } else if (strcasecmp("Type", c->key) == 0) {
      cf_util_get_string(c, &type);
    } else if (strcasecmp("ValuesFrom", c->key) == 0) {
      cf_util_get_string(c, &values_from);
    } else {
      log_warn("ignoring unknown config key: \"%s\"", c->key);
    }
  }

  if (type == NULL || values_from == NULL) {
    free(instance);
    free(type);
    free(values_from);
    return NULL;
  }

  if (instance == NULL) {
    instance = values_from;
  }

  metric = malloc(sizeof(wmi_metric_t));
  metric->instance = instance;
  metric->type = type;
  metric->values_from = values_from;

  return metric;
}

static wmi_query_t *config_get_query(oconfig_item_t *ci) {
  int i;
  char *statement = NULL;
  char *instance_prefix = NULL;
  char *instances_from = NULL;
  wmi_query_t *query = NULL;
  LIST_TYPE(wmi_metric_t) *metrics = NULL;

  assert(strcasecmp("Query", ci->key) == 0);

  for (i = 0; i < ci->children_num; i++) {
    oconfig_item_t *c = &ci->children[i];
    if (strcasecmp("Metric", c->key) == 0) {
      wmi_metric_t *m = config_get_metric(c);
      if (m)
        LIST_INSERT_FRONT(metrics, m);
    } else if (strcasecmp("Statement", c->key) == 0) {
      cf_util_get_string(c, &statement);
    } else if (strcasecmp("InstancePrefix", c->key) == 0) {
      cf_util_get_string(c, &instance_prefix);
    } else if (strcasecmp("InstancesFrom", c->key) == 0) {
      cf_util_get_string(c, &instances_from);
    } else {
      log_warn("ignoring unknown config key: \"%s\"", c->key);
    }
  }

  if (metrics == NULL || statement == NULL || (instance_prefix == NULL && instances_from == NULL)) {
    LIST_FREE(metrics, wmi_metric_free);
    free(statement);
    free(instance_prefix);
    free(instances_from);
    return NULL;
  }

  query = malloc(sizeof(wmi_query_t));
  query->metrics = metrics;
  query->statement = statement;
  query->instance_prefix = instance_prefix;
  query->instances_from = instances_from;
  return query;
}

void wmi_query_free(wmi_query_t *q) {
  if (!q)
    return;

  free(q->statement);
  free(q->instance_prefix);
  free(q->instances_from);
  LIST_FREE(q->metrics, wmi_metric_free);
  free(q);
}

void wmi_metric_free(wmi_metric_t *m) {
  if (!m)
    return;

  free(m->type);
  free(m->instance);
  free(m->values_from);
  free(m);
}

static void store(VARIANT *src, value_t *dst, int dst_type) {
  switch (dst_type) {
  case DS_TYPE_GAUGE:
    dst->gauge = variant_get_double(src);
    break;

  case DS_TYPE_DERIVE:
    dst->derive = variant_get_int64(src);
    break;

  case DS_TYPE_ABSOLUTE:
    dst->absolute = variant_get_uint64(src);
    break;

  case DS_TYPE_COUNTER:
    dst->counter = variant_get_uint64(src);
    break;

  default:
    log_err("destination type '%d' is not supported", dst_type);
    break;
  }
}

/* Find position of `name` in `ds` */
static int find_index_in_ds(const data_set_t *ds, const char *name) {
  int i;
  for (i = 0; i < ds->ds_num; i++)
    if (strcmp(ds->ds[i].name, name) == 0)
      return i;
  return -1;
}

static int wmi_exec_query(wmi_query_t *q) {
  wmi_result_list_t *results;
  results = wmi_query(wmi, q->statement);

  if (results->count == 0) {
    log_warn("no results for query %s.", q->statement);
    wmi_result_list_release(results);
    return 0;
  }

  wmi_result_t *result;
  while ((result = wmi_get_next_result(results))) {
    value_list_t vl = VALUE_LIST_INIT;

    sstrncpy(vl.host, hostname_g, sizeof(vl.host));
    sstrncpy(vl.plugin, "wmi", sizeof(vl.plugin));

    LIST_TYPE(wmi_metric_t) *mn;
    for (mn = q->metrics; mn != NULL; mn = LIST_NEXT(mn)) {
      VARIANT value_v;
      VARIANT plugin_instance_v;
      value_t value_vt;
      char *plugin_instance_s = NULL;
      const data_set_t *ds = NULL;
      wmi_metric_t *m = LIST_NODE(mn);

      ds = plugin_get_ds(m->type);
      int index_in_ds;
      if (wmi_result_get_value(result, m->values_from, &value_v) != 0) {
        VariantClear(&value_v);
        log_err("failed to read field '%s'", m->values_from);
        continue;
      }

      index_in_ds = find_index_in_ds(ds, "value");
      if (index_in_ds != -1)
        store(&value_v, &value_vt, ds->ds[index_in_ds].type);
      else
        log_warn("cannot find field 'value' in type %s.", ds->type);

      if (q->instances_from == NULL) {
        sstrncpy(vl.plugin_instance, q->instance_prefix, sizeof(vl.plugin_instance));
      } else {
        if (wmi_result_get_value(result, q->instances_from, &plugin_instance_v) != 0) {
          log_err("failed to read field '%s'", q->instances_from);
        }
        plugin_instance_s = variant_get_string(&plugin_instance_v);
        if (plugin_instance_s == NULL) {
          log_err("failed to convert plugin_instance to string");
        }
        sstrncpy(vl.plugin_instance, ssnprintf_alloc("%s%s", q->instance_prefix, plugin_instance_s), sizeof(vl.plugin_instance));
      }

      vl.values_len = 1;
      vl.values = calloc(vl.values_len, sizeof(value_t));
      vl.values[0] = value_vt;
      sstrncpy(vl.type_instance, m->instance, sizeof(vl.type_instance));
      sstrncpy(vl.type, m->type, sizeof(vl.type));
      plugin_dispatch_values(&vl);
      free(plugin_instance_s);
      VariantClear(&value_v);
      VariantClear(&plugin_instance_v);
    }
    wmi_result_release(result);
  }
  wmi_result_list_release(results);

  return 0;
}

static int wmi_configure(oconfig_item_t *ci, LIST_TYPE(wmi_query_t) **queries) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *c = &ci->children[i];
    if (strcasecmp("Query", c->key) == 0) {
      wmi_query_t *q = config_get_query(c);
      if (!q) {
        log_err("cannot read Query %d", i+1);
        return -1;
      }
      LIST_INSERT_FRONT(*queries, q);
    }
  }

  if (queries == NULL) {
    log_warn("no queries have been added");
    return -1;
  }

  return 0;
}

static int wmi_configure_wrapper(oconfig_item_t *ci) {
  return wmi_configure(ci, &queries_g);
}

static int wmi_init(void) {
  wmi = wmi_connect();
  return 0;
}

static int wmi_shutdown(void) {
  LIST_FREE(queries_g, wmi_query_free);
  wmi_release(wmi);
  return 0;
}

static int wmi_read(void) {
  LIST_TYPE(wmi_query_t) *q;
  for (q = queries_g; q != NULL; q = LIST_NEXT(q)) {
    int status = wmi_exec_query(LIST_NODE(q));
    if (status != 0)
      return status;
  }
  return 0;
}

void module_register(void) {
  plugin_register_complex_config("wmi", wmi_configure_wrapper);
  plugin_register_init("wmi", wmi_init);
  plugin_register_read("wmi", wmi_read);
  plugin_register_shutdown("wmi", wmi_shutdown);
}