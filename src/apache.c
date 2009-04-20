/**
 * collectd - src/apache.c
 * Copyright (C) 2006-2008  Florian octo Forster
 * Copyright (C) 2007  Florent EppO Monbillard
 * Copyright (C) 2009  Amit Gupta
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
 *   Amit Gupta <amit.gupta221 at gmail.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <curl/curl.h>

enum server_type
{
	APACHE = 0,
	LIGHTTPD
};

struct apache_s
{
	char *name;
	char *host;
	char *url;
	char *user;
	char *pass;
	char *verify_peer;
	char *verify_host;
	char *cacert;
	char *apache_buffer;
	char apache_curl_error[CURL_ERROR_SIZE];
	size_t apache_buffer_size;
	size_t apache_buffer_fill;
	CURL *curl;
}; /* apache_s */

typedef struct apache_s apache_t;

static apache_t **apache     = NULL;
static size_t     apache_num = 0;

static void apache_free (apache_t *st)
{
	if (st == NULL)
		return;

	sfree (st->name);
	sfree (st->host);
	sfree (st->url);
	sfree (st->user);
	sfree (st->pass);
	sfree (st->verify_peer);
	sfree (st->verify_host);
	sfree (st->cacert);
	sfree (st->apache_buffer);
	if (st->curl) {
		curl_easy_cleanup(st->curl);
		st->curl = NULL;
	}
} /* apache_free */

static size_t apache_curl_callback (void *buf, size_t size, size_t nmemb,
		apache_t *st)
{
	size_t len = size * nmemb;

	if (len <= 0)
		return (len);

	if ((st->apache_buffer_fill + len) >= st->apache_buffer_size)
	{
		char *temp;

		temp = (char *) realloc (st->apache_buffer,
				st->apache_buffer_fill + len + 1);
		if (temp == NULL)
		{
			ERROR ("apache plugin: realloc failed.");
			return (0);
		}
		st->apache_buffer = temp;
		st->apache_buffer_size = st->apache_buffer_fill + len + 1;
	}

	memcpy (st->apache_buffer + st->apache_buffer_fill, (char *) buf, len);
	st->apache_buffer_fill += len;
	st->apache_buffer[st->apache_buffer_fill] = 0;

	return (len);
} /* int apache_curl_callback */

/* Configuration handling functiions
 * <Plugin apache>
 *   <Instance "instance_name">
 *     URL ...
 *   </Instance>
 *   URL ...
 * </Plugin>
 */
static int config_set_string (char **ret_string,
				    oconfig_item_t *ci)
{
	char *string;

	if ((ci->values_num != 1)
			|| (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		WARNING ("apache plugin: The `%s' config option "
				"needs exactly one string argument.", ci->key);
		return (-1);
	}

	string = strdup (ci->values[0].value.string);
	if (string == NULL)
	{
		ERROR ("apache plugin: strdup failed.");
		return (-1);
	}

	if (*ret_string != NULL)
		free (*ret_string);
	*ret_string = string;

	return (0);
} /* int config_set_string */

static int config_add (oconfig_item_t *ci)
{
	apache_t *st;
	int i;
	int status;

	if ((ci->values_num != 1)
		|| (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		WARNING ("apache plugin: The `%s' config option "
			"needs exactly one string argument.", ci->key);
		return (-1);
	}

	st = (apache_t *) malloc (sizeof (*st));
	if (st == NULL)
	{
		ERROR ("apache plugin: malloc failed.");
		return (-1);
	}

	memset (st, 0, sizeof (*st));

	status = config_set_string (&st->name, ci);
	if (status != 0)
	{
		sfree (st);
		return (status);
	}

	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("URL", child->key) == 0)
			status = config_set_string (&st->url, child);
		else if (strcasecmp ("Host", child->key) == 0)
			status = config_set_string (&st->host, child);
		else if (strcasecmp ("User", child->key) == 0)
			status = config_set_string (&st->user, child);
		else if (strcasecmp ("Password", child->key) == 0)
			status = config_set_string (&st->pass, child);
		else if (strcasecmp ("VerifyPeer", child->key) == 0)
			status = config_set_string (&st->verify_peer, child);
		else if (strcasecmp ("VerifyHost", child->key) == 0)
			status = config_set_string (&st->verify_host, child);
		else if (strcasecmp ("CACert", child->key) == 0)
			status = config_set_string (&st->cacert, child);
		else
		{
			WARNING ("apache plugin: Option `%s' not allowed here.", child->key);
			status = -1;
		}

		if (status != 0)
			break;
	}

	if (status == 0)
	{
		apache_t **temp;
		temp = (apache_t **) realloc (apache, sizeof (*apache) * (apache_num + 1));
		if (temp == NULL)
		{
			ERROR ("apache plugin: realloc failed");
			status = -1;
		}
		else
		{
			apache = temp;
			apache[apache_num] = st;
			apache_num++;
		}
	}

	if (status != 0)
	{
		apache_free(st);
		return (-1);
	}

	return (0);
} /* int config_add */

