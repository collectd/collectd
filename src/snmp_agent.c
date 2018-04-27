/**
 * collectd - src/snmp_agent.c
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Roman Korynkevych <romanx.korynkevych@intel.com>
 *   Serhiy Pshyk <serhiyx.pshyk@intel.com>
 **/

#include "collectd.h"

#include "common.h"
#include "utils_avltree.h"
#include "utils_cache.h"
#include "utils_llist.h"

#include <net-snmp/net-snmp-config.h>

#include <net-snmp/net-snmp-includes.h>

#include <net-snmp/agent/net-snmp-agent-includes.h>

#define PLUGIN_NAME "snmp_agent"
#define ERR_BUF_SIZE 1024
#define TYPE_STRING -1

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

struct oid_s {
  oid oid[MAX_OID_LEN];
  size_t oid_len;
  u_char type;
};
typedef struct oid_s oid_t;

struct table_definition_s {
  char *name;
  oid_t index_oid;
  oid_t size_oid;
  llist_t *columns;
  c_avl_tree_t *instance_index;
  c_avl_tree_t *index_instance;
};
typedef struct table_definition_s table_definition_t;

struct data_definition_s {
  char *name;
  char *plugin;
  char *plugin_instance;
  char *type;
  char *type_instance;
  const table_definition_t *table;
  _Bool is_instance;
  oid_t *oids;
  size_t oids_len;
  double scale;
  double shift;
};
typedef struct data_definition_s data_definition_t;

struct snmp_agent_ctx_s {
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_mutex_t agentx_lock;
  struct tree *tp;

  llist_t *tables;
  llist_t *scalars;
};
typedef struct snmp_agent_ctx_s snmp_agent_ctx_t;

static snmp_agent_ctx_t *g_agent = NULL;

#define CHECK_DD_TYPE(_dd, _p, _pi, _t, _ti)                                   \
  (_dd->plugin ? !strcmp(_dd->plugin, _p) : 0) &&                              \
      (_dd->plugin_instance ? !strcmp(_dd->plugin_instance, _pi) : 1) &&       \
      (_dd->type ? !strcmp(_dd->type, _t) : 0) &&                              \
      (_dd->type_instance ? !strcmp(_dd->type_instance, _ti) : 1)

static int snmp_agent_shutdown(void);
static void *snmp_agent_thread_run(void *arg);
static int snmp_agent_register_oid(oid_t *oid, Netsnmp_Node_Handler *handler);
static int snmp_agent_set_vardata(void *dst_buf, size_t *dst_buf_len,
                                  u_char asn_type, double scale, double shift,
                                  const void *value, size_t len, int type);
static int snmp_agent_unregister_oid_index(oid_t *oid, int index);

static u_char snmp_agent_get_asn_type(oid *oid, size_t oid_len) {
  struct tree *node = get_tree(oid, oid_len, g_agent->tp);

  return (node != NULL) ? mib_to_asn_type(node->type) : 0;
}

static char *snmp_agent_get_oid_name(oid *oid, size_t oid_len) {
  struct tree *node = get_tree(oid, oid_len, g_agent->tp);

  return (node != NULL) ? node->label : NULL;
}

static int snmp_agent_oid_to_string(char *buf, size_t buf_size,
                                    oid_t const *o) {
  char oid_str[MAX_OID_LEN][16];
  char *oid_str_ptr[MAX_OID_LEN];

  for (size_t i = 0; i < o->oid_len; i++) {
    snprintf(oid_str[i], sizeof(oid_str[i]), "%lu", (unsigned long)o->oid[i]);
    oid_str_ptr[i] = oid_str[i];
  }

  return strjoin(buf, buf_size, oid_str_ptr, o->oid_len, ".");
}

static void snmp_agent_dump_data(void) {
#if COLLECT_DEBUG
  char oid_str[DATA_MAX_NAME_LEN];

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    DEBUG(PLUGIN_NAME ": Table:");
    DEBUG(PLUGIN_NAME ":   Name: %s", td->name);
    if (td->index_oid.oid_len != 0) {
      snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &td->index_oid);
      DEBUG(PLUGIN_NAME ":   IndexOID: %s", oid_str);
    }
    if (td->size_oid.oid_len != 0) {
      snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &td->size_oid);
      DEBUG(PLUGIN_NAME ":   SizeOID: %s", oid_str);
    }

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      DEBUG(PLUGIN_NAME ":   Column:");
      DEBUG(PLUGIN_NAME ":     Name: %s", dd->name);
      if (dd->plugin)
        DEBUG(PLUGIN_NAME ":     Plugin: %s", dd->plugin);
      if (dd->plugin_instance)
        DEBUG(PLUGIN_NAME ":     PluginInstance: %s", dd->plugin_instance);
      if (dd->is_instance)
        DEBUG(PLUGIN_NAME ":     Instance: true");
      if (dd->type)
        DEBUG(PLUGIN_NAME ":     Type: %s", dd->type);
      if (dd->type_instance)
        DEBUG(PLUGIN_NAME ":     TypeInstance: %s", dd->type_instance);
      for (size_t i = 0; i < dd->oids_len; i++) {
        snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &dd->oids[i]);
        DEBUG(PLUGIN_NAME ":     OID[%" PRIsz "]: %s", i, oid_str);
      }
      DEBUG(PLUGIN_NAME ":   Scale: %g", dd->scale);
      DEBUG(PLUGIN_NAME ":   Shift: %g", dd->shift);
    }
  }

  for (llentry_t *e = llist_head(g_agent->scalars); e != NULL; e = e->next) {
    data_definition_t *dd = e->value;

    DEBUG(PLUGIN_NAME ": Scalar:");
    DEBUG(PLUGIN_NAME ":   Name: %s", dd->name);
    if (dd->plugin)
      DEBUG(PLUGIN_NAME ":   Plugin: %s", dd->plugin);
    if (dd->plugin_instance)
      DEBUG(PLUGIN_NAME ":   PluginInstance: %s", dd->plugin_instance);
    if (dd->is_instance)
      DEBUG(PLUGIN_NAME ":   Instance: true");
    if (dd->type)
      DEBUG(PLUGIN_NAME ":   Type: %s", dd->type);
    if (dd->type_instance)
      DEBUG(PLUGIN_NAME ":   TypeInstance: %s", dd->type_instance);
    for (size_t i = 0; i < dd->oids_len; i++) {
      snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &dd->oids[i]);
      DEBUG(PLUGIN_NAME ":   OID[%" PRIsz "]: %s", i, oid_str);
    }
    DEBUG(PLUGIN_NAME ":   Scale: %g", dd->scale);
    DEBUG(PLUGIN_NAME ":   Shift: %g", dd->shift);
  }
