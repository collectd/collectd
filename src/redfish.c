/**
 * collectd - src/redfish.c
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 * Copyright(c) 2021 Atos. All rights reserved.
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
 * Original authors:
 *      Marcin Mozejko <marcinx.mozejko@intel.com>
 *      Martin Kennelly <martin.kennelly@intel.com>
 *      Adrian Boczkowski <adrianx.boczkowski@intel.com>
 *
 * Refactoring and enhancement author:
 *      Mathieu Stoffel <mathieu.stoffel@atos.net>
 **/
#include <unistd.h>

#include "collectd.h"

#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils/deq/deq.h"
#include "utils_llist.h"

#include <redfish.h>

#define PLUGIN_NAME "redfish"
#define MAX_STR_LEN 128

/* For the purpose of mocking the type/data source inference interface in the
 * test framework: */
#if defined(REDFISH_PLUGIN_TEST)
static const data_set_t *
redfish_test_plugin_get_ds_mock(const char *const type);
#endif

/* For the purpose of mocking the dispatching interface in the test
 * framework: */
#if defined(REDFISH_PLUGIN_TEST)
static int redfish_test_plugin_dispatch_values_mock(value_list_t const *vl);
#endif

/******************************************************************************
 * Data structures:
 ******************************************************************************/
struct redfish_attribute_s {
  char *name;
  char *plugin_inst;
  char *type;
  char *type_inst;
};
typedef struct redfish_attribute_s redfish_attribute_t;

/*******/

struct redfish_property_s {
  char *name;
  char *plugin_inst;
  char *type;
  char *type_inst;
  char *type_inst_attr;
  bool type_inst_prefix_id;
  uint64_t *select_ids;
  uint64_t nb_select_ids;
  char **select_attrs;
  uint64_t nb_select_attrs;
  llist_t *select_attrvalues;
};
typedef struct redfish_property_s redfish_property_t;

/*******/

struct redfish_resource_s {
  char *name;
  llist_t *properties;
};
typedef struct redfish_resource_s redfish_resource_t;

/*******/

struct redfish_query_s {
  char *name;
  char *endpoint;
  llist_t *resources;
  llist_t *attributes;
};
typedef struct redfish_query_s redfish_query_t;

/*******/

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

/*******/

struct redfish_payload_ctx_s {
  redfish_service_t *service;
  redfish_query_t *query;
};
typedef struct redfish_payload_ctx_s redfish_payload_ctx_t;

/*******/

enum redfish_value_type_e { VAL_TYPE_STR = 0, VAL_TYPE_INT, VAL_TYPE_REAL };
typedef enum redfish_value_type_e redfish_value_type_t;

/*******/

union redfish_value_u {
  double real;
  int64_t integer;
  char *string;
};
typedef union redfish_value_u redfish_value_t;

/*******/

struct redfish_job_s {
  DEQ_LINKS(struct redfish_job_s);
  redfish_payload_ctx_t *service_query;
};
typedef struct redfish_job_s redfish_job_t;

/*******/

DEQ_DECLARE(redfish_job_t, redfish_job_list_t);

/*******/

struct redfish_ctx_s {
  llist_t *services;
  c_avl_tree_t *queries;
  pthread_t worker_thread;
  redfish_job_list_t jobs;
};
typedef struct redfish_ctx_s redfish_ctx_t;

/******************************************************************************
 * Global variables:
 ******************************************************************************/
static redfish_ctx_t ctx;

/******************************************************************************
 * Functions:
 ******************************************************************************/
static int redfish_cleanup(void);
static int redfish_validate_config(void);
static void *redfish_worker_thread(void *__attribute__((unused)) args);

/*******/

#if COLLECT_DEBUG
/* Hook exposed by the libredfish library to define a printing function
 * dedicated to logging purposes: */
extern libRedfishDebugFunc gDebugFunc;

static void redfish_print_config(void) {
  DEBUG(PLUGIN_NAME ": "
                    "====================CONFIGURATION====================");
  DEBUG(PLUGIN_NAME ": SERVICES: %d", llist_size(ctx.services));

  for (llentry_t *le = llist_head(ctx.services); le != NULL; le = le->next) {
    redfish_service_t *s = (redfish_service_t *)le->value;
    char queries_str[MAX_STR_LEN];

    strjoin(queries_str, MAX_STR_LEN, s->queries, s->queries_num, ", ");

    DEBUG(PLUGIN_NAME ": --------------------");
    DEBUG(PLUGIN_NAME ": Service: %s", s->name);
    DEBUG(PLUGIN_NAME ":   Host: %s", s->host);

    if (s->user && s->passwd) {
      DEBUG(PLUGIN_NAME ":   User: %s", s->user);
      DEBUG(PLUGIN_NAME ":   Passwd: %s", s->passwd);
    } else if (s->token) {
      DEBUG(PLUGIN_NAME ":   Token: %s", s->token);
    }

    DEBUG(PLUGIN_NAME ": "
                      "Queries[%" PRIsz "]: (%s)",
          s->queries_num, queries_str);
  }

  DEBUG(PLUGIN_NAME ": "
                    "=====================================================");

  c_avl_iterator_t *i = c_avl_get_iterator(ctx.queries);
  char *key;
  redfish_query_t *q;

  DEBUG(PLUGIN_NAME ": QUERIES: %d", c_avl_size(ctx.queries));

  while (c_avl_iterator_next(i, (void *)(&key), (void *)(&q)) == 0) {
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

        if (p->type_inst != NULL) {
          DEBUG(PLUGIN_NAME ":       TypeInstance: %s", p->type_inst);
        }

        if (p->type_inst_attr != NULL) {
          DEBUG(PLUGIN_NAME ":       "
                            "TypeInstanceAttr: %s",
                p->type_inst_attr);
        }

        DEBUG(PLUGIN_NAME ":       "
                          "TypeInstancePrefixID: %s",
              (p->type_inst_prefix_id ? "true" : "false"));

        if (p->nb_select_ids > 0) {
          DEBUG(PLUGIN_NAME ":       SelectIDs:");

          for (uint64_t i = 0; i < p->nb_select_ids; i++) {
            DEBUG(PLUGIN_NAME ":         -> %lu", p->select_ids[i]);
          }
        }

        if (p->nb_select_attrs > 0) {
          DEBUG(PLUGIN_NAME ":       SelectAttrs:");

          for (uint64_t i = 0; i < p->nb_select_attrs; i++) {
            DEBUG(PLUGIN_NAME ":         -> %s", p->select_attrs[i]);
          }
        }

        if (llist_size(p->select_attrvalues) > 0) {
          DEBUG(PLUGIN_NAME ":       SelectAttrValue:");

          for (llentry_t *le = llist_head(p->select_attrvalues); le != NULL;
               le = le->next) {
            DEBUG(PLUGIN_NAME ":         -> %s = %s", le->key,
                  (char *)(le->value));
          }
        }
      }
    }

    for (llentry_t *le = llist_head(q->attributes); le != NULL; le = le->next) {
      redfish_attribute_t *attr = (redfish_attribute_t *)le->value;

      DEBUG(PLUGIN_NAME ":   Attribute: %s", attr->name);

      DEBUG(PLUGIN_NAME ":     PluginInstance: %s", attr->plugin_inst);
      DEBUG(PLUGIN_NAME ":     Type: %s", attr->type);
      DEBUG(PLUGIN_NAME ":     TypeInstance: %s", attr->type_inst);
    }
  }

  c_avl_iterator_destroy(i);
  DEBUG(PLUGIN_NAME ": "
                    "=====================================================");
}
#endif