static int config (oconfig_item_t *ci)
{
	int status = 0;
	int i;
	oconfig_item_t *lci = NULL; /* legacy config */

	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("Instance", child->key) == 0 && child->children_num > 0)
			config_add (child);
		else
		{
			/* legacy mode - convert to <Instance ...> config */
			if (lci == NULL)
			{
				lci = malloc (sizeof(*lci));
				if (lci == NULL)
				{
					ERROR ("apache plugin: malloc failed.");
					return (-1);
				}
				memset (lci, '\0', sizeof (*lci));
			}

			lci->children_num++;
			lci->children =
				realloc (lci->children,
					 lci->children_num * sizeof (*child));
			if (lci->children == NULL)
			{
				ERROR ("apache plugin: realloc failed.");
				return (-1);
			}
			memcpy (&lci->children[lci->children_num-1], child, sizeof (*child));
		}
	} /* for (ci->children) */

	if (lci)
	{
		/* create a <Instance ""> entry */
		lci->key = "Instance";
		lci->values_num = 1;
		lci->values = (oconfig_value_t *) malloc (lci->values_num * sizeof (oconfig_value_t));
		lci->values[0].type = OCONFIG_TYPE_STRING;
		lci->values[0].value.string = "";

		status = config_add (lci);
		sfree (lci->values);
		sfree (lci->children);
		sfree (lci);
	}

	return status;
} /* int config */


/* initialize curl for each host */
static int init_host (apache_t *st) /* {{{ */
{
	static char credentials[1024];

	if (st->url == NULL)
	{
		WARNING ("apache plugin: init: No URL configured, returning "
				"an error.");
		return (-1);
	}

	if (st->curl != NULL)
	{
		curl_easy_cleanup (st->curl);
	}

	if ((st->curl = curl_easy_init ()) == NULL)
	{
		ERROR ("apache plugin: init: `curl_easy_init' failed.");
		return (-1);
	}

	curl_easy_setopt (st->curl, CURLOPT_WRITEFUNCTION, apache_curl_callback);
	curl_easy_setopt (st->curl, CURLOPT_WRITEDATA, st);
	curl_easy_setopt (st->curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
	curl_easy_setopt (st->curl, CURLOPT_ERRORBUFFER, st->apache_curl_error);

	if (st->user != NULL)
	{
		int status;

		status = ssnprintf (credentials, sizeof (credentials), "%s:%s",
				st->user, (st->pass == NULL) ? "" : st->pass);
		if ((status < 0) || ((size_t) status >= sizeof (credentials)))
		{
			ERROR ("apache plugin: init: Returning an error "
					"because the credentials have been "
					"truncated.");
			return (-1);
		}

		curl_easy_setopt (st->curl, CURLOPT_USERPWD, credentials);
	}

	curl_easy_setopt (st->curl, CURLOPT_URL, st->url);

	if ((st->verify_peer == NULL) || (strcmp (st->verify_peer, "true") == 0))
	{
		curl_easy_setopt (st->curl, CURLOPT_SSL_VERIFYPEER, 1);
	}
	else
	{
		curl_easy_setopt (st->curl, CURLOPT_SSL_VERIFYPEER, 0);
	}

	if ((st->verify_host == NULL) || (strcmp (st->verify_host, "true") == 0))
	{
		curl_easy_setopt (st->curl, CURLOPT_SSL_VERIFYHOST, 2);
	}
	else
	{
		curl_easy_setopt (st->curl, CURLOPT_SSL_VERIFYHOST, 0);
	}

	if (st->cacert != NULL)
	{
		curl_easy_setopt (st->curl, CURLOPT_CAINFO, st->cacert);
	}

	return (0);
} /* int init_host */

static int init (void)
{
	size_t i;
	int success = 0;
	int status;

	for (i = 0; i < apache_num; i++)
	{
		status = init_host (apache[i]);
		if (status == 0)
			success++;
	}

	if (success == 0)
	{
		ERROR ("apache plugin init: No host could be initialized. Will return an error so "
			"the plugin will be delayed.");
		return (-1);
	}

	return (0);
} /* int init */

static void set_plugin_instance (apache_t *st, value_list_t *vl)
{
	/* if there is no instance name, don't set plugin_instance */
	if ( (st->name != NULL)
		&& (apache_num > 0) )
	{
		sstrncpy (vl->plugin_instance, st->name, sizeof (vl->plugin_instance));
	}
} /* void set_plugin */

static void submit_counter (const char *type, const char *type_instance,
		counter_t value, apache_t *st)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, st->host, sizeof (vl.host));
	sstrncpy (vl.plugin, "apache", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, "", sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance,
				sizeof (vl.type_instance));

	set_plugin_instance (st, &vl);

	plugin_dispatch_values (&vl);
} /* void submit_counter */

