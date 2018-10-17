/**
 * collectd - src/redfish.c
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
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
 *   Marcin Mozejko <marcinx.mozejko@intel.com>
 *   Martin Kennelly <martin.kennelly@intel.com>
 *   Adrian Boczkowski <adrianx.boczkowski@intel.com>
 **/

#include "collectd.h"

#include "common.h"
#include "utils_avltree.h"
#include "utils_llist.h"

#include <redfish.h>
#define PLUGIN_NAME "redfish"
#define MAX_STR_LEN 128

struct redfish_property_s {
  char *name;
  char *plugin_inst;
  char *type;
  char *type_inst;
};
typedef struct redfish_property_s redfish_property_t;

struct redfish_resource_s {
  char *name;
  llist_t *properties;
};
typedef struct redfish_resource_s redfish_resource_t;

struct redfish_query_s {
  char *name;
  char *endpoint;
  llist_t *resources;
};
typedef struct redfish_query_s redfish_query_t;

struct redfish_service_s {
  char *name;
  char *host;
  char *user;
  char *passwd;
  char *token;
  unsigned int flags;
  char **queries;      /* List of queries */
  llist_t *query_ptrs; /* Pointers to query structs */
  size_t queries_num;
  enumeratorAuthentication auth;
  redfishService *redfish;
};
typedef struct redfish_service_s redfish_service_t;

struct redfish_ctx_s {
  llist_t *services;
  c_avl_tree_t *queries;
};
typedef struct redfish_ctx_s redfish_ctx_t;

struct redfish_payload_ctx_s {
  redfish_service_t *service;
  llist_t *resources;
};
typedef struct redfish_payload_ctx_s redfish_payload_ctx_t;

enum redfish_value_type_e { VAL_TYPE_STR = 0, VAL_TYPE_INT, VAL_TYPE_REAL };
typedef enum redfish_value_type_e redfish_value_type_t;

union redfish_value_u {
  double real;
  int integer;
  char *string;
};
typedef union redfish_value_u redfish_value_t;

static redfish_ctx_t *ctx;

static int redfish_cleanup(void);
static int redfish_validate_config(void);

#if COLLECT_DEBUG
static void redfish_print_config(void) {
  DEBUG(PLUGIN_NAME ": ====================CONFIGURATION====================");
  DEBUG(PLUGIN_NAME ": SERVICES: %d", llist_size(ctx->services));
  for (llentry_t *le = llist_head(ctx->services); le != NULL; le = le->next) {
    redfish_service_t *s = (redfish_service_t *)le->value;
    char queries_str[MAX_STR_LEN];

    strjoin(queries_str, MAX_STR_LEN, s->queries, s->queries_num, ", ");

    DEBUG(PLUGIN_NAME ": --------------------");
    DEBUG(PLUGIN_NAME ": Service: %s", s->name);
    DEBUG(PLUGIN_NAME ":   Host: %s", s->host);

    if (s->user && s->passwd) {
      DEBUG(PLUGIN_NAME ":   User: %s", s->user);
      DEBUG(PLUGIN_NAME ":   Passwd: %s", s->passwd);
    } else if (s->token)
      DEBUG(PLUGIN_NAME ":   Token: %s", s->token);

    DEBUG(PLUGIN_NAME ":   Queries[%" PRIsz "]: (%s)", s->queries_num,
          queries_str);
  }

  DEBUG(PLUGIN_NAME ": =====================================================");

  c_avl_iterator_t *i = c_avl_get_iterator(ctx->queries);
  char *key;
  redfish_query_t *q;

  DEBUG(PLUGIN_NAME ": QUERIES: %d", c_avl_size(ctx->queries));

  while (c_avl_iterator_next(i, (void *)&key, (void *)&q) == 0) {
    DEBUG(PLUGIN_NAME ": --------------------");
    DEBUG(PLUGIN_NAME ": Query: %s", q->name);
    DEBUG(PLUGIN_NAME ":   Endpoint: %s", q->endpoint);
    for (llentry_t *le = llist_head(q->resources); le != NULL; le = le->next) {
      redfish_resource_t *r = (redfish_resource_t *)le->value;
      DEBUG(PLUGIN_NAME ":   Resource: %s", r->name);
      for (llentry_t *le = llist_head(r->properties); le != NULL;
           le = le->next) {
        redfish_property_t *p = (redfish_property_t *)le->value;
        DEBUG(PLUGIN_NAME ":     Property: %s", p->name);
        DEBUG(PLUGIN_NAME ":       PluginInstance: %s", p->plugin_inst);
        DEBUG(PLUGIN_NAME ":       Type: %s", p->type);
        DEBUG(PLUGIN_NAME ":       TypeInstance: %s", p->type_inst);
      }
    }
  }

  c_avl_iterator_destroy(i);
  DEBUG(PLUGIN_NAME ": =====================================================");
}
#endif

