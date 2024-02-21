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

// clang-format off
/*
 * Explicit order is required or _FILE_OFFSET_BITS will have definition mismatches on Solaris
 * See Github Issue #3193 for details
 */
#include "utils/common/common.h"
#include "testing.h"
// clang-format on

#if HAVE_KSTAT_H
#include <kstat.h>
#endif

#if HAVE_LIBKSTAT
kstat_ctl_t *kc;
#endif /* HAVE_LIBKSTAT */

DEF_TEST(sstrncpy) {
  struct {
    char const *name;
    char const *src;
    size_t size;
    char const *want;
  } cases[] = {
      {
          .name = "normal copy",
          .src = "Hello, world!",
          .size = 16,
          .want = "Hello, world!",
      },
      {
          .name = "truncated copy",
          .src = "Hello, world!",
          .size = 8,
          .want = "Hello, ",
      },
      {
          .name = "NULL source is treated like an empty string",
          .src = NULL,
          .size = 8,
          .want = "",
      },
      {
          .name = "size is zero",
          .src = "test",
          .size = 0,
          .want = NULL,
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("## Case %zu: %s\n", i + 1, cases[i].name);

    char dest[cases[i].size + 1];
    memset(dest, 0xff, sizeof(dest));

    char *want_ret = (cases[i].size == 0) ? NULL : dest;
    EXPECT_EQ_PTR(want_ret, sstrncpy(dest, cases[i].src, cases[i].size));
    EXPECT_EQ_INT((char)0xff, dest[sizeof(dest) - 1]);
    if (cases[i].want == NULL) {
      continue;
    }
    EXPECT_EQ_STR(cases[i].want, dest);
  }

  printf("## Case %zu: dest is NULL\n", STATIC_ARRAY_SIZE(cases) + 1);
  EXPECT_EQ_PTR(NULL, sstrncpy(NULL, "test", 23));

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

    /* use (NULL, 0) to determine required buffer size. */
    EXPECT_EQ_INT(cases[i].want_return,
                  strjoin(NULL, 0, cases[i].fields, cases[i].fields_num,
                          cases[i].separator));
  }

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
    char buffer[32] = {0};

    strncpy(buffer, cases[i].str, sizeof(buffer) - 1);
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
    char buffer[16] = {0};

    strncpy(buffer, cases[i].str, sizeof(buffer) - 1);
    OK(escape_string(buffer, sizeof(buffer)) == 0);
    EXPECT_EQ_STR(cases[i].want, buffer);
  }

  return 0;
}

