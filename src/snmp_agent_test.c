/**
 * collectd - src/snmp_agent_test.c
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
 *   Marcin Mozejko <marcinx.mozejko@intel.com>
 **/

#include "snmp_agent.c"
#include "testing.h"

#define TEST_HOSTNAME "test_hostname"
#define TEST_PLUGIN "test_plugin"
#define TEST_PLUGIN_INST "test_plugin_inst"
#define TEST_TYPE "test_type"
#define TEST_TYPE_INST "test_type_inst"

DEF_TEST(oid_to_string) {
  oid_t o = {.oid = {1, 2, 3, 4, 5, 6, 7, 8, 9}, .oid_len = 9};
  char oid_str[DATA_MAX_NAME_LEN];

  int ret = snmp_agent_oid_to_string(oid_str, DATA_MAX_NAME_LEN, &o);
  EXPECT_EQ_INT(o.oid_len * 2 - 1, ret);
  EXPECT_EQ_STR("1.2.3.4.5.6.7.8.9", oid_str);

  return 0;
}

/* Testing formatting metric name for simple scalar */
DEF_TEST(format_name_scalar) {
  data_definition_t *dd = calloc(1, sizeof(*dd));

  dd->plugin = TEST_PLUGIN;
  dd->plugin_instance = TEST_PLUGIN_INST;
  dd->type = TEST_TYPE;
  dd->type_instance = TEST_TYPE_INST;

  char name[DATA_MAX_NAME_LEN];
  int ret = snmp_agent_format_name(name, sizeof(name), dd, NULL);

  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR(
      "example.com/test_plugin-test_plugin_inst/test_type-test_type_inst",
      name);

  sfree(dd);

  return 0;
}

DEF_TEST(format_name_simple_index) {
  netsnmp_variable_list *index_list_tmp = NULL;
  oid_t index_oid;
  data_definition_t *dd = calloc(1, sizeof(*dd));
  table_definition_t *td = calloc(1, sizeof(*td));

  td->index_list_cont = NULL;
  td->index_keys[0].source = INDEX_PLUGIN_INSTANCE;
  td->index_keys[0].type = ASN_OCTET_STR;
  td->index_keys[1].source = INDEX_TYPE_INSTANCE;
  td->index_keys[1].type = ASN_OCTET_STR;
  dd->table = td;
  dd->plugin = TEST_PLUGIN;
  dd->type = TEST_TYPE;

  const char plugin_inst[] = TEST_PLUGIN_INST;
  const char type_inst[] = TEST_TYPE_INST;

  snmp_varlist_add_variable(&index_list_tmp, NULL, 0, ASN_OCTET_STR,
                            (const u_char *)plugin_inst, strlen(plugin_inst));
  snmp_varlist_add_variable(&index_list_tmp, NULL, 0, ASN_OCTET_STR,
                            (const u_char *)type_inst, strlen(type_inst));

  build_oid_noalloc(index_oid.oid, sizeof(index_oid.oid), &index_oid.oid_len,
                    NULL, 0, index_list_tmp);

  snmp_varlist_add_variable(&td->index_list_cont, NULL, 0, ASN_OCTET_STR, NULL,
                            0);
  snmp_varlist_add_variable(&td->index_list_cont, NULL, 0, ASN_OCTET_STR, NULL,
                            0);

  char name[DATA_MAX_NAME_LEN];

  int ret = snmp_agent_format_name(name, DATA_MAX_NAME_LEN, dd, &index_oid);

  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR(
      "example.com/test_plugin-test_plugin_inst/test_type-test_type_inst",
      name);

  snmp_free_varbind(index_list_tmp);
  snmp_free_varbind(td->index_list_cont);
  sfree(dd);
  sfree(td);

  return 0;
}

