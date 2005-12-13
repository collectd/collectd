/**
 * collectd - src/users.h
 * Copyright (C) 2005  Sebastian Harl
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Sebastian Harl <sh at tokkee.org>
 **/

#ifndef USERS_H
#define USERS_H 1

#include <config.h>

#if !defined(HAVE_UTMPX_H) || !defined(HAVE_GETUTXENT)
#undef HAVE_UTMPX_H
#undef HAVE_GETUTXENT
#endif

#if !defined(HAVE_UTMP_H) || !defined(HAVE_GETUTENT)
#undef HAVE_UTMPX_H
#undef HAVE_GETUTXENT
#endif

#ifndef COLLECT_USERS
#if defined(HAVE_UTMPX_H) || defined(HAVE_UTMP_H)
#define COLLECT_USERS 1
#else
#define COLLECT_USERS 0
#endif
#endif /* ! defined(COLLECT_USERS) */

void users_init(void);
void users_read(void);
void users_submit(unsigned int);
void users_write(char *, char *, char *);

#endif /* ! defined(USERS_H) */

