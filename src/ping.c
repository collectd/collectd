/**
 * collectd - src/ping.c
 * Copyright (C) 2005-2007  Florian octo Forster
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

#include <netinet/in.h>
#include "liboping/oping.h"

/*
 * Private data types
 */
struct hostlist_s
{
	char *host;
	int   wait_time;
	int   wait_left;
	struct hostlist_s *next;
};
typedef struct hostlist_s hostlist_t;

/*
 * Private variables
 */
static pingobj_t *pingobj = NULL;
static hostlist_t *hosts = NULL;

static const char *config_keys[] =
{
	"Host",
	"TTL",
	NULL
};
static int config_keys_num = 2;

/*
 * Private functions
 */
static void add_hosts (void)
{
	hostlist_t *hl_this;
	hostlist_t *hl_prev;

	hl_this = hosts;
	hl_prev = NULL;
	while (hl_this != NULL)
	{
		DEBUG ("ping plugin: host = %s, wait_left = %i, "
				"wait_time = %i, next = %p",
				hl_this->host, hl_this->wait_left,
				hl_this->wait_time, (void *) hl_this->next);

		if (hl_this->wait_left <= 0)
		{
			if (ping_host_add (pingobj, hl_this->host) == 0)
			{
				DEBUG ("ping plugin: Successfully added host %s", hl_this->host);
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
				WARNING ("ping plugin: Failed adding host "
						"`%s': %s", hl_this->host,
						ping_get_error (pingobj));
				hl_this->wait_left = hl_this->wait_time;
				hl_this->wait_time *= 2;
				if (hl_this->wait_time > 86400)
					hl_this->wait_time = 86400;
			}
		}
		else
		{
			hl_this->wait_left -= interval_g;
		}

		if (hl_this != NULL)
		{
			hl_prev = hl_this;
			hl_this = hl_this->next;
		}
	}
} /* void add_hosts */

static int ping_init (void)
{
	if (pingobj == NULL)
		return (-1);

	if (hosts != NULL)
		add_hosts ();

	return (0);
} /* int ping_init */

static int ping_config (const char *key, const char *value)
{
	if (pingobj == NULL)
	{
		if ((pingobj = ping_construct ()) == NULL)
		{
			ERROR ("ping plugin: `ping_construct' failed.");
			return (1);
		}
	}

	if (strcasecmp (key, "host") == 0)
	{
		hostlist_t *hl;
		char *host;

		if ((hl = (hostlist_t *) malloc (sizeof (hostlist_t))) == NULL)
		{
			char errbuf[1024];
			ERROR ("ping plugin: malloc failed: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			return (1);
		}
		if ((host = strdup (value)) == NULL)
		{
			char errbuf[1024];
			free (hl);
			ERROR ("ping plugin: strdup failed: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			return (1);
		}

		hl->host = host;
		hl->wait_time = 2 * interval_g;
		hl->wait_left = 0;
		hl->next = hosts;
		hosts = hl;
	}
	else if (strcasecmp (key, "ttl") == 0)
	{
		int ttl = atoi (value);
		if (ping_setopt (pingobj, PING_OPT_TTL, (void *) &ttl))
		{
			WARNING ("ping: liboping did not accept the TTL value %i", ttl);
			return (1);
		}
	}
	else
	{
		return (-1);
	}

	return (0);
}

static void ping_submit (char *host, double latency)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = latency;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "ping", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, "", sizeof (vl.plugin_instance));
	sstrncpy (vl.type_instance, host, sizeof (vl.type_instance));
	sstrncpy (vl.type, "ping", sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static int ping_read (void)
{
	pingobj_iter_t *iter;

	char   host[512];
	double latency;
	size_t buf_len;
	int    number_of_hosts;

	if (pingobj == NULL)
		return (-1);

	if (hosts != NULL)
		add_hosts ();

	if (ping_send (pingobj) < 0)
	{
		ERROR ("ping plugin: `ping_send' failed: %s",
				ping_get_error (pingobj));
		return (-1);
	}

	number_of_hosts = 0;
	for (iter = ping_iterator_get (pingobj);
			iter != NULL;
			iter = ping_iterator_next (iter))
	{
		buf_len = sizeof (host);
		if (ping_iterator_get_info (iter, PING_INFO_HOSTNAME,
					host, &buf_len))
		{
			WARNING ("ping plugin: ping_iterator_get_info "
					"(PING_INFO_HOSTNAME) failed.");
			continue;
		}

		buf_len = sizeof (latency);
		if (ping_iterator_get_info (iter, PING_INFO_LATENCY,
					&latency, &buf_len))
		{
			WARNING ("ping plugin: ping_iterator_get_info (%s, "
					"PING_INFO_LATENCY) failed.", host);
			continue;
		}

		DEBUG ("ping plugin: host = %s, latency = %f", host, latency);
		ping_submit (host, latency);
		number_of_hosts++;
	}

	if ((number_of_hosts == 0) && (getuid () != 0))
	{
		ERROR ("ping plugin: All hosts failed. Try starting collectd as root.");
	}

	return (number_of_hosts == 0 ? -1 : 0);
} /* int ping_read */

void module_register (void)
{
	plugin_register_config ("ping", ping_config,
			config_keys, config_keys_num);
	plugin_register_init ("ping", ping_init);
	plugin_register_read ("ping", ping_read);
} /* void module_register */