#endif /* COLLECT_DEBUG */
}

static int snmp_agent_validate_data(void) {

  snmp_agent_dump_data();

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      if (!dd->plugin) {
        ERROR(PLUGIN_NAME ": Plugin not defined for '%s'.'%s'", td->name,
              dd->name);
        return -EINVAL;
      }

      if (dd->plugin_instance) {
        ERROR(PLUGIN_NAME ": PluginInstance should not be defined for table "
                          "data type '%s'.'%s'",
              td->name, dd->name);
        return -EINVAL;
      }

      if (dd->oids_len == 0) {
        ERROR(PLUGIN_NAME ": No OIDs defined for '%s'.'%s'", td->name,
              dd->name);
        return -EINVAL;
      }

      if (dd->is_instance) {

        if (dd->type || dd->type_instance) {
          ERROR(PLUGIN_NAME ": Type and TypeInstance are not valid for "
                            "instance data '%s'.'%s'",
                td->name, dd->name);
          return -EINVAL;
        }

        if (dd->oids_len > 1) {
          ERROR(
              PLUGIN_NAME
              ": Only one OID should be specified for instance data '%s'.'%s'",
              td->name, dd->name);
          return -EINVAL;
        }
      } else {

        if (!dd->type) {
          ERROR(PLUGIN_NAME ": Type not defined for data '%s'.'%s'", td->name,
                dd->name);
          return -EINVAL;
        }
      }
    }
  }

  for (llentry_t *e = llist_head(g_agent->scalars); e != NULL; e = e->next) {
    data_definition_t *dd = e->value;

    if (!dd->plugin) {
      ERROR(PLUGIN_NAME ": Plugin not defined for '%s'", dd->name);
      return -EINVAL;
    }

    if (dd->oids_len == 0) {
      ERROR(PLUGIN_NAME ": No OIDs defined for '%s'", dd->name);
      return -EINVAL;
    }

    if (dd->is_instance) {
      ERROR(PLUGIN_NAME
            ": Instance flag can't be specified for scalar data '%s'",
            dd->name);
      return -EINVAL;
    }

    if (!dd->type) {
      ERROR(PLUGIN_NAME ": Type not defined for data '%s'", dd->name);
      return -EINVAL;
    }
  }

  return 0;
}

static void snmp_agent_generate_oid2string(oid_t *oid, size_t offset,
                                           char *key) {
  int key_len = oid->oid[offset];
  int i;

  for (i = 0; i < key_len && offset < oid->oid_len; i++)
    key[i] = oid->oid[++offset];

  key[i] = '\0';
}

static int snmp_agent_generate_string2oid(oid_t *oid, const char *key) {
  int key_len = strlen(key);

  oid->oid[oid->oid_len++] = key_len;
  for (int i = 0; i < key_len; i++) {
    oid->oid[oid->oid_len++] = key[i];
    if (oid->oid_len >= MAX_OID_LEN) {
      ERROR(PLUGIN_NAME ": Conversion key string %s to OID failed", key);
      return -EINVAL;
    }
  }

  return 0;
}

static int snmp_agent_register_oid_string(oid_t *oid, const char *key,
                                          Netsnmp_Node_Handler *handler) {
  oid_t new_oid;

  memcpy(&new_oid, oid, sizeof(*oid));
  int ret = snmp_agent_generate_string2oid(&new_oid, key);
  if (ret != 0)
    return ret;

  return snmp_agent_register_oid(&new_oid, handler);
}

static int snmp_agent_unregister_oid_string(oid_t *oid, const char *key) {
  oid_t new_oid;

  memcpy(&new_oid, oid, sizeof(*oid));
  int ret = snmp_agent_generate_string2oid(&new_oid, key);
  if (ret != 0)
    return ret;

  return unregister_mib(new_oid.oid, new_oid.oid_len);
}

static int snmp_agent_table_row_remove(table_definition_t *td,
                                       const char *instance) {
  int *index = NULL;
  char *ins = NULL;

  if (td->index_oid.oid_len) {
    if ((c_avl_get(td->instance_index, instance, (void **)&index) != 0) ||
        (c_avl_get(td->index_instance, index, (void **)&ins) != 0))
      return 0;
  } else {
    if (c_avl_get(td->instance_index, instance, (void **)&ins) != 0)
      return 0;
  }

  pthread_mutex_lock(&g_agent->agentx_lock);

  if (td->index_oid.oid_len)
    snmp_agent_unregister_oid_index(&td->index_oid, *index);

  for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
    data_definition_t *dd = de->value;

    for (size_t i = 0; i < dd->oids_len; i++)
      if (td->index_oid.oid_len)
        snmp_agent_unregister_oid_index(&dd->oids[i], *index);
      else
        snmp_agent_unregister_oid_string(&dd->oids[i], ins);
  }

  pthread_mutex_unlock(&g_agent->agentx_lock);

  DEBUG(PLUGIN_NAME ": Removed row for '%s' table [%d, %s]", td->name,
        (index != NULL) ? *index : -1, ins);

  notification_t n = {
      .severity = NOTIF_WARNING, .time = cdtime(), .plugin = PLUGIN_NAME};
  sstrncpy(n.host, hostname_g, sizeof(n.host));
  sstrncpy(n.plugin_instance, ins, sizeof(n.plugin_instance));
  snprintf(n.message, sizeof(n.message),
           "Removed data row from table %s instance %s index %d", td->name, ins,
           (index != NULL) ? *index : -1);
  plugin_dispatch_notification(&n);

  if (td->index_oid.oid_len) {
    c_avl_remove(td->index_instance, index, NULL, (void **)&ins);
    c_avl_remove(td->instance_index, instance, NULL, (void **)&index);
    sfree(index);
    sfree(ins);
  } else {
    c_avl_remove(td->instance_index, instance, NULL, (void **)&ins);
    sfree(ins);
  }

  return 0;
}

