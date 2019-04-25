/**
 * collectd - src/utils/message_parser/message_parser_test.c
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
 **/

#include "testing.h"
#include "utils/message_parser/message_parser.c"

#define TEST_PATTERN_NAME "test_pattern_name"
#define TEST_REGEX "test_regex"
#define TEST_EX_REGEX "test_ex_regex"
#define TEST_MSG_ITEM_NAME "test_msg_item_name"
#define TEST_MSG_ITEM_VAL "test_msg_item_value"
#define TEST_FILENAME "test_filename"
#define TEST_PATTERNS_LEN 4

static int start_message_assembly_mock_error(parser_job_data_t *self) {
  return -1;
}

static int start_message_assembly_mock_success(parser_job_data_t *self) {
  return 0;
}

static void message_item_assembly_mock(parser_job_data_t *self,
                                       checked_match_t *cm,
                                       char *const *matches) {
  return;
}

static void end_message_assembly_mock(parser_job_data_t *self) { return; }

DEF_TEST(msg_item_assembly) {
  parser_job_data_t *job = calloc(1, sizeof(*job));
  checked_match_t *cm = calloc(1, sizeof(*cm));
  char *const matches[] = {"test_match0", "test_match1"};

  job->messages_storage = calloc(1, sizeof(*job->messages_storage));
  job->message_idx = 0;

  cm->parser_job = job;
  cm->msg_pattern.name = TEST_PATTERN_NAME;
  cm->msg_pattern.regex = TEST_REGEX;
  cm->msg_pattern.submatch_idx = 1;
  cm->msg_pattern.excluderegex = TEST_EX_REGEX;
  cm->msg_pattern.is_mandatory = false;
  cm->msg_pattern_idx = 0;

  message_item_t *msg_it = &(job->messages_storage[job->message_idx]
                                 .message_items[job->message_item_idx]);

  message_item_assembly(job, cm, matches);

  EXPECT_EQ_STR(msg_it->name, TEST_PATTERN_NAME);
  EXPECT_EQ_STR(msg_it->value, "test_match1");

  sfree(cm);
  sfree(job->messages_storage);
  sfree(job);

  return 0;
}

DEF_TEST(start_msg_item_assembly_1) {
  parser_job_data_t *job = calloc(1, sizeof(*job));

  job->messages_max_len = MSG_STOR_INIT_LEN;
  job->message_idx = 0;
  job->message_item_idx = 1;
  job->messages_storage =
      calloc(job->messages_max_len, sizeof(*job->messages_storage));
  job->messages_storage[0].started = true;
  job->messages_storage[0].completed = false;

  sstrncpy(job->messages_storage[0].message_items[0].name, TEST_MSG_ITEM_NAME,
           sizeof(TEST_MSG_ITEM_NAME));
  sstrncpy(job->messages_storage[0].message_items[0].value, TEST_MSG_ITEM_VAL,
           sizeof(TEST_MSG_ITEM_VAL));

  int ret = start_message_assembly(job);

  EXPECT_EQ_STR("", job->messages_storage[0].message_items[0].name);
  EXPECT_EQ_STR("", job->messages_storage[0].message_items[0].value);
  EXPECT_EQ_INT(0, job->message_item_idx);
  EXPECT_EQ_INT(0, ret);
  sfree(job->messages_storage);
  sfree(job);

  return 0;
}