static void submit_gauge (const char *type, const char *type_instance,
		gauge_t value, apache_t *st)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, st->host, sizeof (vl.host));
	sstrncpy (vl.plugin, "apache", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, "", sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance,
				sizeof (vl.type_instance));

	set_plugin_instance (st, &vl);

	plugin_dispatch_values (&vl);
} /* void submit_counter */

static void submit_scoreboard (char *buf, int server, apache_t *st)
{
	/*
	 * Scoreboard Key:
	 * "_" Waiting for Connection, "S" Starting up,
	 * "R" Reading Request for apache and read-POST for lighttpd,
	 * "W" Sending Reply, "K" Keepalive (read), "D" DNS Lookup,
	 * "C" Closing connection, "L" Logging, "G" Gracefully finishing,
	 * "I" Idle cleanup of worker, "." Open slot with no current process
	 * Lighttpd specific legends -
	 * "E" hard error, "." connect, "h" handle-request,
	 * "q" request-start, "Q" request-end, "s" response-start
	 * "S" response-end, "r" read
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

	// lighttpd specific
	long long connect        = 0LL;
	long long hard_error     = 0LL;
	long long lighttpd_read  = 0LL;
	long long handle_request = 0LL;
	long long request_start  = 0LL;
	long long request_end    = 0LL;
	long long response_start = 0LL;
	long long response_end   = 0LL;

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
		else if (buf[i] == '.') connect++;
		else if (buf[i] == 'r') lighttpd_read++;
		else if (buf[i] == 'h') handle_request++;
		else if (buf[i] == 'E') hard_error++;
		else if (buf[i] == 'q') request_start++;
		else if (buf[i] == 'Q') request_end++;
		else if (buf[i] == 's') response_start++;
		else if (buf[i] == 'S') response_end++;
	}

	if (server == APACHE)
	{
		submit_gauge ("apache_scoreboard", "open"     , open, st);
		submit_gauge ("apache_scoreboard", "waiting"  , waiting, st);
		submit_gauge ("apache_scoreboard", "starting" , starting, st);
		submit_gauge ("apache_scoreboard", "reading"  , reading, st);
		submit_gauge ("apache_scoreboard", "sending"  , sending, st);
		submit_gauge ("apache_scoreboard", "keepalive", keepalive, st);
		submit_gauge ("apache_scoreboard", "dnslookup", dnslookup, st);
		submit_gauge ("apache_scoreboard", "closing"  , closing, st);
		submit_gauge ("apache_scoreboard", "logging"  , logging, st);
		submit_gauge ("apache_scoreboard", "finishing", finishing, st);
		submit_gauge ("apache_scoreboard", "idle_cleanup", idle_cleanup, st);
	} else
	{
		submit_gauge ("apache_scoreboard", "connect"       , connect, st);
		submit_gauge ("apache_scoreboard", "close"         , closing, st);
		submit_gauge ("apache_scoreboard", "hard_error"    , hard_error, st);
		submit_gauge ("apache_scoreboard", "read"          , lighttpd_read, st);
		submit_gauge ("apache_scoreboard", "read_post"     , reading, st);
		submit_gauge ("apache_scoreboard", "write"         , sending, st);
		submit_gauge ("apache_scoreboard", "handle_request", handle_request, st);
		submit_gauge ("apache_scoreboard", "request_start" , request_start, st);
		submit_gauge ("apache_scoreboard", "request_end"   , request_end, st);
		submit_gauge ("apache_scoreboard", "response_start", response_start, st);
		submit_gauge ("apache_scoreboard", "response_end"  , response_end, st);

	}
}

static int apache_read_host (apache_t *st)
{
	int i;

	char *ptr;
	char *saveptr;
	char *lines[16];
	int   lines_num = 0;

	char *fields[4];
	int   fields_num;
	int server = LIGHTTPD; // default is lighttpd

	if (st->curl == NULL)
		return (-1);
	if (st->url == NULL)
		return (-1);

	st->apache_buffer_fill = 0;
	if (curl_easy_perform (st->curl) != 0)
	{
		ERROR ("apache: curl_easy_perform failed: %s",
				st->apache_curl_error);
		return (-1);
	}

	ptr = st->apache_buffer;
	saveptr = NULL;
	while ((lines[lines_num] = strtok_r (ptr, "\n\r", &saveptr)) != NULL)
	{
		ptr = NULL;
		lines_num++;

		if (lines_num >= 16)
			break;
	}

	/* set the host to localhost if st->host is not specified */
	if ( (st->host == NULL)
		|| (0 == strcmp(st->host, "")) ) {
		st->host = hostname_g;
	}

	for (i = 0; i < lines_num; i++)
	{
		fields_num = strsplit (lines[i], fields, 4);

		if (fields_num == 3)
		{
			if ((strcmp (fields[0], "Total") == 0)
					&& (strcmp (fields[1], "Accesses:") == 0))
				submit_counter ("apache_requests", "",
						atoll (fields[2]), st);
			else if ((strcmp (fields[0], "Total") == 0)
					&& (strcmp (fields[1], "kBytes:") == 0))
				submit_counter ("apache_bytes", "",
						1024LL * atoll (fields[2]), st);
		}
		else if (fields_num == 2)
		{
			// find out if the server is apache from the mod_status output.
			// apache mod_status output has additional fields which lighttpd
			// mod_status output doesn't have e.g: ReqPerSec.
			// submit_scoreboard needs server type information and thus it is
			// important to pick up a field before scoreboard gets parsed
			// to set the server type
			if (strcmp (fields[0], "ReqPerSec:") == 0)
				server = APACHE;
			else if (strcmp (fields[0], "Scoreboard:") == 0)
				submit_scoreboard (fields[1], server, st);
			else if (strcmp (fields[0], "BusyServers:") == 0)
				submit_gauge ("apache_connections", NULL, atol (fields[1]), st);
		}
	}

	st->apache_buffer_fill = 0;

	return (0);
} /* int apache_read_host */

static int apache_read (void)
{
	size_t i;
	int success = 0;
	int status;

	for (i = 0; i < apache_num; i++)
	{
		status = apache_read_host (apache[i]);
		if (status == 0)
			success++;
	}

	if (success == 0)
	{
		ERROR ("apache plugin: No host could be read. Will return an error so "
		       "the plugin will be delayed.");
		return (-1);
	}

	return (0);
 } /* int apache_read */

void module_register (void)
{
	plugin_register_complex_config ("apache", config);
	plugin_register_init ("apache", init);
	plugin_register_read ("apache", apache_read);
} /* void module_register */
