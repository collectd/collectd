/**
 * collectd - src/tests/test_utils_vl_lookup.c
 * Copyright (C) 2012       Florian Forster
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
 **/

#include "collectd.h"

#include "testing.h"
#include "utils/lookup/vl_lookup.h"

static bool expect_new_obj;
static bool have_new_obj;

static lookup_identifier_t last_class_ident;
static lookup_identifier_t last_obj_ident;

static data_source_t dsrc_test = {"value", DS_TYPE_DERIVE, 0.0, NAN};
static data_set_t const ds_test = {"test", 1, &dsrc_test};

static data_source_t dsrc_unknown = {"value", DS_TYPE_DERIVE, 0.0, NAN};
static data_set_t const ds_unknown = {"unknown", 1, &dsrc_unknown};

static int lookup_obj_callback(data_set_t const *ds, value_list_t const *vl,
                               void *user_class, void *user_obj) {
  lookup_identifier_t *class = user_class;
  lookup_identifier_t *obj = user_obj;

  OK1(expect_new_obj == have_new_obj,
      (expect_new_obj ? "New obj is created." : "Updating existing obj."));

  memcpy(&last_class_ident, class, sizeof(last_class_ident));
  memcpy(&last_obj_ident, obj, sizeof(last_obj_ident));

  if (strcmp(obj->plugin_instance, "failure") == 0)
    return -1;

  return 0;
}

static void *lookup_class_callback(data_set_t const *ds, value_list_t const *vl,
                                   void *user_class) {
  lookup_identifier_t *class = user_class;
  lookup_identifier_t *obj;

  assert(expect_new_obj);

  memcpy(&last_class_ident, class, sizeof(last_class_ident));

  obj = malloc(sizeof(*obj));
  strncpy(obj->host, vl->host, sizeof(obj->host));
  strncpy(obj->plugin, vl->plugin, sizeof(obj->plugin));
  strncpy(obj->plugin_instance, vl->plugin_instance,
          sizeof(obj->plugin_instance));
  strncpy(obj->type, vl->type, sizeof(obj->type));
  strncpy(obj->type_instance, vl->type_instance, sizeof(obj->type_instance));

  have_new_obj = true;

  return (void *)obj;
}

static int checked_lookup_add(lookup_t *obj, /* {{{ */
                              char const *host, char const *plugin,
                              char const *plugin_instance, char const *type,
                              char const *type_instance,
                              unsigned int group_by) {
  lookup_identifier_t ident = {{0}};
  void *user_class;

  strncpy(ident.host, host, sizeof(ident.host) - 1);
  strncpy(ident.plugin, plugin, sizeof(ident.plugin) - 1);
  strncpy(ident.plugin_instance, plugin_instance,
          sizeof(ident.plugin_instance) - 1);
  strncpy(ident.type, type, sizeof(ident.type) - 1);
  strncpy(ident.type_instance, type_instance, sizeof(ident.type_instance) - 1);

  user_class = malloc(sizeof(ident));
  memmove(user_class, &ident, sizeof(ident));

  OK(lookup_add(obj, &ident, group_by, user_class) == 0);
  return 0;
} /* }}} int checked_lookup_add */

static int checked_lookup_search(lookup_t *obj, char const *host,
                                 char const *plugin,
                                 char const *plugin_instance, char const *type,
                                 char const *type_instance, bool expect_new) {
  int status;
  value_list_t vl = VALUE_LIST_INIT;
  data_set_t const *ds = &ds_unknown;

  strncpy(vl.host, host, sizeof(vl.host) - 1);
  strncpy(vl.plugin, plugin, sizeof(vl.plugin) - 1);
  strncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance) - 1);
  strncpy(vl.type, type, sizeof(vl.type) - 1);
  strncpy(vl.type_instance, type_instance, sizeof(vl.type_instance) - 1);

  if (strcmp(vl.type, "test") == 0)
    ds = &ds_test;

  expect_new_obj = expect_new;
  have_new_obj = false;

  status = lookup_search(obj, ds, &vl);
  return status;
}

