/**
 * collectd - src/apache.c
 * Copyright (C) 2006  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_debug.h"

#if HAVE_LIBCURL && HAVE_CURL_CURL_H
#  define APACHE_HAVE_READ 1
#  include <curl/curl.h>
#else
#  define APACHE_HAVE_READ 0
#endif

/* Limit to 2^27 bytes/s. That's what a gigabit-ethernet link can handle, in
 * theory. */
static data_source_t apache_bytes_dsrc[1] =
{
	{"count", DS_TYPE_COUNTER, 0, 134217728.0},
};

static data_set_t apache_bytes_ds =
{
	"apache_bytes", 1, apache_bytes_dsrc
};

/* Limit to 2^20 requests/s */
static data_source_t apache_requests_dsrc[1] =
{
	{"count", DS_TYPE_COUNTER, 0, 134217728.0},
};

static data_set_t apache_requests_ds =
{
	"apache_requests", 1, apache_requests_dsrc
};

static data_source_t apache_scoreboard_dsrc[1] =
{
	{"count", DS_TYPE_GAUGE, 0, 65535.0},
};

static data_set_t apache_scoreboard_ds =
{
	"apache_scoreboard", 1, apache_scoreboard_dsrc
};

#if APACHE_HAVE_READ
static char *url    = NULL;
static char *user   = NULL;
static char *pass   = NULL;
static char *cacert = NULL;

static CURL *curl = NULL;

#define ABUFFER_SIZE 16384
static char apache_buffer[ABUFFER_SIZE];
static int  apache_buffer_len = 0;
static char apache_curl_error[CURL_ERROR_SIZE];

static const char *config_keys[] =
{
	"URL",
	"User",
	"Password",
	"CACert",
	NULL
};
static int config_keys_num = 4;

static size_t apache_curl_callback (void *buf, size_t size, size_t nmemb, void *stream)
{
	size_t len = size * nmemb;

	if ((apache_buffer_len + len) >= ABUFFER_SIZE)
	{
		len = (ABUFFER_SIZE - 1) - apache_buffer_len;
	}

	if (len <= 0)
		return (len);

	memcpy (apache_buffer + apache_buffer_len, (char *) buf, len);
	apache_buffer_len += len;
	apache_buffer[apache_buffer_len] = '\0';

	return (len);
}

static int config_set (char **var, const char *value)
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

static int config (const char *key, const char *value)
{
	if (strcasecmp (key, "url") == 0)
		return (config_set (&url, value));
	else if (strcasecmp (key, "user") == 0)
		return (config_set (&user, value));
	else if (strcasecmp (key, "password") == 0)
		return (config_set (&pass, value));
	else if (strcasecmp (key, "cacert") == 0)
		return (config_set (&cacert, value));
	else
		return (-1);
}

static int init (void)
{
	static char credentials[1024];

	if (curl != NULL)
	{
		curl_easy_cleanup (curl);
	}

	if ((curl = curl_easy_init ()) == NULL)
	{
		syslog (LOG_ERR, "apache: `curl_easy_init' failed.");
		return (-1);
	}

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, apache_curl_callback);
	curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
	curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, apache_curl_error);

	if (user != NULL)
	{
		if (snprintf (credentials, 1024, "%s:%s", user, pass == NULL ? "" : pass) >= 1024)
		{
			syslog (LOG_ERR, "apache: Credentials would have been truncated.");
			return (-1);
		}

		curl_easy_setopt (curl, CURLOPT_USERPWD, credentials);
	}

	if (url != NULL)
	{
		curl_easy_setopt (curl, CURLOPT_URL, url);
	}

	if (cacert != NULL)
	{
		curl_easy_setopt (curl, CURLOPT_CAINFO, cacert);
	}

	return (0);
} /* int init */

static void submit_counter (const char *type, const char *type_instance,
		unsigned long long value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	DBG ("type = %s; type_instance = %s; value = %llu;",
			type, type_instance, value);

	values[0].counter = value;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);
	strcpy (vl.host, hostname);
	strcpy (vl.plugin, "apache");
	strcpy (vl.plugin_instance, "");
	strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (type, &vl);
} /* void submit_counter */

static void submit_gauge (const char *type, const char *type_instance,
		double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	DBG ("type = %s; type_instance = %s; value = %lf;",
			type, type_instance, value);

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);
	strcpy (vl.host, hostname);
	strcpy (vl.plugin, "apache");
	strcpy (vl.plugin_instance, "");
	strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (type, &vl);
} /* void submit_counter */

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

	submit_gauge ("apache_scoreboard", "open"     , open);
	submit_gauge ("apache_scoreboard", "waiting"  , waiting);
	submit_gauge ("apache_scoreboard", "starting" , starting);
	submit_gauge ("apache_scoreboard", "reading"  , reading);
	submit_gauge ("apache_scoreboard", "sending"  , sending);
	submit_gauge ("apache_scoreboard", "keepalive", keepalive);
	submit_gauge ("apache_scoreboard", "dnslookup", dnslookup);
	submit_gauge ("apache_scoreboard", "closing"  , closing);
	submit_gauge ("apache_scoreboard", "logging"  , logging);
	submit_gauge ("apache_scoreboard", "finishing", finishing);
	submit_gauge ("apache_scoreboard", "idle_cleanup", idle_cleanup);
}

static int apache_read (void)
{
	int i;

	char *ptr;
	char *lines[16];
	int   lines_num = 0;

	char *fields[4];
	int   fields_num;

	if (curl == NULL)
		return (-1);
	if (url == NULL)
		return (-1);

	apache_buffer_len = 0;
	if (curl_easy_perform (curl) != 0)
	{
		syslog (LOG_ERR, "apache: curl_easy_perform failed: %s",
				apache_curl_error);
		return (-1);
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
				submit_counter ("apache_requests", "",
						atoll (fields[2]));
			else if ((strcmp (fields[0], "Total") == 0)
					&& (strcmp (fields[1], "kBytes:") == 0))
				submit_counter ("apache_bytes", "",
						1024LL * atoll (fields[2]));
		}
		else if (fields_num == 2)
		{
			if (strcmp (fields[0], "Scoreboard:") == 0)
				submit_scoreboard (fields[1]);
		}
	}

	apache_buffer_len = 0;

	return (0);
} /* int apache_read */
#endif /* APACHE_HAVE_READ */

void module_register (void)
{
	plugin_register_data_set (&apache_bytes_ds);
	plugin_register_data_set (&apache_requests_ds);
	plugin_register_data_set (&apache_scoreboard_ds);

#if APACHE_HAVE_READ
	plugin_register_config ("apache", config,
			config_keys, config_keys_num);
	plugin_register_init ("apache", init);
	plugin_register_read ("apache", apache_read);
#endif
}
