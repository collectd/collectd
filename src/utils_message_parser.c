/*
 * collectd - src/utils_message_parser.c
 * MIT License
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
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
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include "utils_message_parser.h"

#define UTIL_NAME "utils_message_parser"

#define MSG_STOR_INIT_LEN 64
#define MSG_STOR_INC_STEP 10

typedef struct checked_match_t {
  parser_job_data *parser_job;
  message_pattern msg_pattern;
  int msg_pattern_idx;
} checked_match;

struct parser_job_data_t {
  const char *filename;
  unsigned int start_idx;
  unsigned int stop_idx;
  cu_tail_match_t *tm;
  message *messages_storage;
  size_t messages_max_len;
  int message_idx;
  unsigned int message_item_idx;
  unsigned int messages_completed;
  message_pattern *message_patterns;
  size_t message_patterns_len;
  int (*resize_message_buffer)(parser_job_data *self, size_t);
  int (*start_message_assembly)(parser_job_data *self);
  void (*end_message_assembly)(parser_job_data *self);
  void (*message_item_assembly)(parser_job_data *self, checked_match *cm,
                                char *const *matches);
};

static void message_item_assembly(parser_job_data *self, checked_match *cm,
                                  char *const *matches) {
  message_item *msg_it = &(self->messages_storage[self->message_idx]
                               .message_items[self->message_item_idx]);
  sstrncpy(msg_it->name, (cm->msg_pattern).name, sizeof(msg_it->name));
  sstrncpy(msg_it->value, matches[cm->msg_pattern.submatch_idx],
           sizeof(msg_it->value));
  self->messages_storage[self->message_idx]
      .matched_patterns_check[cm->msg_pattern_idx] = 1;
  ++(self->message_item_idx);
}

static int start_message_assembly(parser_job_data *self) {
  /* Remove previous message assembly if unfinished */
  if (self->message_idx >= 0 &&
      self->messages_storage[self->message_idx].started == 1 &&
      self->messages_storage[self->message_idx].completed == 0) {
    DEBUG(UTIL_NAME ": Removing unfinished assembly of previous message");
    self->messages_storage[self->message_idx] = (message){0};
    self->message_item_idx = 0;
  } else
    ++(self->message_idx);

  /* Resize messages buffer if needed */
  if (self->message_idx >= self->messages_max_len) {
    INFO(UTIL_NAME ": Exceeded message buffer size: %lu", self->messages_max_len);
    if (self->resize_message_buffer(self, self->messages_max_len +
                                              MSG_STOR_INC_STEP) != 0) {
      ERROR(UTIL_NAME ": Insufficient message buffer size: %lu. Remaining "
                      "messages for this read will be skipped",
            self->messages_max_len);
      self->message_idx = self->messages_max_len;
      return -1;
    }
  }
  self->messages_storage[self->message_idx] = (message){0};
  self->message_item_idx = 0;
  self->messages_storage[self->message_idx].started = 1;
  self->messages_storage[self->message_idx].completed = 0;
  return 0;
}

static int resize_message_buffer(parser_job_data *self, size_t new_size) {
  INFO(UTIL_NAME ": Resizing message buffer size to %lu", new_size);
  void *new_storage = realloc(self->messages_storage,
                              new_size * sizeof(*(self->messages_storage)));
  if (new_storage == NULL) {
    ERROR(UTIL_NAME ": Error while reallocating message buffer");
    return -1;
  }
  self->messages_storage = new_storage;
  self->messages_max_len = new_size;
  /* Fill unused memory with 0 */
  memset(self->messages_storage + self->message_idx, 0,
         (self->messages_max_len - self->message_idx) *
             sizeof(*(self->messages_storage)));
  return 0;
}

static void end_message_assembly(parser_job_data *self) {
  /* Checking mandatory items */
  for (size_t i = 0; i < self->message_patterns_len; i++) {
    if (self->message_patterns[i].is_mandatory &&
        !(self->messages_storage[self->message_idx]
              .matched_patterns_check[i])) {
      WARNING(
          UTIL_NAME
          ": Mandatory message item pattern %s not found. Message discarded",
          self->message_patterns[i].regex);
      self->messages_storage[self->message_idx] = (message){0};
      self->message_item_idx = 0;
      if (self->message_idx > 0)
        --(self->message_idx);
      return;
    }
  }
  self->messages_storage[self->message_idx].completed = 1;
  self->message_item_idx = 0;
}

static int message_assembler(const char *row, char *const *matches,
                             size_t matches_num, void *user_data) {
  if (user_data == NULL) {
    ERROR(UTIL_NAME ": Invalid user_data pointer");
    return -1;
  }
  checked_match *cm = (checked_match *)user_data;
  parser_job_data *parser_job = cm->parser_job;

  if (cm->msg_pattern.submatch_idx < 0 ||
      cm->msg_pattern.submatch_idx >= matches_num) {
    ERROR(UTIL_NAME ": Invalid target submatch index: %d",
          cm->msg_pattern.submatch_idx);
    return -1;
  }
  if (parser_job->message_item_idx >=
      STATIC_ARRAY_SIZE(parser_job->messages_storage[parser_job->message_idx]
                            .message_items)) {
    ERROR(UTIL_NAME ": Message items number exceeded. Forced message end.");
    parser_job->end_message_assembly(parser_job);
    return -1;
  }

  /* Every matched start pattern resets current message items and starts
   * assembling new messages */
  if (strcmp((cm->msg_pattern).regex,
             parser_job->message_patterns[parser_job->start_idx].regex) == 0) {
    DEBUG(UTIL_NAME ": Found beginning pattern");
    if (parser_job->start_message_assembly(parser_job) != 0)
      return -1;
  }
  /* Ignoring message items without corresponding start item */
  if (parser_job->message_idx < 0 ||
      parser_job->messages_storage[parser_job->message_idx].started == 0) {
    DEBUG(UTIL_NAME ": Dropping item with no corresponding start element");
    return 0;
  }
  /* Populate message items */
  parser_job->message_item_assembly(parser_job, cm, matches);

  /* Handle message ending */
  if (strcmp((cm->msg_pattern).regex,
             parser_job->message_patterns[parser_job->stop_idx].regex) == 0) {
    DEBUG(UTIL_NAME ": Found ending pattern");
    parser_job->end_message_assembly(parser_job);
  }
  return 0;
}

