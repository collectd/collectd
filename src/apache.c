/**
 * collectd - src/apache.c
 * Copyright (C) 2006  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#define MODULE_NAME "apache"

#if HAVE_LIBCURL && HAVE_CURL_CURL_H
#  define APACHE_HAVE_READ 1
#  include <curl/curl.h>
#else
#  define APACHE_HAVE_READ 0
#endif

static char *url  = NULL;
static char *user = NULL;
static char *pass = NULL;

#if APACHE_HAVE_READ
static CURL *curl = NULL;

static char apache_buffer[4096];
static int  apache_buffer_len = 0;
static char apache_curl_error[CURL_ERROR_SIZE];
#endif

static char *bytes_file = "apache/apache_bytes.rrd";
static char *bytes_ds_def[] =
{
	"DS:count:COUNTER:25:0:U",
	NULL
};
static int bytes_ds_num = 1;

static char *requests_file = "apache/apache_requests.rrd";
static char *requests_ds_def[] =
{
	"DS:count:COUNTER:25:0:U",
	NULL
};
static int requests_ds_num = 1;

static char *scoreboard_file = "apache/apache_scoreboard-%s.rrd";
static char *scoreboard_ds_def[] =
{
	"DS:count:GAUGE:25:0:U",
	NULL
};
static int scoreboard_ds_num = 1;

static char *config_keys[] =
{
	"URI",
	"User",
	"Password",
	NULL
};
static int config_keys_num = 3;


static size_t apache_curl_callback (void *buf, size_t size, size_t nmemb, void *stream)
{
	size_t len = size * nmemb;

	if ((apache_buffer_len + len) >= 4096)
	{
		len = 4095 - apache_buffer_len;
	}

	if (len <= 0)
		return (len);

	memcpy (apache_buffer + apache_buffer_len, (char *) buf, len);
	apache_buffer_len += len;
	apache_buffer[apache_buffer_len] = '\0';

	return (len);
}

static int config_set (char **var, char *value)
{
	if (*var != NULL)
	{
		free (*var);
		*var = NULL;
	}

	if ((*var = strdup (value)) == NULL)
		return (1);
	else
		return (0);
}

static int config (char *key, char *value)
{
	if (strcasecmp (key, "uri") == 0)
		return (config_set (&key, value));
	else if (strcasecmp (key, "user") == 0)
		return (config_set (&user, value));
	else if (strcasecmp (key, "password") == 0)
		return (config_set (&pass, value));
	else
		return (-1);
}

static void init (void)
{
#if APACHE_HAVE_READ
	static char credentials[1024];

	if (curl != NULL)
	{
		curl_easy_cleanup (curl);
	}

	if ((curl = curl_easy_init ()) == NULL)
	{
		syslog (LOG_ERR, "apache: `curl_easy_init' failed.");
		return;
	}

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, apache_curl_callback);
	curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
	curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, apache_curl_error);

	if (user != NULL)
	{
		if (snprintf (credentials, 1024, "%s:%s", user, pass == NULL ? "" : pass) >= 1024)
		{
			syslog (LOG_ERR, "apache: Credentials would have been truncated.");
			return;
		}

		curl_easy_setopt (curl, CURLOPT_USERPWD, credentials);
	}

	if (url != NULL)
	{
		curl_easy_setopt (curl, CURLOPT_URL, url);
	}
#endif /* APACHE_HAVE_READ */
}

static void bytes_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, bytes_file, val, bytes_ds_def, bytes_ds_num);
}

static void requests_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, requests_file, val, requests_ds_def, requests_ds_num);
}

static void scoreboard_write (char *host, char *inst, char *val)
{
	char buf[1024];

	if (snprintf (buf, 1024, scoreboard_file, inst) >= 1024)
		return;

	rrd_update_file (host, buf, val, scoreboard_ds_def, scoreboard_ds_num);
}

#if APACHE_HAVE_READ
static void submit (char *type, char *inst, long long value)
{
	char buf[1024];
	int  status;

	status = snprintf (buf, 1024, "%u:%lli", (unsigned int) curtime, value);
	if (status < 0)
	{
		syslog (LOG_ERR, "apache: bytes_submit: snprintf failed");
		return;
	}
	else if (status >= 1024)
	{
		syslog (LOG_WARNING, "apache: bytes_submit: snprintf was truncated");
		return;
	}

	plugin_submit (type, inst, buf);
}