static void redfish_service_destroy(redfish_service_t *service) {
  /* This is checked internally by cleanupServiceEnumerator() also,
   * but as long as it's a third-party library let's be a little 'defensive' */
  if (service->redfish != NULL)
    cleanupServiceEnumerator(service->redfish);

  /* Destroy all service members, sfree() as well as strarray_free()
   * and llist_destroy() are safe to call on NULL argument */
  sfree(service->name);
  sfree(service->host);
  sfree(service->user);
  sfree(service->passwd);
  sfree(service->token);
  strarray_free(service->queries, (size_t)service->queries_num);
  llist_destroy(service->query_ptrs);

  sfree(service);
}

static int redfish_init(void) {
#if COLLECT_DEBUG
  redfish_print_config();
#endif
  int ret = redfish_validate_config();

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Validation of configuration file failed");
    return ret;
  }

  for (llentry_t *le = llist_head(ctx->services); le != NULL; le = le->next) {
    redfish_service_t *service = (redfish_service_t *)le->value;
    /* Ignore redfish version */
    service->flags |= REDFISH_FLAG_SERVICE_NO_VERSION_DOC;

    /* Preparing struct for authentication */
    if (service->user && service->passwd) {
      service->auth.authCodes.userPass.username = service->user;
      service->auth.authCodes.userPass.password = service->passwd;
      service->redfish = createServiceEnumerator(
          service->host, NULL, &service->auth, service->flags);
    } else if (service->token) {
      service->auth.authCodes.authToken.token = service->token;
      service->auth.authType = REDFISH_AUTH_BEARER_TOKEN;
      service->redfish = createServiceEnumerator(
          service->host, NULL, &service->auth, service->flags);
    } else {
      service->redfish =
          createServiceEnumerator(service->host, NULL, NULL, service->flags);
    }

    service->query_ptrs = llist_create();
    if (service->query_ptrs == NULL) {
      ERROR(PLUGIN_NAME ": Failed to allocate memory for service query list");
      goto error;
    }

    /* Preparing query pointers list for every service */
    for (size_t i = 0; i < service->queries_num; i++) {
      redfish_query_t *ptr;
      if (c_avl_get(ctx->queries, (void *)service->queries[i], (void *)&ptr) !=
          0) {
        ERROR(PLUGIN_NAME ": Cannot find a service query in a context");
        goto error;
      }

      llentry_t *entry = llentry_create(ptr->name, ptr);
      if (entry != NULL)
        llist_append(service->query_ptrs, entry);
      else {
        ERROR(PLUGIN_NAME ": Failed to allocate memory for a query list entry");
        goto error;
      }
    }
  }

  return 0;

error:
  /* Freeing libredfish resources & llists */
  for (llentry_t *le = llist_head(ctx->services); le != NULL; le = le->next) {
    redfish_service_t *service = (redfish_service_t *)le->value;

    redfish_service_destroy(service);
  }
  return -ENOMEM;
}

