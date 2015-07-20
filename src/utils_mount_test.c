/**
 * collectd - src/tests/test_utils_mount.c
 * Copyright (C) 2013       Florian octo Forster
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
#include "collectd.h"
#include "utils_mount.h"

DEF_TEST(cu_mount_checkoption)
{
  char line_opts[] = "foo=one,bar=two,qux=three";
  char *foo = strstr (line_opts, "foo");
  char *bar = strstr (line_opts, "bar");
  char *qux = strstr (line_opts, "qux");

  char line_bool[] = "one,two,three";
  char *one = strstr (line_bool, "one");
  char *two = strstr (line_bool, "two");
  char *three = strstr (line_bool, "three");

  /* Normal operation */
  OK (foo == cu_mount_checkoption (line_opts, "foo", 0));
  OK (bar == cu_mount_checkoption (line_opts, "bar", 0));
  OK (qux == cu_mount_checkoption (line_opts, "qux", 0));
  OK (NULL == cu_mount_checkoption (line_opts, "unknown", 0));

  OK (one == cu_mount_checkoption (line_bool, "one", 0));
  OK (two == cu_mount_checkoption (line_bool, "two", 0));
  OK (three == cu_mount_checkoption (line_bool, "three", 0));
  OK (NULL == cu_mount_checkoption (line_bool, "four", 0));

  /* Shorter and longer parts */
  OK (foo == cu_mount_checkoption (line_opts, "fo", 0));
  OK (bar == cu_mount_checkoption (line_opts, "bar=", 0));
  OK (qux == cu_mount_checkoption (line_opts, "qux=thr", 0));

  OK (one == cu_mount_checkoption (line_bool, "o", 0));
  OK (two == cu_mount_checkoption (line_bool, "tw", 0));
  OK (three == cu_mount_checkoption (line_bool, "thr", 0));

  /* "full" flag */
  OK (one == cu_mount_checkoption (line_bool, "one", 1));
  OK (two == cu_mount_checkoption (line_bool, "two", 1));
  OK (three == cu_mount_checkoption (line_bool, "three", 1));
  OK (NULL == cu_mount_checkoption (line_bool, "four", 1));

  OK (NULL == cu_mount_checkoption (line_bool, "o", 1));
  OK (NULL == cu_mount_checkoption (line_bool, "tw", 1));
  OK (NULL == cu_mount_checkoption (line_bool, "thr", 1));

  return (0);
}
DEF_TEST(cu_mount_getoptionvalue)
{
  char line_opts[] = "foo=one,bar=two,qux=three";
  char line_bool[] = "one,two,three";

  STREQ ("one", cu_mount_getoptionvalue (line_opts, "foo="));
  STREQ ("two", cu_mount_getoptionvalue (line_opts, "bar="));
  STREQ ("three", cu_mount_getoptionvalue (line_opts, "qux="));
  OK (NULL == cu_mount_getoptionvalue (line_opts, "unknown="));

  STREQ ("", cu_mount_getoptionvalue (line_bool, "one"));
  STREQ ("", cu_mount_getoptionvalue (line_bool, "two"));
  STREQ ("", cu_mount_getoptionvalue (line_bool, "three"));
  OK (NULL == cu_mount_getoptionvalue (line_bool, "four"));

  return (0);
}

int main (void)
{
  RUN_TEST(cu_mount_checkoption);
  RUN_TEST(cu_mount_getoptionvalue);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
