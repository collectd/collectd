/**
 * collectd - src/utils/strbuf/strbuf_test.c
 * Copyright (C) 2020       Florian octo Forster
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
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 */

#include "testing.h"
#include "utils/strbuf/strbuf.h"

#include <errno.h>
#include <stdbool.h>

#define STATIC_BUFFER_SIZE 9

int test_buffer(strbuf_t *buf, bool is_static) {
  CHECK_ZERO(strbuf_print(buf, "foo"));
  EXPECT_EQ_STR("foo", buf->ptr);

  CHECK_ZERO(strbuf_print(buf, "bar"));
  EXPECT_EQ_STR("foobar", buf->ptr);

  CHECK_ZERO(strbuf_printf(buf, "%d\n", 9000));
  char const *want = is_static ? "foobar90" : "foobar9000\n";
  EXPECT_EQ_STR(want, buf->ptr);

  if (is_static) {
    EXPECT_EQ_INT(ENOSPC, strbuf_print(buf, "buffer already filled"));
    EXPECT_EQ_STR("foobar90", buf->ptr);
  }

  strbuf_reset(buf);
  CHECK_ZERO(strlen(buf->ptr));

  CHECK_ZERO(strbuf_print(buf, "new content"));
  want = is_static ? "new cont" : "new content";
  EXPECT_EQ_STR(want, buf->ptr);

  strbuf_reset(buf);
  CHECK_ZERO(strlen(buf->ptr));

  CHECK_ZERO(strbuf_printn(buf, "foobar", 3));
  EXPECT_EQ_STR("foo", buf->ptr);

  return 0;
}

DEF_TEST(dynamic_heap) {
  strbuf_t *buf;
  CHECK_NOT_NULL(buf = strbuf_create());

  int status = test_buffer(buf, false);

  strbuf_destroy(buf);
  return status;
}

DEF_TEST(fixed_heap) {
  char mem[STATIC_BUFFER_SIZE];
  strbuf_t *buf;
  CHECK_NOT_NULL(buf = strbuf_create_fixed(mem, sizeof(mem)));

  int status = test_buffer(buf, true);

  strbuf_destroy(buf);
  return status;
}

DEF_TEST(dynamic_stack) {
  strbuf_t buf = {0};
  buf = STRBUF_CREATE;

  int status = test_buffer(&buf, false);

  STRBUF_DESTROY(buf);
  return status;
}

DEF_TEST(fixed_stack) {
  /* This somewhat unusual syntax ensures that `sizeof(b)` will return a wrong
   * number (size of the pointer, not the buffer; usually 4 or 8, depending on
   * architecture), failing the test. */
  char *b = (char[STATIC_BUFFER_SIZE]){0};
  size_t sz = STATIC_BUFFER_SIZE;
  strbuf_t buf = {0};
  buf = STRBUF_CREATE_FIXED(b, sz);

  int status = test_buffer(&buf, true);

  STRBUF_DESTROY(buf);
  return status;
}

DEF_TEST(static_stack) {
  char b[STATIC_BUFFER_SIZE];
  strbuf_t buf = {0};
  buf = STRBUF_CREATE_STATIC(b);

  int status = test_buffer(&buf, true);

  STRBUF_DESTROY(buf);
  return status;
}

DEF_TEST(print_escaped) {
  struct {
    char const *s;
    char const *need_escape;
    char escape_char;
    char const *want;
  } cases[] = {
      {
          .s = "normal string",
          .need_escape = "\\\"\n\r\t",
          .escape_char = '\\',
          .want = "normal string",
      },
      {
          .s = "\"special\"\n",
          .need_escape = "\\\"\n\r\t",
          .escape_char = '\\',
          .want = "\\\"special\\\"\\n",
      },
      {
          /* string gets truncated */
          .s = "0123456789ABCDEF",
          .need_escape = ">",
          .escape_char = '<',
          .want = "0123456789ABCDE",
      },
      {
          /* string gets truncated */
          .s = "0123456789>BCDEF",
          .need_escape = ">",
          .escape_char = '<',
          .want = "0123456789<>BCD",
      },
      {
          /* truncation between escape_char and to-be-escaped char. */
          .s = "0123456789ABCD>F",
          .need_escape = ">",
          .escape_char = '<',
          .want = "0123456789ABCD",
      },
  };

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
    char mem[16] = {0};
    strbuf_t buf = STRBUF_CREATE_STATIC(mem);

    CHECK_ZERO(strbuf_print_escaped(&buf, cases[i].s, cases[i].need_escape,
                                    cases[i].escape_char));
    EXPECT_EQ_STR(cases[i].want, buf.ptr);

    STRBUF_DESTROY(buf);
  }

  return 0;
}

int main(int argc, char **argv) /* {{{ */
{
  RUN_TEST(dynamic_heap);
  RUN_TEST(fixed_heap);
  RUN_TEST(dynamic_stack);
  RUN_TEST(fixed_stack);
  RUN_TEST(static_stack);
  RUN_TEST(print_escaped);

  END_TEST;
} /* }}} int main */
