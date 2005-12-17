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

extern char *pinghosts[MAX_PINGHOSTS];
extern int   num_pinghosts;
static int   pingerrors[MAX_PINGHOSTS];

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

	for (i = 0; i < num_pinghosts; i++)
		pingerrors[i] = 0;

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
	else if ((pinghosts[num_pinghosts] = strdup (value)) == NULL)
	{
		return (2);
	}
	else
	{
		pingerrors[num_pinghosts] = 0;
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

	if (snprintf (buf, BUFSIZE, "%u:%u", (unsigned int) curtime, ping_time) >= BUFSIZE)
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
		if (pingerrors[i] & 0x30)
			continue;
		
		ping = tpinghost (pinghosts[i]);

		switch (ping)
		{
			case 0:
				if (!(pingerrors[i] & 0x01))
					syslog (LOG_WARNING, "ping %s: Connection timed out.", pinghosts[i]);
				pingerrors[i] |= 0x01;
				break;

			case -1:
				if (!(pingerrors[i] & 0x02))
					syslog (LOG_WARNING, "ping %s: Host or service is not reachable.", pinghosts[i]);
				pingerrors[i] |= 0x02;
				break;

			case -2:
				syslog (LOG_ERR, "ping %s: Socket error. Ping will be disabled.", pinghosts[i]);
				pingerrors[i] |= 0x10;
				break;

			case -3:
				if (!(pingerrors[i] & 0x04))
					syslog (LOG_WARNING, "ping %s: Connection refused.", pinghosts[i]);
				pingerrors[i] |= 0x04;
				break;

			default:
				if (pingerrors[i] != 0x00)
					syslog (LOG_NOTICE, "ping %s: Back to normal: %ims.", pinghosts[i], ping);
				pingerrors[i] = 0x00;
				ping_submit (ping, pinghosts[i]);
		} /* switch (ping) */
	} /* for (i = 0; i < num_pinghosts; i++) */
}

void module_register (void)
{
	plugin_register (MODULE_NAME, ping_init, ping_read, ping_write);
	cf_register (MODULE_NAME, ping_config, config_keys, config_keys_num);
}

#undef MODULE_NAME