static int redfish_preconfig(void) {
  /* Allocating plugin context */
  ctx = calloc(1, sizeof(*ctx));
  if (ctx == NULL)
    goto error;

  /* Creating placeholder for services */
  ctx->services = llist_create();
  if (ctx->services == NULL)
    goto free_ctx;

  /* Creating placeholder for queries */
  ctx->queries = c_avl_create((void *)strcmp);
  if (ctx->services == NULL)
    goto free_services;

  return 0;

free_services:
  llist_destroy(ctx->services);
free_ctx:
  sfree(ctx);
error:
  ERROR(PLUGIN_NAME ": Failed to allocate memory for plugin context");
  return -ENOMEM;
}

static int redfish_config_property(redfish_resource_t *resource,
                                   oconfig_item_t *cfg_item) {
  assert(resource != NULL);
  assert(cfg_item != NULL);

  redfish_property_t *property = calloc(1, sizeof(*property));

  if (property == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for property");
    return -ENOMEM;
  }

  int ret = cf_util_get_string(cfg_item, &property->name);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Could not get property argument in resource section "
                      "named \"%s\"",
          resource->name);
    ret = -EINVAL;
    goto free_all;
  }

  for (int i = 0; i < cfg_item->children_num; i++) {
    oconfig_item_t *opt = cfg_item->children + i;
    if (strcasecmp("PluginInstance", opt->key) == 0)
      ret = cf_util_get_string(opt, &property->plugin_inst);
    else if (strcasecmp("Type", opt->key) == 0)
      ret = cf_util_get_string(opt, &property->type);
    else if (strcasecmp("TypeInstance", opt->key) == 0)
      ret = cf_util_get_string(opt, &property->type_inst);
    else {
      ERROR(PLUGIN_NAME ": Invalid option \"%s\" in property \"%s\" "
                        "in resource \"%s\"",
            opt->key, property->name, resource->name);
      ret = -EINVAL;
      goto free_all;
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME ": Something went wrong going through attributes in "
                        "property named \"%s\" in resource named \"%s\"",
            property->name, resource->name);
      goto free_all;
    }
  }

  llentry_t *entry = llentry_create(property->name, property);
  if (entry == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for property");
    ret = -ENOMEM;
    goto free_all;
  }
  llist_append(resource->properties, entry);

  return 0;

free_all:
  sfree(property->name);
  sfree(property->plugin_inst);
  sfree(property->type);
  sfree(property->type_inst);
  sfree(property);
  return ret;
}

static int redfish_config_resource(redfish_query_t *query,
                                   oconfig_item_t *cfg_item) {
  assert(query != NULL);
  assert(cfg_item != NULL);

  redfish_resource_t *resource = calloc(1, sizeof(*resource));

  if (resource == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for resource");
    return -ENOMEM;
  }

  resource->properties = llist_create();

  if (resource->properties == NULL)
    goto free_memory;

  int ret = cf_util_get_string(cfg_item, &resource->name);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Could not get resource name for query named \"%s\"",
          query->name);
    goto free_memory;
  }
  for (int i = 0; i < cfg_item->children_num; i++) {
    oconfig_item_t *opt = cfg_item->children + i;
    if (strcasecmp("Property", opt->key) != 0) {
      WARNING(PLUGIN_NAME ": Invalid configuration option \"%s\".", opt->key);
      continue;
    }

    ret = redfish_config_property(resource, opt);

    if (ret != 0) {
      goto free_memory;
    }
  }

  llentry_t *entry = llentry_create(resource->name, resource);
  if (entry == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for resource list entry");
    goto free_memory;
  }
  llist_append(query->resources, entry);

  return 0;

free_memory:
  sfree(resource->name);
  llist_destroy(resource->properties);
  sfree(resource);
  return -1;
}