DEF_TEST(start_msg_item_assembly_2) {
  parser_job_data_t *job = calloc(1, sizeof(*job));

  job->messages_max_len = MSG_STOR_INIT_LEN;
  job->message_idx = 0;
  job->message_item_idx = 1;
  job->messages_storage =
      calloc(job->messages_max_len, sizeof(*job->messages_storage));
  job->messages_storage[0].started = true;
  job->messages_storage[0].completed = true;

  sstrncpy(job->messages_storage[0].message_items[0].name, TEST_MSG_ITEM_NAME,
           sizeof(TEST_MSG_ITEM_NAME));
  sstrncpy(job->messages_storage[0].message_items[0].value, TEST_MSG_ITEM_VAL,
           sizeof(TEST_MSG_ITEM_VAL));

  int ret = start_message_assembly(job);

  EXPECT_EQ_STR("", job->messages_storage[1].message_items[0].name);
  EXPECT_EQ_STR("", job->messages_storage[1].message_items[0].value);
  EXPECT_EQ_STR(TEST_MSG_ITEM_NAME,
                job->messages_storage[0].message_items[0].name);
  EXPECT_EQ_STR(TEST_MSG_ITEM_VAL,
                job->messages_storage[0].message_items[0].value);

  EXPECT_EQ_INT(0, job->message_item_idx);
  EXPECT_EQ_INT(1, job->message_idx);
  EXPECT_EQ_INT(0, ret);
  sfree(job->messages_storage);
  sfree(job);

  return 0;
}

DEF_TEST(start_msg_item_assembly_3) {
  parser_job_data_t *job = calloc(1, sizeof(*job));

  job->messages_max_len = 1;
  job->message_idx = 0;
  job->message_item_idx = 1;
  job->messages_storage =
      calloc(job->messages_max_len, sizeof(*job->messages_storage));
  job->messages_storage[0].started = true;
  job->messages_storage[0].completed = true;
  job->resize_message_buffer = resize_message_buffer;

  sstrncpy(job->messages_storage[0].message_items[0].name, TEST_MSG_ITEM_NAME,
           sizeof(TEST_MSG_ITEM_NAME));
  sstrncpy(job->messages_storage[0].message_items[0].value, TEST_MSG_ITEM_VAL,
           sizeof(TEST_MSG_ITEM_VAL));

  int ret = start_message_assembly(job);

  EXPECT_EQ_STR(TEST_MSG_ITEM_NAME,
                job->messages_storage[0].message_items[0].name);
  EXPECT_EQ_STR(TEST_MSG_ITEM_VAL,
                job->messages_storage[0].message_items[0].value);
  EXPECT_EQ_INT(0, job->message_item_idx);
  EXPECT_EQ_INT(1 + MSG_STOR_INC_STEP, job->messages_max_len);
  EXPECT_EQ_INT(0, ret);
  sfree(job->messages_storage);
  sfree(job);

  return 0;
}

DEF_TEST(resize_msg_buffer) {
  const u_int new_size = 5;
  parser_job_data_t *job = calloc(1, sizeof(*job));
  job->messages_storage = calloc(1, sizeof(*job->messages_storage));
  job->messages_max_len = 1;
  int ret = resize_message_buffer(job, new_size);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(new_size, job->messages_max_len);
  sfree(job->messages_storage);
  sfree(job);

  return 0;
}

DEF_TEST(end_msg_assembly_1) {
  parser_job_data_t *job = calloc(1, sizeof(*job));

  job->message_idx = 0;
  job->messages_storage = calloc(1, sizeof(*job->messages_storage));
  job->messages_max_len = 1;

  end_message_assembly(job);

  EXPECT_EQ_INT(1, job->messages_storage[job->message_idx].completed);
  EXPECT_EQ_INT(1, job->messages_completed);
  EXPECT_EQ_INT(0, job->message_item_idx);

  sfree(job->messages_storage);
  sfree(job);

  return 0;
}

DEF_TEST(end_msg_assembly_2) {
  parser_job_data_t *job = calloc(1, sizeof(*job));

  job->message_idx = 1;
  job->message_item_idx = 1;
  job->messages_storage = calloc(2, sizeof(*job->messages_storage));
  job->messages_max_len = 1;
  job->message_patterns_len = 1;
  job->message_patterns = calloc(1, sizeof(*job->message_patterns));
  job->message_patterns[0].is_mandatory = true;
  job->message_patterns[0].regex = TEST_REGEX;

  end_message_assembly(job);

  EXPECT_EQ_INT(0, job->messages_storage[job->message_idx].completed);
  EXPECT_EQ_INT(0, job->messages_completed);
  EXPECT_EQ_INT(0, job->message_item_idx);
  EXPECT_EQ_INT(0, job->message_idx);

  sfree(job->messages_storage);
  sfree(job->message_patterns);
  sfree(job);

  return 0;
}

