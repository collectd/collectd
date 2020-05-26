/**
 * collectd - src/redfish_test.c
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
 *   Martin Kennelly <martin.kennelly@intel.com>
 *   Marcin Mozejko <marcinx.mozejko@intel.com>
 *   Adrian Boczkowski <adrianx.boczkowski@intel.com>
 **/

#define plugin_dispatch_values redfish_test_plugin_dispatch_values_mock
#include "redfish.c"
#include "testing.h"

#define VALUE_CACHE_SIZE (1)

static value_list_t last_dispatched_value_list;
static value_t last_dispatched_values[VALUE_CACHE_SIZE];
int redfish_test_plugin_dispatch_values_mock(value_list_t const *vl) {
  last_dispatched_value_list = *vl;
  size_t len = MIN(vl->values_len, VALUE_CACHE_SIZE);
  for (size_t i = 0; i < len; ++i) {
    last_dispatched_values[i] = vl->values[i];
  }
  last_dispatched_value_list.values = last_dispatched_values;
  return 0;
}

static value_list_t *redfish_test_get_last_dispatched_value_list() {
  return &last_dispatched_value_list;
}

DEF_TEST(read_queries) {
  oconfig_item_t *ci = calloc(1, sizeof(*ci));

  assert(ci != NULL);
  ci->values = calloc(3, sizeof(*ci->values));
  assert(ci->values != NULL);
  ci->values_num = 3;
  ci->values[0].value.string = "temperatures";
  ci->values[0].type = OCONFIG_TYPE_STRING;
  ci->values[1].value.string = "fans";
  ci->values[1].type = OCONFIG_TYPE_STRING;
  ci->values[2].value.string = "power";
  ci->values[2].type = OCONFIG_TYPE_STRING;

  char **queries;
  int ret = redfish_read_queries(ci, &queries);

  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("temperatures", queries[0]);
  EXPECT_EQ_STR("fans", queries[1]);
  EXPECT_EQ_STR("power", queries[2]);

  sfree(ci->values);
  sfree(ci);

  for (int j = 0; j < 3; j++)
    sfree(queries[j]);
  sfree(queries);
  return 0;
}

DEF_TEST(convert_val) {
  redfish_value_t val = {.string = "1"};
  redfish_value_type_t src_type = VAL_TYPE_STR;
  int dst_type = DS_TYPE_GAUGE;
  value_t vl = {0};
  int ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.gauge == 1.0);

  val.integer = 1;
  src_type = VAL_TYPE_INT;
  dst_type = DS_TYPE_GAUGE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.gauge == 1.0);

  val.real = 1.0;
  src_type = VAL_TYPE_REAL;
  dst_type = DS_TYPE_GAUGE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.gauge == 1.0);

  val.string = "-1";
  src_type = VAL_TYPE_STR;
  dst_type = DS_TYPE_DERIVE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.derive == -1);

  val.integer = -1;
  src_type = VAL_TYPE_INT;
  dst_type = DS_TYPE_DERIVE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.derive == -1);

  val.real = -1.0;
  src_type = VAL_TYPE_REAL;
  dst_type = DS_TYPE_DERIVE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.derive == -1);

  val.string = "1";
  src_type = VAL_TYPE_STR;
  dst_type = DS_TYPE_COUNTER;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.counter == 1);

  val.integer = 1;
  src_type = VAL_TYPE_INT;
  dst_type = DS_TYPE_COUNTER;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.counter == 1);

  val.real = 1.0;
  src_type = VAL_TYPE_REAL;
  dst_type = DS_TYPE_COUNTER;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.counter == 1);

  val.string = "1";
  src_type = VAL_TYPE_STR;
  dst_type = DS_TYPE_ABSOLUTE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.absolute == 1);

  val.integer = 1;
  src_type = VAL_TYPE_INT;
  dst_type = DS_TYPE_ABSOLUTE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.absolute == 1);

  val.real = 1.0;
  src_type = VAL_TYPE_REAL;
  dst_type = DS_TYPE_ABSOLUTE;
  ret = redfish_convert_val(&val, src_type, &vl, dst_type);
  EXPECT_EQ_INT(0, ret);
  OK(vl.absolute == 1);

  return 0;
}

