/**
 * collectd - src/users.c
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

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_UTMPX_H
# include <utmpx.h>
#else /* !HAVE_UTMPX_H */
# if HAVE_UTMP_H
#  include <utmp.h>
# endif /* HAVE_UTMP_H */
#endif /* HAVE_UTMPX_H */

#define MODULE_NAME "users"

#if HAVE_GETUTXENT || HAVE_GETUTENT
# define USERS_HAVE_READ 1
#else
# define USERS_HAVE_READ 0
#endif

static char *rrd_file = "users.rrd";
static char *ds_def[] =
{
	"DS:users:GAUGE:25:0:65535",
	NULL
};
static int ds_num = 1;

static void users_init (void)
{
	/* we have nothing to do here :-) */
	return;
} /* static void users_init(void) */

static void users_write (char *host, char *inst, char *val)
{
	rrd_update_file(host, rrd_file, val, ds_def, ds_num);
	return;
} /* static void users_write(char *host, char *inst, char *val) */

#if USERS_HAVE_READ
/* I don't like this temporary macro definition - well it's used everywhere
   else in the collectd-sources, so I will just stick with it...  */
#define BUFSIZE 256
static void users_submit (unsigned int users)
{
	char buf[BUFSIZE] = "";

	if (snprintf(buf, BUFSIZE, "%u:%u", 
		(unsigned int)curtime, users) >= BUFSIZE)
	{
		return;
	}

	plugin_submit(MODULE_NAME, NULL, buf);
	return;
} /* static void users_submit(unsigned int users) */
#undef BUFSIZE

static void users_read (void)
{
#if HAVE_GETUTXENT
	unsigned int users = 0;
	struct utmpx *entry = NULL;

	/* according to the *utent(3) man page none of the functions sets errno
	   in case of an error, so we cannot do any error-checking here */
	setutxent();

	while (NULL != (entry = getutxent())) {
		if (USER_PROCESS == entry->ut_type) {
			++users;
		}
	}
	endutxent();

	users_submit (users);
/* #endif HAVE_GETUTXENT */
	
#elif HAVE_GETUTENT
	unsigned int users = 0;
	struct utmp *entry = NULL;

	/* according to the *utent(3) man page none of the functions sets errno
	   in case of an error, so we cannot do any error-checking here */
	setutent();

	while (NULL != (entry = getutent())) {
		if (USER_PROCESS == entry->ut_type) {
			++users;
		}
	}
	endutent();

	users_submit(users);
#endif /* HAVE_GETUTENT */

	return;
} /* static void users_read(void) */
#else
# define users_read NULL
#endif /* USERS_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, users_init, users_read, users_write);
	return;
} /* void module_register(void) */