DEF_TEST(format_name_regex_index) {
  netsnmp_variable_list *index_list_tmp = NULL;
  oid_t index_oid;
  data_definition_t *dd = calloc(1, sizeof(*dd));
  table_definition_t *td = calloc(1, sizeof(*td));

  td->index_keys_len = 3;
  td->index_list_cont = NULL;
  td->index_keys[0].source = INDEX_PLUGIN_INSTANCE;
  td->index_keys[0].type = ASN_OCTET_STR;
  td->index_keys[1].source = INDEX_TYPE_INSTANCE;
  td->index_keys[1].type = ASN_INTEGER;
  td->index_keys[1].regex = "^vcpu_([0-9]{1,3})-cpu_[0-9]{1,3}$";
  td->index_keys[1].group = 1;
  td->index_keys[2].source = INDEX_TYPE_INSTANCE;
  td->index_keys[2].type = ASN_INTEGER;
  td->index_keys[2].regex = "^vcpu_[0-9]{1,3}-cpu_([0-9]{1,3})$";
  td->index_keys[2].group = 1;

  dd->table = td;
  dd->plugin = TEST_PLUGIN;
  dd->type = TEST_TYPE;

  const char plugin_inst[] = TEST_PLUGIN_INST;
  int vcpu = 1;
  int cpu = 10;

  snmp_varlist_add_variable(&index_list_tmp, NULL, 0, ASN_OCTET_STR,
                            (const u_char *)plugin_inst, strlen(plugin_inst));
  snmp_varlist_add_variable(&index_list_tmp, NULL, 0, ASN_INTEGER,
                            (const u_char *)&vcpu, sizeof(vcpu));
  snmp_varlist_add_variable(&index_list_tmp, NULL, 0, ASN_INTEGER,
                            (const u_char *)&cpu, sizeof(cpu));

  build_oid_noalloc(index_oid.oid, sizeof(index_oid.oid), &index_oid.oid_len,
                    NULL, 0, index_list_tmp);

  token_t *token;
  int *offset;

  td->tokens[INDEX_TYPE_INSTANCE] =
      c_avl_create((int (*)(const void *, const void *))num_compare);
  snmp_varlist_add_variable(&td->index_list_cont, NULL, 0, ASN_OCTET_STR, NULL,
                            0);

  token = malloc(sizeof(*token));
  offset = malloc(sizeof(*offset));
  token->key = snmp_varlist_add_variable(&td->index_list_cont, NULL, 0,
                                         ASN_INTEGER, NULL, 0);
  token->str = strdup("vcpu_");
  *offset = 0;
  int ret = c_avl_insert(td->tokens[INDEX_TYPE_INSTANCE], (void *)offset,
                         (void *)token);

  token = malloc(sizeof(*token));
  offset = malloc(sizeof(*offset));
  token->key = snmp_varlist_add_variable(&td->index_list_cont, NULL, 0,
                                         ASN_INTEGER, NULL, 0);
  token->str = strdup("-cpu_");
  *offset = 6;
  ret += c_avl_insert(td->tokens[INDEX_TYPE_INSTANCE], (void *)offset,
                      (void *)token);
  char name[DATA_MAX_NAME_LEN];

  ret += snmp_agent_format_name(name, DATA_MAX_NAME_LEN, dd, &index_oid);

  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR(
      "example.com/test_plugin-test_plugin_inst/test_type-vcpu_1-cpu_10", name);
  while (c_avl_pick(td->tokens[INDEX_TYPE_INSTANCE], (void **)&offset,
                    (void **)&token) == 0) {
    sfree(offset);
    sfree(token->str);
    sfree(token);
  }
  c_avl_destroy(td->tokens[INDEX_TYPE_INSTANCE]);
  snmp_free_varbind(index_list_tmp);
  snmp_free_varbind(td->index_list_cont);
  sfree(dd);
  sfree(td);

  return 0;
}