parser_job_data *message_parser_init(const char *filename,
                                     unsigned int start_idx,
                                     unsigned int stop_idx,
                                     message_pattern message_patterns[],
                                     size_t message_patterns_len) {
  parser_job_data *parser_job = calloc(1, sizeof(*parser_job));
  if (parser_job == NULL) {
    ERROR(UTIL_NAME ": Error allocating parser_job");
    return NULL;
  }
  parser_job->resize_message_buffer = resize_message_buffer;
  parser_job->start_message_assembly = start_message_assembly;
  parser_job->end_message_assembly = end_message_assembly;
  parser_job->message_item_assembly = message_item_assembly;
  parser_job->messages_max_len = MSG_STOR_INIT_LEN;
  parser_job->filename = filename;
  parser_job->start_idx = start_idx;
  parser_job->stop_idx = stop_idx;
  parser_job->message_idx = -1;
  parser_job->message_patterns =
      calloc(message_patterns_len, sizeof(*(parser_job->message_patterns)));
  if (parser_job->message_patterns == NULL) {
    ERROR(UTIL_NAME ": Error allocating message_patterns");
    return NULL;
  }
  parser_job->messages_storage = calloc(
      parser_job->messages_max_len, sizeof(*(parser_job->messages_storage)));
  if (parser_job->messages_storage == NULL) {
    ERROR(UTIL_NAME ": Error allocating messages_storage");
    return NULL;
  }
  /* Crete own copy of regex patterns */
  memcpy(parser_job->message_patterns, message_patterns,
         sizeof(*(parser_job->message_patterns)) * message_patterns_len);
  parser_job->message_patterns_len = message_patterns_len;
  /* Init tail match */
  parser_job->tm = tail_match_create(parser_job->filename);
  if (parser_job->tm == NULL) {
    ERROR(UTIL_NAME ": Error creating tail match");
    return NULL;
  }

  for (size_t i = 0; i < message_patterns_len; i++) {
    /* Create current_match container for passing regex info
     * to callback function */
    checked_match *current_match = calloc(1, sizeof(*current_match));
    if (current_match == NULL) {
      ERROR(UTIL_NAME ": Error allocating current_match");
      return NULL;
    }
    current_match->parser_job = parser_job;
    current_match->msg_pattern = message_patterns[i];
    current_match->msg_pattern_idx = i;
    /* Create callback */
    cu_match_t *m = match_create_callback(
        message_patterns[i].regex, message_patterns[i].excluderegex,
        message_assembler, current_match, free);
    if (m == NULL) {
      ERROR(UTIL_NAME ": Error creating match callback");
      return NULL;
    }
    if (tail_match_add_match(parser_job->tm, m, 0, 0, 0) != 0) {
      ERROR(UTIL_NAME ": Error adding match callback");
      return NULL;
    }
  }

  return parser_job;
}

int message_parser_read(parser_job_data *parser_job, message **messages_storage,
                        _Bool force_rewind) {
  if (parser_job == NULL) {
    ERROR(UTIL_NAME ": Invalid parser_job pointer");
    return -1;
  }

  /* Finish incomplete message assembly in this read */
  if (parser_job->message_idx >= 0 &&
      parser_job->messages_storage[parser_job->message_idx].started &&
      !(parser_job->messages_storage[parser_job->message_idx].completed)) {
    INFO(UTIL_NAME ": Found incomplete message from previous read.");
    message tmp_message = parser_job->messages_storage[parser_job->message_idx];
    int tmp_message_item_idx = parser_job->message_item_idx;
    memset(parser_job->messages_storage, 0,
           parser_job->messages_max_len *
               sizeof(*(parser_job->messages_storage)));
    memcpy(parser_job->messages_storage, &tmp_message,
           sizeof(*(parser_job->messages_storage)));
    parser_job->message_item_idx = tmp_message_item_idx;
    parser_job->message_idx = 0;
  } else {
    memset(parser_job->messages_storage, 0,
           parser_job->messages_max_len *
               sizeof(*(parser_job->messages_storage)));
    parser_job->message_item_idx = 0;
    parser_job->message_idx = -1;
  }

  int status = tail_match_read(parser_job->tm, force_rewind);
  if (status != 0) {
    ERROR(UTIL_NAME ": Error while parser read. Status: %d", status);
    return -1;
  }

  /* Get no of messagess completed in this read */
  int messages_completed = 0;
  for (size_t i = 0; i < parser_job->messages_max_len; i++) {
    if (parser_job->messages_storage[i].completed)
      ++messages_completed;
  }
  *messages_storage = parser_job->messages_storage;
  return messages_completed;
}

void message_parser_cleanup(parser_job_data *parser_job) {
  if (parser_job == NULL) {
    ERROR(UTIL_NAME ": Invalid parser_job pointer");
    return;
  }
  sfree(parser_job->messages_storage);
  sfree(parser_job->message_patterns);
  if (parser_job->tm)
    tail_match_destroy(parser_job->tm);
  sfree(parser_job);
}
