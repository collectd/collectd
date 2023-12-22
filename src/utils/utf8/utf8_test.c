/*
 * Copyright (c) 2023      Florian "octo" Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "collectd.h"
#include "testing.h"
#include "utils/common/common.h"
#include "utils/utf8/utf8.h"

DEF_TEST(utf8_valid) {
  struct {
    char const *name;
    char *input;
    bool want;
  } cases[] = {
      {
          .name = "simple string",
          .input = "Hello, World!",
          .want = true,
      },
      {
          .name = "empty string",
          .input = "",
          .want = true,
      },
      {
          .name = "The greek work \"kosme\"",
          .input = (char[]){0xce, 0xba, 0xe1, 0xbd, 0xb9, 0xcf, 0x83, 0xce,
                            0xbc, 0xce, 0xb5, 0},
          .want = true,
      },
      {
          .name = "First possible sequence of three bytes",
          .input = (char[]){0xe0, 0xa0, 0x80, 0},
          .want = true,
      },
      {
          .name = "First possible sequence of four bytes",
          .input = (char[]){0xf0, 0x90, 0x80, 0x80, 0},
          .want = true,
      },
      {
          .name = "U-0000D7F",
          .input = (char[]){0xed, 0x9f, 0xbf, 0},
          .want = true,
      },
      {
          .name = "0xFE (invalid byte)",
          .input = (char[]){'H', 0xfe, 'l', 'l', 'o', 0},
          .want = false,
      },
      {
          .name = "0xFF (invalid byte)",
          .input = (char[]){'C', 'o', 0xff, 'e', 'e', 0},
          .want = false,
      },
      {
          .name = "Continuation byte at end of string",
          .input = (char[]){0xce, 0xba, 0xe1, 0xbd, 0xb9, 0xcf, 0x83, 0xce,
                            0xbc, 0xce, 0},
          .want = false,
      },
      {
          .name = "U+002F (overlong ASCII character, 2 bytes)",
          .input = (char[]){0xc0, 0xaf, 0},
          .want = false,
      },
      {
          .name = "U+002F (overlong ASCII character, 3 bytes)",
          .input = (char[]){0xe0, 0x80, 0xaf, 0},
          .want = false,
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("Case #%zu: %s\n", i, cases[i].name);
    EXPECT_EQ_INT(cases[i].want, utf8_valid((uint8_t *)cases[i].input));
  }
  return 0;
}

int main(void) {
  RUN_TEST(utf8_valid);

  END_TEST;
}
