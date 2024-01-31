/**
 * collectd - src/tests/utils_cmds_test.c
 * Copyright (C) 2016       Sebastian 'tokkee' Harl
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
 *   Sebastian 'tokkee' Harl <sh at tokkee.org>
 **/

// clang-format off
/*
 * Explicit order is required or _FILE_OFFSET_BITS will have definition mismatches on Solaris
 * See Github Issue #3193 for details
 */
#include "utils/cmds/cmds.h"

#include "utils/common/common.h"
#include "utils/strbuf/strbuf.h"
#include "testing.h"
#include "utils/cmds/putmetric.h"
// clang-format on

static void error_cb(void *ud, cmd_status_t status, const char *format,
                     va_list ap) {
  if (status == CMD_OK)
    return;

  strbuf_t *buf = ud;

  strbuf_printf(buf, "ERROR[%d]: ", status);

  va_list ap_copy;
  va_copy(ap_copy, ap);

  int size = vsnprintf(NULL, 0, format, ap_copy);
  assert(size > 0);

  char buffer[size + 1];
  vsnprintf(buffer, sizeof(buffer), format, ap);

  strbuf_print(buf, buffer);
} /* void error_cb */

static cmd_options_t default_host_opts = {
    /* identifier_default_host = */ "dummy-host",
};

static struct {
  char *input;
  cmd_options_t *opts;
  cmd_status_t expected_status;
  cmd_type_t expected_type;
} parse_data[] = {
    /* Valid FLUSH commands. */
    {
        "FLUSH",
        NULL,
        CMD_OK,
        CMD_FLUSH,
    },
    {
        "FLUSH identifier=myhost/magic/MAGIC",
        NULL,
        CMD_OK,
        CMD_FLUSH,
    },
    {
        "FLUSH identifier=magic/MAGIC",
        &default_host_opts,
        CMD_OK,
        CMD_FLUSH,
    },
    {
        "FLUSH timeout=123 plugin=\"A\"",
        NULL,
        CMD_OK,
        CMD_FLUSH,
    },
    /* Invalid FLUSH commands. */
    {
        /* Missing hostname; no default. */
        "FLUSH identifier=magic/MAGIC",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        /* Missing 'identifier' key. */
        "FLUSH myhost/magic/MAGIC",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        /* Invalid timeout. */
        "FLUSH timeout=A",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        /* Invalid identifier. */
        "FLUSH identifier=invalid",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        /* Invalid option. */
        "FLUSH invalid=option",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },

    /* Valid GETVAL commands. */
    {
        "GETVAL myhost/magic/MAGIC",
        NULL,
        CMD_OK,
        CMD_GETVAL,
    },
#if 0
    /* TODO(octo): implement default host behavior or remove test. */
    {
        "GETVAL magic/MAGIC",
        &default_host_opts,
        CMD_OK,
        CMD_GETVAL,
    },
#endif

    /* Invalid GETVAL commands. */
    {
        "GETVAL magic/MAGIC",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        "GETVAL",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        "GETVAL invalid",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },

    /* Valid LISTVAL commands. */
    {
        "LISTVAL",
        NULL,
        CMD_OK,
        CMD_LISTVAL,
    },

    /* Invalid LISTVAL commands. */
    {
        "LISTVAL invalid",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },

    /* Valid PUTVAL commands. */
    {
        "PUTVAL magic/MAGIC N:42",
        &default_host_opts,
        CMD_OK,
        CMD_PUTVAL,
    },
    {
        "PUTVAL myhost/magic/MAGIC N:42",
        NULL,
        CMD_OK,
        CMD_PUTVAL,
    },
    {
        "PUTVAL myhost/magic/MAGIC 1234:42",
        NULL,
        CMD_OK,
        CMD_PUTVAL,
    },
    {
        "PUTVAL myhost/magic/MAGIC 1234:42 2345:23",
        NULL,
        CMD_OK,
        CMD_PUTVAL,
    },
    {
        "PUTVAL myhost/magic/MAGIC interval=2 1234:42",
        NULL,
        CMD_OK,
        CMD_PUTVAL,
    },
    {
        "PUTVAL myhost/magic/MAGIC interval=2 1234:42 interval=5 2345:23",
        NULL,
        CMD_OK,
        CMD_PUTVAL,
    },
    {
        "PUTVAL myhost/magic/MAGIC meta:KEY=\"string_value\" 1234:42",
        NULL,
        CMD_OK,
        CMD_PUTVAL,
    },
    {
        "PUTVAL myhost/magic/MAGIC meta:KEY='string_value' 1234:42",
        NULL,
        CMD_OK,
        CMD_PUTVAL,
    },

    /* Invalid PUTVAL commands. */
    {
        "PUTVAL magic/MAGIC N:42",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        "PUTVAL",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        "PUTVAL invalid N:42",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        "PUTVAL myhost/magic/MAGIC A:42",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        "PUTVAL myhost/magic/MAGIC 1234:A",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        "PUTVAL myhost/magic/MAGIC",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        "PUTVAL 1234:A",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    {
        "PUTVAL myhost/magic/UNKNOWN 1234:42",
        NULL,
        CMD_PARSE_ERROR,
        CMD_UNKNOWN,
    },
    /*
     * As of collectd 5.x, PUTVAL accepts invalid options.
    {
            "PUTVAL myhost/magic/MAGIC invalid=2 1234:42",
            NULL,
            CMD_PARSE_ERROR,
            CMD_UNKNOWN,
    },
    */

    /* Valid PUTMETRIC commands. */
    {
        "PUTMETRIC unit_test 42",
        NULL,
        CMD_OK,
        CMD_PUTMETRIC,
    },
    {
        "PUTMETRIC gauge type=GAUGE 42",
        NULL,
        CMD_OK,
        CMD_PUTMETRIC,
    },
    {
        "PUTMETRIC counter type=Counter 42",
        NULL,
        CMD_OK,
        CMD_PUTMETRIC,
    },
    {
        "PUTMETRIC untyped type=untyped 42",
        NULL,
        CMD_ERROR,
        CMD_UNKNOWN,
    },
    {
        "PUTMETRIC quoted_gauge type=\"GAUGE\" 42",
        NULL,
        CMD_OK,
        CMD_PUTMETRIC,
    },
    {
        "PUTMETRIC with_interval interval=10.0 42",
        NULL,
        CMD_OK,
        CMD_PUTMETRIC,
    },
    {
        "PUTMETRIC with_time time=1594806526 42",
        NULL,
        CMD_OK,
        CMD_PUTMETRIC,
    },
    {
        "PUTMETRIC with_label label:unquoted=bare 42",
        NULL,
        CMD_OK,
        CMD_PUTMETRIC,
    },
    {
        "PUTMETRIC with_label label:quoted=\"with space\" 42",
        NULL,
        CMD_OK,
        CMD_PUTMETRIC,
    },
    {
        "PUTMETRIC multiple_label label:foo=1 label:bar=2 42",
        NULL,
        CMD_OK,
        CMD_PUTMETRIC,
    },

    /* Invalid commands. */
    {
        "INVALID",
        NULL,
        CMD_UNKNOWN_COMMAND,
        CMD_UNKNOWN,
    },
    {
        "INVALID interval=2",
        NULL,
        CMD_UNKNOWN_COMMAND,
        CMD_UNKNOWN,
    },
};

DEF_TEST(parse) {
  int test_result = 0;

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(parse_data); i++) {
    char *input = strdup(parse_data[i].input);

    strbuf_t errbuf = STRBUF_CREATE;
    cmd_error_handler_t err = {error_cb, &errbuf};

    cmd_t cmd = {0};
    cmd_status_t status = cmd_parse(input, &cmd, parse_data[i].opts, &err);

    char description[1024];
    ssnprintf(description, sizeof(description),
              "cmd_parse(\"%s\", opts=%p) = "
              "%d (type=%d [%s]); want %d "
              "(type=%d [%s])",
              parse_data[i].input, (void *)parse_data[i].opts, status, cmd.type,
              CMD_TO_STRING(cmd.type), parse_data[i].expected_status,
              parse_data[i].expected_type,
              CMD_TO_STRING(parse_data[i].expected_type));

    bool result = (status == parse_data[i].expected_status) &&
                  (cmd.type == parse_data[i].expected_type);

    if (errbuf.ptr != NULL) {
      printf("error buffer = \"%s\"\n", errbuf.ptr);
    }

    LOG(result, description);

    /* Run all tests before failing. */
    if (!result)
      test_result = -1;

    cmd_destroy(&cmd);
    STRBUF_DESTROY(errbuf);
    free(input);
  }

  return test_result;
}