static int redfish_config_query(oconfig_item_t *cfg_item,
                                c_avl_tree_t *queries) {
  redfish_query_t *query = calloc(1, sizeof(*query));

  if (query == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for query");
    return -ENOMEM;
  }

  query->resources = llist_create();

  int ret;
  if (query->resources == NULL) {
    ret = -ENOMEM;
    goto free_all;
  }

  ret = cf_util_get_string(cfg_item, &query->name);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Unable to get query name. Query ignored");
    ret = -EINVAL;
    goto free_all;
  }

  for (int i = 0; i < cfg_item->children_num; i++) {
    oconfig_item_t *opt = cfg_item->children + i;

    if (strcasecmp("Endpoint", opt->key) == 0)
      ret = cf_util_get_string(opt, &query->endpoint);
    else if (strcasecmp("Resource", opt->key) == 0)
      ret = redfish_config_resource(query, opt);
    else {
      ERROR(PLUGIN_NAME ": Invalid configuration option \"%s\".", opt->key);
      ret = -EINVAL;
      goto free_all;
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME ": Something went wrong processing query \"%s\"",
            query->name);
      ret = -EINVAL;
      goto free_all;
    }
  }

  ret = c_avl_insert(queries, query->name, query);

  if (ret != 0)
    goto free_all;

  return 0;

free_all:
  sfree(query->name);
  sfree(query->endpoint);
  llist_destroy(query->resources);
  sfree(query);
  return ret;
}

static int redfish_read_queries(oconfig_item_t *cfg_item, char ***queries_ptr) {
  char **queries = NULL;
  size_t queries_num = 0;

  for (int i = 0; i < cfg_item->values_num; ++i) {
    strarray_add(&queries, &queries_num, cfg_item->values[i].value.string);
  }

  if (queries_num != (size_t)cfg_item->values_num) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for query list");
    strarray_free(queries, queries_num);
    return -ENOMEM;
  }

  *queries_ptr = queries;
  return 0;
}

static int redfish_config_service(oconfig_item_t *cfg_item) {
  redfish_service_t *service = calloc(1, sizeof(*service));

  if (service == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for service");
    return -ENOMEM;
  }

  int ret = cf_util_get_string(cfg_item, &service->name);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": A service was defined without an argument");
    goto free_service;
  }

  for (int i = 0; i < cfg_item->children_num; i++) {
    oconfig_item_t *opt = cfg_item->children + i;

    if (strcasecmp("Host", opt->key) == 0)
      ret = cf_util_get_string(opt, &service->host);
    else if (strcasecmp("User", opt->key) == 0)
      ret = cf_util_get_string(opt, &service->user);
    else if (strcasecmp("Passwd", opt->key) == 0)
      ret = cf_util_get_string(opt, &service->passwd);
    else if (strcasecmp("Token", opt->key) == 0)
      ret = cf_util_get_string(opt, &service->token);
    else if (strcasecmp("Queries", opt->key) == 0) {
      ret = redfish_read_queries(opt, &service->queries);
      service->queries_num = opt->values_num;
    } else {
      ERROR(PLUGIN_NAME ": Invalid configuration option \"%s\".", opt->key);
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME ": Something went wrong processing the service named \
            \"%s\"",
            service->name);
      goto free_service;
    }
  }

  llentry_t *entry = llentry_create(service->name, service);
  if (entry == NULL) {
    ERROR(PLUGIN_NAME ": Failed to create list for service name \"%s\"",
          service->name);
    goto free_service;
  }
  llist_append(ctx->services, entry);

  return 0;

free_service:
  redfish_service_destroy(service);
  return -1;
}

static int redfish_config(oconfig_item_t *cfg_item) {
  int ret = redfish_preconfig();

  if (ret != 0)
    return ret;

  for (int i = 0; i < cfg_item->children_num; i++) {
    oconfig_item_t *child = cfg_item->children + i;

    if (strcasecmp("Query", child->key) == 0)
      ret = redfish_config_query(child, ctx->queries);
    else if (strcasecmp("Service", child->key) == 0)
      ret = redfish_config_service(child);
    else {
      ERROR(PLUGIN_NAME ": Invalid configuration option \"%s\".", child->key);
    }

    if (ret != 0) {
      redfish_cleanup();
      return ret;
    }
  }

  return 0;
}

