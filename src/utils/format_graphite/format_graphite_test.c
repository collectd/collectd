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
    char const *name;
    size_t labels_num;
    char const **keys;
    char const **values;
    value_t value;
    int value_type;
    unsigned int flags;
    const char *want;
  } cases[] = {
      {
          .name = "unit_test",
          .value = (value_t){.gauge = 42},
          .value_type = DS_TYPE_GAUGE,
          .want = "unit_test 42",
      },
      {
          .name = "test_with_label",
          .labels_num = 2,
          .keys = (char const *[]){"beta", "alpha"},
          .values = (char const *[]){"second", "first"},
          .value = (value_t){.derive = -9223372036854775807LL},
          .value_type = DS_TYPE_DERIVE,
          .want =
              "test_with_label.alpha=first.beta=second -9223372036854775807",
      },
      {
          .name = "separate_instances_test",
          .labels_num = 2,
          .keys = (char const *[]){"beta", "alpha"},
          .values = (char const *[]){"second", "first"},
          .value = (value_t){.derive = 9223372036854775807LL},
          .value_type = DS_TYPE_DERIVE,
          .flags = GRAPHITE_SEPARATE_INSTANCES,
          .want = "separate_instances_test.alpha.first.beta.second "
                  "9223372036854775807",
      },
      {
          .name = "escaped:metric_name",
          .value = (value_t){.gauge = NAN},
          .value_type = DS_TYPE_GAUGE,
          .want = "escaped_metric_name nan",
      },
      {
          .name = "escaped_label_value",
          .labels_num = 2,
          .keys = (char const *[]){"beta", "alpha"},
          .values = (char const *[]){"second value", "first/value"},
          .value = (value_t){.counter = 18446744073709551615LLU},
          .value_type = DS_TYPE_COUNTER,
          .want = "escaped_label_value.alpha=first_value.beta=second_value "
                  "18446744073709551615",
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    identity_t *id;
    CHECK_NOT_NULL(id = identity_create(cases[i].name));
    for (size_t j = 0; j < cases[i].labels_num; j++) {
      CHECK_ZERO(identity_add_label(id, cases[i].keys[j], cases[i].values[j]));
    }

    metric_t m = {
        .identity = id,
        .value = cases[i].value,
        .value_type = cases[i].value_type,
        .time = 1710200311404036096, /* 1592748157.125 */
    };

    strbuf_t buf = STRBUF_CREATE;
    EXPECT_EQ_INT(0, format_graphite(&buf, &m, "", "", '_', cases[i].flags));

    strbuf_t want = STRBUF_CREATE;
    if (cases[i].want != NULL) {
      strbuf_print(&want, cases[i].want);
      strbuf_print(&want, " 1480063672\r\n");
    }
    EXPECT_EQ_STR(want.ptr, buf.ptr);

    STRBUF_DESTROY(want);
    STRBUF_DESTROY(buf);
    identity_destroy(id);
  }

  return 0;
}

int main(void) {
  RUN_TEST(metric_name);

  END_TEST;
}