static int snmp_agent_clear_missing(const value_list_t *vl,
                                    __attribute__((unused)) user_data_t *ud) {
  if (vl == NULL)
    return -EINVAL;

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      if (!dd->is_instance) {
        if (CHECK_DD_TYPE(dd, vl->plugin, vl->plugin_instance, vl->type,
                          vl->type_instance)) {
          snmp_agent_table_row_remove(td, vl->plugin_instance);
          return 0;
        }
      }
    }
  }

  return 0;
}

static void snmp_agent_free_data(data_definition_t **dd) {

  if (dd == NULL || *dd == NULL)
    return;

  /* unregister scalar type OID */
  if ((*dd)->table == NULL) {
    for (size_t i = 0; i < (*dd)->oids_len; i++)
      unregister_mib((*dd)->oids[i].oid, (*dd)->oids[i].oid_len);
  }

  sfree((*dd)->name);
  sfree((*dd)->plugin);
  sfree((*dd)->plugin_instance);
  sfree((*dd)->type);
  sfree((*dd)->type_instance);
  sfree((*dd)->oids);

  sfree(*dd);

  return;
}

static void snmp_agent_free_table_columns(table_definition_t *td) {
  if (td->columns == NULL)
    return;

  for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
    data_definition_t *dd = de->value;

    if (td->index_oid.oid_len) {
      int *index;
      char *instance;

      c_avl_iterator_t *iter = c_avl_get_iterator(td->index_instance);
      while (c_avl_iterator_next(iter, (void *)&index, (void *)&instance) ==
             0) {
        for (size_t i = 0; i < dd->oids_len; i++)
          snmp_agent_unregister_oid_index(&dd->oids[i], *index);
      }
      c_avl_iterator_destroy(iter);
    } else {
      char *instance;

      c_avl_iterator_t *iter = c_avl_get_iterator(dd->table->instance_index);
      while (c_avl_iterator_next(iter, (void *)&instance, (void *)&instance) ==
             0) {
        for (size_t i = 0; i < dd->oids_len; i++)
          snmp_agent_unregister_oid_string(&dd->oids[i], instance);
      }
      c_avl_iterator_destroy(iter);
    }

    snmp_agent_free_data(&dd);
  }

  llist_destroy(td->columns);
  td->columns = NULL;
} /* void snmp_agent_free_table_columns */

static void snmp_agent_free_table(table_definition_t **td) {

  if (td == NULL || *td == NULL)
    return;

  if ((*td)->size_oid.oid_len)
    unregister_mib((*td)->size_oid.oid, (*td)->size_oid.oid_len);

  /* Unregister Index OIDs */
  if ((*td)->index_oid.oid_len) {
    int *index;
    char *instance;

    c_avl_iterator_t *iter = c_avl_get_iterator((*td)->index_instance);
    while (c_avl_iterator_next(iter, (void *)&index, (void *)&instance) == 0)
      snmp_agent_unregister_oid_index(&(*td)->index_oid, *index);

    c_avl_iterator_destroy(iter);
  }

  /* Unregister all table columns and their registered OIDs */
  snmp_agent_free_table_columns(*td);

  void *key = NULL;
  void *value = NULL;

  /* index_instance and instance_index contain the same pointers */
  c_avl_destroy((*td)->index_instance);
  (*td)->index_instance = NULL;

  if ((*td)->instance_index != NULL) {
    while (c_avl_pick((*td)->instance_index, &key, &value) == 0) {
      if (key != value)
        sfree(key);
      sfree(value);
    }
    c_avl_destroy((*td)->instance_index);
    (*td)->instance_index = NULL;
  }

  sfree((*td)->name);
  sfree(*td);

  return;
}

static int snmp_agent_form_reply(struct netsnmp_request_info_s *requests,
                                 data_definition_t *dd, char *instance,
                                 int oid_index) {
  char name[DATA_MAX_NAME_LEN];
  format_name(name, sizeof(name), hostname_g, dd->plugin,
              instance ? instance : dd->plugin_instance, dd->type,
              dd->type_instance);
  DEBUG(PLUGIN_NAME ": Identifier '%s'", name);

  value_t *values;
  size_t values_num;
  const data_set_t *ds = plugin_get_ds(dd->type);
  if (ds == NULL) {
    ERROR(PLUGIN_NAME ": Data set not found for '%s' type", dd->type);
    return SNMP_NOSUCHINSTANCE;
  }

  int ret = uc_get_value_by_name(name, &values, &values_num);

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to get value for '%s'", name);
    return SNMP_NOSUCHINSTANCE;
  }

  assert(ds->ds_num == values_num);
  assert(oid_index < (int)values_num);

  char data[DATA_MAX_NAME_LEN];
  size_t data_len = sizeof(data);
  ret = snmp_agent_set_vardata(
      data, &data_len, dd->oids[oid_index].type, dd->scale, dd->shift,
      &values[oid_index], sizeof(values[oid_index]), ds->ds[oid_index].type);

  sfree(values);

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to convert '%s' value to snmp data", name);
    return SNMP_NOSUCHINSTANCE;
  }

  requests->requestvb->type = dd->oids[oid_index].type;
  snmp_set_var_typed_value(requests->requestvb, requests->requestvb->type,
                           (const u_char *)data, data_len);

  return SNMP_ERR_NOERROR;
}

static int
snmp_agent_table_oid_handler(struct netsnmp_mib_handler_s *handler,
                             struct netsnmp_handler_registration_s *reginfo,
                             struct netsnmp_agent_request_info_s *reqinfo,
                             struct netsnmp_request_info_s *requests) {

  if (reqinfo->mode != MODE_GET && reqinfo->mode != MODE_GETNEXT) {
    DEBUG(PLUGIN_NAME ": Not supported request mode (%d)", reqinfo->mode);
    return SNMP_ERR_NOERROR;
  }

  pthread_mutex_lock(&g_agent->lock);

  oid_t oid;
  memcpy(oid.oid, requests->requestvb->name,
         sizeof(oid.oid[0]) * requests->requestvb->name_length);
  oid.oid_len = requests->requestvb->name_length;

#if COLLECT_DEBUG
  char oid_str[DATA_MAX_NAME_LEN];
  snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &oid);
  DEBUG(PLUGIN_NAME ": Get request received for table OID '%s'", oid_str);
