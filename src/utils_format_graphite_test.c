/**
 * collectd - src/utils_format_graphite_test.c
 * Copyright (C) 2016       Florian octo Forster
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

#include "common.h" /* for STATIC_ARRAY_SIZE */
#include "testing.h"
#include "utils_format_graphite.h"

static data_set_t ds_single = {
    .type = "single",
    .ds_num = 1,
    .ds = &(data_source_t){"value", DS_TYPE_GAUGE, NAN, NAN},
};

/*
static data_set_t ds_double = {
    .type = "double",
    .ds_num = 2,
    .ds =
        (data_source_t[]){
            {"one", DS_TYPE_DERIVE, 0, NAN}, {"two", DS_TYPE_DERIVE, 0, NAN},
        },
};
*/

DEF_TEST(metric_name) {
  struct {
    char *plugin_instance;
    char *type_instance;
    char *prefix;
    char *suffix;
    unsigned int flags;
    char *want_name;
  } cases[] = {
      {
          .want_name = "example@com.test.single",
      },
      /* plugin and type instances */
      {
          .plugin_instance = "foo",
          .type_instance = "bar",
          .want_name = "example@com.test-foo.single-bar",
      },
      {
          .plugin_instance = NULL,
          .type_instance = "bar",
          .want_name = "example@com.test.single-bar",
      },
      {
          .plugin_instance = "foo",
          .type_instance = NULL,
          .want_name = "example@com.test-foo.single",
      },
      /* special chars */
      {
          .plugin_instance = "foo (test)",
          .type_instance = "test: \"hello\"",
          .want_name = "example@com.test-foo@@test@.single-test@@@hello@",
      },
      /* flag GRAPHITE_SEPARATE_INSTANCES */
      {
          .plugin_instance = "foo",
          .type_instance = "bar",
          .flags = GRAPHITE_SEPARATE_INSTANCES,
          .want_name = "example@com.test.foo.single.bar",
      },
      /* flag GRAPHITE_ALWAYS_APPEND_DS */
      {
          .plugin_instance = "foo",
          .type_instance = "bar",
          .flags = GRAPHITE_ALWAYS_APPEND_DS,
          .want_name = "example@com.test-foo.single-bar.value",
      },
      /* flag GRAPHITE_PRESERVE_SEPARATOR */
      {
          .plugin_instance = "f.o.o",
          .type_instance = "b.a.r",
          .flags = 0,
          .want_name = "example@com.test-f@o@o.single-b@a@r",
      },
      {
          .plugin_instance = "f.o.o",
          .type_instance = "b.a.r",
          .flags = GRAPHITE_PRESERVE_SEPARATOR,
          .want_name = "example.com.test-f.o.o.single-b.a.r",
      },
      /* prefix and suffix */
      {
          .prefix = "foo.",
          .suffix = ".bar",
          .want_name = "foo.example@com.bar.test.single",
      },
      {
          .prefix = NULL,
          .suffix = ".bar",
          .want_name = "example@com.bar.test.single",
      },
      {
          .prefix = "foo.",
          .suffix = NULL,
          .want_name = "foo.example@com.test.single",
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    value_list_t vl = {
        .values = &(value_t){.gauge = 42},
        .values_len = 1,
        .time = TIME_T_TO_CDTIME_T_STATIC(1480063672),
        .interval = TIME_T_TO_CDTIME_T_STATIC(10),
        .host = "example.com",
        .plugin = "test",
        .type = "single",
    };

    char want[1024];
    snprintf(want, sizeof(want), "%s 42 1480063672\r\n", cases[i].want_name);

    if (cases[i].plugin_instance != NULL)
      sstrncpy(vl.plugin_instance, cases[i].plugin_instance,
               sizeof(vl.plugin_instance));
    if (cases[i].type_instance != NULL)
      sstrncpy(vl.type_instance, cases[i].type_instance,
               sizeof(vl.type_instance));

    char got[1024];
    EXPECT_EQ_INT(0, format_graphite(got, sizeof(got), &ds_single, &vl,
                                     cases[i].prefix, cases[i].suffix, '@',
                                     cases[i].flags));
    EXPECT_EQ_STR(want, got);
  }

  return 0;
}

DEF_TEST(null_termination) {
  value_list_t vl = {
      .values = &(value_t){.gauge = 1337},
      .values_len = 1,
      .time = TIME_T_TO_CDTIME_T_STATIC(1480063672),
      .interval = TIME_T_TO_CDTIME_T_STATIC(10),
      .host = "example.com",
      .plugin = "test",
      .type = "single",
  };
  char const *want = "example_com.test.single 1337 1480063672\r\n";

  char buffer[128];
  for (size_t i = 0; i < sizeof(buffer); i++)
    buffer[i] = (char)i;

  EXPECT_EQ_INT(0, format_graphite(buffer, sizeof(buffer), &ds_single, &vl,
                                   NULL, NULL, '_', 0));
  EXPECT_EQ_STR(want, buffer);
  EXPECT_EQ_INT(0, buffer[strlen(want)]);
  for (size_t i = strlen(want) + 1; i < sizeof(buffer); i++)
    EXPECT_EQ_INT((int)i, (int)buffer[i]);

  return 0;
}

int main(void) {
  RUN_TEST(metric_name);
  RUN_TEST(null_termination);

  END_TEST;
}