/*******/

static void redfish_service_destroy(redfish_service_t *service) {
  /* This is checked internally by cleanupServiceEnumerator() also,
   * but as long as it's a third-party library let's be a little
   * 'defensive': */
  if (service->redfish != NULL)
    cleanupServiceEnumerator(service->redfish);

  /* Destroy all service members, sfree() as well as strarray_free()
   * and llist_destroy() are safe to call on NULL argument: */
  sfree(service->name);
  sfree(service->host);
  sfree(service->user);
  sfree(service->passwd);
  sfree(service->token);
  strarray_free(service->queries, (size_t)service->queries_num);
  llist_destroy(service->query_ptrs);

  sfree(service);
}

/*******/

#if defined(REDFISH_PLUGIN_TEST)
__attribute__((unused))
#endif
static void
redfish_job_destroy(redfish_job_t *job) {
  sfree(job->service_query);
  sfree(job);
}

/*******/

static int redfish_init(void) {
#if COLLECT_DEBUG
  /* Registering plugin_log as the printing function dedicated to logging
   * purposes within libredfish: */
  gDebugFunc = plugin_log;

  redfish_print_config();
#endif

  int ret = redfish_validate_config();

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Validation of configuration file failed");
    return ret;
  }

  DEQ_INIT(ctx.jobs);
  ret = pthread_create(&ctx.worker_thread, NULL, redfish_worker_thread, NULL);

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Creation of thread failed");
    return ret;
  }

  for (llentry_t *le = llist_head(ctx.services); le != NULL; le = le->next) {
    redfish_service_t *service = (redfish_service_t *)le->value;

    /* Ignore redfish version: */
    service->flags |= REDFISH_FLAG_SERVICE_NO_VERSION_DOC;

    /* Preparing struct for authentication: */
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
      ERROR(PLUGIN_NAME ": "
                        "Failed to allocate memory for service query list");

      goto error;
    }

    /* Preparing query pointers list for every service */
    for (size_t i = 0; i < service->queries_num; i++) {
      redfish_query_t *ptr;

      if (c_avl_get(ctx.queries, (void *)service->queries[i], (void *)&ptr) !=
          0) {
        ERROR(PLUGIN_NAME ": Cannot find a service query in a context");
        goto error;
      }

      llentry_t *entry = llentry_create(ptr->name, ptr);

      if (entry != NULL) {
        llist_append(service->query_ptrs, entry);
      } else {
        ERROR(PLUGIN_NAME ": "
                          "Failed to allocate memory for a query list entry");
        goto error;
      }
    }
  }

  return 0;

error:
  /* Freeing libredfish resources & llists: */
  for (llentry_t *le = llist_head(ctx.services); le != NULL; le = le->next) {
    redfish_service_t *service = (redfish_service_t *)le->value;

    redfish_service_destroy(service);
  }

  return -ENOMEM;
}

/*******/

static int redfish_preconfig(void) {
  /* Creating placeholder for services: */
  ctx.services = llist_create();
  if (ctx.services == NULL)
    goto error;

  /* Creating placeholder for queries: */
  ctx.queries = c_avl_create((void *)strcmp);
  if (ctx.services == NULL)
    goto free_services;

  return 0;

free_services:
  llist_destroy(ctx.services);

error:
  ERROR(PLUGIN_NAME ": Failed to allocate memory for plugin context");
  return -ENOMEM;
}

/*******/

