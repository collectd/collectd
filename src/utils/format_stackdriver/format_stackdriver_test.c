/**
 * collectd - src/utils_format_stackdriver_test.c
 * ISC license
 *
 * Copyright (C) 2017  Florian Forster
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "testing.h"
#include "utils/format_stackdriver/format_stackdriver.h"

DEF_TEST(sd_format_metric_descriptor) {
  struct {
    char *name;
    metric_type_t type;
    label_pair_t *labels;
    size_t labels_num;
    char *want;
  } cases[] = {
      {
          .name = "gauge_metric",
          .type = METRIC_TYPE_GAUGE,
          .want = "{\"type\":\"custom.googleapis.com/collectd/"
                  "gauge_metric\",\"metricKind\":\"GAUGE\",\"valueType\":"
                  "\"DOUBLE\",\"labels\":[]}",
      },
      {
          .name = "counter_metric",
          .type = METRIC_TYPE_COUNTER,
          .want = "{\"type\":\"custom.googleapis.com/collectd/"
                  "counter_metric\",\"metricKind\":\"CUMULATIVE\","
                  "\"valueType\":\"INT64\",\"labels\":[]}",
      },
      {
          .name = "untyped_metric",
          .type = METRIC_TYPE_UNTYPED,
          .want = "{\"type\":\"custom.googleapis.com/collectd/"
                  "untyped_metric\",\"metricKind\":\"GAUGE\",\"valueType\":"
                  "\"DOUBLE\",\"labels\":[]}",
      },
      {
          .name = "metric_with_labels",
          .type = METRIC_TYPE_GAUGE,
          .labels =
              (label_pair_t[]){
                  {"region", "here be dragons"},
                  {"instance", "example.com"},
              },
          .labels_num = 2,
          .want = "{\"type\":\"custom.googleapis.com/collectd/"
                  "metric_with_labels\",\"metricKind\":\"GAUGE\",\"valueType\":"
                  "\"DOUBLE\",\"labels\":[{\"key\":\"instance\",\"valueType\":"
                  "\"STRING\"},{\"key\":\"region\",\"valueType\":\"STRING\"}]}",
      },
      {
          .name = "stored:aggregation:metric",
          .type = METRIC_TYPE_GAUGE,
          .want = "{\"type\":\"custom.googleapis.com/collectd/"
                  "stored_aggregation_metric\",\"metricKind\":\"GAUGE\","
                  "\"valueType\":\"DOUBLE\",\"labels\":[]}",
      },
  };

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    metric_family_t fam = {
        .name = cases[i].name,
        .type = cases[i].type,
    };
    metric_t m = {
        .family = &fam,
    };
    for (size_t j = 0; j < cases[i].labels_num; j++) {
      label_pair_t *l = cases[i].labels + j;
      EXPECT_EQ_INT(0, metric_label_set(&m, l->name, l->value));
    }

    strbuf_t buf = STRBUF_CREATE;
    EXPECT_EQ_INT(0, sd_format_metric_descriptor(&buf, &m));
    EXPECT_EQ_STR(cases[i].want, buf.ptr);

    STRBUF_DESTROY(buf);
    metric_reset(&m);
  }

  return 0;
}

int main(int argc, char **argv) {
  RUN_TEST(sd_format_metric_descriptor);

  END_TEST;
}