#endif

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      for (size_t i = 0; i < dd->oids_len; i++) {
        int ret = snmp_oid_ncompare(oid.oid, oid.oid_len, dd->oids[i].oid,
                                    dd->oids[i].oid_len,
                                    MIN(oid.oid_len, dd->oids[i].oid_len));
        if (ret != 0)
          continue;

        char *instance;

        if (!td->index_oid.oid_len) {
          char key[MAX_OID_LEN];

          memset(key, 0, sizeof(key));
          snmp_agent_generate_oid2string(
              &oid, MIN(oid.oid_len, dd->oids[i].oid_len), key);

          ret = c_avl_get(td->instance_index, key, (void **)&instance);
          if (ret != 0) {
            DEBUG(PLUGIN_NAME ": Nonexisting index string '%s' requested", key);
            pthread_mutex_unlock(&g_agent->lock);
            return SNMP_NOSUCHINSTANCE;
          }
        } else {
          int index = oid.oid[oid.oid_len - 1];

          ret = c_avl_get(td->index_instance, &index, (void **)&instance);
          if (ret != 0) {
            DEBUG(PLUGIN_NAME ": Nonexisting index '%d' requested", index);
            pthread_mutex_unlock(&g_agent->lock);
            return SNMP_NOSUCHINSTANCE;
          }
        }

        if (dd->is_instance) {
          requests->requestvb->type = ASN_OCTET_STR;
          snmp_set_var_typed_value(
              requests->requestvb, requests->requestvb->type,
              (const u_char *)instance, strlen((instance)));

          pthread_mutex_unlock(&g_agent->lock);

          return SNMP_ERR_NOERROR;
        }

        ret = snmp_agent_form_reply(requests, dd, instance, i);

        pthread_mutex_unlock(&g_agent->lock);

        return ret;
      }
    }
  }

  pthread_mutex_unlock(&g_agent->lock);

  return SNMP_NOSUCHINSTANCE;
}

static int snmp_agent_table_index_oid_handler(
    struct netsnmp_mib_handler_s *handler,
    struct netsnmp_handler_registration_s *reginfo,
    struct netsnmp_agent_request_info_s *reqinfo,
    struct netsnmp_request_info_s *requests) {

  if (reqinfo->mode != MODE_GET && reqinfo->mode != MODE_GETNEXT) {
    DEBUG(PLUGIN_NAME ": Not supported request mode (%d)", reqinfo->mode);
    return SNMP_ERR_NOERROR;
  }

  pthread_mutex_lock(&g_agent->lock);

  oid_t oid;
  memcpy(oid.oid, requests->requestvb->name,
         sizeof(oid.oid[0]) * requests->requestvb->name_length);
  oid.oid_len = requests->requestvb->name_length;

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    if (td->index_oid.oid_len &&
        (snmp_oid_ncompare(oid.oid, oid.oid_len, td->index_oid.oid,
                           td->index_oid.oid_len,
                           MIN(oid.oid_len, td->index_oid.oid_len)) == 0)) {

      DEBUG(PLUGIN_NAME ": Handle '%s' table index OID", td->name);

      int index = oid.oid[oid.oid_len - 1];

      int ret = c_avl_get(td->index_instance, &index, &(void *){NULL});
      if (ret != 0) {
        /* nonexisting index requested */
        pthread_mutex_unlock(&g_agent->lock);
        return SNMP_NOSUCHINSTANCE;
      }

      requests->requestvb->type = ASN_INTEGER;
      snmp_set_var_typed_value(requests->requestvb, requests->requestvb->type,
                               (const u_char *)&index, sizeof(index));

      pthread_mutex_unlock(&g_agent->lock);

      return SNMP_ERR_NOERROR;
    }
  }

  pthread_mutex_unlock(&g_agent->lock);

  return SNMP_NOSUCHINSTANCE;
}

static int snmp_agent_table_size_oid_handler(
    struct netsnmp_mib_handler_s *handler,
    struct netsnmp_handler_registration_s *reginfo,
    struct netsnmp_agent_request_info_s *reqinfo,
    struct netsnmp_request_info_s *requests) {

  if (reqinfo->mode != MODE_GET && reqinfo->mode != MODE_GETNEXT) {
    DEBUG(PLUGIN_NAME ": Not supported request mode (%d)", reqinfo->mode);
    return SNMP_ERR_NOERROR;
  }

  pthread_mutex_lock(&g_agent->lock);

  oid_t oid;
  memcpy(oid.oid, requests->requestvb->name,
         sizeof(oid.oid[0]) * requests->requestvb->name_length);
  oid.oid_len = requests->requestvb->name_length;

  DEBUG(PLUGIN_NAME ": Get request received for table size OID");

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    if (td->size_oid.oid_len &&
        (snmp_oid_ncompare(oid.oid, oid.oid_len, td->size_oid.oid,
                           td->size_oid.oid_len,
                           MIN(oid.oid_len, td->size_oid.oid_len)) == 0)) {
      DEBUG(PLUGIN_NAME ": Handle '%s' table size OID", td->name);

      long size = c_avl_size(td->index_instance);

      requests->requestvb->type = td->size_oid.type;
      snmp_set_var_typed_value(requests->requestvb, requests->requestvb->type,
                               (const u_char *)&size, sizeof(size));

      pthread_mutex_unlock(&g_agent->lock);

      return SNMP_ERR_NOERROR;
    }
  }

  pthread_mutex_unlock(&g_agent->lock);

  return SNMP_NOSUCHINSTANCE;
}

