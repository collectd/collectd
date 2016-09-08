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

#include "utils_format_graphite.h"
#include "utils_format_json.h"

#include <netdb.h>

#define WL_BUF_SIZE 16384

#define WL_FORMAT_GRAPHITE 1
#define WL_FORMAT_JSON 2

static int wl_write_graphite (const data_set_t *ds, const value_list_t *vl)
{
    char buffer[WL_BUF_SIZE] = { 0 };
    int status;

    if (0 != strcmp (ds->type, vl->type))
    {
        ERROR ("write_log plugin: DS type does not match value list type");
        return -1;
    }

    status = format_graphite (buffer, sizeof (buffer), ds, vl,
                              NULL, NULL, '_', 0);
    if (status != 0) /* error message has been printed already. */
        return (status);

    INFO ("write_log values:\n%s", buffer);

    return (0);
} /* int wl_write_graphite */

static int wl_write_json (const data_set_t *ds, const value_list_t *vl)
{
    char buffer[WL_BUF_SIZE] = { 0 };
    size_t bfree = sizeof(buffer);
    size_t bfill = 0;

    if (0 != strcmp (ds->type, vl->type))
    {
        ERROR ("write_log plugin: DS type does not match value list type");
        return -1;
    }

    format_json_initialize(buffer, &bfill, &bfree);
    format_json_value_list(buffer, &bfill, &bfree, ds, vl,
                           /* store rates = */ 0);
    format_json_finalize(buffer, &bfill, &bfree);

    INFO ("write_log values:\n%s", buffer);

    return (0);
} /* int wl_write_json */

static int wl_write (const data_set_t *ds, const value_list_t *vl,
        user_data_t *user_data)
{
    int status = 0;
    int mode = (int) (size_t) user_data->data;

    if (mode == WL_FORMAT_GRAPHITE)
    {
        status = wl_write_graphite (ds, vl);
    }
    else if (mode == WL_FORMAT_JSON)
    {
        status = wl_write_json (ds, vl);
    }

    return (status);
}

static int wl_config (oconfig_item_t *ci) /* {{{ */
{
    int mode = 0;
    for (int i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp ("Format", child->key) == 0)
        {
            char *mode_str = NULL;
            if ((child->values_num != 1)
                || (child->values[0].type != OCONFIG_TYPE_STRING))
            {
                ERROR ("write_log plugin: Option `%s' requires "
                    "exactly one string argument.", child->key);
                return (-EINVAL);
            }
            if (mode != 0)
            {
                WARNING ("write_log plugin: Redefining option `%s'.",
                    child->key);
            }
            mode_str = child->values[0].value.string;
            if (strcasecmp ("Graphite", mode_str) == 0)
                mode = WL_FORMAT_GRAPHITE;
            else if (strcasecmp ("JSON", mode_str) == 0)
                mode = WL_FORMAT_JSON;
            else
            {
                ERROR ("write_log plugin: Unknown mode `%s' for option `%s'.",
                    mode_str, child->key);
                return (-EINVAL);
            }
        }
        else
        {
            ERROR ("write_log plugin: Invalid configuration option: `%s'.",
                child->key);
        }
    }
    if (mode == 0)
        mode = WL_FORMAT_GRAPHITE;

    user_data_t ud = {
        .data = (void *) (size_t) mode,
        .free_func = NULL
    };

    plugin_register_write ("write_log", wl_write, &ud);

    return (0);
} /* }}} int wl_config */

void module_register (void)
{
    plugin_register_complex_config ("write_log", wl_config);

    user_data_t ud = {
        .data = (void *) (size_t) WL_FORMAT_GRAPHITE,
        .free_func = NULL
    };

    plugin_register_write ("write_log", wl_write, &ud);
}

/* vim: set sw=4 ts=4 sts=4 tw=78 et : */
