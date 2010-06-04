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

#define USER_CONFIG_INIT {0, 0, 0, 0}
#define SET_MONITOR_FLAG(name, flag, value) if((strcasecmp(name, key) == 0) && IS_TRUE(value)) user_config.flag = 1

/* {{{ user_config_s */
struct user_config_s {
	int monitor_cache;
	int monitor_connections;
	int monitor_esi;
	int monitor_backend;
};

typedef struct user_config_s user_config_t; /* }}} */

/* {{{ Configuration directives */
static user_config_t user_config = USER_CONFIG_INIT;

static const char *config_keys[] =
{
  "MonitorCache",
  "MonitorConnections",
  "MonitorESI",
  "MonitorBackend",
};

static int config_keys_num = STATIC_ARRAY_SIZE (config_keys); /* }}} */

static int varnish_config(const char *key, const char *value) /* {{{ */
{
	SET_MONITOR_FLAG("MonitorCache", monitor_cache, value);
	SET_MONITOR_FLAG("MonitorConnections", monitor_connections, value);
	SET_MONITOR_FLAG("MonitorESI", monitor_esi, value);
	SET_MONITOR_FLAG("MonitorBackend", monitor_backend, value);

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
		varnish_submit("varnish_cache_ratio", "cache_hit"    , VSL_stats->cache_hit);     /* Cache hits          */
		varnish_submit("varnish_cache_ratio", "cache_miss"   , VSL_stats->cache_miss);    /* Cache misses        */
		varnish_submit("varnish_cache_ratio", "cache_hitpass", VSL_stats->cache_hitpass); /* Cache hits for pass */
	}

	if(user_config.monitor_connections == 1)
	{
		varnish_submit("varnish_connections", "client_connections-accepted", VSL_stats->client_conn); /* Client connections accepted */
		varnish_submit("varnish_connections", "client_connections-dropped" , VSL_stats->client_drop); /* Connection dropped, no sess */
		varnish_submit("varnish_connections", "client_connections-received", VSL_stats->client_req);  /* Client requests received    */
	}

	if(user_config.monitor_esi == 1)
	{
		varnish_submit("varnish_esi", "esi_parsed", VSL_stats->esi_parse);  /* Objects ESI parsed (unlock) */
		varnish_submit("varnish_esi", "esi_errors", VSL_stats->esi_errors); /* ESI parse errors (unlock)   */
	}

	if(user_config.monitor_backend == 1)
	{
		varnish_submit("varnish_backend_connections", "backend_connections-success"      , VSL_stats->backend_conn);      /* Backend conn. success       */
		varnish_submit("varnish_backend_connections", "backend_connections-not-attempted", VSL_stats->backend_unhealthy); /* Backend conn. not attempted */
		varnish_submit("varnish_backend_connections", "backend_connections-too-many"     , VSL_stats->backend_busy);      /* Backend conn. too many      */
		varnish_submit("varnish_backend_connections", "backend_connections-failures"     , VSL_stats->backend_fail);      /* Backend conn. failures      */
		varnish_submit("varnish_backend_connections", "backend_connections-reuses"       , VSL_stats->backend_reuse);     /* Backend conn. reuses        */
		varnish_submit("varnish_backend_connections", "backend_connections-was-closed"   , VSL_stats->backend_toolate);   /* Backend conn. was closed    */
		varnish_submit("varnish_backend_connections", "backend_connections-recycles"     , VSL_stats->backend_recycle);   /* Backend conn. recycles      */
		varnish_submit("varnish_backend_connections", "backend_connections-unused"       , VSL_stats->backend_unused);    /* Backend conn. unused        */
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
