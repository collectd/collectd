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

static void format_label_set(strbuf_t *buf, label_set_t labels) {
  for (size_t i = 0; i < labels.num; i++) {
    strbuf_printf(buf, "[i=%zu]", i);
    if (i != 0) {
      strbuf_print(buf, ",");
    }
    strbuf_print_escaped(buf, labels.ptr[i].name, "\\\"\n\r\t", '\\');
    strbuf_print(buf, "=\"");
    strbuf_print_escaped(buf, labels.ptr[i].value, "\\\"\n\r\t", '\\');
    strbuf_print(buf, "\"");
  }
}

DEF_TEST(metric_label_set) {
  struct {
    char const *name;
    label_set_t state;
    char const *label_name;
    char const *label_value;
    label_set_t want;
    int want_err;
  } cases[] = {
      {
          .name = "Add a label",
          .state =
              {
                  .ptr =
                      (label_pair_t[]){
                          {"a", "1"},
                          {"c", "3"},
                          {"d", "4"},
                      },
                  .num = 3,
              },
          .label_name = "b",
          .label_value = "2",
          .want =
              {
                  .ptr =
                      (label_pair_t[]){
                          {"a", "1"},
                          {"b", "2"},
                          {"c", "3"},
                          {"d", "4"},
                      },
                  .num = 4,
              },
      },
      {
          .name = "Change a label",
          .state =
              {
                  .ptr =
                      (label_pair_t[]){
                          {"a", "1"},
                          {"b", "<to be replaced>"},
                          {"c", "3"},
                          {"d", "4"},
                      },
                  .num = 4,
              },
          .label_name = "b",
          .label_value = "2",
          .want =
              {
                  .ptr =
                      (label_pair_t[]){
                          {"a", "1"},
                          {"b", "2"},
                          {"c", "3"},
                          {"d", "4"},
                      },
                  .num = 4,
              },
      },
      {
          .name = "Use empty string to delete a label",
          .state =
              {
                  .ptr =
                      (label_pair_t[]){
                          {"a", "1"},
                          {"b", "2"},
                          {"c", "3"},
                          {"d", "4"},
                      },
                  .num = 4,
              },
          .label_name = "d",
          .label_value = NULL,
          .want =
              {
                  .ptr =
                      (label_pair_t[]){
                          {"a", "1"},
                          {"b", "2"},
                          {"c", "3"},
                      },
                  .num = 3,
              },
      },
      {
          .name = "Use NULL to delete a label",
          .state =
              {
                  .ptr =
                      (label_pair_t[]){
                          {"a", "1"},
                          {"b", "2"},
                          {"c", "3"},
                          {"d", "4"},
                      },
                  .num = 4,
              },
          .label_name = "b",
          .label_value = NULL,
          .want =
              {
                  .ptr =
                      (label_pair_t[]){
                          {"a", "1"},
                          {"c", "3"},
                          {"d", "4"},
                      },
                  .num = 3,
              },
      },
      {
          .name = "NULL name",
          .label_name = NULL,
          .label_value = "bar",
          .want_err = EINVAL,
      },
      {
          .name = "empty name",
          .label_name = "",
          .label_value = "bar",
          .want_err = EINVAL,
      },
  };

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    printf("## Case %zu: %s\n", i, cases[i].name);

    metric_t m = {0};
    CHECK_ZERO(label_set_clone(&m.label, cases[i].state));

    EXPECT_EQ_INT(cases[i].want_err, metric_label_set(&m, cases[i].label_name,
                                                      cases[i].label_value));
    if (cases[i].want_err) {
      metric_reset(&m);
      continue;
    }

    strbuf_t got = STRBUF_CREATE;
    strbuf_t want = STRBUF_CREATE;

    format_label_set(&want, cases[i].want);
    format_label_set(&got, m.label);

    EXPECT_EQ_STR(want.ptr, got.ptr);

    STRBUF_DESTROY(got);
    STRBUF_DESTROY(want);

    metric_reset(&m);
    EXPECT_EQ_PTR(NULL, m.label.ptr);
    EXPECT_EQ_INT(0, m.label.num);
  }

  return 0;
}

DEF_TEST(metric_identity) {
  struct {
    char *name;
    label_pair_t *labels;
    size_t labels_num;
    label_pair_t *rattr;
    size_t rattr_num;
    char const *want;
  } cases[] = {
      {
          .name = "metric_without_labels",
          .want = "metric_without_labels",
      },
      {
          .name = "metric_with_labels",
          .labels =
              (label_pair_t[]){
                  {"sorted", "yes"},
                  {"alphabetically", "true"},
              },
          .labels_num = 2,
          .want = "metric_with_labels{alphabetically=\"true\",sorted=\"yes\"}",
      },
      {
          .name = "escape_sequences",
          .labels =
              (label_pair_t[]){
                  {"newline", "\n"},
                  {"quote", "\""},
                  {"tab", "\t"},
                  {"cardridge_return", "\r"},
              },
          .labels_num = 4,
          .want = "escape_sequences{cardridge_return=\"\\r\",newline=\"\\n\","
                  "quote=\"\\\"\",tab=\"\\t\"}",
      },
      {
          .name = "metric_with_resource",
          .rattr =
              (label_pair_t[]){
                  {"host.name", "example.com"},
              },
          .rattr_num = 1,
          .want = "metric_with_resource{resource:host.name=\"example.com\"}",
      },
      {
          .name = "metric_with_resource_and_labels",
          .rattr =
              (label_pair_t[]){
                  {"omega", "always"},
                  {"alpha", "resources"},
              },
          .rattr_num = 2,
          .labels =
              (label_pair_t[]){
                  {"gamma", "first"},
                  {"beta", "come"},
              },
          .labels_num = 2,
          .want =
              "metric_with_resource_and_labels{resource:alpha=\"resources\","
              "resource:omega=\"always\",beta=\"come\",gamma=\"first\"}",
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
    for (size_t j = 0; j < cases[i].rattr_num; j++) {
      CHECK_ZERO(metric_family_resource_attribute_update(
          &fam, cases[i].rattr[j].name, cases[i].rattr[j].value));
    }

    strbuf_t buf = STRBUF_CREATE;
    CHECK_ZERO(metric_identity(&buf, &m));

    EXPECT_EQ_STR(cases[i].want, buf.ptr);

    STRBUF_DESTROY(buf);
    metric_family_metric_reset(&fam);
    label_set_reset(&fam.resource);
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
    label_pair_t *want_labels;
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
              (label_pair_t[]){
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
              (label_pair_t[]){
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

int main(void) {
  RUN_TEST(metric_label_set);
  RUN_TEST(metric_identity);
  RUN_TEST(metric_family_append);

  END_TEST;
}
