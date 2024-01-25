/**
 * collectd - src/write_redis.c
 * Copyright (C) 2024       Florian Forster
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
 *   Florian Forster <ff at octo.it>
 **/

#include "testing.h"
#include "write_redis.c" /* sic */

char **got_commands = NULL;
size_t got_commands_num = 0;

static int fake_execute(wr_node_t *node, int argc, char const **argv) {
  strbuf_t cmd = STRBUF_CREATE;
  for (int i = 0; i < argc; i++) {
    if (i != 0) {
      strbuf_print(&cmd, " ");
    }
    strbuf_print(&cmd, argv[i]);
  }

  got_commands =
      realloc(got_commands, sizeof(*got_commands) * (got_commands_num + 1));
  got_commands[got_commands_num] = strdup(cmd.ptr);
  got_commands_num++;

  STRBUF_DESTROY(cmd);

  return 0;
}

static void fake_disconnect(wr_node_t *node) {
  for (size_t i = 0; i < got_commands_num; i++) {
    free(got_commands[i]);
  }
  free(got_commands);
  got_commands = NULL;
  got_commands_num = 0;
}

static int fake_reconnect(wr_node_t *node) { return 0; }

DEF_TEST(wr_write) {
  uc_init();

  metric_family_t fam = {
      .name = "unit.test",
      .type = METRIC_TYPE_GAUGE,
      .resource =
          {
              .ptr =
                  (label_pair_t[]){
                      {"test", "wr_write"},
                  },
              .num = 1,
          },
      .metric =
          (metric_list_t){
              .ptr =
                  (metric_t[]){
                      {
                          .label =
                              {
                                  .ptr =
                                      (label_pair_t[]){
                                          {"metric.name", "m1"},
                                      },
                                  .num = 1,
                              },
                          .value = (value_t){.gauge = 42},
                          .time = TIME_T_TO_CDTIME_T(100),
                      },
                      {
                          .label =
                              {
                                  .ptr =
                                      (label_pair_t[]){
                                          {"metric.name", "m2"},
                                      },
                                  .num = 1,
                              },
                          .value = (value_t){.gauge = 23},
                          .time = DOUBLE_TO_CDTIME_T(100.123456780),
                      },
                  },
              .num = 2,
          },
  };

  for (size_t i = 0; i < fam.metric.num; i++) {
    fam.metric.ptr[i].family = &fam;
  }

  CHECK_ZERO(uc_update(&fam));

  wr_node_t node = {
      .store_rates = false,

      .reconnect = fake_reconnect,
      .disconnect = fake_disconnect,
      .execute = fake_execute,

      .lock = PTHREAD_MUTEX_INITIALIZER,
  };

  user_data_t ud = {
      .data = &node,
  };

  CHECK_ZERO(wr_write(&fam, &ud));

#define RESOURCE_ID "{\"test\":\"wr_write\"}"
#define METRIC_ONE_ID                                                          \
  "{\"name\":\"unit.test\",\"resource\":" RESOURCE_ID ",\"labels\":"           \
  "{\"metric.name\":\"m1\"}}"
#define METRIC_TWO_ID                                                          \
  "{\"name\":\"unit.test\",\"resource\":" RESOURCE_ID ",\"labels\":"           \
  "{\"metric.name\":\"m2\"}}"
  char *want_commands_new[] = {
      "ZADD metric/" METRIC_ONE_ID " 100.000000000 100.000:42",
      "SADD resource/" RESOURCE_ID " metric/" METRIC_ONE_ID,
      "ZADD metric/" METRIC_TWO_ID " 100.123456780 100.123:23",
      "SADD resource/" RESOURCE_ID " metric/" METRIC_TWO_ID,
      "SADD resources resource/" RESOURCE_ID,
  };
  size_t want_commands_new_num = STATIC_ARRAY_SIZE(want_commands_new);

  for (size_t i = 0; i < want_commands_new_num && i < got_commands_num; i++) {
    EXPECT_EQ_STR(want_commands_new[i], got_commands[i]);
  }
  EXPECT_EQ_INT(want_commands_new_num, got_commands_num);

  // clear the global got_commands array
  node.disconnect(&node);

  // advance time
  cdtime_t interval = TIME_T_TO_CDTIME_T(10);
  for (size_t i = 0; i < fam.metric.num; i++) {
    fam.metric.ptr[i].time += interval;
  }

  CHECK_ZERO(uc_update(&fam));

  CHECK_ZERO(wr_write(&fam, &ud));

  // for known metrics we expect only the ZADD commands
  char *want_commands_known[] = {
      "ZADD metric/" METRIC_ONE_ID " 110.000000000 110.000:42",
      "ZADD metric/" METRIC_TWO_ID " 110.123456780 110.123:23",
  };
  size_t want_commands_known_num = STATIC_ARRAY_SIZE(want_commands_known);

  for (size_t i = 0; i < want_commands_known_num && i < got_commands_num; i++) {
    EXPECT_EQ_STR(want_commands_known[i], got_commands[i]);
  }
  EXPECT_EQ_INT(want_commands_known_num, got_commands_num);

  // clear the global got_commands array
  node.disconnect(&node);

  return 0;
}

int main(void) {
  RUN_TEST(wr_write);

  END_TEST;
}
