/**
 * collectd - src/users.c
 * Copyright (C) 2005-2007  Sebastian Harl
 * Copyright (C) 2005       Niki W. Waibel
 * Copyright (C) 2005-2007  Florian octo Forster
 * Copyright (C) 2008       Oleg King 
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the license is applicable.
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
 *   Niki W. Waibel <niki.waibel at newlogic.com>
 *   Florian octo Forster <octo at verplant.org>
 *   Oleg King <king2 at kaluga.ru>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_STATGRAB_H
# include <statgrab.h>
#endif /* HAVE_STATGRAB_H */

#if HAVE_UTMPX_H
# include <utmpx.h>
/* #endif HAVE_UTMPX_H */

#elif HAVE_UTMP_H
# include <utmp.h>
/* #endif HAVE_UTMP_H */

#else
# error "No applicable input method."
#endif

static void users_submit (gauge_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "users", sizeof (vl.plugin));
	sstrncpy (vl.type, "users", sizeof (vl.plugin));

	plugin_dispatch_values (&vl);
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
/* #endif HAVE_GETUTENT */

#elif HAVE_LIBSTATGRAB
	sg_user_stats *us;

	us = sg_get_user_stats ();
	if (us == NULL)
		return (-1);   

	users_submit ((gauge_t) us->num_entries);
/* #endif HAVE_LIBSTATGRAB */

#else
# error "No applicable input method."
#endif

	return (0);
} /* int users_read */

void module_register (void)
{
	plugin_register_read ("users", users_read);
} /* void module_register(void) */