static int
snmp_agent_scalar_oid_handler(struct netsnmp_mib_handler_s *handler,
                              struct netsnmp_handler_registration_s *reginfo,
                              struct netsnmp_agent_request_info_s *reqinfo,
                              struct netsnmp_request_info_s *requests) {

  if (reqinfo->mode != MODE_GET && reqinfo->mode != MODE_GETNEXT) {
    DEBUG(PLUGIN_NAME ": Not supported request mode (%d)", reqinfo->mode);
    return SNMP_ERR_NOERROR;
  }

  pthread_mutex_lock(&g_agent->lock);

  oid_t oid;
  memcpy(oid.oid, requests->requestvb->name,
         sizeof(oid.oid[0]) * requests->requestvb->name_length);
  oid.oid_len = requests->requestvb->name_length;

#if COLLECT_DEBUG
  char oid_str[DATA_MAX_NAME_LEN];
  snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &oid);
  DEBUG(PLUGIN_NAME ": Get request received for scalar OID '%s'", oid_str);
#endif

  for (llentry_t *de = llist_head(g_agent->scalars); de != NULL;
       de = de->next) {
    data_definition_t *dd = de->value;

    for (size_t i = 0; i < dd->oids_len; i++) {

      int ret = snmp_oid_compare(oid.oid, oid.oid_len, dd->oids[i].oid,
                                 dd->oids[i].oid_len);
      if (ret != 0)
        continue;

      ret = snmp_agent_form_reply(requests, dd, NULL, i);

      pthread_mutex_unlock(&g_agent->lock);

      return ret;
    }
  }

  pthread_mutex_unlock(&g_agent->lock);

  return SNMP_NOSUCHINSTANCE;
}

static int snmp_agent_register_table_oids(void) {

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    if (td->size_oid.oid_len != 0) {
      td->size_oid.type =
          snmp_agent_get_asn_type(td->size_oid.oid, td->size_oid.oid_len);
      td->size_oid.oid_len++;
      int ret = snmp_agent_register_oid(&td->size_oid,
                                        snmp_agent_table_size_oid_handler);
      if (ret != 0)
        return ret;
    }

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      for (size_t i = 0; i < dd->oids_len; i++) {
        dd->oids[i].type =
            snmp_agent_get_asn_type(dd->oids[i].oid, dd->oids[i].oid_len);
      }
    }
  }

  return 0;
}

static int snmp_agent_register_scalar_oids(void) {

  for (llentry_t *e = llist_head(g_agent->scalars); e != NULL; e = e->next) {
    data_definition_t *dd = e->value;

    for (size_t i = 0; i < dd->oids_len; i++) {

      dd->oids[i].type =
          snmp_agent_get_asn_type(dd->oids[i].oid, dd->oids[i].oid_len);

      int ret =
          snmp_agent_register_oid(&dd->oids[i], snmp_agent_scalar_oid_handler);
      if (ret != 0)
        return ret;
    }
  }

  return 0;
}

static int snmp_agent_config_data_oids(data_definition_t *dd,
                                       oconfig_item_t *ci) {
  if (ci->values_num < 1) {
    WARNING(PLUGIN_NAME ": `OIDs' needs at least one argument");
    return -EINVAL;
  }

  for (int i = 0; i < ci->values_num; i++)
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING(PLUGIN_NAME ": `OIDs' needs only string argument");
      return -EINVAL;
    }

  if (dd->oids != NULL)
    sfree(dd->oids);
  dd->oids_len = 0;
  dd->oids = calloc(ci->values_num, sizeof(*dd->oids));
  if (dd->oids == NULL)
    return -ENOMEM;
  dd->oids_len = (size_t)ci->values_num;

  for (int i = 0; i < ci->values_num; i++) {
    dd->oids[i].oid_len = MAX_OID_LEN;

    if (NULL == snmp_parse_oid(ci->values[i].value.string, dd->oids[i].oid,
                               &dd->oids[i].oid_len)) {
      ERROR(PLUGIN_NAME ": snmp_parse_oid (%s) failed",
            ci->values[i].value.string);
      sfree(dd->oids);
      dd->oids_len = 0;
      return -1;
    }
  }

  return 0;
}

static int snmp_agent_config_table_size_oid(table_definition_t *td,
                                            oconfig_item_t *ci) {
  if (ci->values_num < 1) {
    WARNING(PLUGIN_NAME ": `TableSizeOID' is empty");
    return -EINVAL;
  }

  if (ci->values[0].type != OCONFIG_TYPE_STRING) {
    WARNING(PLUGIN_NAME ": `TableSizeOID' needs only string argument");
    return -EINVAL;
  }

  td->size_oid.oid_len = MAX_OID_LEN;

  if (NULL == snmp_parse_oid(ci->values[0].value.string, td->size_oid.oid,
                             &td->size_oid.oid_len)) {
    ERROR(PLUGIN_NAME ": Failed to parse table size OID (%s)",
          ci->values[0].value.string);
    td->size_oid.oid_len = 0;
    return -EINVAL;
  }

  return 0;
}

static int snmp_agent_config_table_index_oid(table_definition_t *td,
                                             oconfig_item_t *ci) {

  if (ci->values_num < 1) {
    WARNING(PLUGIN_NAME ": `IndexOID' is empty");
    return -EINVAL;
  }

  if (ci->values[0].type != OCONFIG_TYPE_STRING) {
    WARNING(PLUGIN_NAME ": `IndexOID' needs only string argument");
    return -EINVAL;
  }

  td->index_oid.oid_len = MAX_OID_LEN;

  if (NULL == snmp_parse_oid(ci->values[0].value.string, td->index_oid.oid,
                             &td->index_oid.oid_len)) {
    ERROR(PLUGIN_NAME ": Failed to parse table index OID (%s)",
          ci->values[0].value.string);
    td->index_oid.oid_len = 0;
    return -EINVAL;
  }

  return 0;
}

