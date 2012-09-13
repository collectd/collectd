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

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 1978

static const char *config_keys[] =
{
	"Host",
	"Port"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char *config_host = NULL;
static char *config_port = NULL;

static TCRDB *rdb = NULL;

static int tt_config (const char *key, const char *value)
{
	if (strcasecmp ("Host", key) == 0)
	{
		char *temp;

		temp = strdup (value);
		if (temp == NULL)
		{
			ERROR("tokyotyrant plugin: Host strdup failed.");
			return (1);
		}
		sfree (config_host);
		config_host = temp;
	}
	else if (strcasecmp ("Port", key) == 0)
	{
		char *temp;

		temp = strdup (value);
		if (temp == NULL)
		{
			ERROR("tokyotyrant plugin: Port strdup failed.");
			return (1);
		}
		sfree (config_port);
		config_port = temp;
	}
	else
	{
		ERROR ("tokyotyrant plugin: error: unrecognized configuration key %s", key);
		return (-1);
	}

	return (0);
}

static void printerr()
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

	sstrncpy (vl.host, config_host, sizeof (vl.host));
	sstrncpy (vl.plugin, "tokyotyrant", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, config_port,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static void tt_open_db (void)
{
	char* host = NULL;
	int   port = DEFAULT_PORT;

	if (rdb != NULL)
		return;

	host = ((config_host != NULL) ? config_host : DEFAULT_HOST);

	if (config_port != NULL)
	{
		port = service_name_to_port_number (config_port);
		if (port <= 0)
			return;
	}

	rdb = tcrdbnew ();
	if (rdb == NULL)
		return;
	else if (!tcrdbopen(rdb, host, port))
	{
		printerr ();
		tcrdbdel (rdb);
		rdb = NULL;
	}
} /* void tt_open_db */

static int tt_read (void) {
	gauge_t rnum, size;

	tt_open_db ();
	if (rdb == NULL)
		return (-1);

	rnum = tcrdbrnum(rdb);
	tt_submit (rnum, "records");

	size = tcrdbsize(rdb);
	tt_submit (size, "file_size");

	return (0);
}

static int tt_shutdown(void)
{
	sfree(config_host);
	sfree(config_port);

	if (rdb != NULL)
	{
		if (!tcrdbclose(rdb))
		{
			printerr ();
			tcrdbdel (rdb);
			return (1);
		}
		tcrdbdel (rdb);
		rdb = NULL;
	}

	return(0);
}

void module_register (void)
{
	plugin_register_config("tokyotyrant", tt_config,
			config_keys, config_keys_num);
	plugin_register_read("tokyotyrant", tt_read);
	plugin_register_shutdown("tokyotyrant", tt_shutdown);
}

/* vim: set sw=8 ts=8 tw=78 : */