DEF_TEST(prep_index_list) {
  table_definition_t *td = calloc(1, sizeof(*td));

  assert(td != NULL);
  td->index_keys_len = 5;
  td->index_keys[0].source = INDEX_HOST;
  td->index_keys[0].type = ASN_OCTET_STR;
  td->index_keys[1].source = INDEX_PLUGIN;
  td->index_keys[1].type = ASN_OCTET_STR;
  td->index_keys[2].source = INDEX_PLUGIN_INSTANCE;
  td->index_keys[2].type = ASN_INTEGER;
  td->index_keys[3].source = INDEX_TYPE;
  td->index_keys[3].type = ASN_INTEGER;
  td->index_keys[4].source = INDEX_TYPE_INSTANCE;
  td->index_keys[4].type = ASN_OCTET_STR;
  td->index_list_cont = NULL;

  int ret = snmp_agent_prep_index_list(td, &td->index_list_cont);
  EXPECT_EQ_INT(0, ret);

  netsnmp_variable_list *key = td->index_list_cont;

  OK(key != NULL);
  EXPECT_EQ_INT(ASN_OCTET_STR, key->type);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_INT(ASN_OCTET_STR, key->type);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_INT(ASN_INTEGER, key->type);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_INT(ASN_INTEGER, key->type);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_INT(ASN_OCTET_STR, key->type);
  key = key->next_variable;
  OK(key == NULL);

  snmp_free_varbind(td->index_list_cont);
  sfree(td);

  return 0;
}

DEF_TEST(fill_index_list_simple) {
  table_definition_t *td = calloc(1, sizeof(*td));
  assert(td != NULL);

  /* Preparing value list */
  value_list_t *vl = calloc(1, sizeof(*vl));
  assert(vl != NULL);
  strncpy(vl->host, TEST_HOSTNAME, DATA_MAX_NAME_LEN);
  strncpy(vl->plugin, TEST_PLUGIN, DATA_MAX_NAME_LEN);
  strncpy(vl->plugin_instance, TEST_PLUGIN_INST, DATA_MAX_NAME_LEN);
  strncpy(vl->type, TEST_TYPE, DATA_MAX_NAME_LEN);
  strncpy(vl->type_instance, TEST_TYPE_INST, DATA_MAX_NAME_LEN);

  td->index_keys_len = 5;
  td->index_keys[0].source = INDEX_HOST;
  td->index_keys[0].type = ASN_OCTET_STR;
  td->index_keys[1].source = INDEX_PLUGIN;
  td->index_keys[1].type = ASN_OCTET_STR;
  td->index_keys[2].source = INDEX_PLUGIN_INSTANCE;
  td->index_keys[2].type = ASN_OCTET_STR;
  td->index_keys[3].source = INDEX_TYPE;
  td->index_keys[3].type = ASN_OCTET_STR;
  td->index_keys[4].source = INDEX_TYPE_INSTANCE;
  td->index_keys[4].type = ASN_OCTET_STR;

  td->index_list_cont = NULL;
  for (int i = 0; i < td->index_keys_len; i++)
    snmp_varlist_add_variable(&td->index_list_cont, NULL, 0, ASN_OCTET_STR,
                              NULL, 0);

  int ret = snmp_agent_fill_index_list(td, vl);
  EXPECT_EQ_INT(0, ret);

  netsnmp_variable_list *key = td->index_list_cont;

  ret = 0;

  OK(key != NULL);
  EXPECT_EQ_STR(vl->host, (char *)key->val.string);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_STR(vl->plugin, (char *)key->val.string);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_STR(vl->plugin_instance, (char *)key->val.string);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_STR(vl->type, (char *)key->val.string);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_STR(vl->type_instance, (char *)key->val.string);
  key = key->next_variable;
  OK(key == NULL);

  snmp_free_varbind(td->index_list_cont);
  sfree(vl);
  sfree(td);

  return 0;
}

