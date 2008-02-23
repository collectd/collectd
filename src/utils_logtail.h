/*
 * collectd - src/utils_logtail.h
 * Copyright (C) 2007-2008  C-Ware, Inc.
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
 * Author:
 *   Luke Heberling <lukeh at c-ware.com>
 *
 * Description:
 *   Encapsulates useful code to plugins which must parse a log file.
 *
 */

#include "utils_llist.h"
#include "utils_tail.h"

struct logtail_instance_s;
typedef struct logtail_instance_s logtail_instance_t;

/*
 * NAME
 *   logtail_term
 *
 * DESCRIPTION
 *   Call to release resources associated with the plugin.
 *
 * PARAMETERS
 *   `instances' The handle used to identify the plugin.
 *
 * RETURN VALUE
 *   Zero on success, nonzero on failure.
 */
int logtail_term (llist_t **instances);

/*
 * NAME
 *   logtail_init
 *
 * DESCRIPTION
 *   Call to initialize the plugin
 *
 * PARAMETERS
 *   `instances' The handle used to identify the plugin.
 *
 * RETURN VALUE
 *   Zero on success, nonzero on failure.
 */
int logtail_init (llist_t **instances);

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
int logtail_read (llist_t **instances, tailfunc *func, char *plugin,
		char **names);

/*
 * NAME
 *   logtail_config
 *
 * DESCRIPTION
 *   Configures the logtail instance for the given plugin.
 *
 * PARAMETERS
 *   `instances' The handle used to identify the plugin.
 *
 *   `ci'        The configuration item from collectd.
 *
 *   `plugin'    The name of the plugin.
 *
 *   `names'     An array of counter names in the same order as the
 *               counters themselves.
 *
 *   `default_file' The default log file if none is found in the
 *               configuration.
 *
 *   `default_cache_size' The default cache size if none is found in the
 *               configuration.
 *
 * RETURN VALUE
 *   Zero on success, nonzero on failure.
 */
int logtail_config (llist_t **instances, oconfig_item_t *ci, char *plugin,
			char **names, char *default_file,
			int default_cache_size);

/*
 * NAME
 *   logtail_counters
 *
 * DESCRIPTION
 *   Returns the counters maintained for the plugin.
 *
 * PARAMETERS
 *   `instance' The handle used to identify the logtail instance.
 *
 */
unsigned long *logtail_counters (logtail_instance_t *instance);

/*
 * NAME
 *   logtail_cache
 *
 * DESCRIPTION
 *   Stores the data in the cache.
 *
 * PARAMETERS
 *   `instance' The handle used to identify the logtail instance.
 *
 *   `plugin'   The name of the plugin.
 *
 *   `key'      The key to identify the cached data.
 *   
 *   `data'     The data to cache.
 *
 *   `len'      The length of the data to cache.
 *
 * RETURN VALUE
 *   Zero on success, nonzero on failure.
 */
int logtail_cache (logtail_instance_t *instance, char *plugin, char *key,
		void **data, int len);

/*
 * NAME
 *   logtail_decache
 *
 * DESCRIPTION
 *   Removes the data from the cache.
 *
 * PARAMETERS
 *   `instance' The handle used to identify the logtail instance.
 *
 *   `key'      The key to identify the cached data.
 *   
 */
void logtail_decache (logtail_instance_t *instance, char *key);