static int redfish_validate_config(void) {
  /* Service validation */
  for (llentry_t *llserv = llist_head(ctx->services); llserv != NULL;
       llserv = llserv->next) {
    redfish_service_t *service = llserv->value;
    if (service->name == NULL) {
      ERROR(PLUGIN_NAME ": A service has no name");
      return -EINVAL;
    }
    if (service->host == NULL) {
      ERROR(PLUGIN_NAME ": Service \"%s\" has no host attribute",
            service->name);
      return -EINVAL;
    }
    if ((service->user == NULL) ^ (service->passwd == NULL)) {
      ERROR(PLUGIN_NAME ": Service \"%s\" does not have user and/or password "
                        "defined",
            service->name);
      return -EINVAL;
    }
    if (service->user == NULL && service->token == NULL) {
      ERROR(PLUGIN_NAME ": Service \"%s\" does not have an user/pass or "
                        "token defined",
            service->name);
      return -EINVAL;
    }
    if (service->queries_num == 0)
      WARNING(PLUGIN_NAME ": Service \"%s\" does not have queries",
              service->name);

    for (int i = 0; i < service->queries_num; i++) {
      redfish_query_t *query_query;
      bool found = false;
      char *key;
      c_avl_iterator_t *query_iter = c_avl_get_iterator(ctx->queries);
      while (c_avl_iterator_next(query_iter, (void *)&key,
                                 (void *)&query_query) == 0 &&
             !found) {
        if (query_query->name != NULL && service->queries[i] != NULL &&
            strcmp(query_query->name, service->queries[i]) == 0) {
          found = true;
        }
      }

      if (!found) {
        ERROR(PLUGIN_NAME ": Query named \"%s\" in service \"%s\" not found",
              service->queries[i], service->name);
        c_avl_iterator_destroy(query_iter);
        return -EINVAL;
      }

      c_avl_iterator_destroy(query_iter);
    }
  }

  c_avl_iterator_t *queries_iter = c_avl_get_iterator(ctx->queries);
  char *key;
  redfish_query_t *query;

  /* Query validation */
  while (c_avl_iterator_next(queries_iter, (void *)&key, (void *)&query) == 0) {
    if (query->name == NULL) {
      ERROR(PLUGIN_NAME ": A query does not have a name");
      goto error;
    }
    if (query->endpoint == NULL) {
      ERROR(PLUGIN_NAME ": Query \"%s\" does not have a valid endpoint",
            query->name);
      goto error;
    }
    for (llentry_t *llres = llist_head(query->resources); llres != NULL;
         llres = llres->next) {
      redfish_resource_t *resource = (redfish_resource_t *)llres->value;
      /* Resource validation */
      if (resource->name == NULL) {
        WARNING(PLUGIN_NAME ": A resource in query \"%s\" is not named",
                query->name);
      }
      /* Property validation */
      for (llentry_t *llprop = llist_head(resource->properties); llprop != NULL;
           llprop = llprop->next) {
        redfish_property_t *prop = (redfish_property_t *)llprop->value;
        if (prop->name == NULL) {
          ERROR(PLUGIN_NAME ": A property has no name in query \"%s\"",
                query->name);
          goto error;
        }
        if (prop->plugin_inst == NULL) {
          ERROR(PLUGIN_NAME ": A plugin instance is not defined in property "
                            "\"%s\" in query \"%s\"",
                prop->name, query->name);
          goto error;
        }
        if (prop->type == NULL) {
          ERROR(PLUGIN_NAME ": Type is not defined in property \"%s\" in "
                            "query \"%s\"",
                prop->name, query->name);
          goto error;
        }
      }
    }
  }

  c_avl_iterator_destroy(queries_iter);

  return 0;

error:
  c_avl_iterator_destroy(queries_iter);
  return -EINVAL;
}

