/*
 * collectd - src/utils_logtail.h
 * Copyright (C) 2007-2008  C-Ware, Inc.
 * Copyright (C) 2008       Florian Forster
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
 *   Luke Heberling <lukeh at c-ware.com>
 *   Florian Forster <octo at verplant.org>
 *
 * Description:
 *   Encapsulates useful code to plugins which must parse a log file.
 *
 */

#include "utils_match.h"

struct cu_logtail_s;
typedef struct cu_logtail_s cu_logtail_t;

struct cu_logtail_simple_s
{
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
};
typedef struct cu_logtail_simple_s cu_logtail_simple_t;

/*
 * NAME
 *   logtail_create
 *
 * DESCRIPTION
 *   Allocates, initializes and returns a new `cu_logtail_t' object.
 *
 * PARAMETERS
 *   `filename'  The name to read data from.
 *   `plugin'    The plugin name to use when dispatching values.
 *
 * RETURN VALUE
 *   Returns NULL upon failure, non-NULL otherwise.
 */
cu_logtail_t *logtail_create (const char *filename);

/*
 * NAME
 *   logtail_destroy
 *
 * DESCRIPTION
 *   Releases resources used by the `cu_logtail_t' object.
 *
 * PARAMETERS
 *   The object to destroy.
 */
void logtail_destroy (cu_logtail_t *obj);

/*
 * NAME
 *   logtail_add_match_simple
 *
 * DESCRIPTION
 *   Adds a match, in form of a regular expression, to the `cu_logtail_t'
 *   object. The values matched by that regular expression are dispatched to
 *   the daemon using the supplied `type' and `type_instance' fields.
 *
 * PARAMETERS
 *   `obj'
 *   `type'
 *   `type_instance'
 *   `match_ds_type'
 *   `regex'
 *
 * RETURN VALUE
 *   Zero upon success, non-zero otherwise.
 */
int logtail_add_match (cu_logtail_t *obj, cu_match_t *match,
    int (*submit_match) (cu_match_t *match, void *user_data),
    void *user_data,
    void (*free_user_data) (void *user_data));

int logtail_add_match_simple (cu_logtail_t *obj,
    const char *regex, int ds_type,
    const char *plugin, const char *plugin_instance,
    const char *type, const char *type_instance);

/*
 * NAME
 *   logtail_read
 *
 * DESCRIPTION
 *   Looks for more data in the log file, sends each line
 *   through the given function, and submits the counters to
 *   collectd.
 *
 * PARAMETERS
 *   `instances' The handle used to identify the plugin.
 *
 *   `func'      The function used to parse each line from the log.
 *
 *   `plugin'    The name of the plugin.
 *
 *   `names'     An array of counter names in the same order as the
 *               counters themselves.
 *
 * RETURN VALUE
 *   Zero on success, nonzero on failure.
*/
int logtail_read (cu_logtail_t *obj);

/* vim: set sw=2 sts=2 ts=8 : */
