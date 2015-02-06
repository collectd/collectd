/*
 * Copyright (c) 2004-2005, 2007, 2010
 *	Todd C. Miller <Todd.Miller@courtesan.com>
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

#include <config.h>

#include <sys/types.h>
#include <sys/param.h>
#include <unistd.h>
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif /* STDC_HEADERS */
#include <fcntl.h>
#ifdef HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# ifdef HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# ifdef HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# ifdef HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#include "missing.h"

#ifndef HAVE_FCNTL_CLOSEM
# ifndef HAVE_DIRFD
#   define closefrom_fallback	closefrom
# endif
#endif

/*
 * Close all file descriptors greater than or equal to lowfd.
 * This is the expensive (ballback) method.
 */
void
closefrom_fallback(int lowfd)
{
    long fd, maxfd;

    /*
     * Fall back on sysconf() or getdtablesize().  We avoid checking
     * resource limits since it is possible to open a file descriptor
     * and then drop the rlimit such that it is below the open fd.
     */
#ifdef HAVE_SYSCONF
    maxfd = sysconf(_SC_OPEN_MAX);
#else
    maxfd = getdtablesize();
#endif /* HAVE_SYSCONF */
    if (maxfd < 0)
	maxfd = OPEN_MAX;

    for (fd = lowfd; fd < maxfd; fd++)
	(void) close((int) fd);
}

/*
 * Close all file descriptors greater than or equal to lowfd.
 * We try the fast way first, falling back on the slow method.
 */
#ifdef HAVE_FCNTL_CLOSEM
void
closefrom(int lowfd)
{
    if (fcntl(lowfd, F_CLOSEM, 0) == -1)
	closefrom_fallback(lowfd);
}
#else
# ifdef HAVE_DIRFD
void
closefrom(int lowfd)
{
    struct dirent *dent;
    DIR *dirp;
    char *endp;
    long fd;

    /* Use /proc/self/fd directory if it exists. */
    if ((dirp = opendir("/proc/self/fd")) != NULL) {
	while ((dent = readdir(dirp)) != NULL) {
	    fd = strtol(dent->d_name, &endp, 10);
	    if (dent->d_name != endp && *endp == '\0' &&
		fd >= 0 && fd < INT_MAX && fd >= lowfd && fd != dirfd(dirp))
		(void) close((int) fd);
	}
	(void) closedir(dirp);
    } else
	closefrom_fallback(lowfd);
}
#endif /* HAVE_DIRFD */
#endif /* HAVE_FCNTL_CLOSEM */
