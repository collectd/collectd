/**
 * collectd - src/tokyotyrant.c
 * Copyright (C) 2009 Paul Sadauskas
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
 *   Paul Sadauskas <psadauskas@gmail.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "utils_cache.h"
#include "utils_parse_option.h"
#include <tcrdb.h>

static const char *config_keys[] =
{
	"Host",
	"Port"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char *host = NULL;

/* int is for opening connection, string is for plugin_instance */
static char *port_str = NULL;
static int   port;

static int tt_config (const char *key, const char *value)
{
	if (strcasecmp ("Host", key) == 0)
	{
		if (host != NULL)
			free (host);
		host = strdup(value);
	}
	else if (strcasecmp ("Port", key) == 0)
	{
		if (port_str != NULL)
			free (port_str);
		port_str = strdup(value);

		port = atoi(value);

		if ((port < 0) || (port > 65535))
		{
			ERROR ("tokyotyrant plugin: error: Port %s out of range", value);
			return (-1);
		}
	}
	else
	{
		ERROR ("tokyotyrant plugin: error: unrecognized configuration key %s", key);
		return (-1);
	}

	return (0);
}

static void printerr(TCRDB *rdb)
{
	int ecode = tcrdbecode(rdb);
	ERROR ("tokyotyrant plugin: error: %d, %s",
			ecode, tcrdberrmsg(ecode));
}

static void tt_submit (gauge_t val, const char* type)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = val;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);

	sstrncpy (vl.host, host, sizeof (vl.host));
	sstrncpy (vl.plugin, "tokyotyrant", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, port_str,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static int tt_read (void) {
	gauge_t rnum, size;

	TCRDB *rdb = tcrdbnew();

	if (!tcrdbopen(rdb, host, port))
	{
		printerr (rdb);
		tcrdbdel (rdb);
		return (1);
	}

	rnum = tcrdbrnum(rdb);
	size = tcrdbsize(rdb);
	tt_submit (rnum, "records");
	tt_submit (size, "file_size");

	if (!tcrdbclose(rdb))
	{
		printerr (rdb);
		tcrdbdel (rdb);
		return (1);
	}

	return (0);
}

void module_register (void)
{
	plugin_register_config("tokyotyrant", tt_config,
			config_keys, config_keys_num);
	plugin_register_read("tokyotyrant", tt_read);
}

/* vim: set sw=8 ts=8 tw=78 : */