static int redfish_config_property(redfish_resource_t *resource,
                                   oconfig_item_t *cfg_item) {
  assert(resource != NULL);
  assert(cfg_item != NULL);

  redfish_property_t *property = calloc(1, sizeof(*property));
  /***/
  if (property == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for property");
    return -ENOMEM;
  }
  /***/
  property->type_inst_prefix_id = false;

  int ret = cf_util_get_string(cfg_item, &property->name);

  if (ret != 0) {
    ERROR(PLUGIN_NAME
          ": "
          "Could not get property argument in resource section named \"%s\"",
          resource->name);

    ret = -EINVAL;
    goto free_all;
  }

  for (int i = 0; i < cfg_item->children_num; i++) {
    oconfig_item_t *opt = cfg_item->children + i;

    if (strcasecmp("PluginInstance", opt->key) == 0) {
      ret = cf_util_get_string(opt, &(property->plugin_inst));
    } else if (strcasecmp("Type", opt->key) == 0) {
      ret = cf_util_get_string(opt, &(property->type));
    } else if (strcasecmp("TypeInstance", opt->key) == 0) {
      ret = cf_util_get_string(opt, &(property->type_inst));
    } else if (strcasecmp("TypeInstanceAttr", opt->key) == 0) {
      ret = cf_util_get_string(opt, &(property->type_inst_attr));
    } else if (strcasecmp("TypeInstancePrefixID", opt->key) == 0) {
      ret = cf_util_get_boolean(opt, &(property->type_inst_prefix_id));
    } else if (strcasecmp("SelectIDs", opt->key) == 0) {
      property->select_ids = calloc(opt->values_num, sizeof(uint64_t));
      /***/
      if (property->select_ids == NULL) {
        ret = -ENOMEM;
        goto free_all;
      }

      for (int j = 0; j < opt->values_num; j++) {
        property->select_ids[j] = ((uint64_t)(opt->values[j].value.number));

        (property->nb_select_ids)++;
      }
    } else if (strcasecmp("SelectAttrs", opt->key) == 0) {
      for (int j = 0; j < opt->values_num; j++) {
        strarray_add(&(property->select_attrs), &(property->nb_select_attrs),
                     opt->values[j].value.string);
      }

      ret = (((uint64_t)(opt->values_num) == property->nb_select_attrs)
                 ? 0
                 : -ENOMEM);
    } else if (strcasecmp("SelectAttrValue", opt->key) == 0) {
      /* If required, allocating the array storing the attribute/value
       * pairs for member selection: */
      if (property->select_attrvalues == NULL) {
        property->select_attrvalues = llist_create();
        /***/
        if (property->select_attrvalues == NULL) {
          ERROR(PLUGIN_NAME ": "
                            "Could not allocate memory for the name/value "
                            "list associated\nwith property \"%s\" in resource "
                            "\"%s\"",
                property->name, resource->name);

          ret = -ENOMEM;
          goto free_all;
        }
      }

      /* In order to get the attribute name and value: */
      char *name = NULL;
      char *value = NULL;

      /* Getting the attribute name: */
      name = strdup(opt->values[0].value.string);

      if (name == NULL) {
        ERROR(PLUGIN_NAME
              ": "
              "Could not parse the name of the name/value pair of an "
              "array member selection associated\nwith property \"%s\" "
              "in resource \"%s\"",
              property->name, resource->name);

        ret = -EINVAL;
        goto free_all;
      }

      /* Getting the attribute value: */
      value = strdup(opt->values[1].value.string);

      if (value == NULL) {
        ERROR(PLUGIN_NAME
              ": "
              "Could not parse the value of the name/value pair of an "
              "array member selection associated\nwith property \"%s\" "
              "in resource \"%s\"",
              property->name, resource->name);

        ret = -EINVAL;

        sfree(name);

        goto free_all;
      }

      /* Creating the entry associated with the considered name/value
       * pair: */
      llentry_t *entry_attrvalue = llentry_create(name, value);

      if (entry_attrvalue == NULL) {
        ERROR(PLUGIN_NAME
              ": "
              "Could not allocate memory for the list entry associated "
              "with the name/value pair of\nan array member selection "
              "associated\nwith property \"%s\" in resource \"%s\"",
              property->name, resource->name);

        ret = -ENOMEM;

        sfree(name);
        sfree(value);

        goto free_all;
      }

      /* Appending the newly created entry to the list of name/value pairs
       * associated with array member selection: */
      llist_append(property->select_attrvalues, entry_attrvalue);
    } else {
      ERROR(PLUGIN_NAME
            ": "
            "Invalid option \"%s\" in property \"%s\" in resource \"%s\"",
            opt->key, property->name, resource->name);

      ret = -EINVAL;
      goto free_all;
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME ": "
                        "Something went wrong going through fields in property "
                        "named \"%s\" in resource named \"%s\"",
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
  sfree(property->type_inst_attr);
  strarray_free(property->select_attrs, property->nb_select_attrs);
  sfree(property->select_ids);

  llentry_t *current = llist_head(property->select_attrvalues);
  /***/
  while (current != NULL) {
    sfree(current->key);
    sfree(current->value);

    current = current->next;
  }
  /***/
  llist_destroy(property->select_attrvalues);

  sfree(property);

  return ret;
}

/*******/

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
    goto redfish_config_resource_free_memory;

  int ret = cf_util_get_string(cfg_item, &resource->name);

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Could not get resource name for query named \"%s\"",
          query->name);

    goto redfish_config_resource_free_memory;
  }

  for (int i = 0; i < cfg_item->children_num; i++) {
    oconfig_item_t *opt = cfg_item->children + i;
    if (strcasecmp("Property", opt->key) != 0) {
      WARNING(PLUGIN_NAME ": "
                          "Invalid configuration option \"%s\".",
              opt->key);

      continue;
    }

    ret = redfish_config_property(resource, opt);

    if (ret != 0)
      goto redfish_config_resource_free_memory;
  }

  llentry_t *entry = llentry_create(resource->name, resource);
  if (entry == NULL) {
    ERROR(PLUGIN_NAME ": "
                      "Failed to allocate memory for resource list entry");

    goto redfish_config_resource_free_memory;
  }

  llist_append(query->resources, entry);

  return 0;

redfish_config_resource_free_memory:
  sfree(resource->name);
  llist_destroy(resource->properties);
  sfree(resource);

  return -1;
}

/*******/

