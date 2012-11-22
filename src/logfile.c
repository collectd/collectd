/**
 * collectd - src/logfile.c
 * Copyright (C) 2007  Sebastian Harl
 * Copyright (C) 2007,2008  Florian Forster
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
 *   Florian Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <pthread.h>

#define DEFAULT_LOGFILE LOCALSTATEDIR"/log/collectd.log"

#if COLLECT_DEBUG
static int log_level = LOG_DEBUG;
#else
static int log_level = LOG_INFO;
#endif /* COLLECT_DEBUG */

static pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

static char *log_file = NULL;
static int print_timestamp = 1;
static int print_severity = 0;

static const char *config_keys[] =
{
	"LogLevel",
	"File",
	"Timestamp",
	"PrintSeverity"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int logfile_config (const char *key, const char *value)
{
	if (0 == strcasecmp (key, "LogLevel")) {
		log_level = parse_log_severity(value);
		if (log_level == -1) return 1; /* to keep previous behaviour */
	}
	else if (0 == strcasecmp (key, "File")) {
		sfree (log_file);
		log_file = strdup (value);
	}
	else if (0 == strcasecmp (key, "Timestamp")) {
		if (IS_FALSE (value))
			print_timestamp = 0;
		else
			print_timestamp = 1;
	} else if (0 == strcasecmp(key, "PrintSeverity")) {
		if (IS_FALSE (value))
			print_severity = 0;
		else
			print_severity = 1;
	}
	else {
		return -1;
	}
	return 0;
} /* int logfile_config (const char *, const char *) */

static void logfile_print (const char *msg, int severity,
	   	cdtime_t timestamp_time)
{
	FILE *fh;
	int do_close = 0;
	struct tm timestamp_tm;
	char timestamp_str[64];
	char level_str[16] = "";

	if (print_severity)
	{
		switch (severity)
		{
		case LOG_ERR:
			snprintf(level_str, sizeof (level_str), "[error] ");
			break;	
		case LOG_WARNING:
			snprintf(level_str, sizeof (level_str), "[warning] ");
			break;
		case LOG_NOTICE:
			snprintf(level_str, sizeof (level_str), "[notice] ");
			break;	
		case LOG_INFO:
			snprintf(level_str, sizeof (level_str), "[info] ");
			break;	
		case LOG_DEBUG:
			snprintf(level_str, sizeof (level_str), "[debug] ");
			break;	
		default:
			break;
		}
	}

	if (print_timestamp)
	{
		time_t tt = CDTIME_T_TO_TIME_T (timestamp_time);
		localtime_r (&tt, &timestamp_tm);

		strftime (timestamp_str, sizeof (timestamp_str), "%Y-%m-%d %H:%M:%S",
				&timestamp_tm);
		timestamp_str[sizeof (timestamp_str) - 1] = '\0';
	}

	pthread_mutex_lock (&file_lock);

	if (log_file == NULL)
	{
		fh = fopen (DEFAULT_LOGFILE, "a");
		do_close = 1;
	}
	else if (strcasecmp (log_file, "stderr") == 0)
		fh = stderr;
	else if (strcasecmp (log_file, "stdout") == 0)
		fh = stdout;
	else
	{
		fh = fopen (log_file, "a");
		do_close = 1;
	}

	if (fh == NULL)
	{
			char errbuf[1024];
			fprintf (stderr, "logfile plugin: fopen (%s) failed: %s\n",
					(log_file == NULL) ? DEFAULT_LOGFILE : log_file,
					sstrerror (errno, errbuf, sizeof (errbuf)));
	}
	else
	{
		if (print_timestamp)
			fprintf (fh, "[%s] %s%s\n", timestamp_str, level_str, msg);
		else
			fprintf (fh, "%s%s\n", level_str, msg);

		if (do_close != 0)
			fclose (fh);
	}

	pthread_mutex_unlock (&file_lock);

	return;
} /* void logfile_print */

static void logfile_log (int severity, const char *msg,
		user_data_t __attribute__((unused)) *user_data)
{
	if (severity > log_level)
		return;

	logfile_print (msg, severity, cdtime ());
} /* void logfile_log (int, const char *) */

static int logfile_notification (const notification_t *n,
		user_data_t __attribute__((unused)) *user_data)
{
	char  buf[1024] = "";
	char *buf_ptr = buf;
	int   buf_len = sizeof (buf);
	int status;

	status = ssnprintf (buf_ptr, buf_len, "Notification: severity = %s",
			(n->severity == NOTIF_FAILURE) ? "FAILURE"
			: ((n->severity == NOTIF_WARNING) ? "WARNING"
				: ((n->severity == NOTIF_OKAY) ? "OKAY" : "UNKNOWN")));
	if (status > 0)
	{
		buf_ptr += status;
		buf_len -= status;
	}

#define APPEND(bufptr, buflen, key, value) \
	if ((buflen > 0) && (strlen (value) > 0)) { \
		int status = ssnprintf (bufptr, buflen, ", %s = %s", key, value); \
		if (status > 0) { \
			bufptr += status; \
			buflen -= status; \
		} \
	}
	APPEND (buf_ptr, buf_len, "host", n->host);
	APPEND (buf_ptr, buf_len, "plugin", n->plugin);
	APPEND (buf_ptr, buf_len, "plugin_instance", n->plugin_instance);
	APPEND (buf_ptr, buf_len, "type", n->type);
	APPEND (buf_ptr, buf_len, "type_instance", n->type_instance);
	APPEND (buf_ptr, buf_len, "message", n->message);

	buf[sizeof (buf) - 1] = '\0';

	logfile_print (buf, LOG_INFO,
			(n->time != 0) ? n->time : cdtime ());

	return (0);
} /* int logfile_notification */

void module_register (void)
{
	plugin_register_config ("logfile", logfile_config,
			config_keys, config_keys_num);
	plugin_register_log ("logfile", logfile_log, /* user_data = */ NULL);
	plugin_register_notification ("logfile", logfile_notification,
			/* user_data = */ NULL);
} /* void module_register (void) */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

