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

#include "common.h"
#include "configfile.h"
#include "plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <wbemidl.h>
#include <windows.h>

#include "utils_complain.h"
#include "utils_wmi.h"

#define log_err(...) ERROR("wmi: " __VA_ARGS__)
#define log_warn(...) WARNING("wmi: " __VA_ARGS__)

#define LIST_INSERT_FRONT(list, new_node)                                      \
  do {                                                                         \
    __typeof__(list) n = malloc(sizeof(__typeof__(*list)));                    \
    n->next = list;                                                            \
    n->node = new_node;                                                        \
    list = n;                                                                  \
  } while (0)

#define LIST_FREE(list, node_free)                                             \
  do {                                                                         \
    __typeof__(list) head = list;                                              \
    while (head != NULL) {                                                     \
      __typeof__(head) next = head->next;                                      \
      node_free(head->node);                                                   \
      free(head);                                                              \
      head = next;                                                             \
    }                                                                          \
  } while (0)

typedef struct wmi_metric_s {
  char *type;
  char *instance;
  char *values_from;
  int data_source_type;
} wmi_metric_t;

typedef struct wmi_metric_list_s {
  wmi_metric_t *node;
  struct wmi_metric_list_s *next;
} wmi_metric_list_t;

void wmi_metric_free(wmi_metric_t *m);

typedef struct wmi_query_s {
  char *statement;
  char *instance_prefix;
  char *instances_from;
  wmi_metric_list_t *metrics;
} wmi_query_t;

typedef struct wmi_query_list_s {
  wmi_query_t *node;
  struct wmi_query_list_s *next;
} wmi_query_list_t;

void wmi_query_free(wmi_query_t *q);

static wmi_query_list_t *queries_g;
static wmi_connection_t *wmi;

static wmi_metric_t *config_get_metric(oconfig_item_t *ci) {
  int i;
  char *instance = NULL;
  char *type = NULL;
  char *values_from = NULL;
  const data_set_t *ds = NULL;
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

  ds = plugin_get_ds(metric->type);
  if (ds == NULL) {
    log_err("wmi: Failed to look up type \"%s\" for metric. It may "
            "not be defined in the types.db file. Please read the "
            "types.db(5) manual page for more details.",
            metric->type);
    return NULL;
  } else if (ds->ds_num != 1) {
    log_err("wmi: Data set for metric type \"%s\" has %" PRIsz
            " data sources, but the wmi plugin only works for types "
            "with 1 source",
            metric->type, ds->ds_num);
    return NULL;
  }

  metric->data_source_type = ds->ds->type;
  return metric;
}

static wmi_query_t *config_get_query(oconfig_item_t *ci) {
  int i;
  char *statement = NULL;
  char *instance_prefix = NULL;
  char *instances_from = NULL;
  wmi_query_t *query = NULL;
  wmi_metric_list_t *metrics = NULL;

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

  if (metrics == NULL || statement == NULL ||
      (instance_prefix == NULL && instances_from == NULL)) {
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

static char *get_plugin_instance(const char *instances_from,
                                 const char *instance_prefix,
                                 wmi_result_t *wmi_result) {
  VARIANT plugin_instance_base_v;
  char *plugin_instance_base_s = NULL;
  char *plugin_instance_s = NULL;

  if (instances_from == NULL)
    return ssnprintf_alloc(instance_prefix);

  if (wmi_result_get_value(wmi_result, instances_from,
                           &plugin_instance_base_v) != 0) {
    log_err("failed to read field \"%s\"", instances_from);
    return "";
  }
  plugin_instance_base_s = variant_get_string(&plugin_instance_base_v);
  if (plugin_instance_base_s == NULL) {
    log_err("failed to convert plugin_instance to string");
    return "";
  }
  plugin_instance_s =
      ssnprintf_alloc("%s%s", instance_prefix, plugin_instance_s);
  VariantClear(&plugin_instance_base_v);
  free(plugin_instance_base_s);
  return plugin_instance_s;
}

static void submit(const char *type, const char *type_instance,
                   const char *plugin_instance, value_t *values,
                   size_t values_len) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = values_len;

  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "wmi", sizeof(vl.plugin));

  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void gauge_submit(const char *type, const char *type_instance,
                         const char *plugin_instance, gauge_t value) {
  submit(type, type_instance, plugin_instance, &(value_t){.gauge = value}, 1);
}

static void derive_submit(const char *type, const char *type_instance,
                          const char *plugin_instance, derive_t value) {
  submit(type, type_instance, plugin_instance, &(value_t){.derive = value}, 1);
}

static void absolute_submit(const char *type, const char *type_instance,
                            const char *plugin_instance, absolute_t value) {
  submit(type, type_instance, plugin_instance, &(value_t){.absolute = value},
         1);
}

static void counter_submit(const char *type, const char *type_instance,
                           const char *plugin_instance, counter_t value) {
  submit(type, type_instance, plugin_instance, &(value_t){.counter = value}, 1);
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

    wmi_metric_list_t *mn;
    for (mn = q->metrics; mn != NULL; mn = mn->next) {
      VARIANT value_v;
      char *plugin_instance_s = NULL;
      wmi_metric_t *m = mn->node;

      if (wmi_result_get_value(result, m->values_from, &value_v) != 0) {
        VariantClear(&value_v);
        log_err("failed to read field \"%s\"", m->values_from);
        continue;
      }

      plugin_instance_s =
          get_plugin_instance(q->instances_from, q->instance_prefix, result);

      switch (m->data_source_type) {
      case DS_TYPE_ABSOLUTE:
        absolute_submit(m->type, m->instance, plugin_instance_s,
                        variant_get_uint64(&value_v));
        break;
      case DS_TYPE_COUNTER:
        counter_submit(m->type, m->instance, plugin_instance_s,
                       variant_get_uint64(&value_v));
        break;
      case DS_TYPE_GAUGE:
        gauge_submit(m->type, m->instance, plugin_instance_s,
                     variant_get_double(&value_v));
        break;
      case DS_TYPE_DERIVE:
        derive_submit(m->type, m->instance, plugin_instance_s,
                      variant_get_int64(&value_v));
        break;
      }

      free(plugin_instance_s);
      VariantClear(&value_v);
    }
    wmi_result_release(result);
  }
  wmi_result_list_release(results);

  return 0;
}

static int wmi_configure(oconfig_item_t *ci, wmi_query_list_t **queries) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *c = &ci->children[i];
    if (strcasecmp("Query", c->key) == 0) {
      wmi_query_t *q = config_get_query(c);
      if (!q) {
        log_err("cannot read Query %d", i + 1);
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
  wmi_query_list_t *q;
  for (q = queries_g; q != NULL; q = q->next) {
    int status = wmi_exec_query(q->node);
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