static int redfish_config_attribute(redfish_query_t *query,
                                    oconfig_item_t *cfg_item) {
  assert(query != NULL);
  assert(cfg_item != NULL);

  redfish_attribute_t *attr = calloc(1, sizeof(*attr));

  if (attr == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for a query attribute");
    return -ENOMEM;
  }

  int ret = cf_util_get_string(cfg_item, &(attr->name));

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Could not get the name of an attribute for query "
                      "named \"%s\"",
          query->name);

    goto redfish_config_attribute_free_memory;
  }

  for (int i = 0; i < cfg_item->children_num; i++) {
    oconfig_item_t *opt = cfg_item->children + i;

    if (strcasecmp("PluginInstance", opt->key) == 0) {
      ret = cf_util_get_string(opt, &(attr->plugin_inst));
    } else if (strcasecmp("Type", opt->key) == 0) {
      ret = cf_util_get_string(opt, &(attr->type));
    } else if (strcasecmp("TypeInstance", opt->key) == 0) {
      ret = cf_util_get_string(opt, &(attr->type_inst));
    } else {
      ERROR(PLUGIN_NAME
            ": "
            "Invalid field \"%s\" in attribute \"%s\" of query \"%s\"",
            opt->key, attr->name, query->name);

      ret = -EINVAL;
      goto redfish_config_attribute_free_memory;
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME
            ": "
            "Something went wrong going through fields in attribute "
            "named \"%s\" in query named \"%s\"",
            attr->name, query->name);

      goto redfish_config_attribute_free_memory;
    }
  }

  llentry_t *entry = llentry_create(attr->name, attr);
  if (entry == NULL) {
    ERROR(PLUGIN_NAME ": "
                      "Failed to allocate memory for an attribute list entry");

    goto redfish_config_attribute_free_memory;
  }

  llist_append(query->attributes, entry);

  return 0;

redfish_config_attribute_free_memory:
  sfree(attr->name);
  sfree(attr->plugin_inst);
  sfree(attr->type);
  sfree(attr->type_inst);
  sfree(attr);

  return -1;
}

/*******/

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

  query->attributes = llist_create();

  if (query->attributes == NULL) {
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

    if (strcasecmp("Endpoint", opt->key) == 0) {
      ret = cf_util_get_string(opt, &query->endpoint);
    } else if (strcasecmp("Resource", opt->key) == 0) {
      ret = redfish_config_resource(query, opt);
    } else if (strcasecmp("Attribute", opt->key) == 0) {
      ret = redfish_config_attribute(query, opt);
    } else {
      ERROR(PLUGIN_NAME ": "
                        "Invalid configuration option \"%s\".",
            opt->key);

      ret = -EINVAL;
      goto free_all;
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME ": "
                        "Something went wrong processing query \"%s\"",
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
  llist_destroy(query->attributes);
  sfree(query);

  return ret;
}

/*******/

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

/*******/

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

    if (strcasecmp("Host", opt->key) == 0) {
      ret = cf_util_get_string(opt, &service->host);
    } else if (strcasecmp("User", opt->key) == 0) {
      ret = cf_util_get_string(opt, &service->user);
    } else if (strcasecmp("Passwd", opt->key) == 0) {
      ret = cf_util_get_string(opt, &service->passwd);
    } else if (strcasecmp("Token", opt->key) == 0) {
      ret = cf_util_get_string(opt, &service->token);
    } else if (strcasecmp("Queries", opt->key) == 0) {
      ret = redfish_read_queries(opt, &service->queries);
      service->queries_num = opt->values_num;
    } else {
      ERROR(PLUGIN_NAME ": "
                        "Invalid configuration option \"%s\".",
            opt->key);
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME
            ": "
            "Something went wrong processing the service named \"%s\"",
            service->name);

      goto free_service;
    }
  }

  llentry_t *entry = llentry_create(service->name, service);

  if (entry != NULL)
    llist_append(ctx.services, entry);
  else {
    ERROR(PLUGIN_NAME ": "
                      "Failed to create list for service name \"%s\"",
          service->name);

    goto free_service;
  }

  return 0;

free_service:
  redfish_service_destroy(service);
  return -1;
}

/*******/

static int redfish_config(oconfig_item_t *cfg_item) {
  int ret = redfish_preconfig();

  if (ret != 0)
    return ret;

  for (int i = 0; i < cfg_item->children_num; i++) {
    oconfig_item_t *child = cfg_item->children + i;

    if (strcasecmp("Query", child->key) == 0) {
      ret = redfish_config_query(child, ctx.queries);
    } else if (strcasecmp("Service", child->key) == 0) {
      ret = redfish_config_service(child);
    } else {
      ERROR(PLUGIN_NAME ": "
                        "Invalid configuration option \"%s\".",
            child->key);
    }

    if (ret != 0) {
      redfish_cleanup();

      return ret;
    }
  }

  return 0;
}

/*******/

