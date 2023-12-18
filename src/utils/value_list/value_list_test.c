/**
 * collectd - src/utils/value_list/value_list_test.c
 * Copyright (C) 2013-2023  Florian octo Forster
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

#include "testing.h"
#include "utils/value_list/value_list.h"

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
        .name = "value",
        .type = DS_TYPE_GAUGE,
        .min = 0.0,
        .max = NAN,
    };
    data_set_t ds = {
        .type = "example",
        .ds_num = 1,
        .ds = &dsrc,
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

int main(void) {
  RUN_TEST(parse_values);

  END_TEST;
}