DEF_TEST(fill_index_list_regex) {
  table_definition_t *td = calloc(1, sizeof(*td));
  int ret = 0;

  assert(td != NULL);

  /* Preparing value list */
  value_list_t *vl = calloc(1, sizeof(*vl));
  strncpy(vl->plugin_instance, TEST_PLUGIN_INST, DATA_MAX_NAME_LEN);
  strncpy(vl->type_instance, "1test2test3", DATA_MAX_NAME_LEN);

  td->index_keys_len = 4;
  td->index_keys[0].source = INDEX_PLUGIN_INSTANCE;
  td->index_keys[0].type = ASN_OCTET_STR;
  td->index_keys[1].source = INDEX_TYPE_INSTANCE;
  td->index_keys[1].type = ASN_INTEGER;
  td->index_keys[1].regex = "^([0-9])test[0-9]test[0-9]$";
  td->index_keys[1].group = 1;
  td->index_keys[2].source = INDEX_TYPE_INSTANCE;
  td->index_keys[2].type = ASN_INTEGER;
  td->index_keys[2].regex = "^[0-9]test([0-9])test[0-9]$";
  td->index_keys[2].group = 1;
  td->index_keys[3].source = INDEX_TYPE_INSTANCE;
  td->index_keys[3].type = ASN_INTEGER;
  td->index_keys[3].regex = "^[0-9]test[0-9]test([0-9])$";
  td->index_keys[3].group = 1;

  td->index_list_cont = NULL;
  snmp_varlist_add_variable(&td->index_list_cont, NULL, 0, ASN_OCTET_STR, NULL,
                            0);
  for (int i = 1; i < td->index_keys_len; i++) {
    snmp_varlist_add_variable(&td->index_list_cont, NULL, 0, ASN_INTEGER, NULL,
                              0);
    ret = regcomp(&td->index_keys[i].regex_info, td->index_keys[i].regex,
                  REG_EXTENDED);
    EXPECT_EQ_INT(0, ret);
  }
  td->tokens[INDEX_TYPE_INSTANCE] =
      c_avl_create((int (*)(const void *, const void *))num_compare);
  assert(td->tokens[INDEX_TYPE_INSTANCE] != NULL);

  ret = snmp_agent_fill_index_list(td, vl);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, td->tokens_done);

  netsnmp_variable_list *key = td->index_list_cont;

  OK(key != NULL);
  EXPECT_EQ_STR(vl->plugin_instance, (char *)key->val.string);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_INT(1, *key->val.integer);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_INT(2, *key->val.integer);
  key = key->next_variable;
  OK(key != NULL);
  EXPECT_EQ_INT(3, *key->val.integer);
  key = key->next_variable;
  OK(key == NULL);

  token_t *token;
  int *offset;

  while (c_avl_pick(td->tokens[INDEX_TYPE_INSTANCE], (void **)&offset,
                    (void **)&token) == 0) {
    sfree(offset);
    sfree(token->str);
    sfree(token);
  }

  c_avl_destroy(td->tokens[INDEX_TYPE_INSTANCE]);
  snmp_free_varbind(td->index_list_cont);
  sfree(vl);

  for (int i = 0; i < td->index_keys_len; i++) {
    regfree(&td->index_keys[i].regex_info);
  }
  sfree(td);

  return 0;
}

DEF_TEST(config_index_key_source) {
  oconfig_item_t *ci = calloc(1, sizeof(*ci));
  table_definition_t *td = calloc(1, sizeof(*td));
  data_definition_t *dd = calloc(1, sizeof(*dd));

  assert(ci != NULL);
  assert(td != NULL);
  assert(dd != NULL);

  ci->values = calloc(1, sizeof(*ci->values));
  assert(ci->values != NULL);
  ci->values_num = 1;
  ci->values->value.string = "PluginInstance";
  ci->values->type = OCONFIG_TYPE_STRING;

  int ret = snmp_agent_config_index_key_source(td, dd, ci);

  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, td->index_keys_len);
  EXPECT_EQ_INT(0, dd->index_key_pos);
  EXPECT_EQ_INT(INDEX_PLUGIN_INSTANCE, td->index_keys[0].source);
  EXPECT_EQ_INT(GROUP_UNUSED, td->index_keys[0].group);
  OK(td->index_keys[0].regex == NULL);

  sfree(ci->values);
  sfree(ci);
  sfree(td);
  sfree(dd);

  return 0;
}

