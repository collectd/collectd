/**
 * collectd - src/ping.c
 * Copyright (C) 2005  Florian octo Forster
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

#define MODULE_NAME "ping"

#include <netinet/in.h>
#include "libping/ping.h"

#define MAX_PINGHOSTS 32

typedef struct
{
	char *name;
	int   flags;
	int   disable; /* How long (how many iterations) this host is still disabled */
	int   backoff; /* How long the host will be disabled, if it failes again */
} pinghost_t;

static pinghost_t hosts[MAX_PINGHOSTS];
static int        num_pinghosts;

static char *file_template = "ping-%s.rrd";

static char *ds_def[] = 
{
	"DS:ping:GAUGE:25:0:65535",
	NULL
};
static int ds_num = 1;

static char *config_keys[] =
{
	"Host",
	NULL
};
static int config_keys_num = 1;

static void ping_init (void)
{
	int i;

	for (i = 0; i < MAX_PINGHOSTS; i++)
	{
		hosts[i].flags = 0;
		hosts[i].disable = 0;
		hosts[i].backoff = 1;
	}

	return;
}

static int ping_config (char *key, char *value)
{
	if (strcasecmp (key, "host"))
	{
		return (-1);
	}
	else if (num_pinghosts >= MAX_PINGHOSTS)
	{
		return (1);
	}
	else if ((hosts[num_pinghosts].name = strdup (value)) == NULL)
	{
		return (2);
	}
	else
	{
		num_pinghosts++;
		return (0);
	}
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
static void ping_submit (int ping_time, char *host)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%i", (unsigned int) curtime, ping_time) >= BUFSIZE)
		return;

	plugin_submit (MODULE_NAME, host, buf);
}
#undef BUFSIZE

static void ping_read (void)
{
	int ping;
	int i;

	for (i = 0; i < num_pinghosts; i++)
	{
		if (hosts[i].disable > 0)
		{
			hosts[i].disable--;
			continue;
		}
		
		ping = tpinghost (hosts[i].name);

		switch (ping)
		{
			case 0:
				if (!(hosts[i].flags & 0x01))
					syslog (LOG_WARNING, "ping %s: Connection timed out.", hosts[i].name);
				hosts[i].flags |= 0x01;
				break;

			case -1:
				if (!(hosts[i].flags & 0x02))
					syslog (LOG_WARNING, "ping %s: Host or service is not reachable.", hosts[i].name);
				hosts[i].flags |= 0x02;
				break;

			case -2:
				syslog (LOG_ERR, "ping %s: Socket error. Ping will be disabled for %i iteration(s).",
						hosts[i].name, hosts[i].backoff);
				hosts[i].disable = hosts[i].backoff;
				if (hosts[i].backoff < 8192) /* 22 3/4 hours */
					hosts[i].backoff *= 2;
				hosts[i].flags |= 0x10;
				break;

			case -3:
				if (!(hosts[i].flags & 0x04))
					syslog (LOG_WARNING, "ping %s: Connection refused.", hosts[i].name);
				hosts[i].flags |= 0x04;
				break;

			default:
				if (hosts[i].flags != 0x00)
					syslog (LOG_NOTICE, "ping %s: Back to normal: %ims.", hosts[i].name, ping);
				hosts[i].flags = 0x00;
				hosts[i].backoff = 1;
				ping_submit (ping, hosts[i].name);
		} /* switch (ping) */
	} /* for (i = 0; i < num_pinghosts; i++) */
}

void module_register (void)
{
	plugin_register (MODULE_NAME, ping_init, ping_read, ping_write);
	cf_register (MODULE_NAME, ping_config, config_keys, config_keys_num);
}

#undef MODULE_NAME
