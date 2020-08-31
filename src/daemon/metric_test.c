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

DEF_TEST(metric_label_set) {
  struct {
    char const *key;
    char const *value;
    int want_err;
    char const *want_get;
  } cases[] = {
      {
          .key = "foo",
          .value = "bar",
          .want_get = "bar",
      },
      {
          .key = NULL,
          .value = "bar",
          .want_err = EINVAL,
      },
      {
          .key = "foo",
          .value = NULL,
      },
      {
          .key = "",
          .value = "bar",
          .want_err = EINVAL,
      },
      {
          .key = "valid",
          .value = "",
      },
      {
          .key = "1nvalid",
          .value = "bar",
          .want_err = EINVAL,
      },
      {
          .key = "val1d",
          .value = "bar",
          .want_get = "bar",
      },
      {
          .key = "inva!id",
          .value = "bar",
          .want_err = EINVAL,
      },
  };

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    printf("## Case %zu: %s=\"%s\"\n", i,
           cases[i].key ? cases[i].key : "(null)",
           cases[i].value ? cases[i].value : "(null)");

    metric_family_t m_fam = {
        .type = METRIC_TYPE_DISTRIBUTION,
    };
    metric_family_t *metric_fam = &m_fam;

    metric_t m = {
        .family = metric_fam,
        .value.distribution = NULL,
    };
    // metric_t m = {0};

    EXPECT_EQ_INT(cases[i].want_err,
                  metric_label_set(&m, cases[i].key, cases[i].value));
    EXPECT_EQ_STR(cases[i].want_get, metric_label_get(&m, cases[i].key));

    metric_reset(&m);
    // EXPECT_EQ_PTR(NULL, m.label.ptr);
    // EXPECT_EQ_INT(0, m.label.num);
  }

  return 0;
}

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

DEF_TEST(metric_family_append) {
  struct {
    char const *lname;
    char const *lvalue;
    gauge_t v;
    metric_t *templ;
    int want_err;
    label_t *want_labels;
    size_t want_labels_num;
    gauge_t want_value;
    cdtime_t want_time;
    cdtime_t want_interval;
  } cases[] = {
      {
          .v = 42,
          .want_value = 42,
      },
      {
          .lname = "type",
          .lvalue = "test",
          .v = 42,
          .want_labels =
              (label_t[]){
                  {"type", "test"},
              },
          .want_labels_num = 1,
          .want_value = 42,
      },
      {
          .v = 42,
          .templ =
              &(metric_t){
                  .time = TIME_T_TO_CDTIME_T(1594107920),
              },
          .want_value = 42,
          .want_time = TIME_T_TO_CDTIME_T(1594107920),
      },
      {
          .v = 42,
          .templ =
              &(metric_t){
                  .interval = TIME_T_TO_CDTIME_T(10),
              },
          .want_value = 42,
          .want_interval = TIME_T_TO_CDTIME_T(10),
      },
      {
          .lname = "type",
          .lvalue = "test",
          .v = 42,
          .templ =
              &(metric_t){
                  .label =
                      {
                          .ptr = &(label_pair_t){"common", "label"},
                          .num = 1,
                      },
              },
          .want_labels =
              (label_t[]){
                  {"common", "label"},
                  {"type", "test"},
              },
          .want_labels_num = 2,
          .want_value = 42,
      },
  };

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    metric_family_t fam = {
        .name = "test_total",
        .type = METRIC_TYPE_GAUGE,
    };

    EXPECT_EQ_INT(cases[i].want_err,
                  metric_family_append(&fam, cases[i].lname, cases[i].lvalue,
                                       (value_t){.gauge = cases[i].v},
                                       cases[i].templ));
    if (cases[i].want_err != 0) {
      continue;
    }

    EXPECT_EQ_INT(1, fam.metric.num);
    metric_t const *m = fam.metric.ptr;

    EXPECT_EQ_INT(cases[i].want_labels_num, m->label.num);
    for (size_t j = 0; j < cases[i].want_labels_num; j++) {
      EXPECT_EQ_STR(cases[i].want_labels[j].value,
                    metric_label_get(m, cases[i].want_labels[j].name));
    }

    EXPECT_EQ_DOUBLE(cases[i].want_value, m->value.gauge);
    EXPECT_EQ_UINT64(cases[i].want_time, m->time);
    EXPECT_EQ_UINT64(cases[i].want_interval, m->interval);

    metric_family_metric_reset(&fam);
  }

  return 0;
}

DEF_TEST(metric_reset) {
  struct {
    value_t value;
  } cases[] = {
      {
          .value.distribution = distribution_new_linear(10, 25),
      },
      {
          .value.distribution = distribution_new_exponential(10, 3, 2),
      },
      {
          .value.distribution =
              distribution_new_custom(5, (double[]){5, 10, 20, 30, 50}),
      },
  };

  metric_family_t m_fam = {
      .type = METRIC_TYPE_DISTRIBUTION,
  };
  metric_family_t *metric_fam = &m_fam;

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    printf("## Case %zu: \n", i);

    metric_t m = {
        .family = metric_fam,
        .value.distribution = cases[i].value.distribution,
    };

    EXPECT_EQ_INT(metric_reset(&m), 0);
  }
  return 0;
}

DEF_TEST(distribution_marshal_text) {
  struct {
    value_t value;
    char const *want;
  } cases[] = {
      {
          .value.distribution = distribution_new_linear(2, 20),
          .want = "bucket{l=\"20.00\"} 0\n"
                  "bucket{l=\"+inf\"} 0\n"
                  "sum 0.00\n"
                  "count 0\n"
      },
      {
          .value.distribution = distribution_new_exponential(10, 2, 3),
          .want = "bucket{l=\"3.00\"} 0\n" 
                  "bucket{l=\"6.00\"} 0\n" 
                  "bucket{l=\"12.00\"} 0\n" 
                  "bucket{l=\"24.00\"} 0\n" 
                  "bucket{l=\"48.00\"} 0\n" 
                  "bucket{l=\"96.00\"} 0\n" 
                  "bucket{l=\"192.00\"} 0\n" 
                  "bucket{l=\"384.00\"} 0\n" 
                  "bucket{l=\"768.00\"} 0\n" 
                  "bucket{l=\"+inf\"} 0\n" 
                  "sum 0.00\n"
                  "count 0\n"
      },
      {
          .value.distribution =
              distribution_new_custom(4, (double[]){3, 10, 50, 100}),
          .want = "bucket{l=\"3.00\"} 0\n" 
                  "bucket{l=\"10.00\"} 0\n" 
                  "bucket{l=\"50.00\"} 0\n" 
                  "bucket{l=\"100.00\"} 0\n" 
                  "bucket{l=\"+inf\"} 0\n" 
                  "sum 0.00\n"
                  "count 0\n"
      },
  };

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    printf("## Case %zu: \n", i);

    distribution_t *dist = cases[i].value.distribution;
    strbuf_t buf = STRBUF_CREATE;

    CHECK_ZERO(distribution_marshal_text(&buf, dist));
    EXPECT_EQ_STR(cases[i].want, buf.ptr);
    distribution_destroy(dist);
    STRBUF_DESTROY(buf);
  }
  return 0;
}

int main(void) {
  RUN_TEST(metric_label_set);
  RUN_TEST(metric_identity);
  RUN_TEST(metric_family_append);
  RUN_TEST(metric_reset);
  RUN_TEST(distribution_marshal_text);
  END_TEST;
}
