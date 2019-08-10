/**
 * Attribution to sudo.ws sources as a inspiration for this:
 *
 * Copyright (c) 2004-2005, 2007, 2010, 2012-2015, 2017-2018
 *  Todd C. Miller <Todd.Miller@sudo.ws>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "utils_closefrom.h"

#include <dirent.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils_strtonum.h"

#ifndef _POSIX_OPEN_MAX
#define _POSIX_OPEN_MAX 20
#endif

/*
 * Close all file descriptors greater than or equal to lowfd.
 * This is the expensive (fallback) method.
 */
static void closefrom_fallback(int lowfd) {
  /*
   * Fall back on sysconf(_SC_OPEN_MAX)
   */
  long maxfd = sysconf(_SC_OPEN_MAX);
  if (maxfd < 0)
    maxfd = _POSIX_OPEN_MAX;

  for (long fd = lowfd; fd < maxfd; fd++)
    (void)close((int)fd);
}

/*
 * Close all file descriptors greater than or equal to lowfd.
 * We try the fast way first, falling back on the slow method.
 */
void closefrom(int lowfd) {
  const char *path;
  DIR *dirp;

  /* try only closing just opened FDs */
  path = "/proc/self/fd";
  if ((dirp = opendir(path)) != NULL) {
    struct dirent *dent;
    while ((dent = readdir(dirp)) != NULL) {
      const char *errstr;
      int fd = strtonum(dent->d_name, lowfd, INT_MAX, &errstr);
      if (errstr == NULL && fd != dirfd(dirp)) {
        (void)close(fd);
      }
    }
    (void)closedir(dirp);
    return;
  }

  /* just close "numbers" and don't care about actually opened FDs */
  closefrom_fallback(lowfd);
}
