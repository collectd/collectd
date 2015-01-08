/**
 * collectd - src/write_log.c
 * Copyright (C) 2015       Pierre-Yves Ritschard
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
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 *
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include "utils_format_graphite.h"

/* Folks without pthread will need to disable this plugin. */
#include <pthread.h>

#include <sys/socket.h>
#include <netdb.h>

#define WL_BUF_SIZE 8192

static int wl_write_messages (const data_set_t *ds, const value_list_t *vl)
{
    char buffer[WL_BUF_SIZE];
    int status;

    if (0 != strcmp (ds->type, vl->type))
    {
        ERROR ("write_log plugin: DS type does not match "
                "value list type");
        return -1;
    }

    memset (buffer, 0, sizeof (buffer));
    status = format_graphite (buffer, sizeof (buffer), ds, vl,
                              NULL, NULL, '_', 0);
    if (status != 0) /* error message has been printed already. */
        return (status);

    INFO ("write_log values:\n%s", buffer);

    return (0);
} /* int wl_write_messages */

static int wl_write (const data_set_t *ds, const value_list_t *vl,
        user_data_t *user_data)
{
    int status;

    status = wl_write_messages (ds, vl);

    return (status);
}

void module_register (void)
{
    plugin_register_write ("write_log", wl_write, NULL);
}

/* vim: set sw=4 ts=4 sts=4 tw=78 et : */
