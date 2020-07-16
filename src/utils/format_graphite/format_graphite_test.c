/**
 * collectd - src/utils_format_graphite_test.c
 * Copyright (C) 2016-2020  Florian octo Forster
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

#include "testing.h"
#include "utils/common/common.h" /* for STATIC_ARRAY_SIZE */
#include "utils/format_graphite/format_graphite.h"

DEF_TEST(metric_name) {
  struct {
    char *name;
    size_t labels_num;
    char const **keys;
    char const **values;
    value_t value;
    metric_type_t type;
    unsigned int flags;
    const char *want;
  } cases[] = {
      {
          .name = "unit_test",
          .value = (value_t){.gauge = 42},
          .type = METRIC_TYPE_GAUGE,
          .want = "unit_test 42 1592748157\r\n",
      },
      {
          .name = "test_with_label",
          .labels_num = 2,
          .keys = (char const *[]){"beta", "alpha"},
          .values = (char const *[]){"second", "first"},
          .value = (value_t){.counter = 0},
          .type = METRIC_TYPE_COUNTER,
          .want =
              "test_with_label.alpha=first.beta=second 0 1592748157\r\n",
      },
      {
          .name = "separate_instances_test",
          .labels_num = 2,
          .keys = (char const *[]){"beta", "alpha"},
          .values = (char const *[]){"second", "first"},
          .value = (value_t){.counter = 0},
          .type = METRIC_TYPE_COUNTER,
          .flags = GRAPHITE_SEPARATE_INSTANCES,
          .want = "separate_instances_test.alpha.first.beta.second 0 1592748157\r\n",
      },
      {
          .name = "escaped:metric_name",
          .value = (value_t){.gauge = NAN},
          .type = METRIC_TYPE_GAUGE,
          .want = "escaped_metric_name nan 1592748157\r\n",
      },
      {
          .name = "escaped_label_value",
          .labels_num = 2,
          .keys = (char const *[]){"beta", "alpha"},
          .values = (char const *[]){"second value", "first/value"},
          .value = (value_t){.counter = 18446744073709551615LLU},
          .type = DS_TYPE_COUNTER,
          .want = "escaped_label_value.alpha=first_value.beta=second_value "
                  "18446744073709551615 1592748157\r\n",
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    metric_family_t fam = {
        .name = cases[i].name,
        .type = cases[i].type,
    };

    metric_t m = {
        .family = &fam,
        .value = cases[i].value,
        .time = 1710200311404036096, /* 1592748157.125 */
    };
    for (size_t j = 0; j < cases[i].labels_num; j++) {
      CHECK_ZERO(metric_label_set(&m, cases[i].keys[j], cases[i].values[j]));
    }

    strbuf_t buf = STRBUF_CREATE;
    EXPECT_EQ_INT(0, format_graphite(&buf, &m, "", "", '_', cases[i].flags));
    EXPECT_EQ_STR(cases[i].want, buf.ptr);

    STRBUF_DESTROY(want);
    STRBUF_DESTROY(buf);
    metric_reset(&m);
  }

  return 0;
}

int main(void) {
  RUN_TEST(metric_name);

  END_TEST;
}