static void submit_scoreboard (char *buf)
{
	/*
	 * Scoreboard Key:
	 * "_" Waiting for Connection, "S" Starting up, "R" Reading Request,
	 * "W" Sending Reply, "K" Keepalive (read), "D" DNS Lookup,
	 * "C" Closing connection, "L" Logging, "G" Gracefully finishing,
	 * "I" Idle cleanup of worker, "." Open slot with no current process
	 */
	long long open      = 0LL;
	long long waiting   = 0LL;
	long long starting  = 0LL;
	long long reading   = 0LL;
	long long sending   = 0LL;
	long long keepalive = 0LL;
	long long dnslookup = 0LL;
	long long closing   = 0LL;
	long long logging   = 0LL;
	long long finishing = 0LL;
	long long idle_cleanup = 0LL;

	int i;

	for (i = 0; buf[i] != '\0'; i++)
	{
		if (buf[i] == '.') open++;
		else if (buf[i] == '_') waiting++;
		else if (buf[i] == 'S') starting++;
		else if (buf[i] == 'R') reading++;
		else if (buf[i] == 'W') sending++;
		else if (buf[i] == 'K') keepalive++;
		else if (buf[i] == 'D') dnslookup++;
		else if (buf[i] == 'C') closing++;
		else if (buf[i] == 'L') logging++;
		else if (buf[i] == 'G') finishing++;
		else if (buf[i] == 'I') idle_cleanup++;
	}

	submit ("apache_scoreboard", "open"     , open);
	submit ("apache_scoreboard", "waiting"  , waiting);
	submit ("apache_scoreboard", "starting" , starting);
	submit ("apache_scoreboard", "reading"  , reading);
	submit ("apache_scoreboard", "sending"  , sending);
	submit ("apache_scoreboard", "keepalive", keepalive);
	submit ("apache_scoreboard", "dnslookup", dnslookup);
	submit ("apache_scoreboard", "closing"  , closing);
	submit ("apache_scoreboard", "logging"  , logging);
	submit ("apache_scoreboard", "finishing", finishing);
	submit ("apache_scoreboard", "idle_cleanup", idle_cleanup);
}

static void apache_read (void)
{
	int i;

	char *ptr;
	char *lines[16];
	int   lines_num = 0;

	char *fields[4];
	int   fields_num;

	if (curl == NULL)
		return;
	if (url == NULL)
		return;

	if (curl_easy_perform (curl) != 0)
	{
		syslog (LOG_WARNING, "apache: curl_easy_perform failed: %s", apache_curl_error);
		return;
	}

	ptr = apache_buffer;
	while ((lines[lines_num] = strtok (ptr, "\n\r")) != NULL)
	{
		ptr = NULL;
		lines_num++;

		if (lines_num >= 16)
			break;
	}

	for (i = 0; i < lines_num; i++)
	{
		fields_num = strsplit (lines[i], fields, 4);

		if (fields_num == 3)
		{
			if ((strcmp (fields[0], "Total") == 0)
					&& (strcmp (fields[1], "Accesses:") == 0))
				submit ("apache_requests", NULL, atoll (fields[2]));
			else if ((strcmp (fields[0], "Total") == 0)
					&& (strcmp (fields[1], "kBytes:") == 0))
				submit ("apache_bytes", NULL, 1024LL * atoll (fields[2]));
		}
		else if (fields_num == 2)
		{
			if (strcmp (fields[0], "Scoreboard:") == 0)
				submit_scoreboard (fields[1]);
		}
	}

	apache_buffer_len = 0;
}
#else
#  define apache_read NULL
#endif /* APACHE_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, init, apache_read, NULL);
	plugin_register ("apache_requests",   NULL, NULL, requests_write);
	plugin_register ("apache_bytes",      NULL, NULL, bytes_write);
	plugin_register ("apache_scoreboard", NULL, NULL, scoreboard_write);
	cf_register (MODULE_NAME, config, config_keys, config_keys_num);
}

#undef MODULE_NAME
