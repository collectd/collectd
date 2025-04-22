/**
 * collectd - src/utils/cmds/putval_test.c
 * Copyright (C) 2023       Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 */

#include "collectd.h"

#include "daemon/plugin.h"
#include "testing.h"
#include "utils/cmds/cmds.h"
#include "utils/cmds/putval.h"
#include "utils/common/common.h"

static int value_list_compare(value_list_t const *want,
                              value_list_t const *got) {
  EXPECT_EQ_STR(want->host, got->host);
  EXPECT_EQ_STR(want->plugin, got->plugin);
  EXPECT_EQ_STR(want->plugin_instance, got->plugin_instance);
  EXPECT_EQ_STR(want->type, got->type);
  EXPECT_EQ_STR(want->type_instance, got->type_instance);

  EXPECT_EQ_UINT64(want->time, got->time);
  EXPECT_EQ_UINT64(want->interval, got->interval);
  EXPECT_EQ_UINT64(want->values_len, got->values_len);

  for (size_t i = 0; i < want->values_len; i++) {
    EXPECT_EQ_UINT64(want->values[i].derive, got->values[i].derive);
  }

  return 0;
}

static void err_callback(void *ud, cmd_status_t status, const char *format,
                         va_list ap) {
  vprintf(format, ap);
  puts("");
}

DEF_TEST(cmd_parse_putval) {
  struct {
    int argc;
    char **argv;
    size_t want_num;
    value_list_t *want;
  } cases[] = {
      {
          .argc = 3,
          .argv =
              (char *[]){
                  "/MAGIC",
                  "interval=1",
                  "1685945973:281000",
              },
          .want_num = 1,
          .want =
              &(value_list_t){
                  .host = "example.com",
                  .type = "MAGIC",
                  .time = TIME_T_TO_CDTIME_T_STATIC(1685945973),
                  .interval = TIME_T_TO_CDTIME_T_STATIC(1),
                  .values_len = 1,
                  .values = &(value_t){.derive = 281000},
              },
      },
      {
          .argc = 4,
          .argv =
              (char *[]){
                  "/MAGIC",
                  "interval=1",
                  "1685945973:281000",
                  "1685945974:562000",
              },
          .want_num = 2,
          .want =
              (value_list_t[]){
                  {
                      .host = "example.com",
                      .type = "MAGIC",
                      .time = TIME_T_TO_CDTIME_T_STATIC(1685945973),
                      .interval = TIME_T_TO_CDTIME_T_STATIC(1),
                      .values_len = 1,
                      .values = &(value_t){.derive = 281000},
                  },
                  {
                      .host = "example.com",
                      .type = "MAGIC",
                      .time = TIME_T_TO_CDTIME_T_STATIC(1685945974),
                      .interval = TIME_T_TO_CDTIME_T_STATIC(1),
                      .values_len = 1,
                      .values = &(value_t){.derive = 562000},
                  },
              },
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    cmd_putval_t got = {0};

    cmd_options_t opts = {
        .identifier_default_host = "example.com",
    };

    cmd_error_handler_t err_hndl = {
        .cb = err_callback,
    };
    EXPECT_EQ_INT(CMD_OK, cmd_parse_putval(cases[i].argc, cases[i].argv, &got,
                                           &opts, &err_hndl));

    EXPECT_EQ_UINT64(cases[i].want_num, got.vl_num);
    for (size_t j = 0; j < cases[i].want_num; j++) {
      value_list_compare(&cases[i].want[j], &got.vl[j]);
    }

    cmd_destroy_putval(&got);
  }

  return 0;
}

int main(void) {
  RUN_TEST(cmd_parse_putval);

  END_TEST;
}
