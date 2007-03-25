/**
 * collectd - src/stderr.c
 * Copyright (C) 2007  Sebastian Harl
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
 *   Sebastian Harl <sh at tokkee.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if COLLECT_DEBUG
static int log_level = LOG_DEBUG;
#else
static int log_level = LOG_INFO;
#endif /* COLLECT_DEBUG */

static const char *config_keys[] =
{
	"LogLevel"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int stderr_config (const char *key, const char *value)
{
	if (0 == strcasecmp (key, "LogLevel")) {
		if ((0 == strcasecmp (value, "emerg"))
				|| (0 == strcasecmp (value, "alert"))
				|| (0 == strcasecmp (value, "crit"))
				|| (0 == strcasecmp (value, "err")))
			log_level = LOG_ERR;
		else if (0 == strcasecmp (value, "warning"))
			log_level = LOG_WARNING;
		else if (0 == strcasecmp (value, "notice"))
			log_level = LOG_NOTICE;
		else if (0 == strcasecmp (value, "info"))
			log_level = LOG_INFO;
#if COLLECT_DEBUG
		else if (0 == strcasecmp (value, "debug"))
			log_level = LOG_DEBUG;
#endif /* COLLECT_DEBUG */
		else
			return 1;
	}
	else {
		return -1;
	}
	return 0;
} /* int stderr_config (const char *, const char *) */

static void stderr_log (int severity, const char *msg)
{
	if (severity > log_level)
		return;

	fprintf (stderr, "%s\n", msg);
	return;
} /* void stderr_log (int, const char *) */

void module_register (void)
{
	plugin_register_config ("stderr", stderr_config,
			config_keys, config_keys_num);
	plugin_register_log ("stderr", stderr_log);
	return;
} /* void module_register (void) */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