/* Testing allocation of memory for ctx struct. Creation of services list
 * & queries avl tree */
DEF_TEST(redfish_preconfig) {
  int ret = redfish_preconfig();

  EXPECT_EQ_INT(0, ret);
  CHECK_NOT_NULL(ctx.queries);
  CHECK_NOT_NULL(ctx.services);

  llist_destroy(ctx.services);
  c_avl_destroy(ctx.queries);

  return 0;
}

/* Testing correct input of properties from conf file */
DEF_TEST(config_property) {
  redfish_resource_t *resource = calloc(1, sizeof(*resource));
  assert(resource != NULL);
  resource->name = "test property";
  resource->properties = llist_create();
  oconfig_item_t *ci = calloc(1, sizeof(*ci));

  assert(ci != NULL);
  ci->values_num = 1;
  ci->values = calloc(1, sizeof(*ci->values));
  ci->values[0].type = OCONFIG_TYPE_STRING;
  ci->values[0].value.string = "ReadingRPM";

  ci->children_num = 3;
  ci->children = calloc(1, sizeof(*ci->children) * ci->children_num);

  ci->children[0].key = "PluginInstance";
  ci->children[0].parent = ci;
  ci->children[0].values = calloc(1, sizeof(*ci->children[0].values));
  assert(ci->children[0].values != NULL);
  ci->children[0].values_num = 1;
  ci->children[0].values->value.string = "chassis1";
  ci->children[0].values->type = OCONFIG_TYPE_STRING;

  ci->children[1].key = "Type";
  ci->children[1].parent = ci;
  ci->children[1].values = calloc(1, sizeof(*ci->children[1].values));
  assert(ci->children[1].values != NULL);
  ci->children[1].values_num = 1;
  ci->children[1].values->value.string = "degrees";
  ci->children[1].values->type = OCONFIG_TYPE_STRING;

  ci->children[2].key = "TypeInstance";
  ci->children[2].parent = ci;
  ci->children[2].values = calloc(1, sizeof(*ci->children[2].values));
  assert(ci->children[2].values != NULL);
  ci->children[2].values_num = 1;
  ci->children[2].values->value.string = "0";
  ci->children[2].values->type = OCONFIG_TYPE_STRING;

  int ret = redfish_config_property(resource, ci);

  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, llist_size(resource->properties));

  sfree(ci->children[0].values);
  sfree(ci->children[1].values);
  sfree(ci->children[2].values);

  sfree(ci->children);
  sfree(ci->values);
  sfree(ci);

  for (llentry_t *llprop = llist_head(resource->properties); llprop != NULL;
       llprop = llprop->next) {
    redfish_property_t *property = (redfish_property_t *)llprop->value;
    sfree(property->name);
    sfree(property->plugin_inst);
    sfree(property->type);
    sfree(property->type_inst);
    sfree(property);
  }
  llist_destroy(resource->properties);
  free(resource);
  return 0;
}

