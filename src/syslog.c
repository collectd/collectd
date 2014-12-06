/**
 * collectd - src/syslog.c
 * Copyright (C) 2007       Florian Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
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
		{
			log_level = LOG_INFO;
			ERROR ("syslog: invalid loglevel [%s] defaulting to 'info'", value);
			return (1);
		}
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
