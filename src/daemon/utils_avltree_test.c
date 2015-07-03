/**
 * collectd - src/tests/test_utils_avltree.c
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
#include "utils_avltree.h"

static int compare_total_count = 0;
#define RESET_COUNTS() do { compare_total_count = 0; } while (0)

static int compare_callback (void const *v0, void const *v1)
{
  assert (v0 != NULL);
  assert (v1 != NULL);

  compare_total_count++;
  return (strcmp (v0, v1));
}

DEF_TEST(success)
{
  c_avl_tree_t *t;
  char key_orig[] = "foo";
  char value_orig[] = "bar";
  char *key_ret = NULL;
  char *value_ret = NULL;

  RESET_COUNTS ();
  t = c_avl_create (compare_callback);
  OK (t != NULL);

  OK (c_avl_insert (t, key_orig, value_orig) == 0);
  OK (c_avl_size (t) == 1);

  /* Key already exists. */
  OK (c_avl_insert (t, "foo", "qux") > 0);

  OK (c_avl_get (t, "foo", (void *) &value_ret) == 0);
  OK (value_ret == &value_orig[0]);

  key_ret = value_ret = NULL;
  OK (c_avl_remove (t, "foo", (void *) &key_ret, (void *) &value_ret) == 0);
  OK (key_ret == &key_orig[0]);
  OK (value_ret == &value_orig[0]);
  OK (c_avl_size (t) == 0);

  c_avl_destroy (t);

  return (0);
}

int main (void)
{
  RUN_TEST(success);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
