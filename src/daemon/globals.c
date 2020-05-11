/**
 * collectd - src/globals.c
 * Copyright (C) 2017  Google LLC
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
 **/

// clang-format off
/*
 * Explicit order is required or _FILE_OFFSET_BITS will have definition mismatches on Solaris
 * See Github Issue #3193 for details
 */
#include "utils/common/common.h"
#include "globals.h"
// clang-format on

#if HAVE_KSTAT_H
#include <kstat.h>
#endif

/*
 * Global variables
 */
char *hostname_g;
cdtime_t interval_g;
int timeout_g;
#if HAVE_KSTAT_H
kstat_ctl_t *kc;
#endif

void hostname_set(char const *hostname) {
  char *h = strdup(hostname);
  if (h == NULL)
    return;

  sfree(hostname_g);
  hostname_g = h;
}

/* sanitize_version copies v into buf and drops the third period and everything
 * following it, so that "5.11.0.32.g86275a6+" becomes "5.11.0". */
static void sanitize_version(char *buf, size_t buf_size, char const *v) {
  sstrncpy(buf, v, buf_size);

  // find the third period.
  char *ptr = buf;
  for (int i = 0; i < 3; i++) {
    if (i != 0) {
      // point to the character *following* the period.
      ptr++;
    }
    char *chr = strchr(ptr, '.');
    if (chr == NULL) {
      return;
    }
    ptr = chr;
  }

  // If we get here, there are at least three period in the version
  // string. Such a version string may look like this:
  // "5.11.0.32.g86275a6+". `ptr` is pointing here:
  //        ^-- ptr
  *ptr = 0;
}

static char clean_version[32] = "";
char const *collectd_version(void) {
  if (strlen(clean_version) == 0) {
    sanitize_version(clean_version, sizeof(clean_version), PACKAGE_VERSION);
  }
  return clean_version;
}
