/**
 * collectd - src/write_log.c
 * Copyright (C) 2015       Pierre-Yves Ritschard
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
