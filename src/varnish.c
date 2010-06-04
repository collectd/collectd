/**
 * collectd - src/varnish.c
 * Copyright (C) 2010 Jerome Renard
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
 *   Jerome Renard <jerome.renard@gmail.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <varnish/varnishapi.h>

#define USER_CONFIG_INIT {0, 0, 0}
#define SET_MONITOR_FLAG(name, flag, value) if((strcasecmp(name, key) == 0) && IS_TRUE(value)) user_config.flag = 1

/* {{{ user_config_s */
struct user_config_s {
	int monitor_cache;
	int monitor_connections;
	int monitor_esi;
};

typedef struct user_config_s user_config_t; /* }}} */

/* {{{ Configuration directives */
static user_config_t user_config = USER_CONFIG_INIT;

static const char *config_keys[] =
{
  "MonitorCache",
  "MonitorConnections",
  "MonitorESI"
};

static int config_keys_num = STATIC_ARRAY_SIZE (config_keys); /* }}} */

static int varnish_config(const char *key, const char *value) /* {{{ */
{
	SET_MONITOR_FLAG("MonitorCache", monitor_cache, value);
	SET_MONITOR_FLAG("MonitorConnections", monitor_connections, value);
	SET_MONITOR_FLAG("MonitorESI", monitor_esi, value);

	return (0);
} /* }}} */

static void varnish_submit(const char *type, const char *type_instance, gauge_t value) /* {{{ */
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;
	vl.values_len = 1;
	vl.values = values;

	sstrncpy(vl.host         , hostname_g   , sizeof(vl.host));
	sstrncpy(vl.plugin       , "varnish"    , sizeof(vl.plugin));
	sstrncpy(vl.type         , type         , sizeof(vl.type));
	sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

	plugin_dispatch_values(&vl);
} /* }}} */

static void varnish_monitor(struct varnish_stats *VSL_stats) /* {{{ */
{
	if(user_config.monitor_cache == 1)
	{
		varnish_submit("varnish_cache_ratio", "cache_hit"    , VSL_stats->cache_hit);
		varnish_submit("varnish_cache_ratio", "cache_miss"   , VSL_stats->cache_miss);
		varnish_submit("varnish_cache_ratio", "cache_hitpass", VSL_stats->cache_hitpass);
	}

	if(user_config.monitor_connections == 1)
	{
		varnish_submit("varnish_connections", "client_connections-accepted", VSL_stats->client_conn);
		varnish_submit("varnish_connections", "client_connections-dropped" , VSL_stats->client_drop);
		varnish_submit("varnish_connections", "client_connections-received", VSL_stats->client_req);
	}

	if(user_config.monitor_esi == 1)
	{
		varnish_submit("varnish_esi", "esi_parsed", VSL_stats->esi_parse);
		varnish_submit("varnish_esi", "esi_errors", VSL_stats->esi_errors);
	}
} /* }}} */

static int varnish_read(void) /* {{{ */
{
	struct varnish_stats *VSL_stats;
	const char *varnish_instance_name = NULL;

	if ((VSL_stats = VSL_OpenStats(varnish_instance_name)) == NULL)
	{
		ERROR("Varnish plugin : unable to load statistics");

		return (-1);
	}

	varnish_monitor(VSL_stats);

    return (0);
} /* }}} */

void module_register (void) /* {{{ */
{
	plugin_register_config("varnish", varnish_config, config_keys, config_keys_num);
	plugin_register_read("varnish", varnish_read);
} /* }}} */

/* vim: set sw=8 noet fdm=marker : */
