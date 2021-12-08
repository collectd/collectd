/**
 * collectd - src/logparser_test.c
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
 *   Adrian Boczkowski <adrianx.boczkowski@intel.com>
 **/

#include "logparser.c"
#include "testing.h"

DEF_TEST(init) {
  logparser_ctx.parsers_len = 3;
  logparser_ctx.parsers = calloc(3, sizeof(log_parser_t));

  logparser_ctx.parsers[0].filename = "test_filename0";
  logparser_ctx.parsers[0].patterns_len = 3;
  logparser_ctx.parsers[0].patterns = calloc(3, sizeof(message_pattern_t));
  logparser_ctx.parsers[0].patterns[0].regex = "test_regex00";
  logparser_ctx.parsers[0].patterns[0].excluderegex = "exclude_regex00";
  logparser_ctx.parsers[0].patterns[0].is_mandatory = true;
  logparser_ctx.parsers[0].patterns[0].submatch_idx = 1;
  logparser_ctx.parsers[0].patterns[0].name = "test_name00";
  logparser_ctx.parsers[0].patterns[1].regex = "test_regex01";
  logparser_ctx.parsers[0].patterns[1].excluderegex = "exclude_regex01";
  logparser_ctx.parsers[0].patterns[1].is_mandatory = true;
  logparser_ctx.parsers[0].patterns[1].submatch_idx = 1;
  logparser_ctx.parsers[0].patterns[1].name = "test_name01";
  logparser_ctx.parsers[0].patterns[2].regex = "test_regex02";
  logparser_ctx.parsers[0].patterns[2].excluderegex = "exclude_regex02";
  logparser_ctx.parsers[0].patterns[2].is_mandatory = true;
  logparser_ctx.parsers[0].patterns[2].submatch_idx = 1;
  logparser_ctx.parsers[0].patterns[2].name = "test_name02";

  logparser_ctx.parsers[1].filename = "test_filename1";
  logparser_ctx.parsers[1].patterns_len = 3;
  logparser_ctx.parsers[1].patterns = calloc(3, sizeof(message_pattern_t));
  logparser_ctx.parsers[1].patterns[0].regex = "test_regex10";
  logparser_ctx.parsers[1].patterns[0].excluderegex = "exclude_regex10";
  logparser_ctx.parsers[1].patterns[0].is_mandatory = true;
  logparser_ctx.parsers[1].patterns[0].submatch_idx = 1;
  logparser_ctx.parsers[1].patterns[0].name = "test_name10";
  logparser_ctx.parsers[1].patterns[1].regex = "test_regex11";
  logparser_ctx.parsers[1].patterns[1].excluderegex = "exclude_regex11";
  logparser_ctx.parsers[1].patterns[1].is_mandatory = true;
  logparser_ctx.parsers[1].patterns[1].submatch_idx = 1;
  logparser_ctx.parsers[1].patterns[1].name = "test_name11";
  logparser_ctx.parsers[1].patterns[2].regex = "test_regex12";
  logparser_ctx.parsers[1].patterns[2].excluderegex = "exclude_regex12";
  logparser_ctx.parsers[1].patterns[2].is_mandatory = true;
  logparser_ctx.parsers[1].patterns[2].submatch_idx = 1;
  logparser_ctx.parsers[1].patterns[2].name = "test_name12";

  logparser_ctx.parsers[2].filename = "test_filename2";
  logparser_ctx.parsers[2].patterns_len = 3;
  logparser_ctx.parsers[2].patterns = calloc(3, sizeof(message_pattern_t));
  logparser_ctx.parsers[2].patterns[0].regex = "test_regex20";
  logparser_ctx.parsers[2].patterns[0].excluderegex = "exclude_regex20";
  logparser_ctx.parsers[2].patterns[0].is_mandatory = true;
  logparser_ctx.parsers[2].patterns[0].submatch_idx = 1;
  logparser_ctx.parsers[2].patterns[0].name = "test_name20";
  logparser_ctx.parsers[2].patterns[1].regex = "test_regex21";
  logparser_ctx.parsers[2].patterns[1].excluderegex = "exclude_regex21";
  logparser_ctx.parsers[2].patterns[1].is_mandatory = true;
  logparser_ctx.parsers[2].patterns[1].submatch_idx = 1;
  logparser_ctx.parsers[2].patterns[1].name = "test_name21";
  logparser_ctx.parsers[2].patterns[2].regex = "test_regex22";
  logparser_ctx.parsers[2].patterns[2].excluderegex = "exclude_regex22";
  logparser_ctx.parsers[2].patterns[2].is_mandatory = true;
  logparser_ctx.parsers[2].patterns[2].submatch_idx = 1;
  logparser_ctx.parsers[2].patterns[2].name = "test_name22";

  int ret = logparser_init();
  sfree(logparser_ctx.parsers[0].patterns);
  sfree(logparser_ctx.parsers[1].patterns);
  sfree(logparser_ctx.parsers[2].patterns);
  message_parser_cleanup(logparser_ctx.parsers[0].job);
  message_parser_cleanup(logparser_ctx.parsers[1].job);
  message_parser_cleanup(logparser_ctx.parsers[2].job);
  sfree(logparser_ctx.parsers);
  logparser_ctx.parsers_len = 0;
  EXPECT_EQ_INT(0, ret);

  return 0;
}