DEF_TEST(group_by_specific_host) {
  lookup_t *obj;
  CHECK_NOT_NULL(obj = lookup_create(lookup_class_callback, lookup_obj_callback,
                                     (void *)free, (void *)free));

  checked_lookup_add(obj, "/.*/", "test", "", "test", "/.*/", LU_GROUP_BY_HOST);
  checked_lookup_search(obj, "host0", "test", "", "test", "0",
                        /* expect new = */ 1);
  checked_lookup_search(obj, "host0", "test", "", "test", "1",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "host1", "test", "", "test", "0",
                        /* expect new = */ 1);
  checked_lookup_search(obj, "host1", "test", "", "test", "1",
                        /* expect new = */ 0);

  lookup_destroy(obj);
  return 0;
}

DEF_TEST(group_by_any_host) {
  lookup_t *obj;
  CHECK_NOT_NULL(obj = lookup_create(lookup_class_callback, lookup_obj_callback,
                                     (void *)free, (void *)free));

  checked_lookup_add(obj, "/.*/", "/.*/", "/.*/", "test", "/.*/",
                     LU_GROUP_BY_HOST);
  checked_lookup_search(obj, "host0", "plugin0", "", "test", "0",
                        /* expect new = */ 1);
  checked_lookup_search(obj, "host0", "plugin0", "", "test", "1",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "host0", "plugin1", "", "test", "0",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "host0", "plugin1", "", "test", "1",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "host1", "plugin0", "", "test", "0",
                        /* expect new = */ 1);
  checked_lookup_search(obj, "host1", "plugin0", "", "test", "1",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "host1", "plugin1", "", "test", "0",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "host1", "plugin1", "", "test", "1",
                        /* expect new = */ 0);

  lookup_destroy(obj);
  return 0;
}

DEF_TEST(multiple_lookups) {
  lookup_t *obj;
  int status;

  CHECK_NOT_NULL(obj = lookup_create(lookup_class_callback, lookup_obj_callback,
                                     (void *)free, (void *)free));

  checked_lookup_add(obj, "/.*/", "plugin0", "", "test", "/.*/",
                     LU_GROUP_BY_HOST);
  checked_lookup_add(obj, "/.*/", "/.*/", "", "test", "ti0", LU_GROUP_BY_HOST);

  status = checked_lookup_search(obj, "host0", "plugin1", "", "test", "",
                                 /* expect new = */ 0);
  assert(status == 0);
  status = checked_lookup_search(obj, "host0", "plugin0", "", "test", "",
                                 /* expect new = */ 1);
  assert(status == 1);
  status = checked_lookup_search(obj, "host0", "plugin1", "", "test", "ti0",
                                 /* expect new = */ 1);
  assert(status == 1);
  status = checked_lookup_search(obj, "host0", "plugin0", "", "test", "ti0",
                                 /* expect new = */ 0);
  assert(status == 2);

  lookup_destroy(obj);
  return 0;
}

DEF_TEST(regex) {
  lookup_t *obj;
  CHECK_NOT_NULL(obj = lookup_create(lookup_class_callback, lookup_obj_callback,
                                     (void *)free, (void *)free));

  checked_lookup_add(obj, "/^db[0-9]\\./", "cpu", "/.*/", "cpu", "/.*/",
                     LU_GROUP_BY_TYPE_INSTANCE);
  checked_lookup_search(obj, "db0.example.com", "cpu", "0", "cpu", "user",
                        /* expect new = */ 1);
  checked_lookup_search(obj, "db0.example.com", "cpu", "0", "cpu", "idle",
                        /* expect new = */ 1);
  checked_lookup_search(obj, "db0.example.com", "cpu", "1", "cpu", "user",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "db0.example.com", "cpu", "1", "cpu", "idle",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "app0.example.com", "cpu", "0", "cpu", "user",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "app0.example.com", "cpu", "0", "cpu", "idle",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "db1.example.com", "cpu", "0", "cpu", "user",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "db1.example.com", "cpu", "0", "cpu", "idle",
                        /* expect new = */ 0);
  checked_lookup_search(obj, "db1.example.com", "cpu", "0", "cpu", "system",
                        /* expect new = */ 1);

  lookup_destroy(obj);
  return 0;
}

int main(int argc, char **argv) /* {{{ */
{
  RUN_TEST(group_by_specific_host);
  RUN_TEST(group_by_any_host);
  RUN_TEST(multiple_lookups);
  RUN_TEST(regex);

  END_TEST;
} /* }}} int main */
