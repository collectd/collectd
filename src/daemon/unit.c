/**
 * collectd - src/daemon/unit.c
 * Copyright (C) 2023       Florian "octo" Forster
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
 **/

#include "collectd.h"
#include "daemon/unit.h"

static bool string_has_suffix(char const *s, char const *suffix) {
  size_t s_len = strlen(s);
  size_t suffix_len = strlen(suffix);

  if (s_len < suffix_len) {
    return false;
  }

  s += (s_len - suffix_len);
  return strcmp(s, suffix) == 0;
}

static char const *default_unit_static(metric_family_t const *fam) {
  if (string_has_suffix(fam->name, ".utilization")) {
    return "1";
  }
  if (string_has_suffix(fam->name, ".time")) {
    return "s";
  }
  if (string_has_suffix(fam->name, ".io")) {
    return "By";
  }
  if (string_has_suffix(fam->name, ".operations")) {
    return "{operation}";
  }

  return NULL;
}

char *default_unit(metric_family_t const *fam) {
  if (fam->unit != NULL) {
    return fam->unit;
  }

  char const *unit = default_unit_static(fam);
  if (unit == NULL) {
    return NULL;
  }

  return strdup(unit);
}