static int redfish_validate_config(void) {
  /* Service validation */
  for (llentry_t *llserv = llist_head(ctx.services); llserv != NULL;
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
      ERROR(PLUGIN_NAME
            ": "
            "Service \"%s\" does not have user and/or password defined",
            service->name);

      return -EINVAL;
    }

    if ((service->user == NULL) && (service->token == NULL)) {
      ERROR(PLUGIN_NAME
            ": "
            "Service \"%s\" does not have an user/pass or token defined",
            service->name);

      return -EINVAL;
    }

    if (service->queries_num == 0) {
      WARNING(PLUGIN_NAME ": "
                          "Service \"%s\" does not have queries",
              service->name);
    }

    for (uint64_t i = 0; i < service->queries_num; i++) {
      redfish_query_t *query_query;

      bool found = false;
      char *key;
      c_avl_iterator_t *query_iter = c_avl_get_iterator(ctx.queries);

      while ((c_avl_iterator_next(query_iter, (void *)(&key),
                                  (void *)(&query_query)) == 0) &&
             !found) {
        if ((query_query->name != NULL) && (service->queries[i] != NULL) &&
            (strcmp(query_query->name, service->queries[i]) == 0)) {
          found = true;
        }
      }

      if (!found) {
        ERROR(PLUGIN_NAME ": "
                          "Query named \"%s\" in service \"%s\" not found",
              service->queries[i], service->name);

        c_avl_iterator_destroy(query_iter);

        return -EINVAL;
      }

      c_avl_iterator_destroy(query_iter);
    }
  }

  c_avl_iterator_t *queries_iter = c_avl_get_iterator(ctx.queries);
  char *key;
  redfish_query_t *query;

  /* Query validation */
  while (c_avl_iterator_next(queries_iter, (void *)(&key), (void *)(&query)) ==
         0) {
    if (query->name == NULL) {
      ERROR(PLUGIN_NAME ": A query does not have a name");
      goto error;
    }

    if (query->endpoint == NULL) {
      ERROR(PLUGIN_NAME ": "
                        "Query \"%s\" does not have a valid endpoint",
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
          ERROR(PLUGIN_NAME
                ": "
                "A plugin instance is not defined in property \"%s\" "
                "in query \"%s\"",
                prop->name, query->name);

          goto error;
        }

        if (prop->type == NULL) {
          ERROR(PLUGIN_NAME ": "
                            "Type is not defined in property \"%s\" in query "
                            "\"%s\"",
                prop->name, query->name);

          goto error;
        }
      }
    }

    for (llentry_t *llres = llist_head(query->attributes); llres != NULL;
         llres = llres->next) {
      redfish_attribute_t *attr = (redfish_attribute_t *)llres->value;

      /* Attribute validation: */
      if (attr->name == NULL) {
        ERROR(PLUGIN_NAME ": An attribute in query \"%s\" is not named",
              query->name);

        goto error;
      }

      if (attr->plugin_inst == NULL) {
        ERROR(PLUGIN_NAME
              ": "
              "A plugin instance is not defined in attribute \"%s\" "
              "of query \"%s\"",
              attr->name, query->name);

        goto error;
      }

      if (attr->type == NULL) {
        ERROR(PLUGIN_NAME ": "
                          "Type is not defined in attribute \"%s\" in query "
                          "\"%s\"",
              attr->name, query->name);

        goto error;
      }
    }
  }

  c_avl_iterator_destroy(queries_iter);

  return 0;

error:
  c_avl_iterator_destroy(queries_iter);

  return -EINVAL;
}

/*******/

static int redfish_convert_val(redfish_value_t *value,
                               redfish_value_type_t src_type, value_t *vl,
                               int dst_type) {
  switch (dst_type) {
  case DS_TYPE_GAUGE:
    if (src_type == VAL_TYPE_STR) {
      vl->gauge = strtod(value->string, NULL);
    } else if (src_type == VAL_TYPE_INT) {
      vl->gauge = (gauge_t)value->integer;
    } else if (src_type == VAL_TYPE_REAL) {
      vl->gauge = value->real;
    }

    break;

  case DS_TYPE_DERIVE:
    if (src_type == VAL_TYPE_STR) {
      vl->derive = strtoll(value->string, NULL, 0);
    } else if (src_type == VAL_TYPE_INT) {
      vl->derive = (derive_t)value->integer;
    } else if (src_type == VAL_TYPE_REAL) {
      vl->derive = (derive_t)value->real;
    }

    break;

  case DS_TYPE_COUNTER:
    if (src_type == VAL_TYPE_STR) {
      vl->derive = strtoull(value->string, NULL, 0);
    } else if (src_type == VAL_TYPE_INT) {
      vl->derive = (derive_t)value->integer;
    } else if (src_type == VAL_TYPE_REAL) {
      vl->derive = (derive_t)value->real;
    }

    break;

  case DS_TYPE_ABSOLUTE:
    if (src_type == VAL_TYPE_STR) {
      vl->absolute = strtoull(value->string, NULL, 0);
    } else if (src_type == VAL_TYPE_INT) {
      vl->absolute = (absolute_t)value->integer;
    } else if (src_type == VAL_TYPE_REAL) {
      vl->absolute = (absolute_t)value->real;
    }

    break;

  default:
    ERROR(PLUGIN_NAME ": Invalid data set type. Cannot convert value");

    return -EINVAL;
  }

  return 0;
}

/*******/

static int redfish_json_get_string(char *const value, const size_t value_len,
                                   const json_t *const json) {
  if (json_is_string(json)) {
    const char *str_val = json_string_value(json);
    sstrncpy(value, str_val, value_len);

    return 0;
  } else if (json_is_integer(json)) {
    snprintf(value, value_len, "%d", (int)json_integer_value(json));
    return 0;
  }

  ERROR(PLUGIN_NAME ": Expected JSON value to be a string or an integer");

  return -EINVAL;
}

/*******/

static void redfish_process_payload_attribute(
    const redfish_attribute_t *attr, const json_t *json_payload,
    const redfish_query_t *query, const redfish_service_t *service) {
  json_t *json_attr = json_object_get(json_payload, attr->name);

  if (json_attr == NULL) {
    ERROR(PLUGIN_NAME
          ": Could not find the attribute \"%s\" in the payload associated "
          "with the query \"%s\"",
          attr->name, query->name);

    return;
  }

  value_list_t v1 = VALUE_LIST_INIT;
  v1.values_len = 1;

  if (attr->plugin_inst != NULL) {
    sstrncpy(v1.plugin_instance, attr->plugin_inst, sizeof(v1.plugin_instance));
  }

  /* If the "TypeInstance" was not specified, then the name of the attribute
   * is used as default value: */
  sstrncpy(v1.type_instance,
           ((attr->type_inst != NULL) ? attr->type_inst : attr->name),
           sizeof(v1.type_instance));

  /* Determining the type of the content associated with the attribute: */
  redfish_value_t redfish_value;
  redfish_value_type_t type = VAL_TYPE_STR;

  if (json_is_string(json_attr)) {
    redfish_value.string = (char *)json_string_value(json_attr);
  } else if (json_is_integer(json_attr)) {
    type = VAL_TYPE_INT;
    redfish_value.integer = json_integer_value(json_attr);
  } else if (json_is_real(json_attr)) {
    type = VAL_TYPE_REAL;
    redfish_value.real = json_real_value(json_attr);
  }

#if !defined(REDFISH_PLUGIN_TEST)
  const data_set_t *ds = plugin_get_ds(attr->type);
#else
  const data_set_t *ds = redfish_test_plugin_get_ds_mock(attr->type);
#endif

  /* Checking if the collectd type associated with the attribute exists: */
  if (ds == NULL)
    return;

  value_t values = {0};
  v1.values = &values;
  redfish_convert_val(&redfish_value, type, v1.values, ds->ds[0].type);

  sstrncpy(v1.host, service->name, sizeof(v1.host));
  sstrncpy(v1.plugin, PLUGIN_NAME, sizeof(v1.plugin));
  sstrncpy(v1.type, attr->type, sizeof(v1.type));

#if !defined(REDFISH_PLUGIN_TEST)
  plugin_dispatch_values(&v1);
#else
  redfish_test_plugin_dispatch_values_mock(&v1);
#endif

  /* Clear values assigned in case of leakage */
  v1.values = NULL;
  v1.values_len = 0;
}