DEF_TEST(config_resource) {
  oconfig_item_t *ci = calloc(1, sizeof(*ci));
  assert(ci != NULL);

  ci->values_num = 1;
  ci->values = calloc(1, sizeof(*ci->values));
  assert(ci->values != NULL);
  ci->values[0].value.string = "Temperatures";

  ci->children_num = 1;
  ci->children = calloc(1, sizeof(*ci->children));
  assert(ci->children != NULL);
  ci->children[0].children_num = 1;
  ci->children[0].parent = ci;
  ci->children[0].key = "Property";
  ci->children[0].values_num = 1;
  ci->children[0].values = calloc(1, sizeof(*ci->children[0].values));
  ci->children[0].values->value.string = "ReadingRPM";
  assert(ci->children[0].values != NULL);

  oconfig_item_t *ci_prop = calloc(1, sizeof(*ci_prop));
  assert(ci_prop != NULL);

  ci->children[0].children = ci_prop;
  ci->children_num = 1;

  ci_prop->key = "PluginInstance";
  ci_prop->parent = ci;
  ci_prop->values_num = 1;
  ci_prop->values = calloc(1, sizeof(*ci_prop->values));
  assert(ci_prop->values != NULL);
  ci_prop->values[0].type = OCONFIG_TYPE_STRING;
  ci_prop->values[0].value.string = "chassis-1";

  redfish_query_t *query = calloc(1, sizeof(*query));
  query->endpoint = "/redfish/v1/Chassis/Chassis-1/Thermal";
  query->name = "fans";
  query->resources = llist_create();

  int ret = redfish_config_resource(query, ci);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, llist_size(query->resources));

  sfree(ci_prop->values);
  sfree(ci_prop);

  sfree(ci->values);
  sfree(ci->children[0].values);
  sfree(ci->children);
  sfree(ci);

  for (llentry_t *llres = llist_head(query->resources); llres != NULL;
       llres = llres->next) {
    redfish_resource_t *resource = (redfish_resource_t *)llres->value;
    for (llentry_t *llprop = llist_head(resource->properties); llprop != NULL;
         llprop = llprop->next) {
      redfish_property_t *property = (redfish_property_t *)llprop->value;
      sfree(property->name);
      sfree(property->plugin_inst);
      sfree(property->type);
      sfree(property->type_inst);
      free(property);
    }
    llist_destroy(resource->properties);
    free(resource->name);
    free(resource);
  }
  llist_destroy(query->resources);
  sfree(query);
  return 0;
}

DEF_TEST(config_query) {
  oconfig_item_t *qci = calloc(1, sizeof(*qci));
  assert(qci != NULL);
  qci->key = "Query";
  qci->values_num = 1;
  qci->values = calloc(1, sizeof(*qci->values));
  assert(qci->values != NULL);
  qci->values->type = OCONFIG_TYPE_STRING;
  qci->values->value.string = "fans";

  qci->children_num = 2;
  qci->children = calloc(1, sizeof(*qci->children) * qci->children_num);
  assert(qci->children != NULL);

  qci->children[0].key = "Endpoint";
  qci->children[0].values = calloc(1, sizeof(*qci->children[0].values));
  assert(qci->children[0].values != NULL);
  qci->children[0].values->type = OCONFIG_TYPE_STRING;
  qci->children[0].values_num = 1;
  qci->children[0].values->value.string =
      "/redfish/v1/Chassis/Chassis-1/Thermal";

  qci->children[1].key = "Resource";
  qci->children[1].values_num = 1;
  qci->children[1].values = calloc(1, sizeof(*qci->children[1].values));
  assert(qci->children[1].values != NULL);
  qci->children[1].values->type = OCONFIG_TYPE_STRING;
  qci->children[1].values->value.string = "Temperature";
  qci->children[1].children_num = 1;

  oconfig_item_t *ci = calloc(1, sizeof(*ci));
  assert(ci != NULL);

  qci->children[1].children = ci;

  ci->key = "Property";
  ci->values_num = 1;
  ci->values = calloc(1, sizeof(*ci->values));
  assert(ci->values != NULL);
  ci->values->type = OCONFIG_TYPE_STRING;
  ci->values->value.string = "ReadingRPM";

  oconfig_item_t *ci_prop = calloc(1, sizeof(*ci_prop));
  assert(ci_prop != NULL);

  ci->children = ci_prop;
  ci->children_num = 1;

  ci_prop->key = "PluginInstance";
  ci_prop->parent = ci;
  ci_prop->values_num = 1;
  ci_prop->values = calloc(1, sizeof(*ci_prop->values));
  assert(ci_prop->values != NULL);
  ci_prop->values[0].type = OCONFIG_TYPE_STRING;
  ci_prop->values[0].value.string = "chassis-1";

  c_avl_tree_t *queries =
      c_avl_create((int (*)(const void *, const void *))strcmp);

  int ret = redfish_config_query(qci, queries);
  EXPECT_EQ_INT(0, ret);

  sfree(ci_prop->values);
  sfree(ci_prop);

  sfree(ci->values);
  sfree(ci);
  sfree(qci->children[0].values);
  sfree(qci->children[1].values);
  sfree(qci->children);
  sfree(qci->values);
  sfree(qci);

  redfish_query_t *query;
  char *key;
  c_avl_iterator_t *query_iter = c_avl_get_iterator(queries);
  while (c_avl_iterator_next(query_iter, (void **)&key, (void **)&query) == 0) {
    for (llentry_t *llres = llist_head(query->resources); llres != NULL;
         llres = llres->next) {
      redfish_resource_t *resource = (redfish_resource_t *)llres->value;
      for (llentry_t *llprop = llist_head(resource->properties); llprop != NULL;
           llprop = llprop->next) {
        redfish_property_t *property = (redfish_property_t *)llprop->value;
        sfree(property->name);
        sfree(property->plugin_inst);
        sfree(property->type);
        sfree(property->type_inst);
        sfree(property);
      }
      llist_destroy(resource->properties);
      sfree(resource->name);
      sfree(resource);
    }
    llist_destroy(query->resources);
    sfree(query->name);
    sfree(query->endpoint);
  }
  sfree(query_iter);
  c_avl_destroy(queries);
  return 0;
}

