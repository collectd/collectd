/**
 * collectd - contrib/docker/rootfs_prefix/rootfs_prefix.c
 * Copyright (C) 2016-2018  Marc Fournier
 * Copyright (C) 2016-2018  Ruben Kerkhof
 *
 * MIT License:
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
 *   Marc Fournier <marc.fournier at camptocamp.com>
 *   Ruben Kerkhof <ruben at rubenkerkhof.com>
 **/

#define _GNU_SOURCE

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "/rootfs"
#define BUFSIZE 256

const char *add_prefix(const char *orig, char *prefixed) {
  if ((strncmp(orig, "/proc", strlen("/proc")) != 0) &&
      (strncmp(orig, "/sys", strlen("/sys")) != 0))
    return orig;

  int status = snprintf(prefixed, BUFSIZE, "%s%s", PREFIX, orig);
  if (status < 1) {
    error(status, errno, "adding '%s' prefix to file path failed: '%s' -> '%s'",
          PREFIX, orig, prefixed);
    return orig;
  } else if ((unsigned int)status >= BUFSIZE) {
    error(status, ENAMETOOLONG,
          "'%s' got truncated when adding '%s' prefix: '%s'", orig, PREFIX,
          prefixed);
    return orig;
  } else {
    return (const char *)prefixed;
  }
}

FILE *fopen(const char *path, const char *mode) {
  char filename[BUFSIZE] = "\0";

  FILE *(*original_fopen)(const char *, const char *);
  original_fopen = dlsym(RTLD_NEXT, "fopen");

  return (*original_fopen)(add_prefix(path, filename), mode);
}

DIR *opendir(const char *name) {
  char filename[BUFSIZE] = "\0";

  DIR *(*original_opendir)(const char *);
  original_opendir = dlsym(RTLD_NEXT, "opendir");

  return (*original_opendir)(add_prefix(name, filename));
}

int *open(const char *pathname, int flags) {
  char filename[BUFSIZE] = "\0";

  int *(*original_open)(const char *, int);
  original_open = dlsym(RTLD_NEXT, "open");

  return (*original_open)(add_prefix(pathname, filename), flags);
}
