/*
 * collectd - src/utils/message_parser/message_parser.h
 * MIT License
 *
 * Copyright(c) 2017-2018 Intel Corporation. All rights reserved.
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
 *   Krzysztof Matczak <krzysztofx.matczak@intel.com>
 *   Marcin Mozejko <marcinx.mozejko@intel.com>
 */

#ifndef UTILS_MESSAGE_PARSER_H
#define UTILS_MESSAGE_PARSER_H 1

#include "utils_tail_match.h"

typedef struct message_pattern_s {
  /* User defined name for message item */
  char *name;
  /* Regular expression for finding out specific message item. The match result
   * is taken only from submatch pointed by submatch_idx variable, so multiple
   * submatches are supported. Example syntax:
   * "(STRING1|STRING2)(.*)"
   * For this example (STRING1|STRING2) is the first submatch and (.*) second
   * so user can choose which one to store by setting submatch_idx argument.
   */
  char *regex;
  /* Index of regex submatch that is stored as result. Value 0 takes whole
   * match result. Value -1 does not add result to message item. */
  int submatch_idx;
  /* Regular expression that excludes string from further processing */
  char *excluderegex;
  /* Flag indicating if this item is mandatory for message validation */
  bool is_mandatory;
  /* Pointer to optional user data */
  void *user_data;
  /* Freeing function */
  void (*free_user_data)(void *data);
} message_pattern_t;

typedef struct message_item_s {
  char name[32];
  char value[64];
  void *user_data;
  void (*free_user_data)(void *data);
} message_item_t;

typedef struct message_s {
  message_item_t message_items[32];
  int matched_patterns_check[32];
  bool started;
  bool completed;
} message_t;

typedef struct parser_job_data_s parser_job_data_t;

/*
 * NAME
 *   message_parser_init
 *
 * DESCRIPTION
 *   Configures unique message parser job. Performs necessary buffer
 *   allocations. If there's need to create multiple parser jobs with different
 *   parameters, this function may be called many times accordingly.
 *
 * PARAMETERS
 *   `filename'   The name of the file to be parsed.
 *   `start_idx'  Index of regular expression that indicates beginning of
 *                message.
 *                This index refers to 'message_patterns' array.
 *   `stop_idx'   Index of the regular expression that indicates the end of
 *                message. This index refers to 'message_patterns' array.
 *   `message_patterns' Array of regular expression patterns for populating
 *                      message items. Example:
 *    message_pattern message_patterns[]= {
 *      {.name = "START", .regex = "(Running trigger.*)", submatch_idx = 1,
 *       .excluderegex = "kernel", .is_mandatory = 1},
 *      {.name = "BANK", .regex = "BANK ([0-9]*)", submatch_idx = 1,
 *       .excluderegex = "kernel", .is_mandatory = 1},
 *      {.name = "TSC", .regex = "TSC ([a-z0-9]*)", submatch_idx = 1,
 *       .excluderegex = "kernel", .is_mandatory = 1},
 *      {.name = "MCA", .regex = "MCA: (.*)", submatch_idx = 1,
 *       .excluderegex = "kernel", .is_mandatory = 0},
 *      {.name = "END", .regex = "(CPUID Vendor.*)", submatch_idx = 1,
 *       .excluderegex = "kernel", .is_mandatory = 1}
 *    };
 *
 *   `message_patterns_len' Length of message_patterns array.
 *
 * RETURN VALUE
 *   Returns NULL upon failure, pointer to new parser job otherwise.
 */
parser_job_data_t *message_parser_init(const char *filename,
                                       unsigned int start_idx,
                                       unsigned int stop_idx,
                                       message_pattern_t message_patterns[],
                                       size_t message_patterns_len);

/*
 * NAME
 *   message_parser_read
 *
 * DESCRIPTION
 *   Collects all new messages matching criteria set in 'message_parser_init'.
 *   New messages means those reported or finished after previous call for this
 *   function.
 *
 * PARAMETERS
 *   `parser_job' Pointer to parser job to read.
 *   `messages_storage' Pointer to be set to internally allocated messages
 *                      storage.
 *   `force_rewind' If value set to non zero, file will be parsed from
 *                  beginning, otherwise tailing end of file.
 *
 * RETURN VALUE
 *   Returns -1 upon failure, number of messages collected from last read
 *   otherwise.
 */
int message_parser_read(parser_job_data_t *parser_job,
                        message_t **messages_storage, bool force_rewind);

/*
 * NAME
 *   message_parser_cleanup
 *
 * DESCRIPTION
 *   Performs parser job resource deallocation and cleanup.
 *
 * PARAMETERS
 *   `parser_job' Pointer to parser job to be cleaned up.
 *
 */
void message_parser_cleanup(parser_job_data_t *parser_job);

#endif /* UTILS_MESSAGE_PARSER_H */
