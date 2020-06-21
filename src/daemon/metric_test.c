/**
 * collectd - src/daemon/metric_test.c
 * Copyright (C) 2020       Google LLC
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

#include "metric.h"
#include "testing.h"

DEF_TEST(metric_identity) {
  struct {
    char *name;
    label_t *labels;
    size_t labels_num;
    char const *want;
  } cases[] = {
      {
          .name = "metric_without_labels",
          .want = "metric_without_labels",
      },
      {
          .name = "metric_with_labels",
          .labels =
              (label_t[]){
                  {"sorted", "yes"},
                  {"alphabetically", "true"},
              },
          .labels_num = 2,
          .want = "metric_with_labels{alphabetically=\"true\",sorted=\"yes\"}",
      },
      {
          .name = "escape_sequences",
          .labels =
              (label_t[]){
                  {"newline", "\n"},
                  {"quote", "\""},
                  {"tab", "\t"},
                  {"cardridge_return", "\r"},
              },
          .labels_num = 4,
          .want = "escape_sequences{cardridge_return=\"\\r\",newline=\"\\n\","
                  "quote=\"\\\"\",tab=\"\\t\"}",
      },
  };

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    printf("## Case %zu: %s\n", i, cases[i].name);

    metric_family_t fam = {
        .name = cases[i].name,
        .type = METRIC_TYPE_UNTYPED,
    };
    metric_t m = {
        .family = &fam,
    };
    for (size_t j = 0; j < cases[i].labels_num; j++) {
      CHECK_ZERO(metric_label_set(&m, cases[i].labels[j].name,
                                  cases[i].labels[j].value));
    }

    strbuf_t buf = STRBUF_CREATE;
    CHECK_ZERO(metric_identity(&buf, &m));

    EXPECT_EQ_STR(cases[i].want, buf.ptr);

    STRBUF_DESTROY(buf);
    metric_family_metric_reset(&fam);
    metric_reset(&m);
  }

  return 0;
}

int main(void) {
  RUN_TEST(metric_identity);

  END_TEST;
}
