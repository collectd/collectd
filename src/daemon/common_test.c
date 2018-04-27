/**
 * collectd - src/tests/test_common.c
 * Copyright (C) 2013       Florian octo Forster
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

#include "common.h"
#include "testing.h"

#if HAVE_KSTAT_H
#include <kstat.h>
#endif

#if HAVE_LIBKSTAT
kstat_ctl_t *kc;
#endif /* HAVE_LIBKSTAT */

DEF_TEST(sstrncpy) {
  char buffer[16] = "";
  char *ptr = &buffer[4];
  char *ret;

  buffer[0] = buffer[1] = buffer[2] = buffer[3] = 0xff;
  buffer[12] = buffer[13] = buffer[14] = buffer[15] = 0xff;

  ret = sstrncpy(ptr, "foobar", 8);
  OK(ret == ptr);
  EXPECT_EQ_STR("foobar", ptr);
  OK(buffer[3] == buffer[12]);

  ret = sstrncpy(ptr, "abc", 8);
  OK(ret == ptr);
  EXPECT_EQ_STR("abc", ptr);
  OK(buffer[3] == buffer[12]);

  ret = sstrncpy(ptr, "collectd", 8);
  OK(ret == ptr);
  OK(ptr[7] == 0);
  EXPECT_EQ_STR("collect", ptr);
  OK(buffer[3] == buffer[12]);

  return 0;
}

DEF_TEST(sstrdup) {
  char *ptr;

  ptr = sstrdup("collectd");
  OK(ptr != NULL);
  EXPECT_EQ_STR("collectd", ptr);

  sfree(ptr);

  ptr = sstrdup(NULL);
  OK(ptr == NULL);

  return 0;
}

DEF_TEST(strsplit) {
  char buffer[32];
  char *fields[8];
  int status;

  strncpy(buffer, "foo bar", sizeof(buffer));
  status = strsplit(buffer, fields, 8);
  OK(status == 2);
  EXPECT_EQ_STR("foo", fields[0]);
  EXPECT_EQ_STR("bar", fields[1]);

  strncpy(buffer, "foo \t bar", sizeof(buffer));
  status = strsplit(buffer, fields, 8);
  OK(status == 2);
  EXPECT_EQ_STR("foo", fields[0]);
  EXPECT_EQ_STR("bar", fields[1]);

  strncpy(buffer, "one two\tthree\rfour\nfive", sizeof(buffer));
  status = strsplit(buffer, fields, 8);
  OK(status == 5);
  EXPECT_EQ_STR("one", fields[0]);
  EXPECT_EQ_STR("two", fields[1]);
  EXPECT_EQ_STR("three", fields[2]);
  EXPECT_EQ_STR("four", fields[3]);
  EXPECT_EQ_STR("five", fields[4]);

  strncpy(buffer, "\twith trailing\n", sizeof(buffer));
  status = strsplit(buffer, fields, 8);
  OK(status == 2);
  EXPECT_EQ_STR("with", fields[0]);
  EXPECT_EQ_STR("trailing", fields[1]);

  strncpy(buffer, "1 2 3 4 5 6 7 8 9 10 11 12 13", sizeof(buffer));
  status = strsplit(buffer, fields, 8);
  OK(status == 8);
  EXPECT_EQ_STR("7", fields[6]);
  EXPECT_EQ_STR("8", fields[7]);

  strncpy(buffer, "single", sizeof(buffer));
  status = strsplit(buffer, fields, 8);
  OK(status == 1);
  EXPECT_EQ_STR("single", fields[0]);

  strncpy(buffer, "", sizeof(buffer));
  status = strsplit(buffer, fields, 8);
  OK(status == 0);

  return 0;
}