DEF_TEST(strunescape) {
  char buffer[32] = {0};
  int status;

  strncpy(buffer, "foo\\tbar", sizeof(buffer) - 1);
  status = strunescape(buffer, sizeof(buffer));
  OK(status == 0);
  EXPECT_EQ_STR("foo\tbar", buffer);

  strncpy(buffer, "\\tfoo\\r\\n", sizeof(buffer) - 1);
  status = strunescape(buffer, sizeof(buffer));
  OK(status == 0);
  EXPECT_EQ_STR("\tfoo\r\n", buffer);

  strncpy(buffer, "With \\\"quotes\\\"", sizeof(buffer) - 1);
  status = strunescape(buffer, sizeof(buffer));
  OK(status == 0);
  EXPECT_EQ_STR("With \"quotes\"", buffer);

  /* Backslash before null byte */
  strncpy(buffer, "\\tbackslash end\\", sizeof(buffer) - 1);
  status = strunescape(buffer, sizeof(buffer));
  OK(status != 0);
  EXPECT_EQ_STR("\tbackslash end", buffer);
  return 0;

  /* Backslash at buffer end */
  strncpy(buffer, "\\t3\\56", sizeof(buffer) - 1);
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

DEF_TEST(rate_to_value) {
  struct {
    char *name;
    gauge_t rate;
    rate_to_value_state_t state;
    metric_type_t type;
    cdtime_t time;
    value_t want;
    gauge_t want_residual;
    int want_err;
  } cases[] = {
      {
          .name = "zero value",
          .rate = 1.,
          .state = {.last_time = 0},
          .type = METRIC_TYPE_COUNTER,
          .time = TIME_T_TO_CDTIME_T(10),
          .want_err = EAGAIN,
      },
      {
          .name = "counter",
          .rate = 1.,
          .state =
              {
                  .last_value = {.counter = 1000},
                  .last_time = TIME_T_TO_CDTIME_T(10),
                  .residual = 0,
              },
          .type = METRIC_TYPE_COUNTER,
          .time = TIME_T_TO_CDTIME_T(20),
          .want = {.counter = 1010},
      },
      {
          .name = "residual gets rounded down",
          .rate = 0.999,
          .state =
              {
                  .last_value = {.counter = 1000},
                  .last_time = TIME_T_TO_CDTIME_T(10),
                  .residual = 0,
              },
          .type = METRIC_TYPE_COUNTER,
          .time = TIME_T_TO_CDTIME_T(20),
          .want = {.counter = 1009},
          .want_residual = 0.99,
      },
      {
          .name = "residual gets added to result",
          .rate = 0.0011,
          .state =
              {
                  .last_value = {.counter = 1000},
                  .last_time = TIME_T_TO_CDTIME_T(10),
                  .residual = 0.99,
              },
          .type = METRIC_TYPE_COUNTER,
          .time = TIME_T_TO_CDTIME_T(20),
          .want = {.counter = 1001},
          .want_residual = 0.001,
      },
      {
          .name = "fpcounter",
          .rate = 1.234,
          .state =
              {
                  .last_value = {.fpcounter = 1000},
                  .last_time = TIME_T_TO_CDTIME_T(10),
                  .residual = 0,
              },
          .type = METRIC_TYPE_FPCOUNTER,
          .time = TIME_T_TO_CDTIME_T(20),
          .want = {.fpcounter = 1012.34},
      },
      {
          .name = "derive",
          .rate = 1.,
          .state =
              {
                  .last_value = {.derive = 1000},
                  .last_time = TIME_T_TO_CDTIME_T(10),
                  .residual = 0,
              },
          .type = DS_TYPE_DERIVE,
          .time = TIME_T_TO_CDTIME_T(20),
          .want = {.derive = 1010},
      },
      {
          .name = "derive initialization with negative rate",
          .rate = 1.05,
          .state = {.last_time = 0},
          .type = DS_TYPE_DERIVE,
          .time = TIME_T_TO_CDTIME_T(20),
          .want_err = EAGAIN,
          .want_residual = .5,
      },
      {
          .name = "derive with negative rate",
          .rate = -1.,
          .state =
              {
                  .last_value = {.derive = 1000},
                  .last_time = TIME_T_TO_CDTIME_T(10),
                  .residual = 0,
              },
          .type = DS_TYPE_DERIVE,
          .time = TIME_T_TO_CDTIME_T(20),
          .want = {.derive = 990},
      },
      {
          .name = "residual gets rounded down",
          .rate = -1.01,
          .state =
              {
                  .last_value = {.derive = 1000},
                  .last_time = TIME_T_TO_CDTIME_T(10),
                  .residual = 0,
              },
          .type = DS_TYPE_DERIVE,
          .time = TIME_T_TO_CDTIME_T(20),
          .want = {.derive = 989},
          .want_residual = .9,
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("## Case %zu %s\n", i, cases[i].name);

    rate_to_value_state_t state = cases[i].state;
    value_t got = {0};
    EXPECT_EQ_INT(cases[i].want_err,
                  rate_to_value(&got, cases[i].rate, &state, cases[i].type,
                                cases[i].time));
    if (cases[i].want_err) {
      continue;
    }

    switch (cases[i].type) {
    case METRIC_TYPE_GAUGE:
      EXPECT_EQ_DOUBLE(cases[i].want.gauge, got.gauge);
      EXPECT_EQ_DOUBLE(cases[i].want.gauge, state.last_value.gauge);
      break;
    case METRIC_TYPE_COUNTER:
      EXPECT_EQ_UINT64(cases[i].want.counter, got.counter);
      EXPECT_EQ_UINT64(cases[i].want.counter, state.last_value.counter);
      break;
    case METRIC_TYPE_FPCOUNTER:
      EXPECT_EQ_DOUBLE(cases[i].want.fpcounter, got.fpcounter);
      EXPECT_EQ_UINT64(cases[i].want.fpcounter, state.last_value.fpcounter);
      break;
    case METRIC_TYPE_UP_DOWN:
      EXPECT_EQ_UINT64(cases[i].want.up_down, got.up_down);
      EXPECT_EQ_UINT64(cases[i].want.up_down, state.last_value.up_down);
      break;
    case METRIC_TYPE_UP_DOWN_FP:
      EXPECT_EQ_DOUBLE(cases[i].want.up_down_fp, got.up_down_fp);
      EXPECT_EQ_UINT64(cases[i].want.up_down_fp, state.last_value.up_down_fp);
      break;
    case METRIC_TYPE_UNTYPED:
      LOG(false, "invalid metric type");
      break;
    }

    EXPECT_EQ_UINT64(cases[i].time, state.last_time);
    EXPECT_EQ_DOUBLE(cases[i].want_residual, state.residual);
  }

  return 0;
}

DEF_TEST(value_to_rate) {
  struct {
    char *name;
    time_t t0;
    time_t t1;
    metric_type_t type;
    value_t v0;
    value_t v1;
    gauge_t want;
    int want_err;
  } cases[] = {
      {
          .name = "derive_t init",
          .t0 = 0,
          .t1 = 10,
          .type = DS_TYPE_DERIVE,
          .v0 = {.derive = 0},
          .v1 = {.derive = 1000},
          .want_err = EAGAIN,
      },
      {
          .name = "derive_t increase",
          .t0 = 10,
          .t1 = 20,
          .type = DS_TYPE_DERIVE,
          .v0 = {.derive = 1000},
          .v1 = {.derive = 2000},
          .want = 100.0,
      },
      {
          .name = "derive_t decrease",
          .t0 = 20,
          .t1 = 30,
          .type = DS_TYPE_DERIVE,
          .v0 = {.derive = 2000},
          .v1 = {.derive = 1800},
          .want = -20.0,
      },
      {
          .name = "counter_t init",
          .t0 = 0,
          .t1 = 10,
          .type = METRIC_TYPE_COUNTER,
          .v0 = {.counter = 0},
          .v1 = {.counter = 1000},
          .want_err = EAGAIN,
      },
      {
          .name = "counter_t increase",
          .t0 = 10,
          .t1 = 20,
          .type = METRIC_TYPE_COUNTER,
          .v0 = {.counter = 1000},
          .v1 = {.counter = 5000},
          .want = 400.0,
      },
      {
          .name = "counter_t 32bit wrap-around",
          .t0 = 20,
          .t1 = 30,
          .type = METRIC_TYPE_COUNTER,
          .v0 = {.counter = 4294967238ULL},
          .v1 = {.counter = 42},
          .want = 10.0,
      },
      {
          .name = "counter_t 64bit wrap-around",
          .t0 = 30,
          .t1 = 40,
          .type = METRIC_TYPE_COUNTER,
          .v0 = {.counter = 18446744073709551558ULL},
          .v1 = {.counter = 42},
          .want = 10.0,
      },
      {
          .name = "fpcounter_t init",
          .t0 = 0,
          .t1 = 10,
          .type = METRIC_TYPE_FPCOUNTER,
          .v0 = {.fpcounter = 0.},
          .v1 = {.fpcounter = 10.},
          .want_err = EAGAIN,
      },
      {
          .name = "fpcounter_t increase",
          .t0 = 10,
          .t1 = 20,
          .type = METRIC_TYPE_FPCOUNTER,
          .v0 = {.fpcounter = 10.},
          .v1 = {.fpcounter = 50.5},
          .want = (50.5 - 10.) / (20. - 10.),
      },
      {
          .name = "fpcounter_t reset",
          .t0 = 20,
          .t1 = 30,
          .type = METRIC_TYPE_FPCOUNTER,
          .v0 = {.fpcounter = 100.0},
          .v1 = {.fpcounter = 20.0},
          .want_err = EAGAIN,
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("## Case %zu %s\n", i, cases[i].name);

    cdtime_t t0 = TIME_T_TO_CDTIME_T(cases[i].t0);
    value_to_rate_state_t state = {
        .last_value = cases[i].v0,
        .last_time = t0,
    };
    gauge_t got = 0;
    EXPECT_EQ_INT(cases[i].want_err,
                  value_to_rate(&got, cases[i].v1, cases[i].type,
                                TIME_T_TO_CDTIME_T(cases[i].t1), &state));
    if (cases[i].want_err) {
      continue;
    }
    EXPECT_EQ_DOUBLE(cases[i].want, got);
  }

  return 0;
}

DEF_TEST(format_values) {
  struct {
    metric_type_t type;
    value_t value;
    char const *want;
  } cases[] = {
      {METRIC_TYPE_GAUGE, (value_t){.gauge = 47.11}, "1592558427.435:47.11"},
      {METRIC_TYPE_GAUGE, (value_t){.gauge = NAN}, "1592558427.435:nan"},
      {METRIC_TYPE_COUNTER, (value_t){.counter = 18446744073709551615LLU},
       "1592558427.435:18446744073709551615"},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    metric_family_t fam = {
        .name = "testing",
        .type = cases[i].type,
    };
    metric_t m = {
        .family = &fam,
        .value = cases[i].value,
        .time = MS_TO_CDTIME_T(1592558427435),
    };
    metric_family_metric_append(&fam, m);

    strbuf_t buf = STRBUF_CREATE;

    EXPECT_EQ_INT(0, format_values(&buf, &m, false));
    EXPECT_EQ_STR(cases[i].want, buf.ptr);

    STRBUF_DESTROY(buf);
    metric_family_metric_reset(&fam);
  }

  return 0;
}

DEF_TEST(string_has_suffix) {
  struct {
    char const *s;
    char const *suffix;
    bool want;
  } cases[] = {
      {"foo.bar", "bar", true},  {"foo.qux", "bar", false},
      {"foo.Bar", "bar", false}, {"foo", "foo", true},
      {"foo", "foo.bar", false}, {"foo", NULL, false},
      {NULL, "foo", false},
  };
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    EXPECT_EQ_INT(cases[i].want,
                  string_has_suffix(cases[i].s, cases[i].suffix));
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
  RUN_TEST(rate_to_value);
  RUN_TEST(value_to_rate);
  RUN_TEST(format_values);
  RUN_TEST(string_has_suffix);

  END_TEST;
}
