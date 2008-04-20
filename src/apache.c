/**
 * collectd - src/apache.c
 * Copyright (C) 2006-2008  Florian octo Forster
 * Copyright (C) 2007  Florent EppO Monbillard
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
 *   Florent EppO Monbillard <eppo at darox.net>
 *   - connections/lighttpd extension
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <curl/curl.h>

static char *url         = NULL;
static char *user        = NULL;
static char *pass        = NULL;
static char *verify_peer = NULL;
static char *verify_host = NULL;
static char *cacert      = NULL;

static CURL *curl = NULL;

static char  *apache_buffer = NULL;
static size_t apache_buffer_size = 0;
static size_t apache_buffer_fill = 0;
static char   apache_curl_error[CURL_ERROR_SIZE];

static const char *config_keys[] =
{
	"URL",
	"User",
	"Password",
	"VerifyPeer",
	"VerifyHost",
	"CACert"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static size_t apache_curl_callback (void *buf, size_t size, size_t nmemb,
		void *stream)
{
	size_t len = size * nmemb;

	if (len <= 0)
		return (len);

	if ((apache_buffer_fill + len) >= apache_buffer_size)
	{
		char *temp;

		temp = (char *) realloc (apache_buffer,
				apache_buffer_fill + len + 1);
		if (temp == NULL)
		{
			ERROR ("apache plugin: realloc failed.");
			return (0);
		}
		apache_buffer = temp;
		apache_buffer_size = apache_buffer_fill + len + 1;
	}

	memcpy (apache_buffer + apache_buffer_fill, (char *) buf, len);
	apache_buffer_fill += len;
	apache_buffer[apache_buffer_fill] = 0;

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
	else if (strcasecmp (key, "verifypeer") == 0)
		return (config_set (&verify_peer, value));
	else if (strcasecmp (key, "verifyhost") == 0)
		return (config_set (&verify_host, value));
	else if (strcasecmp (key, "cacert") == 0)
		return (config_set (&cacert, value));
	else
		return (-1);
}

static int init (void)
{
	static char credentials[1024];

	if (url == NULL)
	{
		WARNING ("apache plugin: init: No URL configured, returning "
				"an error.");
		return (-1);
	}

	if (curl != NULL)
	{
		curl_easy_cleanup (curl);
	}

	if ((curl = curl_easy_init ()) == NULL)
	{
		ERROR ("apache plugin: init: `curl_easy_init' failed.");
		return (-1);
	}

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, apache_curl_callback);
	curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
	curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, apache_curl_error);

	if (user != NULL)
	{
		int status;

		status = ssnprintf (credentials, sizeof (credentials), "%s:%s",
				user, (pass == NULL) ? "" : pass);
		if (status >= sizeof (credentials))
		{
			ERROR ("apache plugin: init: Returning an error "
					"because the credentials have been "
					"truncated.");
			return (-1);
		}

		curl_easy_setopt (curl, CURLOPT_USERPWD, credentials);
	}

	curl_easy_setopt (curl, CURLOPT_URL, url);

	if ((verify_peer == NULL) || (strcmp (verify_peer, "true") == 0))
	{
		curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 1);
	}
	else
	{
		curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 0);
	}

	if ((verify_host == NULL) || (strcmp (verify_host, "true") == 0))
	{
		curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 2);
	}
	else
	{
		curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 0);
	}

	if (cacert != NULL)
	{
		curl_easy_setopt (curl, CURLOPT_CAINFO, cacert);
	}

	return (0);
} /* int init */

static void submit_counter (const char *type, const char *type_instance,
		counter_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = value;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "apache");
	strcpy (vl.plugin_instance, "");
	sstrncpy (vl.type, type, sizeof (vl.type));

	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance,
				sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void submit_counter */

static void submit_gauge (const char *type, const char *type_instance,
		gauge_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "apache");
	strcpy (vl.plugin_instance, "");
	sstrncpy (vl.type, type, sizeof (vl.type));

	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance,
				sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
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
	char *saveptr;
	char *lines[16];
	int   lines_num = 0;

	char *fields[4];
	int   fields_num;

	if (curl == NULL)
		return (-1);
	if (url == NULL)
		return (-1);

	apache_buffer_fill = 0;
	if (curl_easy_perform (curl) != 0)
	{
		ERROR ("apache: curl_easy_perform failed: %s",
				apache_curl_error);
		return (-1);
	}

	ptr = apache_buffer;
	saveptr = NULL;
	while ((lines[lines_num] = strtok_r (ptr, "\n\r", &saveptr)) != NULL)
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
			else if (strcmp (fields[0], "BusyServers:") == 0)
				submit_gauge ("apache_connections", NULL, atol (fields[1]));
		}
	}

	apache_buffer_fill = 0;

	return (0);
} /* int apache_read */

void module_register (void)
{
	plugin_register_config ("apache", config,
			config_keys, config_keys_num);
	plugin_register_init ("apache", init);
	plugin_register_read ("apache", apache_read);
} /* void module_register */
