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
 *   Florian Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <pthread.h>

#if COLLECT_DEBUG
static int log_level = LOG_DEBUG;
#else
static int log_level = LOG_INFO;
#endif /* COLLECT_DEBUG */

static pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

static char *log_file = NULL;

static const char *config_keys[] =
{
	"LogLevel",
	"File"
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
	else if (0 == strcasecmp (key, "File")) {
		sfree (log_file);

		if (access (value, W_OK) == 0)
			log_file = strdup (value);
		else {
			char errbuf[1024];
			/* We can't use `ERROR' yet.. */
			fprintf (stderr, "stderr plugin: Access to %s denied: %s\n",
					value, sstrerror (errno, errbuf, sizeof (errbuf)));
			return 1;
		}
	}
	else {
		return -1;
	}
	return 0;
} /* int stderr_config (const char *, const char *) */

static void stderr_log (int severity, const char *msg)
{
	FILE *fh;
	int do_close = 0;

	if (severity > log_level)
		return;

	pthread_mutex_lock (&file_lock);

	if ((log_file == NULL) || (strcasecmp (log_file, "stderr") == 0))
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
			fprintf (stderr, "stderr plugin: fopen (%s) failed: %s\n",
					(log_file == NULL) ? "<null>" : log_file,
					sstrerror (errno, errbuf, sizeof (errbuf)));
	}
	else
	{
		fprintf (fh, "%s\n", msg);
		if (do_close != 0)
			fclose (fh);
	}

	pthread_mutex_unlock (&file_lock);

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

