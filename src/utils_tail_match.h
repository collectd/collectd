/*
 * collectd - src/utils_tail_match.h
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
 *   `tail_match' uses `utils_tail' and `utils_match' to tail a file and try to
 *   match it using several regular expressions. Matches are then passed to
 *   user-provided callback functions or default handlers. This should keep all
 *   of the parsing logic out of the actual plugin, which only operate with
 *   regular expressions.
 */

#include "utils_match.h"

struct cu_tail_match_s;
typedef struct cu_tail_match_s cu_tail_match_t;

/*
 * NAME
 *   tail_match_create
 *
 * DESCRIPTION
 *   Allocates, initializes and returns a new `cu_tail_match_t' object.
 *
 * PARAMETERS
 *   `filename'  The name to read data from.
 *
 * RETURN VALUE
 *   Returns NULL upon failure, non-NULL otherwise.
 */
cu_tail_match_t *tail_match_create (const char *filename);

/*
 * NAME
 *   tail_match_destroy
 *
 * DESCRIPTION
 *   Releases resources used by the `cu_tail_match_t' object.
 *
 * PARAMETERS
 *   The object to destroy.
 */
void tail_match_destroy (cu_tail_match_t *obj);

/*
 * NAME
 *   tail_match_add_match
 *
 * DESCRIPTION
 *   Adds a match, in form of a `cu_match_t' object, to the object.
 *   After data has been read from the logfile (using utils_tail) the callback
 *   function `submit_match' is called with the match object and the user
 *   supplied data.
 *   Please note that his function is called regardless whether this match
 *   matched any lines recently or not.
 *   When `tail_match_destroy' is called the `user_data' pointer is freed using
 *   the `free_user_data' callback - if it is not NULL.
 *   When using this interface the `tail_match' module doesn't dispatch any values
 *   itself - all that has to happen in either the match-callbacks or the
 *   submit_match callback.
 *
 * RETURN VALUE
 *   Zero upon success, non-zero otherwise.
 */
int tail_match_add_match (cu_tail_match_t *obj, cu_match_t *match,
    int (*submit_match) (cu_match_t *match, void *user_data),
    void *user_data,
    void (*free_user_data) (void *user_data));

/*
 * NAME
 *  tail_match_add_match_simple
 *
 * DESCRIPTION
 *  A simplified version of `tail_match_add_match'. The regular expressen `regex'
 *  must match a number, which is then dispatched according to `ds_type'. See
 *  the `match_create_simple' function in utils_match.h for a description how
 *  this flag effects calculation of a new value.
 *  The values gathered are dispatched by the tail_match module in this case. The
 *  passed `plugin', `plugin_instance', `type', and `type_instance' are
 *  directly used when submitting these values.
 *  With excluderegex it is possible to exlude lines from the match.
 *
 * RETURN VALUE
 *   Zero upon success, non-zero otherwise.
 */
int tail_match_add_match_simple (cu_tail_match_t *obj,
    const char *regex, const char *excluderegex, int ds_type,
    const char *plugin, const char *plugin_instance,
    const char *type, const char *type_instance);

/*
 * NAME
 *   tail_match_read
 *
 * DESCRIPTION
 *   This function should be called periodically by plugins. It reads new lines
 *   from the logfile using `utils_tail' and tries to match them using all
 *   added `utils_match' objects.
 *   After all lines have been read and processed, the submit_match callback is
 *   called or, in case of tail_match_add_match_simple, the data is dispatched to
 *   the daemon directly.
 *
 * RETURN VALUE
 *   Zero on success, nonzero on failure.
*/
int tail_match_read (cu_tail_match_t *obj);

/* vim: set sw=2 sts=2 ts=8 : */
