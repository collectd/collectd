#ifndef CONFIGFILE_H
#define CONFIGFILE_H
/**
 * collectd - src/configfile.h
 * Copyright (C) 2005-2011  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "utils_time.h"
#include "liboconfig/oconfig.h"

/*
 * DESCRIPTION
 *  Remove a registered plugin from the internal data structures.
 * 
 * PARAMETERS
 *  `type'      Name of the plugin (must be the same as passed to
 *              `plugin_register'
 */
void cf_unregister (const char *type);
void cf_unregister_complex (const char *type);

/*
 * DESCRIPTION
 *  `cf_register' is called by plugins that wish to receive config keys. The
 *  plugin will then receive all keys it registered for if they're found in a
 *  `<Plugin $type>' section.
 *
 * PARAMETERS
 *  `type'      Name of the plugin (must be the same as passed to
 *              `plugin_register'
 *  `callback'  Pointer to the callback function. The callback must return zero
 *              upon success, a value smaller than zero if it doesn't know how
 *              to handle the `key' passed to it (the first argument) or a
 *              value greater than zero if it knows how to handle the key but
 *              failed.
 *  `keys'      Array of key values this plugin wished to receive. The last
 *              element must be a NULL-pointer.
 *  `keys_num'  Number of elements in the array (not counting the last NULL-
 *              pointer.
 *
 * NOTES
 *  `cf_unregister' will be called for `type' to make sure only one record
 *  exists for each `type' at any time. This means that `cf_register' may be
 *  called multiple times, but only the last call will have an effect.
 */
void cf_register (const char *type,
		int (*callback) (const char *, const char *),
		const char **keys, int keys_num);

int cf_register_complex (const char *type, int (*callback) (oconfig_item_t *));

/*
 * DESCRIPTION
 *  `cf_read' reads the config file `filename' and dispatches the read
 *  information to functions/variables. Most important: Is calls `plugin_load'
 *  to load specific plugins, depending on the current mode of operation.
 *
 * PARAMETERS
 *  `filename'  An additional filename to look for. This function calls
 *              `lc_process' which already searches many standard locations..
 *              If set to NULL will use the `CONFIGFILE' define.
 *
 * RETURN VALUE
 *  Returns zero upon success and non-zero otherwise. A error-message will have
 *  been printed in this case.
 */
int cf_read (char *filename);

int global_option_set (const char *option, const char *value);
const char *global_option_get (const char *option);

/* Assures the config option is a string, duplicates it and returns the copy in
 * "ret_string". If necessary "*ret_string" is freed first. Returns zero upon
 * success. */
int cf_util_get_string (const oconfig_item_t *ci, char **ret_string);

/* Assures the config option is a string and copies it to the provided buffer.
 * Assures null-termination. */
int cf_util_get_string_buffer (const oconfig_item_t *ci, char *buffer,
		size_t buffer_size);

/* Assures the config option is a number and returns it as an int. */
int cf_util_get_int (const oconfig_item_t *ci, int *ret_value);

/* Assures the config option is a boolean and assignes it to `ret_bool'.
 * Otherwise, `ret_bool' is not changed and non-zero is returned. */
int cf_util_get_boolean (const oconfig_item_t *ci, _Bool *ret_bool);

/* Assures the config option is a boolean and set or unset the given flag in
 * `ret_value' as appropriate. Returns non-zero on error. */
int cf_util_get_flag (const oconfig_item_t *ci,
		unsigned int *ret_value, unsigned int flag);

/* Assures that the config option is a string or a number if the correct range
 * of 1-65535. The string is then converted to a port number using
 * `service_name_to_port_number' and returned.
 * Returns the port number in the range [1-65535] or less than zero upon
 * failure. */
int cf_util_get_port_number (const oconfig_item_t *ci);

/* Assures that the config option is either a service name (a string) or a port
 * number (an integer in the appropriate range) and returns a newly allocated
 * string. If ret_string points to a non-NULL pointer, it is freed before
 * assigning a new value. */
int cf_util_get_service (const oconfig_item_t *ci, char **ret_string);

int cf_util_get_cdtime (const oconfig_item_t *ci, cdtime_t *ret_value);

#endif /* defined(CONFIGFILE_H) */