static int redfish_convert_val(redfish_value_t *value,
                               redfish_value_type_t src_type, value_t *vl,
                               int dst_type) {
  switch (dst_type) {
  case DS_TYPE_GAUGE:
    if (src_type == VAL_TYPE_STR)
      vl->gauge = strtod(value->string, NULL);
    else if (src_type == VAL_TYPE_INT)
      vl->gauge = (gauge_t)value->integer;
    else if (src_type == VAL_TYPE_REAL)
      vl->gauge = value->real;
    break;
  case DS_TYPE_DERIVE:
    if (src_type == VAL_TYPE_STR)
      vl->derive = strtoll(value->string, NULL, 0);
    else if (src_type == VAL_TYPE_INT)
      vl->derive = (derive_t)value->integer;
    else if (src_type == VAL_TYPE_REAL)
      vl->derive = (derive_t)value->real;
    break;
  case DS_TYPE_COUNTER:
    if (src_type == VAL_TYPE_STR)
      vl->derive = strtoull(value->string, NULL, 0);
    else if (src_type == VAL_TYPE_INT)
      vl->derive = (derive_t)value->integer;
    else if (src_type == VAL_TYPE_REAL)
      vl->derive = (derive_t)value->real;
    break;
  case DS_TYPE_ABSOLUTE:
    if (src_type == VAL_TYPE_STR)
      vl->absolute = strtoull(value->string, NULL, 0);
    else if (src_type == VAL_TYPE_INT)
      vl->absolute = (absolute_t)value->integer;
    else if (src_type == VAL_TYPE_REAL)
      vl->absolute = (absolute_t)value->real;
    break;
  default:
    ERROR(PLUGIN_NAME ": Invalid data set type. Cannot convert value");
    return -EINVAL;
  }

  return 0;
}

static void redfish_process_payload_property(const redfish_property_t *prop,
                                             const json_t *json_array,
                                             const redfish_resource_t *res,
                                             const redfish_service_t *serv) {
  /* Iterating through array of sensor(s) */
  for (int i = 0; i < json_array_size(json_array); i++) {
    json_t *item = json_array_get(json_array, i);
    if (item == NULL) {
      ERROR(PLUGIN_NAME ": Failure retrieving array member for resource \"%s\"",
            res->name);
      continue;
    }
    json_t *object = json_object_get(item, prop->name);
    if (object == NULL) {
      ERROR(PLUGIN_NAME
            ": Failure retreiving property \"%s\" from resource \"%s\"",
            prop->name, res->name);
      continue;
    }
    value_list_t v1 = VALUE_LIST_INIT;
    v1.values_len = 1;
    if (prop->plugin_inst != NULL)
      sstrncpy(v1.plugin_instance, prop->plugin_inst,
               sizeof(v1.plugin_instance));
    if (prop->type_inst != NULL)
      sstrncpy(v1.type_instance, prop->type_inst, sizeof(v1.type_instance));
    else {
      /* Retrieving MemberId of sensor */
      json_t *member_id = json_object_get(item, "MemberId");
      if (member_id == NULL) {
        ERROR(PLUGIN_NAME
              ": Failed to get MemberId for property \"%s\" in resource "
              "\"%s\"",
              prop->name, res->name);
        continue;
      }

      int ch_count = snprintf(v1.type_instance, sizeof(v1.type_instance), "%d",
                              (int)json_integer_value(member_id));
      if (ch_count == 0) {
        ERROR(PLUGIN_NAME ": Failed to convert MemberId to a character");
        continue;
      }
    }

    /* Checking whether real or integer value */
    redfish_value_t value;
    redfish_value_type_t type = VAL_TYPE_STR;
    if (json_is_string(object)) {
      value.string = (char *)json_string_value(object);
    } else if (json_is_integer(object)) {
      type = VAL_TYPE_INT;
      value.integer = json_integer_value(object);
    } else if (json_is_real(object)) {
      type = VAL_TYPE_REAL;
      value.real = json_real_value(object);
    }
    const data_set_t *ds = plugin_get_ds(prop->type);

    /* Check if data set found */
    if (ds == NULL)
      continue;

    v1.values = &(value_t){0};
    redfish_convert_val(&value, type, v1.values, ds->ds[0].type);

    sstrncpy(v1.host, serv->host, sizeof(v1.host));
    sstrncpy(v1.plugin, PLUGIN_NAME, sizeof(v1.plugin));
    sstrncpy(v1.type, prop->type, sizeof(v1.type));
    plugin_dispatch_values(&v1);
    /* Clear values assigned in case of leakage */
    v1.values = NULL;
    v1.values_len = 0;
  }
}

