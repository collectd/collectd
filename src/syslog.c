/**
 * collectd - src/syslog.c
 * Copyright (C) 2007  Florian Forster
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
 *   Florian Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_SYSLOG_H
# include <syslog.h>
#endif

static int log_level = LOG_DEBUG;

static const char *config_keys[] =
{
	"LogLevel"
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int sl_config (const char *key, const char *value)
{
	if (strcasecmp (key, "LogLevel") == 0)
	{
		if ((strcasecmp (value, "emerg") == 0)
				|| (strcasecmp (value, "alert") == 0)
				|| (strcasecmp (value, "crit") == 0)
				|| (strcasecmp (value, "err") == 0))
			log_level = LOG_ERR;
		else if (strcasecmp (value, "warning") == 0)
			log_level = LOG_WARNING;
		else if (strcasecmp (value, "notice") == 0)
			log_level = LOG_NOTICE;
		else if (strcasecmp (value, "info") == 0)
			log_level = LOG_INFO;
#if COLLECT_DEBUG
		else if (strcasecmp (value, "debug") == 0)
			log_level = LOG_DEBUG;
#endif
		else
			return (1);
	}
	else
		return (-1);

	return (0);
} /* int sl_config */

static int sl_init (void)
{
	openlog ("collectd", LOG_CONS | LOG_PID, LOG_DAEMON);

	return (0);
}

static void sl_log (int severity, const char *msg)
{
	if (severity > log_level)
		return;

	syslog (severity, "%s", msg);
} /* void sl_log */

static int sl_shutdown (void)
{
	closelog ();

	return (0);
}

void module_register (void)
{
	plugin_register_config ("syslog", sl_config, config_keys, config_keys_num);
	plugin_register_init ("syslog", sl_init);
	plugin_register_log ("syslog", sl_log);
	plugin_register_shutdown ("syslog", sl_shutdown);
} /* void module_register(void) */
