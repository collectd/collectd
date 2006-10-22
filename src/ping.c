/**
 * collectd - src/ping.c
 * Copyright (C) 2005,2006  Florian octo Forster
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
#include "utils_debug.h"

#define MODULE_NAME "ping"

#include <netinet/in.h>
#include "liboping/oping.h"

struct hostlist_s
{
	char *host;
	int   wait_time;
	int   wait_left;
	struct hostlist_s *next;
};
typedef struct hostlist_s hostlist_t;

static pingobj_t *pingobj = NULL;
static hostlist_t *hosts = NULL;

static char *file_template = "ping-%s.rrd";

static char *ds_def[] = 
{
	"DS:ping:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int ds_num = 1;

static char *config_keys[] =
{
	"Host",
	"TTL",
	NULL
};
static int config_keys_num = 2;

static void add_hosts (void)
{
	hostlist_t *hl_this;
	hostlist_t *hl_prev;

	int step = atoi (COLLECTD_STEP);

	hl_this = hosts;
	hl_prev = NULL;
	while (hl_this != NULL)
	{
		DBG ("host = %s, wait_left = %i, wait_time = %i, next = %p",
				hl_this->host, hl_this->wait_left, hl_this->wait_time, (void *) hl_this->next);

		if (hl_this->wait_left <= 0)
		{
			if (ping_host_add (pingobj, hl_this->host) == 0)
			{
				DBG ("Successfully added host %s", hl_this->host);
				/* Remove the host from the linked list */
				if (hl_prev != NULL)
					hl_prev->next = hl_this->next;
				else
					hosts = hl_this->next;
				free (hl_this->host);
				free (hl_this);
				hl_this = (hl_prev != NULL) ? hl_prev : hosts;
			}
			else
			{
				hl_this->wait_left = hl_this->wait_time;
				hl_this->wait_time *= 2;
				if (hl_this->wait_time > 86400)
					hl_this->wait_time = 86400;
			}
		}
		else
		{
			hl_this->wait_left -= step;
		}

		if (hl_this != NULL)
		{
			hl_prev = hl_this;
			hl_this = hl_this->next;
		}
	}
}

static void ping_init (void)
{
	if (hosts != NULL)
		add_hosts ();
}

static int ping_config (char *key, char *value)
{
	if (pingobj == NULL)
	{
		if ((pingobj = ping_construct ()) == NULL)
		{
			syslog (LOG_ERR, "ping: `ping_construct' failed: %s",
				       	ping_get_error (pingobj));
			return (1);
		}
	}

	if (strcasecmp (key, "host") == 0)
	{
		hostlist_t *hl;
		char *host;
		int step = atoi (COLLECTD_STEP);

		if ((hl = (hostlist_t *) malloc (sizeof (hostlist_t))) == NULL)
		{
			syslog (LOG_ERR, "ping plugin: malloc failed: %s",
					strerror (errno));
			return (1);
		}
		if ((host = strdup (value)) == NULL)
		{
			free (hl);
			syslog (LOG_ERR, "ping plugin: strdup failed: %s",
					strerror (errno));
			return (1);
		}

		hl->host = host;
		hl->wait_time = 2 * step;
		hl->wait_left = 0;
		hl->next = hosts;
		hosts = hl;
	}
	else if (strcasecmp (key, "ttl") == 0)
	{
		int ttl = atoi (value);
		if (ping_setopt (pingobj, PING_DEF_TIMEOUT, (void *) &ttl))
		{
			syslog (LOG_WARNING, "ping: liboping did not accept the TTL value %i", ttl);
			return (1);
		}
	}
	else
	{
		return (-1);
	}

	return (0);
}

static void ping_write (char *host, char *inst, char *val)
{
	char file[512];
	int status;

	status = snprintf (file, 512, file_template, inst);
	if (status < 1)
		return;
	else if (status >= 512)
		return;

	rrd_update_file (host, file, val, ds_def, ds_num);
}

#define BUFSIZE 256
static void ping_submit (char *host, double latency)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%f", (unsigned int) curtime, latency) >= BUFSIZE)
		return;

	plugin_submit (MODULE_NAME, host, buf);
}
#undef BUFSIZE

static void ping_read (void)
{
	pingobj_iter_t *iter;

	char   host[512];
	double latency;
	size_t buf_len;

	if (pingobj == NULL)
		return;

	if (hosts != NULL)
		add_hosts ();

	if (ping_send (pingobj) < 0)
	{
		syslog (LOG_ERR, "ping: `ping_send' failed: %s",
				ping_get_error (pingobj));
		return;
	}

	for (iter = ping_iterator_get (pingobj);
			iter != NULL;
			iter = ping_iterator_next (iter))
	{
		buf_len = sizeof (host);
		if (ping_iterator_get_info (iter, PING_INFO_HOSTNAME,
					host, &buf_len))
			continue;

		buf_len = sizeof (latency);
		if (ping_iterator_get_info (iter, PING_INFO_LATENCY,
					&latency, &buf_len))
			continue;

		DBG ("host = %s, latency = %f", host, latency);
		ping_submit (host, latency);
	}
}

void module_register (void)
{
	plugin_register (MODULE_NAME, ping_init, ping_read, ping_write);
	cf_register (MODULE_NAME, ping_config, config_keys, config_keys_num);
}

#undef MODULE_NAME