DEF_TEST(config_index_key_regex) {
  oconfig_item_t *ci = calloc(1, sizeof(*ci));
  table_definition_t *td = calloc(1, sizeof(*td));
  data_definition_t *dd = calloc(1, sizeof(*dd));

  assert(ci != NULL);
  assert(td != NULL);
  assert(dd != NULL);

  dd->index_key_pos = 0;
  td->index_keys_len = 1;
  td->index_keys[0].source = INDEX_PLUGIN_INSTANCE;
  td->index_keys[0].group = 1;
  ci->values = calloc(1, sizeof(*ci->values));
  assert(ci->values != NULL);
  ci->values_num = 1;
  ci->values->value.string = "^([0-9])test[0-9]test[0-9]$";
  ci->values->type = OCONFIG_TYPE_STRING;

  int ret = snmp_agent_config_index_key_regex(td, dd, ci);

  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR(td->index_keys[0].regex, "^([0-9])test[0-9]test[0-9]$");
  OK(td->tokens[INDEX_PLUGIN_INSTANCE] != NULL);

  c_avl_destroy(td->tokens[INDEX_PLUGIN_INSTANCE]);
  sfree(ci->values);
  sfree(ci);
  sfree(td->index_keys[0].regex);
  regfree(&td->index_keys[0].regex_info);
  sfree(td);
  sfree(dd);

  return 0;
}

DEF_TEST(config_index_key) {
  oconfig_item_t *ci = calloc(1, sizeof(*ci));
  table_definition_t *td = calloc(1, sizeof(*td));
  data_definition_t *dd = calloc(1, sizeof(*dd));

  assert(ci != NULL);
  assert(td != NULL);
  assert(dd != NULL);

  ci->children_num = 3;
  ci->children = calloc(1, sizeof(*ci->children) * ci->children_num);

  ci->children[0].key = "Source";
  ci->children[0].parent = ci;
  ci->children[0].values_num = 1;
  ci->children[0].values = calloc(1, sizeof(*ci->children[0].values));
  assert(ci->children[0].values != NULL);
  ci->children[0].values->value.string = "PluginInstance";
  ci->children[0].values->type = OCONFIG_TYPE_STRING;

  ci->children[1].key = "Regex";
  ci->children[1].parent = ci;
  ci->children[1].values_num = 1;
  ci->children[1].values = calloc(1, sizeof(*ci->children[0].values));
  assert(ci->children[1].values != NULL);
  ci->children[1].values->value.string = "^([0-9])test[0-9]test[0-9]$";
  ci->children[1].values->type = OCONFIG_TYPE_STRING;

  ci->children[2].key = "Group";
  ci->children[2].parent = ci;
  ci->children[2].values_num = 1;
  ci->children[2].values = calloc(1, sizeof(*ci->children[0].values));
  assert(ci->children[2].values != NULL);
  ci->children[2].values->value.number = 1;
  ci->children[2].values->type = OCONFIG_TYPE_NUMBER;

  int ret = snmp_agent_config_index_key(td, dd, ci);

  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, td->index_keys_len);
  EXPECT_EQ_INT(0, dd->index_key_pos);
  EXPECT_EQ_INT(INDEX_PLUGIN_INSTANCE, td->index_keys[0].source);
  EXPECT_EQ_INT(1, td->index_keys[0].group);
  EXPECT_EQ_STR("^([0-9])test[0-9]test[0-9]$", td->index_keys[0].regex);
  OK(td->tokens[INDEX_PLUGIN_INSTANCE] != NULL);

  sfree(ci->children[0].values);
  sfree(ci->children[1].values);
  sfree(ci->children[2].values);

  sfree(ci->children);
  sfree(ci);

  c_avl_destroy(td->tokens[INDEX_PLUGIN_INSTANCE]);
  sfree(dd);
  sfree(td->index_keys[0].regex);
  regfree(&td->index_keys[0].regex_info);
  sfree(td);

  return 0;
}

DEF_TEST(parse_index_key) {
  const char regex[] = "test-([0-9])-([0-9])";
  const char input[] = "snmp-test-5-6";
  regex_t regex_info;
  regmatch_t match;

  int ret = regcomp(&regex_info, regex, REG_EXTENDED);
  EXPECT_EQ_INT(0, ret);

  ret = snmp_agent_parse_index_key(input, &regex_info, 0, &match);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(5, match.rm_so);
  EXPECT_EQ_INT(13, match.rm_eo);

  ret = snmp_agent_parse_index_key(input, &regex_info, 1, &match);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(10, match.rm_so);
  EXPECT_EQ_INT(11, match.rm_eo);

  ret = snmp_agent_parse_index_key(input, &regex_info, 2, &match);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(12, match.rm_so);
  EXPECT_EQ_INT(13, match.rm_eo);

  regfree(&regex_info);

  return 0;
}