DEF_TEST(msg_assembler_1) {
  int ret = message_assembler(NULL, NULL, 0, NULL);

  EXPECT_EQ_INT(-1, ret);

  return 0;
}

DEF_TEST(msg_assembler_2) {

  checked_match_t *cm = calloc(1, sizeof(*cm));
  cm->msg_pattern.submatch_idx = 1;

  int ret = message_assembler(NULL, NULL, 1, cm);

  EXPECT_EQ_INT(-1, ret);

  sfree(cm);

  return 0;
}

DEF_TEST(msg_assembler_3) {
  parser_job_data_t *job = calloc(1, sizeof(*job));
  checked_match_t *cm = calloc(1, sizeof(*cm));

  job->messages_storage = calloc(1, sizeof(*job->messages_storage));
  job->message_patterns = calloc(1, sizeof(*job->message_patterns));
  job->message_patterns_len = 1;
  job->message_item_idx = 32;
  job->message_idx = 0;
  job->end_message_assembly = end_message_assembly;
  cm->msg_pattern.submatch_idx = 1;
  cm->parser_job = job;

  int ret = message_assembler(NULL, NULL, 3, cm);

  EXPECT_EQ_INT(-1, ret);

  sfree(cm);
  sfree(job->messages_storage);
  sfree(job->message_patterns);
  sfree(job);

  return 0;
}

DEF_TEST(msg_assembler_4) {
  parser_job_data_t *job = calloc(1, sizeof(*job));
  checked_match_t *cm = calloc(1, sizeof(*cm));

  cm->parser_job = job;
  cm->msg_pattern_idx = 0;
  cm->msg_pattern.submatch_idx = 0;
  cm->msg_pattern.regex = TEST_REGEX;
  job->messages_storage = calloc(1, sizeof(*job->messages_storage));
  job->message_patterns = calloc(1, sizeof(*job->message_patterns));
  job->message_patterns_len = 1;
  job->message_patterns[0].regex = cm->msg_pattern.regex;
  job->message_item_idx = 0;
  job->message_idx = 0;
  job->start_message_assembly = start_message_assembly_mock_error;

  int ret = message_assembler(NULL, NULL, 3, cm);
  EXPECT_EQ_INT(-1, ret);

  job->start_message_assembly = start_message_assembly_mock_success;
  job->message_idx = -1;
  job->messages_storage[0].started = true;
  job->messages_storage[0].completed = false;
  ret = message_assembler(NULL, NULL, 3, cm);
  EXPECT_EQ_INT(0, ret);

  job->message_idx = 0;
  job->messages_storage[0].started = false;
  job->messages_storage[0].completed = false;
  ret = message_assembler(NULL, NULL, 3, cm);
  EXPECT_EQ_INT(0, ret);

  job->message_idx = 0;
  job->messages_storage[0].started = true;
  job->messages_storage[0].completed = true;
  ret = message_assembler(NULL, NULL, 3, cm);
  EXPECT_EQ_INT(0, ret);

  job->messages_storage[0].completed = false;
  job->message_item_assembly = message_item_assembly_mock;
  job->end_message_assembly = end_message_assembly_mock;
  ret = message_assembler(NULL, NULL, 3, cm);

  EXPECT_EQ_INT(1, job->messages_storage[0].matched_patterns_check[0]);
  EXPECT_EQ_INT(0, ret);

  sfree(cm);
  sfree(job->messages_storage);
  sfree(job->message_patterns);
  sfree(job);

  return 0;
}

