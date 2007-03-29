/**
 * collectd - src/users.c
 * Copyright (C) 2005-2007  Sebastian Harl
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

static data_source_t dsrc[1] =
{
	{"users",  DS_TYPE_GAUGE, 0.0, 65535.0}
};

static data_set_t ds =
{
	"users", 1, dsrc
};

#if USERS_HAVE_READ
static void users_submit (gauge_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "users");

	plugin_dispatch_values ("users", &vl);
} /* void users_submit */

static int users_read (void)
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

	users_submit (users);
#endif /* HAVE_GETUTENT */

	return (0);
} /* int users_read */
#endif /* USERS_HAVE_READ */

void module_register (modreg_e load)
{
	if (load & MR_DATASETS)
		plugin_register_data_set (&ds);

#if USERS_HAVE_READ
	if (load & MR_READ)
		plugin_register_read ("users", users_read);
#endif
} /* void module_register(void) */