DEF_TEST(create_token) {
  c_avl_tree_t *tokens =
      c_avl_create((int (*)(const void *, const void *))num_compare);
  const char input[] = "testA1-testB2";

  assert(tokens != NULL);

  int ret = snmp_agent_create_token(input, 0, 5, tokens, NULL);
  EXPECT_EQ_INT(0, ret);
  ret = snmp_agent_create_token(input, 6, 6, tokens, NULL);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(2, c_avl_size(tokens));

  token_t *token;
  int *offset;

  ret = c_avl_pick(tokens, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(6, *offset);
  EXPECT_EQ_STR("-testB", token->str);
  sfree(offset);
  sfree(token->str);
  sfree(token);

  ret = c_avl_pick(tokens, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, *offset);
  EXPECT_EQ_STR("testA", token->str);
  sfree(offset);
  sfree(token->str);
  sfree(token);

  ret = c_avl_pick(tokens, (void **)&offset, (void **)&token);
  OK(ret != 0);

  c_avl_destroy(tokens);

  return 0;
}

DEF_TEST(delete_token) {
  c_avl_tree_t *tokens =
      c_avl_create((int (*)(const void *, const void *))num_compare);
  const char input[] = "testA1-testB2-testC3";

  assert(tokens != NULL);

  int ret = snmp_agent_create_token(input, 0, 5, tokens, NULL);
  EXPECT_EQ_INT(0, ret);
  ret = snmp_agent_create_token(input, 6, 6, tokens, NULL);
  EXPECT_EQ_INT(0, ret);
  ret = snmp_agent_create_token(input, 13, 6, tokens, NULL);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(3, c_avl_size(tokens));
  ret = snmp_agent_delete_token(6, tokens);
  EXPECT_EQ_INT(0, ret);

  token_t *token;
  int *offset;

  ret = c_avl_pick(tokens, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, *offset);
  EXPECT_EQ_STR("testA", token->str);
  sfree(offset);
  sfree(token->str);
  sfree(token);

  ret = c_avl_pick(tokens, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(13, *offset);
  EXPECT_EQ_STR("-testC", token->str);
  sfree(offset);
  sfree(token->str);
  sfree(token);

  ret = c_avl_pick(tokens, (void **)&offset, (void **)&token);
  OK(ret != 0);

  c_avl_destroy(tokens);

  return 0;
}

DEF_TEST(get_token) {
  c_avl_tree_t *tokens =
      c_avl_create((int (*)(const void *, const void *))num_compare);
  const char input[] = "testA1-testB2-testC3";

  assert(tokens != NULL);

  int ret = snmp_agent_create_token(input, 0, 5, tokens, NULL);
  EXPECT_EQ_INT(0, ret);
  ret = snmp_agent_create_token(input, 6, 6, tokens, NULL);
  EXPECT_EQ_INT(0, ret);
  ret = snmp_agent_create_token(input, 13, 6, tokens, NULL);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(3, c_avl_size(tokens));
  ret = snmp_agent_get_token(tokens, 12);
  EXPECT_EQ_INT(6, ret);

  token_t *token;
  int *offset;

  while (c_avl_pick(tokens, (void **)&offset, (void **)&token) == 0) {
    sfree(offset);
    sfree(token->str);
    sfree(token);
  }

  c_avl_destroy(tokens);

  return 0;
}

DEF_TEST(tokenize) {
  regmatch_t m[3];

  m[0].rm_so = 5;
  m[0].rm_eo = 6;
  m[1].rm_so = 12;
  m[1].rm_eo = 13;
  m[2].rm_so = 19;
  m[2].rm_eo = 20;

  c_avl_tree_t *tokens =
      c_avl_create((int (*)(const void *, const void *))num_compare);
  const char input[] = "testA1-testB2-testC3";
  token_t *token;
  int *offset;
  c_avl_iterator_t *it;
  int ret;

  assert(tokens != NULL);

  /* First pass */
  ret = snmp_agent_tokenize(input, tokens, &m[0], NULL);
  EXPECT_EQ_INT(0, ret);
  it = c_avl_get_iterator(tokens);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("testA", token->str);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("-testB2-testC3", token->str);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  OK(ret != 0);
  c_avl_iterator_destroy(it);

  /* Second pass */
  ret = snmp_agent_tokenize(input, tokens, &m[1], NULL);
  EXPECT_EQ_INT(0, ret);
  it = c_avl_get_iterator(tokens);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("testA", token->str);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("-testB", token->str);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("-testC3", token->str);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  OK(ret != 0);
  c_avl_iterator_destroy(it);

  /* Third pass */
  ret = snmp_agent_tokenize(input, tokens, &m[2], NULL);
  EXPECT_EQ_INT(0, ret);
  it = c_avl_get_iterator(tokens);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("testA", token->str);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("-testB", token->str);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("-testC", token->str);
  ret = c_avl_iterator_next(it, (void **)&offset, (void **)&token);
  OK(ret != 0);
  c_avl_iterator_destroy(it);

  while (c_avl_pick(tokens, (void **)&offset, (void **)&token) == 0) {
    sfree(offset);
    sfree(token->str);
    sfree(token);
  }

  c_avl_destroy(tokens);

  return 0;
}

DEF_TEST(build_name) {
  table_definition_t *td = calloc(1, sizeof(*td));
  c_avl_tree_t *tokens =
      c_avl_create((int (*)(const void *, const void *))num_compare);

  assert(tokens != NULL);
  assert(td != NULL);

  int n[3] = {1, 2, 3};
  char *t[3] = {"testA", "-testB", "-testC"};
  int off[3] = {0, 6, 13};
  token_t *token;
  int *offset;
  int ret = 0;
  char *name = NULL;

  td->index_list_cont = NULL;
  for (int i = 0; i < 3; i++) {
    token = malloc(sizeof(*token));
    token->str = t[i];
    token->key =
        snmp_varlist_add_variable(&td->index_list_cont, NULL, 0, ASN_INTEGER,
                                  (const u_char *)&n[i], sizeof(n[i]));
    assert(token->key != NULL);
    offset = &off[i];
    ret = c_avl_insert(tokens, (void *)offset, (void *)token);
    assert(ret == 0);
  }

  ret = snmp_agent_build_name(&name, tokens);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("testA1-testB2-testC3", name);

  while (c_avl_pick(tokens, (void **)&offset, (void **)&token) == 0)
    sfree(token);

  c_avl_destroy(tokens);
  snmp_free_varbind(td->index_list_cont);
  sfree(td);
  sfree(name);
  return 0;
}

int main(void) {
  /* snmp_agent_oid_to_string */
  RUN_TEST(oid_to_string);

  /* snmp_agent_prep_index_list */
  RUN_TEST(prep_index_list);

  /* snmp_agent_fill_index_list */
  RUN_TEST(fill_index_list_simple);
  RUN_TEST(fill_index_list_regex);

  /* snmp_agent_format_name */
  RUN_TEST(format_name_scalar);
  RUN_TEST(format_name_simple_index);
  RUN_TEST(format_name_regex_index);

  /* snmp_agent_config_index_key_source */
  RUN_TEST(config_index_key_source);

  /* snmp_agent_config_index_key_regex */
  RUN_TEST(config_index_key_regex);

  /* snmp_agent_config_index_key */
  RUN_TEST(config_index_key);

  /*snmp_agent_parse_index_key */
  RUN_TEST(parse_index_key);

  /* snmp_agent_create_token */
  RUN_TEST(create_token);

  /* snmp_agent_delete_token */
  RUN_TEST(delete_token);

  /* snmp_agent_get_token */
  RUN_TEST(get_token);

  /* snmp_agent_tokenize */
  RUN_TEST(tokenize);

  /* snmp_agent_build_name */
  RUN_TEST(build_name);

  END_TEST;
}