DEF_TEST(strjoin) {
  struct {
    char **fields;
    size_t fields_num;
    char *separator;

    int want_return;
    char *want_buffer;
  } cases[] = {
      /* Normal case. */
      {(char *[]){"foo", "bar"}, 2, "!", 7, "foo!bar"},
      /* One field only. */
      {(char *[]){"foo"}, 1, "!", 3, "foo"},
      /* No fields at all. */
      {NULL, 0, "!", 0, ""},
      /* Longer separator. */
      {(char *[]){"foo", "bar"}, 2, "rcht", 10, "foorchtbar"},
      /* Empty separator. */
      {(char *[]){"foo", "bar"}, 2, "", 6, "foobar"},
      /* NULL separator. */
      {(char *[]){"foo", "bar"}, 2, NULL, 6, "foobar"},
      /* buffer not large enough -> string is truncated. */
      {(char *[]){"aaaaaa", "bbbbbb", "c!"}, 3, "-", 16, "aaaaaa-bbbbbb-c"},
      /* buffer not large enough -> last field fills buffer completely. */
      {(char *[]){"aaaaaaa", "bbbbbbb", "!"}, 3, "-", 17, "aaaaaaa-bbbbbbb"},
      /* buffer not large enough -> string does *not* end in separator. */
      {(char *[]){"aaaa", "bbbb", "cccc", "!"}, 4, "-", 16, "aaaa-bbbb-cccc"},
      /* buffer not large enough -> string does not end with partial
         separator. */
      {(char *[]){"aaaaaa", "bbbbbb", "!"}, 3, "+-", 17, "aaaaaa+-bbbbbb"},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    char buffer[16];
    int status;

    memset(buffer, 0xFF, sizeof(buffer));
    status = strjoin(buffer, sizeof(buffer), cases[i].fields,
                     cases[i].fields_num, cases[i].separator);
    EXPECT_EQ_INT(cases[i].want_return, status);
    EXPECT_EQ_STR(cases[i].want_buffer, buffer);
  }

  /* use (NULL, 0) to determine required buffer size. */
  EXPECT_EQ_INT(3, strjoin(NULL, 0, (char *[]){"a", "b"}, 2, "-"));

  return 0;
}

DEF_TEST(escape_slashes) {
  struct {
    char *str;
    char *want;
  } cases[] = {
      {"foo/bar/baz", "foo_bar_baz"},
      {"/like/a/path", "like_a_path"},
      {"trailing/slash/", "trailing_slash_"},
      {"foo//bar", "foo__bar"},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    char buffer[32];

    strncpy(buffer, cases[i].str, sizeof(buffer));
    OK(escape_slashes(buffer, sizeof(buffer)) == 0);
    EXPECT_EQ_STR(cases[i].want, buffer);
  }

  return 0;
}

DEF_TEST(escape_string) {
  struct {
    char *str;
    char *want;
  } cases[] = {
      {"foobar", "foobar"},
      {"f00bar", "f00bar"},
      {"foo bar", "\"foo bar\""},
      {"foo \"bar\"", "\"foo \\\"bar\\\"\""},
      {"012345678901234", "012345678901234"},
      {"012345 78901234", "\"012345 789012\""},
      {"012345 78901\"34", "\"012345 78901\""},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    char buffer[16];

    strncpy(buffer, cases[i].str, sizeof(buffer));
    OK(escape_string(buffer, sizeof(buffer)) == 0);
    EXPECT_EQ_STR(cases[i].want, buffer);
  }

  return 0;
}

DEF_TEST(strunescape) {
  char buffer[16];
  int status;

  strncpy(buffer, "foo\\tbar", sizeof(buffer));
  status = strunescape(buffer, sizeof(buffer));
  OK(status == 0);
  EXPECT_EQ_STR("foo\tbar", buffer);

  strncpy(buffer, "\\tfoo\\r\\n", sizeof(buffer));
  status = strunescape(buffer, sizeof(buffer));
  OK(status == 0);
  EXPECT_EQ_STR("\tfoo\r\n", buffer);

  strncpy(buffer, "With \\\"quotes\\\"", sizeof(buffer));
  status = strunescape(buffer, sizeof(buffer));
  OK(status == 0);
  EXPECT_EQ_STR("With \"quotes\"", buffer);

  /* Backslash before null byte */
  strncpy(buffer, "\\tbackslash end\\", sizeof(buffer));
  status = strunescape(buffer, sizeof(buffer));
  OK(status != 0);
  EXPECT_EQ_STR("\tbackslash end", buffer);
  return 0;

  /* Backslash at buffer end */
  strncpy(buffer, "\\t3\\56", sizeof(buffer));
  status = strunescape(buffer, 4);
  OK(status != 0);
  OK(buffer[0] == '\t');
  OK(buffer[1] == '3');
  OK(buffer[2] == 0);
  OK(buffer[3] == 0);
  OK(buffer[4] == '5');
  OK(buffer[5] == '6');
  OK(buffer[6] == '7');

  return 0;
}

