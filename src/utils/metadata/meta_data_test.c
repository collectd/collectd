/**
 * collectd - src/daemon/meta_data_test.c
 * Copyright (C) 2015       Florian octo Forster
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

#include "utils/common/common.h" /* for STATIC_ARRAY_SIZE */

#include "testing.h"
#include "utils/metadata/meta_data.h"

DEF_TEST(base) {
  meta_data_t *m;

  char *s;
  int64_t si;
  uint64_t ui;
  double d;
  bool b;

  CHECK_NOT_NULL(m = meta_data_create());

  /* all of these are absent */
  OK(meta_data_get_string(m, "string", &s) != 0);
  OK(meta_data_get_signed_int(m, "signed_int", &si) != 0);
  OK(meta_data_get_unsigned_int(m, "unsigned_int", &ui) != 0);
  OK(meta_data_get_double(m, "double", &d) != 0);
  OK(meta_data_get_boolean(m, "boolean", &b) != 0);

  /* populate structure */
  CHECK_ZERO(meta_data_add_string(m, "string", "foobar"));
  OK(meta_data_exists(m, "string"));
  OK(meta_data_type(m, "string") == MD_TYPE_STRING);

  CHECK_ZERO(meta_data_add_signed_int(m, "signed_int", -1));
  OK(meta_data_exists(m, "signed_int"));
  OK(meta_data_type(m, "signed_int") == MD_TYPE_SIGNED_INT);

  CHECK_ZERO(meta_data_add_unsigned_int(m, "unsigned_int", 1));
  OK(meta_data_exists(m, "unsigned_int"));
  OK(meta_data_type(m, "unsigned_int") == MD_TYPE_UNSIGNED_INT);

  CHECK_ZERO(meta_data_add_double(m, "double", 47.11));
  OK(meta_data_exists(m, "double"));
  OK(meta_data_type(m, "double") == MD_TYPE_DOUBLE);

  CHECK_ZERO(meta_data_add_boolean(m, "boolean", 1));
  OK(meta_data_exists(m, "boolean"));
  OK(meta_data_type(m, "boolean") == MD_TYPE_BOOLEAN);

  /* retrieve and check all values */
  CHECK_ZERO(meta_data_get_string(m, "string", &s));
  EXPECT_EQ_STR("foobar", s);
  sfree(s);

  CHECK_ZERO(meta_data_get_signed_int(m, "signed_int", &si));
  EXPECT_EQ_INT(-1, (int)si);

  CHECK_ZERO(meta_data_get_unsigned_int(m, "unsigned_int", &ui));
  EXPECT_EQ_INT(1, (int)ui);

  CHECK_ZERO(meta_data_get_double(m, "double", &d));
  EXPECT_EQ_DOUBLE(47.11, d);

  CHECK_ZERO(meta_data_get_boolean(m, "boolean", &b));
  OK1(b, "b evaluates to true");

  /* retrieving the wrong type always fails */
  EXPECT_EQ_INT(-2, meta_data_get_boolean(m, "string", &b));
  EXPECT_EQ_INT(-2, meta_data_get_string(m, "signed_int", &s));
  EXPECT_EQ_INT(-2, meta_data_get_string(m, "unsigned_int", &s));
  EXPECT_EQ_INT(-2, meta_data_get_string(m, "double", &s));
  EXPECT_EQ_INT(-2, meta_data_get_string(m, "boolean", &s));

  /* replace existing keys */
  CHECK_ZERO(meta_data_add_signed_int(m, "string", 666));
  OK(meta_data_type(m, "string") == MD_TYPE_SIGNED_INT);

  CHECK_ZERO(meta_data_add_signed_int(m, "signed_int", 666));
  CHECK_ZERO(meta_data_get_signed_int(m, "signed_int", &si));
  EXPECT_EQ_INT(666, (int)si);

  /* deleting keys */
  CHECK_ZERO(meta_data_delete(m, "signed_int"));
  EXPECT_EQ_INT(-2, meta_data_delete(m, "doesnt exist"));

  meta_data_destroy(m);
  return 0;
}

int main(void) {
  RUN_TEST(base);

  END_TEST;
}