/*******/

static void redfish_process_payload_object(const redfish_property_t *prop,
                                           const json_t *json_object,
                                           const uint64_t json_object_id,
                                           const redfish_resource_t *res,
                                           const redfish_service_t *serv) {
  json_t *json_property = json_object_get(json_object, prop->name);

  if (json_property == NULL) {
    ERROR(PLUGIN_NAME
          ": Failure retreiving property \"%s\" from resource \"%s\"",
          prop->name, res->name);

    return;
  }

  value_list_t v1 = VALUE_LIST_INIT;
  v1.values_len = 1;

  if (prop->plugin_inst != NULL) {
    sstrncpy(v1.plugin_instance, prop->plugin_inst, sizeof(v1.plugin_instance));
  }

  /* Determining the "TypeInstance" of the metric in collectd and storing it
   * in a temporary string, in order to be able to later prefix it with the ID
   * of the considered member of the array. */
  char type_inst[DATA_MAX_NAME_LEN] = {0};

  /* First alternative - it was specified in the configuration file of
   * collectd: */
  if (prop->type_inst != NULL) {
    sstrncpy(type_inst, prop->type_inst, sizeof(type_inst));
  } else if (prop->type_inst_attr != NULL) {
    /* Second alternative - the name of a property of the target JSON object
     * which content should be used as "TypeInstance" was specified in the
     * configuration file of collectd through "TypeInstanceAttr":
     * NB: "tia" stands for "TypeInstanceAttr".*/
    json_t *json_tia = json_object_get(json_object, prop->type_inst_attr);

    if (json_tia == NULL) {
      ERROR(PLUGIN_NAME
            ": "
            "Could not find the property \"%s\" which was specified as the "
            "\"TypeInstanceAttr\"\nof the target property \"%s\" in the "
            "resource \"%s\".",
            prop->type_inst_attr, prop->name, res->name);

      return;
    }

    int ret = redfish_json_get_string(type_inst, sizeof(type_inst), json_tia);

    if (ret != 0) {
      ERROR(PLUGIN_NAME
            ": Could not convert the content of the \"%s\" property to a "
            "type instance.",
            prop->type_inst_attr);

      return;
    }
  } else {
    /* Last alternative - if the target JSON object contains a property
     * named "Name", it should be used as the "TypeInstance" of the metric
     * in collectd: */
    json_t *json_name = json_object_get(json_object, "Name");

    if (json_name == NULL) {
      ERROR(PLUGIN_NAME
            ": "
            "Neither \"TypeInstance\" nor \"TypeInstanceAttr\" "
            "specified.\n"
            "Failed to get \"Name\" property associated with the target "
            "property \"%s\" in resource \"%s\".",
            prop->name, res->name);

      return;
    }

    int ret = redfish_json_get_string(type_inst, sizeof(type_inst), json_name);

    if (ret != 0) {
      ERROR(PLUGIN_NAME
            ": "
            "Could not convert the \"Name\" attribute of the property "
            "\"%s\" property of the \"%s\" resource to a type instance.",
            prop->name, res->name);

      return;
    }
  }

  /* If required, prefixing the "TypeInstance" by the ID of the considered
   * member of the array: */
  if (prop->type_inst_prefix_id) {
    uint64_t nb_written_chars =
        snprintf(v1.type_instance, sizeof(v1.type_instance), "%lu-%s",
                 json_object_id, type_inst);

    if (nb_written_chars > sizeof(v1.type_instance)) {
      WARNING(PLUGIN_NAME
              " - property \"%s\" of the \"%s\" resource :\n"
              "The \"TypeInstance\" generated by ID prefixing was longer "
              "than the maximum number of characters, and was thus"
              "truncated.",
              prop->name, res->name);
    }
  } else {
    /* No need for length checking since "v1.type_instance" and
     * "type_instance" have the same length: */
    sstrncpy(v1.type_instance, type_inst, sizeof(v1.type_instance));
  }

  /* Determining the type of the value of the monitored metric, which is the
   * type of the associated JSON property: */
  redfish_value_t value;
  redfish_value_type_t type = VAL_TYPE_STR;

  if (json_is_string(json_property)) {
    value.string = (char *)json_string_value(json_property);
  } else if (json_is_integer(json_property)) {
    type = VAL_TYPE_INT;
    value.integer = json_integer_value(json_property);
  } else if (json_is_real(json_property)) {
    type = VAL_TYPE_REAL;
    value.real = json_real_value(json_property);
  }

#if !defined(REDFISH_PLUGIN_TEST)
  const data_set_t *ds = plugin_get_ds(prop->type);
#else
  const data_set_t *ds = redfish_test_plugin_get_ds_mock(prop->type);
#endif

  /* Check if data set found */
  if (ds == NULL)
    return;

  value_t tmp = {0};
  v1.values = &tmp;
  redfish_convert_val(&value, type, v1.values, ds->ds[0].type);

  sstrncpy(v1.host, serv->name, sizeof(v1.host));
  sstrncpy(v1.plugin, PLUGIN_NAME, sizeof(v1.plugin));
  sstrncpy(v1.type, prop->type, sizeof(v1.type));

#if !defined(REDFISH_PLUGIN_TEST)
  plugin_dispatch_values(&v1);
#else
  redfish_test_plugin_dispatch_values_mock(&v1);
#endif

  /* Clear values assigned in case of leakage */
  v1.values = NULL;
  v1.values_len = 0;
}

