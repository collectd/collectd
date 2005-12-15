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

#include "users.h"

#if COLLECT_USERS
#define MODULE_NAME "users"

#include "plugin.h"
#include "common.h"

#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#elif defined(HAVE_UTMP_H)
#include <utmp.h>
#endif

static char *rrd_file = "users.rrd";

static char *ds_def[] = {
    "DS:users:GAUGE:25:0:65535",
    NULL
};
static int ds_num = 1;

void users_init(void)
{
    /* we have nothing to do here :-) */
    return;
}

void users_read(void)
{
#ifdef HAVE_GETUTXENT
    unsigned int users = 0;
    struct utmpx *entry = NULL;

    /* according to the *utent(3) man page none of the functions sets errno in
     * case of an error, so we cannot do any error-checking here */
    setutxent();

    while (NULL != (entry = getutxent()))
        if (USER_PROCESS == entry->ut_type)
            ++users;
    endutxent();

    users_submit(users);
/* #endif HAVE_GETUTXENT */

#elif defined(HAVE_GETUTENT)
    unsigned int users = 0;
    struct utmp *entry = NULL;

    /* according to the *utent(3) man page none of the functions sets errno in
     * case of an error, so we cannot do any error-checking here */
    setutent();

    while (NULL != (entry = getutent()))
        if (USER_PROCESS == entry->ut_type)
            ++users;
    endutent();

    users_submit(users);
#endif

	return;
}

/* I don't like this temporary macro definition - well it's used everywhere
 * else in the collectd-sources, so I will just stick with it...  */
#define BUFSIZE 256
void users_submit(users)
    unsigned int users;
{
    char buf[BUFSIZE] = "";

    if (snprintf(buf, BUFSIZE, "%u:%u", 
                (unsigned int)curtime, 
                users) >= BUFSIZE)
        return;

    plugin_submit(MODULE_NAME, NULL, buf);
    return;
}
#undef BUFSIZE

void users_write(host, inst, val)
    char *host;
    char *inst;
    char *val;
{
    rrd_update_file(host, rrd_file, val, ds_def, ds_num);
    return;
}

void module_register(void)
{
    plugin_register(MODULE_NAME, users_init, users_read, users_write);
    return;
}

#undef MODULE_NAME
#endif /* COLLECT_USERS */

