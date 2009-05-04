/**
 * collectd - src/collectd.h
 * Copyright (C) 2009  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 **/

#ifndef COLLECTD_STANDARDS_H
#define COLLECTD_STANDARDS_H 1

# ifndef _ISOC99_SOURCE
#  define _ISOC99_SOURCE
# endif
# ifndef _POSIX_SOURCE
#  define _POSIX_SOURCE
# endif
# ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200112L
# endif
# ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 600
# endif
# ifndef _REENTRANT
#  define _REENTRANT
# endif

#if 0
/* Disable non-standard extensions */
# ifdef _BSD_SOURCE
#  undef _BSD_SOURCE
# endif
# ifdef _SVID_SOURCE
#  undef _SVID_SOURCE
# endif
# ifdef _GNU_SOURCE
#  undef _GNU_SOURCE
# endif
#endif /* 0 */

#endif /* COLLECTD_STANDARDS_H */