DEF_TEST(config_service) {
  oconfig_item_t *ci = calloc(1, sizeof(*ci));
  assert(ci != NULL);
  ci->key = "Service";
  ci->values_num = 1;
  ci->values = calloc(1, sizeof(*ci->values));
  ci->values->type = OCONFIG_TYPE_STRING;
  ci->values->value.string = "Server 5";
  ci->children_num = 4;
  ci->children = calloc(1, sizeof(*ci->children) * ci->children_num);
  ci->children[0].key = "Host";
  ci->children[0].values_num = 1;
  ci->children[0].values = calloc(1, sizeof(*ci->children[0].values));
  ci->children[0].values->type = OCONFIG_TYPE_STRING;
  ci->children[0].values->value.string = "127.0.0.1:5000";
  ci->children[1].key = "User";
  ci->children[1].values_num = 1;
  ci->children[1].values = calloc(1, sizeof(*ci->children[1].values));
  ci->children[1].values->type = OCONFIG_TYPE_STRING;
  ci->children[1].values->value.string = "user";
  ci->children[2].key = "Passwd";
  ci->children[2].values_num = 1;
  ci->children[2].values = calloc(1, sizeof(*ci->children[2].values));
  ci->children[2].values->type = OCONFIG_TYPE_STRING;
  ci->children[2].values->value.string = "passwd";
  ci->children[3].key = "Queries";
  ci->children[3].values_num = 1;
  ci->children[3].values = calloc(1, sizeof(*ci->children[2].values));
  ci->children[3].values->type = OCONFIG_TYPE_STRING;
  ci->children[3].values->value.string = "fans";

  ctx.services = llist_create();

  int ret = redfish_config_service(ci);

  EXPECT_EQ_INT(0, ret);

  for (llentry_t *llserv = llist_head(ctx.services); llserv != NULL;
       llserv = llserv->next) {
    redfish_service_t *serv = (redfish_service_t *)llserv->value;
    sfree(serv->name);
    sfree(serv->host);
    sfree(serv->user);
    sfree(serv->passwd);
    for (int i = 0; i < serv->queries_num; i++)
      sfree(serv->queries[i]);
    sfree(serv->queries);
    sfree(serv);
  }
  llist_destroy(ctx.services);

  sfree(ci->children[3].values);
  sfree(ci->children[2].values);
  sfree(ci->children[1].values);
  sfree(ci->children[0].values);
  sfree(ci->children);
  sfree(ci->values);
  sfree(ci);
  return 0;
}

