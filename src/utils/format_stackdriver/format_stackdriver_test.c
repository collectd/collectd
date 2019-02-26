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
  value_list_t vl = {
      .host = "example.com", .plugin = "unit-test", .type = "example",
  };
  char got[1024];

  data_set_t ds_single = {
      .type = "example",
      .ds_num = 1,
      .ds =
          &(data_source_t){
              .name = "value", .type = DS_TYPE_GAUGE, .min = NAN, .max = NAN,
          },
  };
  EXPECT_EQ_INT(
      0, sd_format_metric_descriptor(got, sizeof(got), &ds_single, &vl, 0));
  char const *want_single =
      "{\"type\":\"custom.googleapis.com/collectd/unit_test/"
      "example\",\"metricKind\":\"GAUGE\",\"valueType\":\"DOUBLE\",\"labels\":["
      "{\"key\":\"host\",\"valueType\":\"STRING\"},{\"key\":\"plugin_"
      "instance\",\"valueType\":\"STRING\"},{\"key\":\"type_instance\","
      "\"valueType\":\"STRING\"}]}";
  EXPECT_EQ_STR(want_single, got);

  data_set_t ds_double = {
      .type = "example",
      .ds_num = 2,
      .ds =
          (data_source_t[]){
              {.name = "one", .type = DS_TYPE_DERIVE, .min = 0, .max = NAN},
              {.name = "two", .type = DS_TYPE_DERIVE, .min = 0, .max = NAN},
          },
  };
  EXPECT_EQ_INT(
      0, sd_format_metric_descriptor(got, sizeof(got), &ds_double, &vl, 0));
  char const *want_double =
      "{\"type\":\"custom.googleapis.com/collectd/unit_test/"
      "example_one\",\"metricKind\":\"CUMULATIVE\",\"valueType\":\"INT64\","
      "\"labels\":[{\"key\":\"host\",\"valueType\":\"STRING\"},{\"key\":"
      "\"plugin_instance\",\"valueType\":\"STRING\"},{\"key\":\"type_"
      "instance\",\"valueType\":\"STRING\"}]}";
  EXPECT_EQ_STR(want_double, got);
  return 0;
}

int main(int argc, char **argv) {
  RUN_TEST(sd_format_metric_descriptor);

  END_TEST;
}
