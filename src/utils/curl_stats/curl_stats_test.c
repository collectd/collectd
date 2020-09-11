/**
 * collectd - src/daemon/curl_stats_test.c
 * Copyright (C) 2020       Barbara bkjg Kaczorowska
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
 *   Barbara bkjg Kaczorowska <bkjg at google.com>
 */

#include "collectd.h"
#include "curl_stats.h"
#include "testing.h"

DEF_TEST(curl_stats_from_config) {
  struct {
    oconfig_item_t ci;
    curl_stats_t *want_get;
  } cases[] = {
      {
        .ci = {
          .children = (oconfig_item_t[]) {
              {
                .values_num = 1,
                .values = (oconfig_value_t[]){
                    {
                      .type = OCONFIG_TYPE_STRING,
                    },
                },
              }
          }
        },
      }
  };

  return 0;
}

DEF_TEST(curl_stats_dispatch) {
  return 0;
}

int main() {
  RUN_TEST(curl_stats_from_config);
  RUN_TEST(curl_stats_dispatch);

  END_TEST;
}