DEF_TEST(process_payload_property) {
  redfish_property_t property;
  property.name = "Abc";
  property.plugin_inst = "TestPluginInstance";
  property.type = "MAGIC";
  property.type_inst = "TestTypeInstance";

  redfish_resource_t resource;
  resource.name = "ResourceName";

  redfish_service_t service;
  service.name = "localhost";

  const char *json_text = "["
                          "  { \"Abc\": 4567 }"
                          "]";
  json_error_t error;
  json_t *root = json_loads(json_text, 0, &error);

  if (!root) {
    return -1;
  }

  redfish_process_payload_property(&property, root, &resource, &service);

  json_decref(root);

  value_list_t *v = redfish_test_get_last_dispatched_value_list();
  EXPECT_EQ_INT(1, v->values_len);
  EXPECT_EQ_STR("MAGIC", v->type);
  EXPECT_EQ_INT(4567, v->values->derive);
  EXPECT_EQ_STR("TestPluginInstance", v->plugin_instance);
  EXPECT_EQ_STR("TestTypeInstance", v->type_instance);
  EXPECT_EQ_STR("localhost", v->host);
  EXPECT_EQ_STR("redfish", v->plugin);
  return 0;
}

DEF_TEST(service_destroy) {
  /* Check for memory leaks when a service is destroyed */
  redfish_service_t *service = calloc(1, sizeof(*service));

  service->name = strdup("Name");
  service->host = strdup("http://localhost:1234");
  service->user = strdup("User");
  service->passwd = strdup("Password");
  service->token = strdup("Token");

  service->queries = calloc(2, sizeof(*service->queries));
  service->queries[0] = strdup("Query1");
  service->queries[1] = strdup("Query2");
  service->queries_num = 2;

  service->query_ptrs = llist_create();

  service->flags |= REDFISH_FLAG_SERVICE_NO_VERSION_DOC;
  service->auth.authCodes.userPass.username = service->user;
  service->auth.authCodes.userPass.password = service->passwd;
  service->redfish = createServiceEnumerator(service->host, NULL,
                                             &service->auth, service->flags);

  redfish_service_destroy(service);
  return 0;
}

DEF_TEST(job_destroy) {
  /* Check for memory leaks when a job is destroyed */
  redfish_job_t *job = calloc(1, sizeof(*job));
  redfish_job_destroy(job);
  return 0;
}

DEF_TEST(json_get_string_1) {
  const char *json_text = "{ \"MemberId\": \"1234\" }";

  json_error_t error;
  json_t *root = json_loads(json_text, 0, &error);

  if (!root) {
    return -1;
  }

  char str[20];
  json_t *json = json_object_get(root, "MemberId");
  redfish_json_get_string(str, sizeof(str), json);

  json_decref(root);

  EXPECT_EQ_STR("1234", str);
  return 0;
}

DEF_TEST(json_get_string_2) {
  const char *json_text = "{ \"MemberId\": 9876 }";

  json_error_t error;
  json_t *root = json_loads(json_text, 0, &error);

  if (!root) {
    return -1;
  }

  char str[20];
  json_t *json = json_object_get(root, "MemberId");
  redfish_json_get_string(str, sizeof(str), json);

  json_decref(root);

  EXPECT_EQ_STR("9876", str);
  return 0;
}

int main(void) {
  RUN_TEST(read_queries);
  RUN_TEST(convert_val);
  RUN_TEST(redfish_preconfig);
  RUN_TEST(config_property);
  RUN_TEST(config_resource);
  RUN_TEST(config_query);
  RUN_TEST(config_service);
  RUN_TEST(process_payload_property);
  RUN_TEST(service_destroy);
  RUN_TEST(job_destroy);
  RUN_TEST(json_get_string_1);
  RUN_TEST(json_get_string_2);
  END_TEST;
}
