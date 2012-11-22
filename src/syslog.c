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

#if COLLECT_DEBUG
static int log_level = LOG_DEBUG;
#else
static int log_level = LOG_INFO;
#endif /* COLLECT_DEBUG */
static int notif_severity = 0;

static const char *config_keys[] =
{
	"LogLevel",
	"NotifyLevel",
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int sl_config (const char *key, const char *value)
{
	if (strcasecmp (key, "LogLevel") == 0)
	{
		log_level = parse_log_severity (value);
		if (log_level < 0)
			return (1);
	}
	else if (strcasecmp (key, "NotifyLevel") == 0)
	{
		notif_severity = parse_notif_severity (value);
		if (notif_severity < 0)
			return (1);
	}

	return (0);
} /* int sl_config */

static void sl_log (int severity, const char *msg,
		user_data_t __attribute__((unused)) *user_data)
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

static int sl_notification (const notification_t *n,
		user_data_t __attribute__((unused)) *user_data)
{
	char  buf[1024] = "";
	size_t offset = 0;
	int log_severity;
	char *severity_string;
	int status;

	if (n->severity > notif_severity)
		return (0);

	switch (n->severity)
	{
		case NOTIF_FAILURE:
			severity_string = "FAILURE";
			log_severity = LOG_ERR;
			break;
		case NOTIF_WARNING:
			severity_string = "WARNING";
			log_severity = LOG_WARNING;
			break;
		case NOTIF_OKAY:
			severity_string = "OKAY";
			log_severity = LOG_NOTICE;
			break;
		default:
			severity_string = "UNKNOWN";
			log_severity = LOG_ERR;
	}

#define BUFFER_ADD(...) do { \
	status = ssnprintf (&buf[offset], sizeof (buf) - offset, \
			__VA_ARGS__); \
	if (status < 1) \
		return (-1); \
	else if (((size_t) status) >= (sizeof (buf) - offset)) \
		return (-ENOMEM); \
	else \
		offset += ((size_t) status); \
} while (0)

#define BUFFER_ADD_FIELD(field) do { \
	if (n->field[0]) \
		BUFFER_ADD (", " #field " = %s", n->field); \
} while (0)

	BUFFER_ADD ("Notification: severity = %s", severity_string);
	BUFFER_ADD_FIELD (host);
	BUFFER_ADD_FIELD (plugin);
	BUFFER_ADD_FIELD (plugin_instance);
	BUFFER_ADD_FIELD (type);
	BUFFER_ADD_FIELD (type_instance);
	BUFFER_ADD_FIELD (message);

#undef BUFFER_ADD_FIELD
#undef BUFFER_ADD

	buf[sizeof (buf) - 1] = '\0';

	sl_log (log_severity, buf, NULL);

	return (0);
} /* int sl_notification */

void module_register (void)
{
	openlog ("collectd", LOG_CONS | LOG_PID, LOG_DAEMON);

	plugin_register_config ("syslog", sl_config, config_keys, config_keys_num);
	plugin_register_log ("syslog", sl_log, /* user_data = */ NULL);
	plugin_register_notification ("syslog", sl_notification, NULL);
	plugin_register_shutdown ("syslog", sl_shutdown);
} /* void module_register(void) */