DEF_TEST(free_user_data) {
  message_item_user_data_t *user_data = calloc(1, sizeof(*user_data));

  user_data->infos_len = 4;
  user_data->infos[0].type = MSG_ITEM_SEVERITY;
  user_data->infos[0].val.severity = NOTIF_OKAY;
  user_data->infos[1].type = MSG_ITEM_PLUGIN_INST;
  user_data->infos[1].val.str_override = strdup("test_plugin_inst");
  user_data->infos[2].type = MSG_ITEM_TYPE;
  user_data->infos[2].val.str_override = strdup("test_type");
  user_data->infos[3].type = MSG_ITEM_TYPE_INST;
  user_data->infos[3].val.str_override = strdup("test_type_inst");
  logparser_free_user_data(user_data);

  return 0;
}

DEF_TEST(config_logfile) {
  oconfig_item_t *logfile_ci = calloc(1, sizeof(*logfile_ci));
  assert(logfile_ci != NULL);
  logfile_ci->key = "Logfile";

  logfile_ci->values_num = 1;
  logfile_ci->values =
      calloc(logfile_ci->values_num, sizeof(*logfile_ci->values));
  assert(logfile_ci->values != NULL);
  logfile_ci->values->type = OCONFIG_TYPE_STRING;
  logfile_ci->values->value.string = "/path/to/a/file";

  logfile_ci->children_num = 2;
  logfile_ci->children =
      calloc(logfile_ci->children_num, sizeof(*logfile_ci->children));
  assert(logfile_ci->children != NULL);

  oconfig_item_t *first_full_read_ci = &logfile_ci->children[0];
  first_full_read_ci->key = "FirstFullRead";

  first_full_read_ci->values_num = 1;
  first_full_read_ci->values = calloc(first_full_read_ci->values_num,
                                      sizeof(*first_full_read_ci->values));
  assert(first_full_read_ci->values != NULL);
  first_full_read_ci->values->type = OCONFIG_TYPE_BOOLEAN;
  first_full_read_ci->values->value.boolean = true;

  oconfig_item_t *msg_ci = &logfile_ci->children[1];
  msg_ci->key = "Message";

  msg_ci->values_num = 1;
  msg_ci->values = calloc(msg_ci->values_num, sizeof(*msg_ci->values));
  assert(msg_ci->values != NULL);
  msg_ci->values->type = OCONFIG_TYPE_STRING;
  msg_ci->values->value.string = "msg-name";

  msg_ci->children_num = 6;
  msg_ci->children = calloc(msg_ci->children_num, sizeof(*msg_ci->children));
  assert(msg_ci->children != NULL);

  oconfig_item_t *def_plugin_inst_ci = &msg_ci->children[0];
  def_plugin_inst_ci->key = "DefaultPluginInstance";
  def_plugin_inst_ci->values_num = 1;
  def_plugin_inst_ci->values = calloc(def_plugin_inst_ci->values_num,
                                      sizeof(*def_plugin_inst_ci->values));
  assert(def_plugin_inst_ci->values != NULL);
  def_plugin_inst_ci->values->type = OCONFIG_TYPE_STRING;
  def_plugin_inst_ci->values->value.string = "test_default_plugin_instance_1";

  oconfig_item_t *def_type_ci = &msg_ci->children[1];
  def_type_ci->key = "DefaultType";
  def_type_ci->values_num = 1;
  def_type_ci->values =
      calloc(def_type_ci->values_num, sizeof(*def_type_ci->values));
  assert(def_type_ci->values != NULL);
  def_type_ci->values->type = OCONFIG_TYPE_STRING;
  def_type_ci->values->value.string = "test_default_type_1";

  oconfig_item_t *def_type_inst_ci = &msg_ci->children[2];
  def_type_inst_ci->key = "DefaultTypeInstance";
  def_type_inst_ci->values_num = 1;
  def_type_inst_ci->values =
      calloc(def_type_inst_ci->values_num, sizeof(*def_type_inst_ci->values));
  assert(def_type_inst_ci->values != NULL);
  def_type_inst_ci->values->type = OCONFIG_TYPE_STRING;
  def_type_inst_ci->values->value.string = "test_default_type_instance_1";

  oconfig_item_t *def_severity_ci = &msg_ci->children[3];
  def_severity_ci->key = "DefaultSeverity";
  def_severity_ci->values_num = 1;
  def_severity_ci->values =
      calloc(def_severity_ci->values_num, sizeof(*def_severity_ci->values));
  assert(def_severity_ci->values != NULL);
  def_severity_ci->values->type = OCONFIG_TYPE_STRING;
  def_severity_ci->values->value.string = "OK";

  oconfig_item_t *match1_ci = &msg_ci->children[4];
  assert(match1_ci != NULL);
  match1_ci->key = "Match";
  match1_ci->values_num = 1;
  match1_ci->values = calloc(match1_ci->values_num, sizeof(*match1_ci->values));
  assert(match1_ci->values != NULL);
  match1_ci->values->type = OCONFIG_TYPE_STRING;
  match1_ci->values->value.string = "test_match_1";

  match1_ci->children_num = 6;
  match1_ci->children =
      calloc(match1_ci->children_num, sizeof(*match1_ci->children));
  assert(match1_ci->children != NULL);

  oconfig_item_t *regex1_ci = &match1_ci->children[0];
  regex1_ci->key = "Regex";
  regex1_ci->values_num = 1;
  regex1_ci->values = calloc(regex1_ci->values_num, sizeof(*regex1_ci->values));
  assert(regex1_ci->values != NULL);
  regex1_ci->values->type = OCONFIG_TYPE_STRING;
  regex1_ci->values->value.string = "test_regex_1";

  oconfig_item_t *submatch_idx1_ci = &match1_ci->children[1];
  submatch_idx1_ci->key = "SubmatchIdx";
  submatch_idx1_ci->values_num = 1;
  submatch_idx1_ci->values =
      calloc(submatch_idx1_ci->values_num, sizeof(*submatch_idx1_ci->values));
  assert(submatch_idx1_ci->values != NULL);
  submatch_idx1_ci->values->type = OCONFIG_TYPE_NUMBER;
  submatch_idx1_ci->values->value.number = 15;

  oconfig_item_t *is_mandatory1_ci = &match1_ci->children[2];
  is_mandatory1_ci->key = "IsMandatory";
  is_mandatory1_ci->values_num = 1;
  is_mandatory1_ci->values =
      calloc(is_mandatory1_ci->values_num, sizeof(*is_mandatory1_ci->values));
  assert(is_mandatory1_ci->values != NULL);
  is_mandatory1_ci->values->type = OCONFIG_TYPE_BOOLEAN;
  is_mandatory1_ci->values->value.boolean = false;

  oconfig_item_t *severity1_ci = &match1_ci->children[3];
  severity1_ci->key = "Severity";
  severity1_ci->values_num = 1;
  severity1_ci->values =
      calloc(severity1_ci->values_num, sizeof(*severity1_ci->values));
  assert(severity1_ci->values != NULL);
  severity1_ci->values->type = OCONFIG_TYPE_STRING;
  severity1_ci->values->value.string = "failure";

  oconfig_item_t *type_inst1_ci = &match1_ci->children[4];
  type_inst1_ci->key = "TypeInstance";
  type_inst1_ci->values_num = 1;
  type_inst1_ci->values =
      calloc(type_inst1_ci->values_num, sizeof(*type_inst1_ci->values));
  assert(type_inst1_ci->values != NULL);
  type_inst1_ci->values->type = OCONFIG_TYPE_STRING;
  type_inst1_ci->values->value.string = "test_type_instance_1";

  oconfig_item_t *plugin_inst1_ci = &match1_ci->children[5];
  plugin_inst1_ci->key = "PluginInstance";
  plugin_inst1_ci->values_num = 1;
  plugin_inst1_ci->values =
      calloc(plugin_inst1_ci->values_num, sizeof(*plugin_inst1_ci->values));
  assert(plugin_inst1_ci->values != NULL);
  plugin_inst1_ci->values->type = OCONFIG_TYPE_STRING;
  plugin_inst1_ci->values->value.string = "test_plugin_instance_1";

  oconfig_item_t *match2_ci = &msg_ci->children[5];
  assert(match2_ci != NULL);
  match2_ci->key = "Match";
  match2_ci->values_num = 1;
  match2_ci->values = calloc(match2_ci->values_num, sizeof(*match2_ci->values));
  assert(match2_ci->values != NULL);
  match2_ci->values->type = OCONFIG_TYPE_STRING;
  match2_ci->values->value.string = "test_match_2";

  match2_ci->children_num = 6;
  match2_ci->children =
      calloc(match2_ci->children_num, sizeof(*match2_ci->children));
  assert(match2_ci->children != NULL);

  oconfig_item_t *regex2_ci = &match2_ci->children[0];
  regex2_ci->key = "Regex";
  regex2_ci->values_num = 1;
  regex2_ci->values = calloc(regex2_ci->values_num, sizeof(*regex2_ci->values));
  assert(regex2_ci->values != NULL);
  regex2_ci->values->type = OCONFIG_TYPE_STRING;
  regex2_ci->values->value.string = "test_regex_2";

  oconfig_item_t *submatch_idx2_ci = &match2_ci->children[1];
  submatch_idx2_ci->key = "SubmatchIdx";
  submatch_idx2_ci->values_num = 1;
  submatch_idx2_ci->values =
      calloc(submatch_idx2_ci->values_num, sizeof(*submatch_idx2_ci->values));
  assert(submatch_idx2_ci->values != NULL);
  submatch_idx2_ci->values->type = OCONFIG_TYPE_NUMBER;
  submatch_idx2_ci->values->value.number = 8;

  oconfig_item_t *is_mandatory2_ci = &match2_ci->children[2];
  is_mandatory2_ci->key = "IsMandatory";
  is_mandatory2_ci->values_num = 1;
  is_mandatory2_ci->values =
      calloc(is_mandatory2_ci->values_num, sizeof(*is_mandatory2_ci->values));
  assert(is_mandatory2_ci->values != NULL);
  is_mandatory2_ci->values->type = OCONFIG_TYPE_BOOLEAN;
  is_mandatory2_ci->values->value.boolean = true;

  oconfig_item_t *severity2_ci = &match2_ci->children[3];
  severity2_ci->key = "Severity";
  severity2_ci->values_num = 1;
  severity2_ci->values =
      calloc(severity2_ci->values_num, sizeof(*severity2_ci->values));
  assert(severity2_ci->values != NULL);
  severity2_ci->values->type = OCONFIG_TYPE_STRING;
  severity2_ci->values->value.string = "warning";

  oconfig_item_t *type_inst2_ci = &match2_ci->children[4];
  type_inst2_ci->key = "TypeInstance";
  type_inst2_ci->values_num = 1;
  type_inst2_ci->values =
      calloc(type_inst2_ci->values_num, sizeof(*type_inst2_ci->values));
  assert(type_inst2_ci->values != NULL);
  type_inst2_ci->values->type = OCONFIG_TYPE_STRING;
  type_inst2_ci->values->value.string = "test_type_instance_2";

  oconfig_item_t *plugin_inst2_ci = &match2_ci->children[5];
  plugin_inst2_ci->key = "PluginInstance";
  plugin_inst2_ci->values_num = 1;
  plugin_inst2_ci->values =
      calloc(plugin_inst2_ci->values_num, sizeof(*plugin_inst2_ci->values));
  assert(plugin_inst2_ci->values != NULL);
  plugin_inst2_ci->values->type = OCONFIG_TYPE_STRING;
  plugin_inst2_ci->values->value.string = "test_plugin_instance_2";

  int ret = logparser_config_logfile(logfile_ci);
  EXPECT_EQ_INT(0, ret);

  EXPECT_EQ_INT(1, logparser_ctx.parsers_len);

  log_parser_t *parser = &logparser_ctx.parsers[0];

  EXPECT_EQ_INT(1, parser->first_read);
  EXPECT_EQ_STR("/path/to/a/file", parser->filename);
  EXPECT_EQ_STR("test_default_plugin_instance_1", parser->def_plugin_inst);
  EXPECT_EQ_INT(NOTIF_OKAY, parser->def_severity);
  EXPECT_EQ_STR("test_default_type_1", parser->def_type);
  EXPECT_EQ_STR("test_default_type_instance_1", parser->def_type_inst);
  EXPECT_EQ_INT(2, parser->patterns_len);

  message_pattern_t *pattern = &logparser_ctx.parsers[0].patterns[0];
  EXPECT_EQ_STR("test_regex_1", pattern->regex);
  EXPECT_EQ_INT(0, pattern->is_mandatory);
  EXPECT_EQ_INT(15, pattern->submatch_idx);
  EXPECT_EQ_STR("test_match_1", pattern->name);

  message_item_user_data_t *user_data = pattern->user_data;
  EXPECT_EQ_INT(3, user_data->infos_len);
  for (size_t i = 0; i < user_data->infos_len; ++i) {
    switch (user_data->infos[i].type) {
    case MSG_ITEM_PLUGIN_INST:
      EXPECT_EQ_STR("test_plugin_instance_1",
                    user_data->infos[i].val.str_override);
      break;
    case MSG_ITEM_TYPE_INST:
      EXPECT_EQ_STR("test_type_instance_1",
                    user_data->infos[i].val.str_override);
      break;
    case MSG_ITEM_SEVERITY:
      EXPECT_EQ_INT(NOTIF_FAILURE, user_data->infos[i].val.severity);
      break;
    default:
      OK1(false, "Unknown message item");
    }
  }

  pattern = &logparser_ctx.parsers[0].patterns[1];
  EXPECT_EQ_STR("test_regex_2", pattern->regex);
  EXPECT_EQ_INT(1, pattern->is_mandatory);
  EXPECT_EQ_INT(8, pattern->submatch_idx);
  EXPECT_EQ_STR("test_match_2", pattern->name);

  user_data = pattern->user_data;
  EXPECT_EQ_INT(3, user_data->infos_len);
  for (size_t i = 0; i < user_data->infos_len; ++i) {
    switch (user_data->infos[i].type) {
    case MSG_ITEM_PLUGIN_INST:
      EXPECT_EQ_STR("test_plugin_instance_2",
                    user_data->infos[i].val.str_override);
      break;
    case MSG_ITEM_TYPE_INST:
      EXPECT_EQ_STR("test_type_instance_2",
                    user_data->infos[i].val.str_override);
      break;
    case MSG_ITEM_SEVERITY:
      EXPECT_EQ_INT(NOTIF_WARNING, user_data->infos[i].val.severity);
      break;
    default:
      OK1(false, "Unknown message item");
    }
  }

  sfree(regex1_ci->values);
  sfree(submatch_idx1_ci->values);
  sfree(is_mandatory1_ci->values);
  sfree(severity1_ci->values);
  sfree(type_inst1_ci->values);
  sfree(plugin_inst1_ci->values);

  sfree(regex2_ci->values);
  sfree(submatch_idx2_ci->values);
  sfree(is_mandatory2_ci->values);
  sfree(severity2_ci->values);
  sfree(type_inst2_ci->values);
  sfree(plugin_inst2_ci->values);

  sfree(match2_ci->children);
  sfree(match2_ci->values);

  sfree(match1_ci->children);
  sfree(match1_ci->values);

  sfree(def_plugin_inst_ci->values);
  sfree(def_type_ci->values);
  sfree(def_type_inst_ci->values);
  sfree(def_severity_ci->values);

  sfree(msg_ci->children);
  sfree(msg_ci->values);

  sfree(first_full_read_ci->values);

  sfree(logfile_ci->children);
  sfree(logfile_ci->values);

  sfree(logfile_ci);

  logparser_shutdown();

  return 0;
}

int main(void) {

  RUN_TEST(init);
  RUN_TEST(free_user_data);
  RUN_TEST(config_logfile);

  END_TEST;
}
