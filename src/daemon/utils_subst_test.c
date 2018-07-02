/**
 * collectd - src/daemon/utils_subst_test.c
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
#include "common.h" /* for STATIC_ARRAY_SIZE */

#include "testing.h"
#include "utils_subst.h"

#if HAVE_KSTAT_H
#include <kstat.h>
#endif

#if HAVE_LIBKSTAT
kstat_ctl_t *kc;
#endif /* HAVE_LIBKSTAT */

DEF_TEST(subst) {
  struct {
    const char *str;
    int off1;
    int off2;
    const char *rplmt;
    const char *want;
  } cases[] = {
      {"foo_____bar", 3, 8, " - ", "foo - bar"}, /* documentation example */
      {"foo bar", 0, 2, "m", "mo bar"},          /* beginning, shorten */
      {"foo bar", 0, 1, "m", "moo bar"},         /* beginning, same length */
      {"foo bar", 0, 3, "milk", "milk bar"},     /* beginning, extend */
      {"foo bar", 3, 6, "de", "fooder"},         /* center, shorten */
      {"foo bar", 2, 6, "rste", "forster"},      /* center, same length */
      {"foo bar", 1, 3, "ish", "fish bar"},      /* center, extend */
      {"foo bar", 2, 7, "ul", "foul"},           /* end, shorten */
      {"foo bar", 3, 7, "lish", "foolish"},      /* end, same length */
      {"foo bar", 3, 7, "dwear", "foodwear"},    /* end, extend */
      /* truncation (buffer is 16 chars) */
      {"01234567890123", 8, 8, "", "01234567890123"},
      {"01234567890123", 8, 8, "*", "01234567*890123"},
      {"01234567890123", 8, 8, "**", "01234567**89012"},
      /* input > buffer */
      {"012345678901234----", 0, 0, "", "012345678901234"},
      {"012345678901234----", 17, 18, "", "012345678901234"},
      {"012345678901234----", 0, 3, "", "345678901234---"},
      {"012345678901234----", 0, 4, "", "45678901234----"},
      {"012345678901234----", 0, 5, "", "5678901234----"},
      {"012345678901234----", 8, 8, "#", "01234567#890123"},
      {"012345678901234----", 12, 12, "##", "012345678901##2"},
      {"012345678901234----", 13, 13, "##", "0123456789012##"},
      {"012345678901234----", 14, 14, "##", "01234567890123#"},
      {"012345678901234----", 15, 15, "##", "012345678901234"},
      {"012345678901234----", 16, 16, "##", "012345678901234"},
      /* error cases */
      {NULL, 3, 4, "_", NULL},        /* no input */
      {"foo bar", 3, 10, "_", NULL},  /* offset exceeds input */
      {"foo bar", 10, 13, "_", NULL}, /* offset exceeds input */
      {"foo bar", 4, 3, "_", NULL},   /* off1 > off2 */
      {"foo bar", 3, 4, NULL, NULL},  /* no replacement */
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    char buffer[16] = "!!!!!!!!!!!!!!!";

    if (cases[i].want == NULL) {
      OK(subst(buffer, sizeof(buffer), cases[i].str, cases[i].off1,
               cases[i].off2, cases[i].rplmt) == NULL);
      continue;
    }

    OK(subst(buffer, sizeof(buffer), cases[i].str, cases[i].off1, cases[i].off2,
             cases[i].rplmt) == &buffer[0]);
    EXPECT_EQ_STR(cases[i].want, buffer);
  }

  return 0;
}

DEF_TEST(subst_string) {
  struct {
    const char *str;
    const char *srch;
    const char *rplmt;
    const char *want;
  } cases[] = {
      {"Hello %{name}", "%{name}", "world", "Hello world"},
      {"abcccccc", "abc", "cab", "ccccccab"},
      {"(((()(())))())", "()", "", ""},
      {"food booth", "oo", "ee", "feed beeth"},
      {"foo bar", "baz", "qux", "foo bar"},
      {"foo bar", "oo", "oo", "foo bar"},
      {"sixteen chars", "chars", "characters", "sixteen charact"},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    char buffer[16];

    if (cases[i].want == NULL) {
      OK(subst_string(buffer, sizeof(buffer), cases[i].str, cases[i].srch,
                      cases[i].rplmt) == NULL);
      continue;
    }

    OK(subst_string(buffer, sizeof(buffer), cases[i].str, cases[i].srch,
                    cases[i].rplmt) == buffer);
    EXPECT_EQ_STR(cases[i].want, buffer);
  }

  return 0;
}

int main(void) {
  RUN_TEST(subst);
  RUN_TEST(subst_string);

  END_TEST;
}