DEF_TEST(parse_values) {
  struct {
    char buffer[64];
    int status;
    gauge_t value;
  } cases[] = {
      {"1435044576:42", 0, 42.0}, {"1435044576:42:23", -1, NAN},
      {"1435044576:U", 0, NAN},   {"N:12.3", 0, 12.3},
      {"N:42.0:23", -1, NAN},     {"N:U", 0, NAN},
      {"T:42.0", -1, NAN},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    data_source_t dsrc = {
        .name = "value", .type = DS_TYPE_GAUGE, .min = 0.0, .max = NAN,
    };
    data_set_t ds = {
        .type = "example", .ds_num = 1, .ds = &dsrc,
    };

    value_t v = {
        .gauge = NAN,
    };
    value_list_t vl = {
        .values = &v,
        .values_len = 1,
        .time = 0,
        .interval = 0,
        .host = "example.com",
        .plugin = "common_test",
        .type = "example",
        .meta = NULL,
    };

    int status = parse_values(cases[i].buffer, &vl, &ds);
    EXPECT_EQ_INT(cases[i].status, status);
    if (status != 0)
      continue;

    EXPECT_EQ_DOUBLE(cases[i].value, vl.values[0].gauge);
  }

  return 0;
}

DEF_TEST(value_to_rate) {
  struct {
    time_t t0;
    time_t t1;
    int ds_type;
    value_t v0;
    value_t v1;
    gauge_t want;
  } cases[] = {
      {0, 10, DS_TYPE_DERIVE, {.derive = 0}, {.derive = 1000}, NAN},
      {10, 20, DS_TYPE_DERIVE, {.derive = 1000}, {.derive = 2000}, 100.0},
      {20, 30, DS_TYPE_DERIVE, {.derive = 2000}, {.derive = 1800}, -20.0},
      {0, 10, DS_TYPE_COUNTER, {.counter = 0}, {.counter = 1000}, NAN},
      {10, 20, DS_TYPE_COUNTER, {.counter = 1000}, {.counter = 5000}, 400.0},
      /* 32bit wrap-around. */
      {20,
       30,
       DS_TYPE_COUNTER,
       {.counter = 4294967238ULL},
       {.counter = 42},
       10.0},
      /* 64bit wrap-around. */
      {30,
       40,
       DS_TYPE_COUNTER,
       {.counter = 18446744073709551558ULL},
       {.counter = 42},
       10.0},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    cdtime_t t0 = TIME_T_TO_CDTIME_T(cases[i].t0);
    value_to_rate_state_t state = {
        .last_value = cases[i].v0, .last_time = t0,
    };
    gauge_t got;

    if (cases[i].t0 == 0) {
      EXPECT_EQ_INT(EAGAIN,
                    value_to_rate(&got, cases[i].v1, cases[i].ds_type,
                                  TIME_T_TO_CDTIME_T(cases[i].t1), &state));
      continue;
    }

    EXPECT_EQ_INT(0, value_to_rate(&got, cases[i].v1, cases[i].ds_type,
                                   TIME_T_TO_CDTIME_T(cases[i].t1), &state));
    EXPECT_EQ_DOUBLE(cases[i].want, got);
  }

  return 0;
}

int main(void) {
  RUN_TEST(sstrncpy);
  RUN_TEST(sstrdup);
  RUN_TEST(strsplit);
  RUN_TEST(strjoin);
  RUN_TEST(escape_slashes);
  RUN_TEST(escape_string);
  RUN_TEST(strunescape);
  RUN_TEST(parse_values);
  RUN_TEST(value_to_rate);

  END_TEST;
}