DEF_TEST(msg_parser_init) {
  message_pattern_t patterns[TEST_PATTERNS_LEN] = {
      {"pattern_1", "test_regex_1", 0, "", 1},
      {"pattern_2", "test_regex_2", 0, "", 0},
      {"pattern_3", "test_regex_3", 0, "", 0},
      {"pattern_4", "test_regex_4", 0, "", 1}

  };
  unsigned int start_idx = 0;
  unsigned int stop_idx = 1;
  const char *filename = TEST_FILENAME;
  parser_job_data_t *job = NULL;

  job = message_parser_init(filename, start_idx, stop_idx, patterns,
                            TEST_PATTERNS_LEN);

  OK(job != NULL);
  OK(job->resize_message_buffer == resize_message_buffer);
  OK(job->start_message_assembly == start_message_assembly);
  OK(job->end_message_assembly == end_message_assembly);
  OK(job->message_item_assembly == message_item_assembly);
  OK(job->messages_max_len == MSG_STOR_INIT_LEN);
  OK(job->filename == filename);
  OK(job->start_idx == start_idx);
  OK(job->stop_idx == stop_idx);
  OK(job->message_idx == -1);
  OK(job->messages_completed == 0);
  OK(memcmp(patterns, job->message_patterns,
            sizeof(message_pattern_t) * TEST_PATTERNS_LEN) == 0);
  OK(job->message_patterns_len == TEST_PATTERNS_LEN);

  sfree(job->messages_storage);
  sfree(job->message_patterns);
  tail_match_destroy(job->tm);
  sfree(job);

  return 0;
}

DEF_TEST(msg_parser_read_1) {
  parser_job_data_t *job = NULL;
  int ret = message_parser_read(job, NULL, 0);

  EXPECT_EQ_INT(-1, ret);

  return 0;
}

DEF_TEST(msg_parser_read_2) {

  message_pattern_t patterns[TEST_PATTERNS_LEN] = {
      {"pattern_1", "test_regex_1", 0, "", 1},
      {"pattern_2", "test_regex_2", 0, "", 0},
      {"pattern_3", "test_regex_3", 0, "", 0},
      {"pattern_4", "test_regex_4", 0, "", 1}

  };

  unsigned int start_idx = 0;
  unsigned int stop_idx = 1;
  const char *filename = TEST_FILENAME;
  parser_job_data_t *job = calloc(1, sizeof(*job));

  job->resize_message_buffer = resize_message_buffer;
  job->start_message_assembly = start_message_assembly;
  job->end_message_assembly = end_message_assembly;
  job->message_item_assembly = message_item_assembly;
  job->messages_max_len = MSG_STOR_INIT_LEN;
  job->filename = filename;
  job->start_idx = start_idx;
  job->stop_idx = stop_idx;
  job->message_idx = -1;
  job->messages_completed = 0;
  job->message_patterns = patterns;
  job->message_patterns_len = TEST_PATTERNS_LEN;
  job->tm = tail_match_create(job->filename);

  int ret = message_parser_read(job, NULL, 0);

  EXPECT_EQ_INT(-1, ret);

  tail_match_destroy(job->tm);
  sfree(job);

  return 0;
}

int main(void) {
  /* message_item_assembly */
  RUN_TEST(msg_item_assembly);
  /* start_message_item_assembly */
  RUN_TEST(start_msg_item_assembly_1);
  RUN_TEST(start_msg_item_assembly_2);
  RUN_TEST(start_msg_item_assembly_3);
  /* resize_message_buffer */
  RUN_TEST(resize_msg_buffer);
  /* end_message assembly */
  RUN_TEST(end_msg_assembly_1);
  RUN_TEST(end_msg_assembly_2);
  /* message_assembler */
  RUN_TEST(msg_assembler_1);
  RUN_TEST(msg_assembler_2);
  RUN_TEST(msg_assembler_3);
  RUN_TEST(msg_assembler_4);
  /* message_parser_init */
  RUN_TEST(msg_parser_init);
  /* message_parser_read */
  RUN_TEST(msg_parser_read_1);
  RUN_TEST(msg_parser_read_2);

  END_TEST;
}