static void redfish_process_payload(bool success, unsigned short http_code,
                                    redfishPayload *payload, void *context) {
  if (!success) {
    WARNING(PLUGIN_NAME ": Query has failed, HTTP code = %u\n", http_code);
    return;
  }
  redfish_payload_ctx_t *res_serv = (redfish_payload_ctx_t *)context;
  redfish_service_t *serv = res_serv->service;
  if (!payload) {
    WARNING(PLUGIN_NAME ": Failed to get payload for service name \"%s\"",
            serv->name);
    return;
  }

  for (llentry_t *llres = llist_head(res_serv->resources); llres != NULL;
       llres = llres->next) {
    redfish_resource_t *res = (redfish_resource_t *)llres->value;
    json_t *json_array = json_object_get(payload->json, res->name);

    if (json_array == NULL) {
      ERROR(PLUGIN_NAME ": Could not find resource \"%s\"", res->name);
      continue;
    }

    for (llentry_t *llprop = llist_head(res->properties); llprop != NULL;
         llprop = llprop->next) {
      redfish_property_t *prop = (redfish_property_t *)llprop->value;

      redfish_process_payload_property(prop, json_array, res, serv);
    }
    json_decref(json_array);
  }
}

static int redfish_read(__attribute__((unused)) user_data_t *ud) {
  for (llentry_t *le = llist_head(ctx->services); le != NULL; le = le->next) {
    redfish_service_t *service = (redfish_service_t *)le->value;
    for (llentry_t *le = llist_head(service->query_ptrs); le != NULL;
         le = le->next) {
      redfish_query_t *query = (redfish_query_t *)le->value;
      redfish_payload_ctx_t rs = {.service = service,
                                  .resources = query->resources};
      getPayloadByPathAsync(service->redfish, query->endpoint, NULL,
                            redfish_process_payload, &rs);
      /* TODO: Work around for race condition. Needs permanent fix. */
      sleep(10);
      // serviceDecRefAndWait(service->redfish);
    }
  }
  return 0;
}

static int redfish_cleanup(void) {
  for (llentry_t *le = llist_head(ctx->services); le; le = le->next) {
    redfish_service_t *service = (redfish_service_t *)le->value;

    redfish_service_destroy(service);
  }
  llist_destroy(ctx->services);

  c_avl_iterator_t *i = c_avl_get_iterator(ctx->queries);

  char *key;
  redfish_query_t *query;

  while (c_avl_iterator_next(i, (void *)&key, (void *)&query) == 0) {
    for (llentry_t *le = llist_head(query->resources); le != NULL;
         le = le->next) {
      redfish_resource_t *resource = (redfish_resource_t *)le->value;
      for (llentry_t *le = llist_head(resource->properties); le != NULL;
           le = le->next) {
        redfish_property_t *property = (redfish_property_t *)le->value;
        sfree(property->name);
        sfree(property->plugin_inst);
        sfree(property->type);
        sfree(property->type_inst);
      }
      sfree(resource->name);
    }
    sfree(query->name);
    sfree(query->endpoint);
    sfree(query);
  }

  c_avl_iterator_destroy(i);
  c_avl_destroy(ctx->queries);
  sfree(ctx);
  return 0;
}

void module_register(void) {
  plugin_register_init(PLUGIN_NAME, redfish_init);
  plugin_register_complex_config(PLUGIN_NAME, redfish_config);
  plugin_register_complex_read(NULL, PLUGIN_NAME, redfish_read, 0, NULL);
  plugin_register_shutdown(PLUGIN_NAME, redfish_cleanup);
}