/*******/

static void redfish_process_payload_resource_property(
    const redfish_property_t *prop, const json_t *json_resource,
    const redfish_resource_t *res, const redfish_service_t *serv) {
  /* Testing if the resource to process is an array, or if it is an object: */
  if (json_array_size(json_resource) == 0) {
    /* It is an object. */
    redfish_process_payload_object(prop, json_resource, 0, res, serv);
  } else {
    /* It is an array. */
    /* Iterating through an array of objects: */
    for (uint64_t i = 0; i < json_array_size(json_resource); i++) {
      /* Checking if an ID-based member selection should be performed: */
      if ((prop->select_ids != NULL) && (prop->nb_select_ids > 0)) {
        /* Roaming all the specified IDs to determine whether or not the
         * currently considered one is among them: */
        bool id_selected = false;
        /***/
        for (uint64_t j = 0; j < prop->nb_select_ids; j++) {
          if (i == (prop->select_ids)[j]) {
            id_selected = true;
            break;
          }
        }
        /***/
        if (!id_selected)
          continue;
      }

      json_t *json_object = json_array_get(json_resource, i);

      if (json_object == NULL) {
        ERROR(PLUGIN_NAME ": Failure retrieving array member for "
                          "resource \"%s\"",
              res->name);

        continue;
      }

      /* Checking if an attribute-based member selection should be
       * performed: */
      if ((prop->select_attrs != NULL) && (prop->nb_select_attrs > 0)) {
        /* Roaming all the specified attributes to determine whether or
         * not the currently considered member defines the former: */
        bool member_selected = true;
        /***/
        for (uint64_t j = 0; j < prop->nb_select_attrs; j++) {
          json_t *json_attr =
              json_object_get(json_object, prop->select_attrs[j]);

          if (json_attr == NULL) {
            member_selected = false;
            break;
          }
        }
        /***/
        if (!member_selected)
          continue;
      }

      /* Checking if the members of the considered array should be
       * filtered according to the values of some of their attributes: */
      if (prop->select_attrvalues != NULL) {
        bool member_selected = true;

        /* Roaming all the selection/filtering criteria: */
        for (llentry_t *attrvalue = llist_head(prop->select_attrvalues);
             attrvalue != NULL; attrvalue = attrvalue->next) {
          /* Getting the JSON object associated with the considered
           * attribute: */
          json_t *json_attr = json_object_get(json_object, attrvalue->key);
          /***/
          if (json_attr == NULL) {
            member_selected = false;
            break;
          }

          /* Getting the value associated with the considered
           * attribute: */
          char attrvalue_value[DATA_MAX_NAME_LEN] = {0};
          /***/
          int ret = redfish_json_get_string(attrvalue_value,
                                            sizeof(attrvalue_value), json_attr);
          /***/
          if (ret != 0) {
            WARNING(PLUGIN_NAME ": Could not convert the content of the \"%s\" "
                                "attribute to a string for property \"%s\".",
                    attrvalue->key, prop->name);

            member_selected = false;
            break;
          }

          /* Checking if the attribute as the proper value: */
          if (strcmp(attrvalue_value, attrvalue->value) != 0) {
            member_selected = false;
            break;
          }
        }

        if (!member_selected)
          continue;
      }

      redfish_process_payload_object(prop, json_object, i, res, serv);
    }
  }
}

/*******/

static void redfish_process_payload(bool success, unsigned short http_code,
                                    redfishPayload *payload, void *context) {
  redfish_job_t *job = (redfish_job_t *)context;

  if (!success) {
    WARNING(PLUGIN_NAME ": Query has failed, HTTP code = %u\n", http_code);
#if !defined(REDFISH_PLUGIN_TEST)
    goto free_job;
#else
    return;
#endif
  }

  redfish_service_t *serv = job->service_query->service;

  if (!payload) {
    WARNING(PLUGIN_NAME ": Failed to get payload for service name \"%s\"",
            serv->name);
#if !defined(REDFISH_PLUGIN_TEST)
    goto free_job;
#else
    return;
#endif
  }

  for (llentry_t *llres = llist_head(job->service_query->query->resources);
       llres != NULL; llres = llres->next) {
    redfish_resource_t *res = (redfish_resource_t *)llres->value;
    json_t *json_resource = json_object_get(payload->json, res->name);

    if (json_resource == NULL) {
      WARNING(PLUGIN_NAME ": Could not find resource \"%s\"", res->name);
      continue;
    }

    for (llentry_t *llprop = llist_head(res->properties); llprop != NULL;
         llprop = llprop->next) {
      redfish_property_t *prop = (redfish_property_t *)llprop->value;

      redfish_process_payload_resource_property(prop, json_resource, res, serv);
    }
  }

  for (llentry_t *llattr = llist_head(job->service_query->query->attributes);
       llattr != NULL; llattr = llattr->next) {
    redfish_attribute_t *attr = (redfish_attribute_t *)(llattr->value);

    redfish_process_payload_attribute(attr, payload->json,
                                      job->service_query->query, serv);
  }

#if !defined(REDFISH_PLUGIN_TEST)
free_job:
  cleanupPayload(payload);
  redfish_job_destroy(job);
#endif
}

/*******/

static void *redfish_worker_thread(void *__attribute__((unused)) args) {
  INFO(PLUGIN_NAME ": Worker is running");

  for (;;) {
    usleep(10);

    if (DEQ_IS_EMPTY(ctx.jobs))
      continue;

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    redfish_job_t *job = DEQ_HEAD(ctx.jobs);

    getPayloadByPathAsync(job->service_query->service->redfish,
                          job->service_query->query->endpoint, NULL,
                          redfish_process_payload, job);

    DEQ_REMOVE_HEAD(ctx.jobs);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  }

  pthread_exit(NULL);

  return NULL;
}