static int snmp_agent_config_table_data(table_definition_t *td,
                                        oconfig_item_t *ci) {
  data_definition_t *dd;
  int ret = 0;

  assert(ci != NULL);

  dd = calloc(1, sizeof(*dd));
  if (dd == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for table data definition");
    return -ENOMEM;
  }

  ret = cf_util_get_string(ci, &dd->name);
  if (ret != 0) {
    sfree(dd);
    return -1;
  }

  dd->scale = 1.0;
  dd->shift = 0.0;

  dd->table = td;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Instance", option->key) == 0)
      ret = cf_util_get_boolean(option, &dd->is_instance);
    else if (strcasecmp("Plugin", option->key) == 0)
      ret = cf_util_get_string(option, &dd->plugin);
    else if (strcasecmp("PluginInstance", option->key) == 0)
      ret = cf_util_get_string(option, &dd->plugin_instance);
    else if (strcasecmp("Type", option->key) == 0)
      ret = cf_util_get_string(option, &dd->type);
    else if (strcasecmp("TypeInstance", option->key) == 0)
      ret = cf_util_get_string(option, &dd->type_instance);
    else if (strcasecmp("Shift", option->key) == 0)
      ret = cf_util_get_double(option, &dd->shift);
    else if (strcasecmp("Scale", option->key) == 0)
      ret = cf_util_get_double(option, &dd->scale);
    else if (strcasecmp("OIDs", option->key) == 0)
      ret = snmp_agent_config_data_oids(dd, option);
    else {
      WARNING(PLUGIN_NAME ": Option `%s' not allowed here", option->key);
      ret = -1;
    }

    if (ret != 0) {
      snmp_agent_free_data(&dd);
      return -1;
    }
  }

  llentry_t *entry = llentry_create(dd->name, dd);
  if (entry == NULL) {
    snmp_agent_free_data(&dd);
    return -ENOMEM;
  }

  llist_append(td->columns, entry);

  return 0;
}

static int snmp_agent_config_data(oconfig_item_t *ci) {
  data_definition_t *dd;
  int ret = 0;

  assert(ci != NULL);

  dd = calloc(1, sizeof(*dd));
  if (dd == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for data definition");
    return -ENOMEM;
  }

  ret = cf_util_get_string(ci, &dd->name);
  if (ret != 0) {
    free(dd);
    return -1;
  }

  dd->scale = 1.0;
  dd->shift = 0.0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Instance", option->key) == 0)
      ret = cf_util_get_boolean(option, &dd->is_instance);
    else if (strcasecmp("Plugin", option->key) == 0)
      ret = cf_util_get_string(option, &dd->plugin);
    else if (strcasecmp("PluginInstance", option->key) == 0)
      ret = cf_util_get_string(option, &dd->plugin_instance);
    else if (strcasecmp("Type", option->key) == 0)
      ret = cf_util_get_string(option, &dd->type);
    else if (strcasecmp("TypeInstance", option->key) == 0)
      ret = cf_util_get_string(option, &dd->type_instance);
    else if (strcasecmp("Shift", option->key) == 0)
      ret = cf_util_get_double(option, &dd->shift);
    else if (strcasecmp("Scale", option->key) == 0)
      ret = cf_util_get_double(option, &dd->scale);
    else if (strcasecmp("OIDs", option->key) == 0)
      ret = snmp_agent_config_data_oids(dd, option);
    else {
      WARNING(PLUGIN_NAME ": Option `%s' not allowed here", option->key);
      ret = -1;
    }

    if (ret != 0) {
      snmp_agent_free_data(&dd);
      return -1;
    }
  }

  llentry_t *entry = llentry_create(dd->name, dd);
  if (entry == NULL) {
    snmp_agent_free_data(&dd);
    return -ENOMEM;
  }

  llist_append(g_agent->scalars, entry);

  return 0;
}

static int num_compare(const int *a, const int *b) {
  assert((a != NULL) && (b != NULL));
  if (*a < *b)
    return -1;
  else if (*a > *b)
    return 1;
  else
    return 0;
}

static int snmp_agent_config_table(oconfig_item_t *ci) {
  table_definition_t *td;
  int ret = 0;

  assert(ci != NULL);

  td = calloc(1, sizeof(*td));
  if (td == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for table definition");
    return -ENOMEM;
  }

  ret = cf_util_get_string(ci, &td->name);
  if (ret != 0) {
    sfree(td);
    return -1;
  }

  td->columns = llist_create();
  if (td->columns == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for columns list");
    snmp_agent_free_table(&td);
    return -ENOMEM;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("IndexOID", option->key) == 0)
      ret = snmp_agent_config_table_index_oid(td, option);
    else if (strcasecmp("SizeOID", option->key) == 0)
      ret = snmp_agent_config_table_size_oid(td, option);
    else if (strcasecmp("Data", option->key) == 0)
      ret = snmp_agent_config_table_data(td, option);
    else {
      WARNING(PLUGIN_NAME ": Option `%s' not allowed here", option->key);
      ret = -1;
    }

    if (ret != 0) {
      snmp_agent_free_table(&td);
      return -ENOMEM;
    }
  }

  td->instance_index =
      c_avl_create((int (*)(const void *, const void *))strcmp);
  if (td->instance_index == NULL) {
    snmp_agent_free_table(&td);
    return -ENOMEM;
  }

  td->index_instance =
      c_avl_create((int (*)(const void *, const void *))num_compare);
  if (td->index_instance == NULL) {
    snmp_agent_free_table(&td);
    return -ENOMEM;
  }

  llentry_t *entry = llentry_create(td->name, td);
  if (entry == NULL) {
    snmp_agent_free_table(&td);
    return -ENOMEM;
  }
  llist_append(g_agent->tables, entry);

  return 0;
}

static int snmp_agent_get_value_from_ds_type(const value_t *val, int type,
                                             double scale, double shift,
                                             long *value) {
  switch (type) {
  case DS_TYPE_COUNTER:
    *value = (long)((val->counter * scale) + shift);
    break;
  case DS_TYPE_ABSOLUTE:
    *value = (long)((val->absolute * scale) + shift);
    break;
  case DS_TYPE_DERIVE:
    *value = (long)((val->derive * scale) + shift);
    break;
  case DS_TYPE_GAUGE:
    *value = (long)((val->gauge * scale) + shift);
    break;
  case TYPE_STRING:
    break;
  default:
    ERROR(PLUGIN_NAME ": Unknown data source type: %i", type);
    return -EINVAL;
  }

  return 0;
}