DEF_TEST(format_putmetric) {
  struct {
    metric_t m;
    char *want;
    int want_err;
  } cases[] = {
      {
          .m =
              {
                  .family =
                      &(metric_family_t){
                          .name = "test",
                          .type = METRIC_TYPE_UNTYPED,
                      },
                  .value.gauge = 42,
              },
          .want_err = EINVAL,
      },
      {
          .m =
              {
                  .family =
                      &(metric_family_t){
                          .name = "test",
                          .type = METRIC_TYPE_GAUGE,
                      },
                  .value.gauge = 42,
              },
          .want = "PUTMETRIC test type=GAUGE 42",
      },
      {
          .m =
              {
                  .family =
                      &(metric_family_t){
                          .name = "test",
                          .type = METRIC_TYPE_COUNTER,
                      },
                  .value.counter = 42,
              },
          .want = "PUTMETRIC test type=COUNTER 42",
      },
      {
          .m =
              {
                  .family =
                      &(metric_family_t){
                          .name = "test",
                          .type = METRIC_TYPE_GAUGE,
                      },
                  .value.gauge = 42,
                  .time = TIME_T_TO_CDTIME_T(1594809888),
              },
          .want = "PUTMETRIC test type=GAUGE time=1594809888.000 42",
      },
      {
          .m =
              {
                  .family =
                      &(metric_family_t){
                          .name = "test",
                          .type = METRIC_TYPE_GAUGE,
                      },
                  .value.gauge = 42,
                  .interval = TIME_T_TO_CDTIME_T(10),
              },
          .want = "PUTMETRIC test type=GAUGE interval=10.000 42",
      },
      {
          .m =
              {
                  .family =
                      &(metric_family_t){
                          .name = "test",
                          .type = METRIC_TYPE_GAUGE,
                      },
                  .value.gauge = 42,
                  .label.ptr =
                      &(label_pair_t){
                          .name = "foo",
                          .value = "with \"quotes\"",
                      },
                  .label.num = 1,
              },
          .want =
              "PUTMETRIC test type=GAUGE label:foo=\"with \\\"quotes\\\"\" 42",
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    strbuf_t buf = STRBUF_CREATE;

    EXPECT_EQ_INT(cases[i].want_err, cmd_format_putmetric(&buf, &cases[i].m));
    if (cases[i].want_err) {
      STRBUF_DESTROY(buf);
      continue;
    }

    EXPECT_EQ_STR(cases[i].want, buf.ptr);

    STRBUF_DESTROY(buf);
  }

  return 0;
}

int main(int argc, char **argv) {
  RUN_TEST(parse);
  RUN_TEST(format_putmetric);
  END_TEST;
}
