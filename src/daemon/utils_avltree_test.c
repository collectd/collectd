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

#include "common.h" /* STATIC_ARRAY_SIZE */
#include "collectd.h"

#include "testing.h"
#include "utils_avltree.h"

static int compare_total_count = 0;
#define RESET_COUNTS()                                                         \
  do {                                                                         \
    compare_total_count = 0;                                                   \
  } while (0)

static int compare_callback(void const *v0, void const *v1) {
  assert(v0 != NULL);
  assert(v1 != NULL);

  compare_total_count++;
  return (strcmp(v0, v1));
}

DEF_TEST(success) {
  struct {
    char *key;
    char *value;
  } cases[] = {
      {"Eeph7chu", "vai1reiV"}, {"igh3Paiz", "teegh1Ee"},
      {"caip6Uu8", "ooteQu8n"}, {"Aech6vah", "AijeeT0l"},
      {"Xah0et2L", "gah8Taep"}, {"BocaeB8n", "oGaig8io"},
      {"thai8AhM", "ohjeFo3f"}, {"ohth6ieC", "hoo8ieWo"},
      {"aej7Woow", "phahuC2s"}, {"Hai8ier2", "Yie6eimi"},
      {"phuXi3Li", "JaiF7ieb"}, {"Shaig5ef", "aihi5Zai"},
      {"voh6Aith", "Oozaeto0"}, {"zaiP5kie", "seep5veM"},
      {"pae7ba7D", "chie8Ojo"}, {"Gou2ril3", "ouVoo0ha"},
      {"lo3Thee3", "ahDu4Zuj"}, {"Rah8kohv", "ieShoc7E"},
      {"ieN5engi", "Aevou1ah"}, {"ooTe4OhP", "aingai5Y"},
  };

  c_avl_tree_t *t;

  RESET_COUNTS();
  CHECK_NOT_NULL(t = c_avl_create(compare_callback));

  /* insert */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    char *key;
    char *value;

    CHECK_NOT_NULL(key = strdup(cases[i].key));
    CHECK_NOT_NULL(value = strdup(cases[i].value));

    CHECK_ZERO(c_avl_insert(t, key, value));
    EXPECT_EQ_INT((int)(i + 1), c_avl_size(t));
  }

  /* Key already exists. */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++)
    EXPECT_EQ_INT(1, c_avl_insert(t, cases[i].key, cases[i].value));

  /* get */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    char *value_ret = NULL;

    CHECK_ZERO(c_avl_get(t, cases[i].key, (void *)&value_ret));
    EXPECT_EQ_STR(cases[i].value, value_ret);
  }

  /* remove half */
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases) / 2; i++) {
    char *key = NULL;
    char *value = NULL;

    int expected_size = (int)(STATIC_ARRAY_SIZE(cases) - (i + 1));

    CHECK_ZERO(c_avl_remove(t, cases[i].key, (void *)&key, (void *)&value));

    EXPECT_EQ_STR(cases[i].key, key);
    EXPECT_EQ_STR(cases[i].value, value);

    free(key);
    free(value);

    EXPECT_EQ_INT(expected_size, c_avl_size(t));
  }

  /* pick the other half */
  for (size_t i = STATIC_ARRAY_SIZE(cases) / 2; i < STATIC_ARRAY_SIZE(cases);
       i++) {
    char *key = NULL;
    char *value = NULL;

    int expected_size = (int)(STATIC_ARRAY_SIZE(cases) - (i + 1));

    EXPECT_EQ_INT(expected_size + 1, c_avl_size(t));
    EXPECT_EQ_INT(0, c_avl_pick(t, (void *)&key, (void *)&value));

    free(key);
    free(value);

    EXPECT_EQ_INT(expected_size, c_avl_size(t));
  }

  c_avl_destroy(t);

  return (0);
}

int main(void) {
  RUN_TEST(success);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
