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
#include "liboping/liboping.h"

static pingobj_t *pingobj = NULL;

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

static void ping_init (void)
{
	return;
}

static int ping_config (char *key, char *value)
{
	if (pingobj == NULL)
	{
		if ((pingobj = ping_construct ()) == NULL)
		{
			syslog (LOG_ERR, "ping: `ping_construct' failed.\n");
			return (1);
		}
	}

	if (strcasecmp (key, "host") == 0)
	{
		if (ping_host_add (pingobj, value) < 0)
		{
			syslog (LOG_WARNING, "ping: `ping_host_add' failed.");
			return (1);
		}
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

	char   *host;
	double  latency;

	if (pingobj == NULL)
		return;

	if (ping_send (pingobj) < 0)
	{
		syslog (LOG_ERR, "ping: `ping_send' failed.");
		return;
	}

	for (iter = ping_iterator_get (pingobj); iter != NULL; iter = ping_iterator_next (iter))
	{
		const char *tmp;

		if ((tmp = ping_iterator_get_host (iter)) == NULL)
			continue;
		if ((host = strdup (tmp)) == NULL)
			continue;

		latency = ping_iterator_get_latency (iter);

		DBG ("host = %s, latency = %f", host, latency);
		ping_submit (host, latency);

		free (host); host = NULL;
	}
}

void module_register (void)
{
	plugin_register (MODULE_NAME, ping_init, ping_read, ping_write);
	cf_register (MODULE_NAME, ping_config, config_keys, config_keys_num);
}

#undef MODULE_NAME