static int snmp_agent_set_vardata(void *data, size_t *data_len, u_char asn_type,
                                  double scale, double shift, const void *value,
                                  size_t len, int type) {

  int ret;
  netsnmp_vardata var;
  const value_t *val;
  long new_value = 0;

  val = value;
  var.string = (u_char *)data;

  ret = snmp_agent_get_value_from_ds_type(val, type, scale, shift, &new_value);
  if (ret != 0)
    return ret;

  switch (asn_type) {
  case ASN_INTEGER:
  case ASN_UINTEGER:
  case ASN_COUNTER:
  case ASN_TIMETICKS:
  case ASN_GAUGE:
    if (*data_len < sizeof(*var.integer))
      return -EINVAL;
    *var.integer = new_value;
    *data_len = sizeof(*var.integer);
    break;
  case ASN_COUNTER64:
    if (*data_len < sizeof(*var.counter64))
      return -EINVAL;
    var.counter64->high = (u_long)((int64_t)new_value >> 32);
    var.counter64->low = (u_long)((int64_t)new_value & 0xFFFFFFFF);
    *data_len = sizeof(*var.counter64);
    break;
  case ASN_OCTET_STR:
    if (type == DS_TYPE_GAUGE) {
      char buf[DATA_MAX_NAME_LEN];
      snprintf(buf, sizeof(buf), "%.2f", val->gauge);
      if (*data_len < strlen(buf))
        return -EINVAL;
      *data_len = strlen(buf);
      memcpy(var.string, buf, *data_len);
    } else {
      ERROR(PLUGIN_NAME ": Failed to convert %d ds type to %d asn type", type,
            asn_type);
      return -EINVAL;
    }
    break;
  default:
    ERROR(PLUGIN_NAME ": Failed to convert %d ds type to %d asn type", type,
          asn_type);
    return -EINVAL;
  }

  return 0;
}

static int snmp_agent_register_oid_index(oid_t *oid, int index,
                                         Netsnmp_Node_Handler *handler) {
  oid_t new_oid;
  memcpy(&new_oid, oid, sizeof(*oid));
  new_oid.oid[new_oid.oid_len++] = index;
  return snmp_agent_register_oid(&new_oid, handler);
}

static int snmp_agent_unregister_oid_index(oid_t *oid, int index) {
  oid_t new_oid;
  memcpy(&new_oid, oid, sizeof(*oid));
  new_oid.oid[new_oid.oid_len++] = index;
  return unregister_mib(new_oid.oid, new_oid.oid_len);
}

static int snmp_agent_update_index(table_definition_t *td,
                                   const char *instance) {

  if (c_avl_get(td->instance_index, instance, NULL) == 0)
    return 0;

  int ret;
  int *index = NULL;
  char *ins;

  ins = strdup(instance);
  if (ins == NULL)
    return -ENOMEM;

  /* need to generate index for the table */
  if (td->index_oid.oid_len) {
    index = calloc(1, sizeof(*index));
    if (index == NULL) {
      sfree(ins);
      return -ENOMEM;
    }

    *index = c_avl_size(td->instance_index) + 1;

    ret = c_avl_insert(td->instance_index, ins, index);
    if (ret != 0) {
      sfree(ins);
      sfree(index);
      return ret;
    }

    ret = c_avl_insert(td->index_instance, index, ins);
    if (ret < 0) {
      DEBUG(PLUGIN_NAME ": Failed to update index_instance for '%s' table",
            td->name);
      c_avl_remove(td->instance_index, ins, NULL, (void **)&index);
      sfree(ins);
      sfree(index);
      return ret;
    }

    ret = snmp_agent_register_oid_index(&td->index_oid, *index,
                                        snmp_agent_table_index_oid_handler);
    if (ret != 0)
      return ret;
  } else {
    /* instance as a key is required for any table */
    ret = c_avl_insert(td->instance_index, ins, ins);
    if (ret != 0) {
      sfree(ins);
      return ret;
    }
  }

  /* register new oids for all columns */
  for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
    data_definition_t *dd = de->value;

    for (size_t i = 0; i < dd->oids_len; i++) {
      if (td->index_oid.oid_len) {
        ret = snmp_agent_register_oid_index(&dd->oids[i], *index,
                                            snmp_agent_table_oid_handler);
      } else {
        ret = snmp_agent_register_oid_string(&dd->oids[i], ins,
                                             snmp_agent_table_oid_handler);
      }

      if (ret != 0)
        return ret;
    }
  }

  DEBUG(PLUGIN_NAME ": Updated index for '%s' table [%d, %s]", td->name,
        (index != NULL) ? *index : -1, ins);

  notification_t n = {
      .severity = NOTIF_OKAY, .time = cdtime(), .plugin = PLUGIN_NAME};
  sstrncpy(n.host, hostname_g, sizeof(n.host));
  sstrncpy(n.plugin_instance, ins, sizeof(n.plugin_instance));
  snprintf(n.message, sizeof(n.message),
           "Data row added to table %s instance %s index %d", td->name, ins,
           (index != NULL) ? *index : -1);
  plugin_dispatch_notification(&n);

  return 0;
}

static int snmp_agent_write(value_list_t const *vl) {

  if (vl == NULL)
    return -EINVAL;

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      if (!dd->is_instance) {
        if (CHECK_DD_TYPE(dd, vl->plugin, vl->plugin_instance, vl->type,
                          vl->type_instance)) {
          snmp_agent_update_index(td, vl->plugin_instance);
          return 0;
        }
      }
    }
  }

  return 0;
}

static int snmp_agent_collect(const data_set_t *ds, const value_list_t *vl,
                              user_data_t __attribute__((unused)) * user_data) {

  pthread_mutex_lock(&g_agent->lock);

  snmp_agent_write(vl);

  pthread_mutex_unlock(&g_agent->lock);

  return 0;
}

static int snmp_agent_preinit(void) {
  if (g_agent != NULL) {
    /* already initialized if config callback was called before init callback */
    return 0;
  }

  g_agent = calloc(1, sizeof(*g_agent));
  if (g_agent == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for snmp agent context");
    return -ENOMEM;
  }

  g_agent->tables = llist_create();
  g_agent->scalars = llist_create();

  if (g_agent->tables == NULL || g_agent->scalars == NULL) {
    ERROR(PLUGIN_NAME ": llist_create() failed");
    llist_destroy(g_agent->scalars);
    llist_destroy(g_agent->tables);
    return -ENOMEM;
  }

  int err;
  /* make us a agentx client. */
  err = netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_AGENT_ROLE,
                               1);
  if (err != 0) {
    ERROR(PLUGIN_NAME ": Failed to set agent role (%d)", err);
    llist_destroy(g_agent->scalars);
    llist_destroy(g_agent->tables);
    return -1;
  }

  /*
   *  For SNMP debug purposes uses snmp_set_do_debugging(1);
   */

  /* initialize the agent library */
  err = init_agent(PLUGIN_NAME);
  if (err != 0) {
    ERROR(PLUGIN_NAME ": Failed to initialize the agent library (%d)", err);
    llist_destroy(g_agent->scalars);
    llist_destroy(g_agent->tables);
    return -1;
  }

  init_snmp(PLUGIN_NAME);

  g_agent->tp = read_all_mibs();

  return 0;
}

