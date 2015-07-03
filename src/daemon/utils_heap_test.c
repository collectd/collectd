/**
 * collectd - src/tests/test_utils_heap.c
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
#include "utils_heap.h"

static int compare (void const *v0, void const *v1)
{
  int const *i0 = v0;
  int const *i1 = v1;

  if ((*i0) < (*i1))
    return -1;
  else if ((*i0) > (*i1))
    return 1;
  else
    return 0;
}

DEF_TEST(simple)
{
  int values[] = { 9, 5, 6, 1, 3, 4, 0, 8, 2, 7 };
  int i;
  c_heap_t *h;

  CHECK_NOT_NULL(h = c_heap_create (compare));
  for (i = 0; i < 10; i++)
    CHECK_ZERO(c_heap_insert (h, &values[i]));

  for (i = 0; i < 5; i++)
  {
    int *ret = NULL;
    CHECK_NOT_NULL(ret = c_heap_get_root(h));
    OK(*ret == i);
  }

  CHECK_ZERO(c_heap_insert (h, &values[6] /* = 0 */));
  CHECK_ZERO(c_heap_insert (h, &values[3] /* = 1 */));
  CHECK_ZERO(c_heap_insert (h, &values[8] /* = 2 */));
  CHECK_ZERO(c_heap_insert (h, &values[4] /* = 3 */));
  CHECK_ZERO(c_heap_insert (h, &values[5] /* = 4 */));

  for (i = 0; i < 10; i++)
  {
    int *ret = NULL;
    CHECK_NOT_NULL(ret = c_heap_get_root(h));
    OK(*ret == i);
  }

  c_heap_destroy(h);
  return (0);
}

int main (void)
{
  RUN_TEST(simple);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