/*******/

static int redfish_read(__attribute__((unused)) user_data_t *ud) {
  for (llentry_t *le = llist_head(ctx.services); le != NULL; le = le->next) {
    redfish_service_t *service = (redfish_service_t *)le->value;

    for (llentry_t *le = llist_head(service->query_ptrs); le != NULL;
         le = le->next) {
      redfish_query_t *query = (redfish_query_t *)le->value;
      redfish_job_t *job = calloc(1, sizeof(*job));

      if (job == NULL) {
        WARNING(PLUGIN_NAME ": Failed to allocate memory for task");
        continue;
      }

      DEQ_ITEM_INIT(job);

      redfish_payload_ctx_t *serv_res = calloc(1, sizeof(*serv_res));

      if (serv_res == NULL) {
        WARNING(PLUGIN_NAME ": "
                            "Failed to allocate memory for task's context");
        sfree(job);

        continue;
      }

      serv_res->query = query;
      serv_res->service = service;
      job->service_query = serv_res;

      DEQ_INSERT_TAIL(ctx.jobs, job);
    }
  }

  return 0;
}

/*******/

static void redfish_destroy_property(redfish_property_t *property) {
  /* Freeing the fields of the considered property: */
  sfree(property->name);
  sfree(property->plugin_inst);
  sfree(property->type);
  sfree(property->type_inst);
  sfree(property->type_inst_attr);
  strarray_free(property->select_attrs, property->nb_select_attrs);
  sfree(property->select_ids);

  llentry_t *current = llist_head(property->select_attrvalues);
  /***/
  while (current != NULL) {
    sfree(current->key);
    sfree(current->value);

    current = current->next;
  }
  /***/
  llist_destroy(property->select_attrvalues);

  /* Freeing the property itself: */
  sfree(property);
}

/*******/

static void redfish_destroy_resource(redfish_resource_t *resource) {
  /* Roaming all the properties of the considered resource: */
  for (llentry_t *le = llist_head(resource->properties); le != NULL;
       le = le->next) {
    /* Getting the considered property: */
    redfish_property_t *property = (redfish_property_t *)le->value;

    /* Freeing the considered property: */
    redfish_destroy_property(property);
  }

  /* Freeing the fields of the resource and the resource itself: */
  sfree(resource->name);
  llist_destroy(resource->properties);
  sfree(resource);
}

/*******/

static void redfish_destroy_attribute(redfish_attribute_t *attribute) {
  /* Freeing the fields of the attribute: */
  sfree(attribute->name);
  sfree(attribute->plugin_inst);
  sfree(attribute->type);
  sfree(attribute->type_inst);

  /* Freeing the attributeibute itself: */
  sfree(attribute);
}

/*******/

static void redfish_destroy_query(redfish_query_t *query) {
  /* Roaming all the resources of the considered query: */
  for (llentry_t *le = llist_head(query->resources); le != NULL;
       le = le->next) {
    /* Getting the considered resource: */
    redfish_resource_t *resource = (redfish_resource_t *)le->value;

    /* Freeing the considered resource: */
    redfish_destroy_resource(resource);
  }

  /* Roaming all the attribute of the considered query: */
  for (llentry_t *le = llist_head(query->attributes); le != NULL;
       le = le->next) {
    /* Getting the considered attribute: */
    redfish_attribute_t *attribute = (redfish_attribute_t *)(le->value);

    /* Freeing the considered attribute: */
    redfish_destroy_attribute(attribute);
  }

  /* Freeing the fields of the query, and the query itself: */
  sfree(query->name);
  sfree(query->endpoint);
  llist_destroy(query->resources);
  llist_destroy(query->attributes);
  sfree(query);
}

/*******/

static int redfish_cleanup(void) {
#if !defined(REDFISH_PLUGIN_TEST)
  INFO(PLUGIN_NAME ": Cleaning up");

  /* Shutting down the worker thread, if it was spawned: */
  if (ctx.worker_thread != 0) {
    if (pthread_cancel(ctx.worker_thread) != 0) {
      ERROR(PLUGIN_NAME ": Failed to cancel the worker thread");
    }

    if (pthread_join(ctx.worker_thread, NULL) != 0) {
      ERROR(PLUGIN_NAME ": Failed to join the worker thread");
    }
  }

  /* Cleaning worker's queue: */
  while (!DEQ_IS_EMPTY(ctx.jobs)) {
    redfish_job_t *job = DEQ_HEAD(ctx.jobs);
    DEQ_REMOVE_HEAD(ctx.jobs);
    redfish_job_destroy(job);
  }
#endif

  /* Roaming all the services to destroy them: */
  for (llentry_t *le = llist_head(ctx.services); le; le = le->next) {
    redfish_service_t *service = (redfish_service_t *)le->value;
    redfish_service_destroy(service);
  }

  /* Destroying the list of services itself: */
  llist_destroy(ctx.services);

  /* Roaming all the queries to destroy them: */
  c_avl_iterator_t *i = c_avl_get_iterator(ctx.queries);
  /***/
  char *key;
  redfish_query_t *query;
  /***/
  while (c_avl_iterator_next(i, (void *)(&key), (void *)(&query)) == 0) {
    redfish_destroy_query(query);
  }
  /***/
  c_avl_iterator_destroy(i);

  /* Destroying the AVL tree storing the queries itself: */
  c_avl_destroy(ctx.queries);

  return 0;
}

/*******/

void module_register(void) {
  plugin_register_init(PLUGIN_NAME, redfish_init);
  plugin_register_complex_config(PLUGIN_NAME, redfish_config);
  plugin_register_complex_read(NULL, PLUGIN_NAME, redfish_read, 0, NULL);
  plugin_register_shutdown(PLUGIN_NAME, redfish_cleanup);
}