static int snmp_agent_init(void) {
  int ret;

  if (g_agent == NULL || ((llist_head(g_agent->scalars) == NULL) &&
                          (llist_head(g_agent->tables) == NULL))) {
    ERROR(PLUGIN_NAME ": snmp_agent_init: plugin not configured");
    return -EINVAL;
  }

  plugin_register_shutdown(PLUGIN_NAME, snmp_agent_shutdown);

  ret = snmp_agent_register_scalar_oids();
  if (ret != 0)
    return ret;

  ret = snmp_agent_register_table_oids();
  if (ret != 0)
    return ret;

  ret = pthread_mutex_init(&g_agent->lock, NULL);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to initialize mutex, err %u", ret);
    return ret;
  }

  ret = pthread_mutex_init(&g_agent->agentx_lock, NULL);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to initialize AgentX mutex, err %u", ret);
    return ret;
  }

  /* create a second thread to listen for requests from AgentX*/
  ret = pthread_create(&g_agent->thread, NULL, &snmp_agent_thread_run, NULL);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to create a separate thread, err %u", ret);
    return ret;
  }

  if (llist_head(g_agent->tables) != NULL) {
    plugin_register_write(PLUGIN_NAME, snmp_agent_collect, NULL);
    plugin_register_missing(PLUGIN_NAME, snmp_agent_clear_missing, NULL);
  }

  return 0;
}

static void *snmp_agent_thread_run(void __attribute__((unused)) * arg) {
  INFO(PLUGIN_NAME ": Thread is up and running");

  for (;;) {
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    pthread_mutex_lock(&g_agent->agentx_lock);
    agent_check_and_process(0); /* 0 == don't block */
    pthread_mutex_unlock(&g_agent->agentx_lock);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    usleep(10);
  }

  pthread_exit(0);
}

static int snmp_agent_register_oid(oid_t *oid, Netsnmp_Node_Handler *handler) {
  netsnmp_handler_registration *reg;
  char *oid_name = snmp_agent_get_oid_name(oid->oid, oid->oid_len - 1);
  char oid_str[DATA_MAX_NAME_LEN];

  snmp_agent_oid_to_string(oid_str, sizeof(oid_str), oid);

  if (oid_name == NULL) {
    WARNING(PLUGIN_NAME
            ": Skipped registration: OID (%s) is not found in main tree",
            oid_str);
    return 0;
  }

  reg = netsnmp_create_handler_registration(oid_name, handler, oid->oid,
                                            oid->oid_len, HANDLER_CAN_RONLY);
  if (reg == NULL) {
    ERROR(PLUGIN_NAME ": Failed to create handler registration for OID (%s)",
          oid_str);
    return -1;
  }

  pthread_mutex_lock(&g_agent->agentx_lock);

  if (netsnmp_register_instance(reg) != MIB_REGISTERED_OK) {
    ERROR(PLUGIN_NAME ": Failed to register handler for OID (%s)", oid_str);
    pthread_mutex_unlock(&g_agent->agentx_lock);
    return -1;
  }

  pthread_mutex_unlock(&g_agent->agentx_lock);

  DEBUG(PLUGIN_NAME ": Registered handler for OID (%s)", oid_str);

  return 0;
}

static int snmp_agent_free_config(void) {

  if (g_agent == NULL)
    return -EINVAL;

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next)
    snmp_agent_free_table((table_definition_t **)&te->value);
  llist_destroy(g_agent->tables);

  for (llentry_t *de = llist_head(g_agent->scalars); de != NULL; de = de->next)
    snmp_agent_free_data((data_definition_t **)&de->value);
  llist_destroy(g_agent->scalars);

  return 0;
}

static int snmp_agent_shutdown(void) {
  int ret = 0;

  DEBUG(PLUGIN_NAME ": snmp_agent_shutdown");

  if (g_agent == NULL) {
    ERROR(PLUGIN_NAME ": snmp_agent_shutdown: plugin not initialized");
    return -EINVAL;
  }

  if (pthread_cancel(g_agent->thread) != 0)
    ERROR(PLUGIN_NAME ": snmp_agent_shutdown: failed to cancel the thread");

  if (pthread_join(g_agent->thread, NULL) != 0)
    ERROR(PLUGIN_NAME ": snmp_agent_shutdown: failed to join the thread");

  snmp_agent_free_config();

  snmp_shutdown(PLUGIN_NAME);

  pthread_mutex_destroy(&g_agent->lock);
  pthread_mutex_destroy(&g_agent->agentx_lock);

  sfree(g_agent);

  return ret;
}

static int snmp_agent_config(oconfig_item_t *ci) {

  int ret = snmp_agent_preinit();

  if (ret != 0) {
    sfree(g_agent);
    return -EINVAL;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Data", child->key) == 0) {
      ret = snmp_agent_config_data(child);
    } else if (strcasecmp("Table", child->key) == 0) {
      ret = snmp_agent_config_table(child);
    } else {
      ERROR(PLUGIN_NAME ": Unknown configuration option `%s'", child->key);
      ret = (-EINVAL);
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME ": Failed to parse configuration");
      snmp_agent_free_config();
      snmp_shutdown(PLUGIN_NAME);
      sfree(g_agent);
      return -EINVAL;
    }
  }

  ret = snmp_agent_validate_data();
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Invalid configuration provided");
    snmp_agent_free_config();
    snmp_shutdown(PLUGIN_NAME);
    sfree(g_agent);
    return -EINVAL;
  }

  return 0;
}

void module_register(void) {
  plugin_register_init(PLUGIN_NAME, snmp_agent_init);
  plugin_register_complex_config(PLUGIN_NAME, snmp_agent_config);
}
