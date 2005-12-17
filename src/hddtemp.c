/**
 * collectd - src/hddtemp.c
 * Copyright (C) 2005  Vincent Stehlé
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
 *   Vincent Stehlé <vincent.stehle at free.fr>
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#define MODULE_NAME "hddtemp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <libgen.h> /* for basename */

/* LOCALHOST_ADDR
   The ip address 127.0.0.1, as a 32 bit. */
#define LOCALHOST_ADDR 0x7F000001

/* HDDTEMP_PORT
   The tcp port the hddtemp daemon is listening on. */
#define HDDTEMP_PORT 7634

/* BUFFER_SIZE
   Size of the buffer we use to receive from the hddtemp daemon. */
#define BUFFER_SIZE 1024

static char *filename_format = "hddtemp-%s.rrd";

static char *ds_def[] =
{
	"DS:value:GAUGE:25:U:U",
	NULL
};
static int ds_num = 1;

typedef struct hddname
{
	int major;
	int minor;
	char *name;
	struct hddname *next;
} hddname_t;

static hddname_t *first_hddname = NULL;

/*
 * NAME
 *  hddtemp_query_daemon
 *
 * DESCRIPTION
 * Connect to the hddtemp daemon and receive data.
 *
 * ARGUMENTS:
 *  `buffer'            The buffer where we put the received ascii string.
 *  `buffer_size'       Size of the buffer
 *
 * RETURN VALUE:
 *   >= 0 if ok, < 0 otherwise.
 *
 * NOTES:
 *  Example of possible strings, as received from daemon:
 *    |/dev/hda|ST340014A|36|C|
 *    |/dev/hda|ST380011A|46|C||/dev/hdd|ST340016A|SLP|*|
 *
 * FIXME:
 *  we need to create a new socket each time. Is there another way?
 */
static int hddtemp_query_daemon (char *buffer, int buffer_size)
{
	int sock;
	ssize_t size;
	const struct sockaddr_in addr =
	{
		AF_INET,			/* sin_family */
		htons(HDDTEMP_PORT),		/* sin_port */
		{				/* sin_addr */
			htonl(LOCALHOST_ADDR),	/* s_addr */
		}
	};

	/* create our socket descriptor */
	if ((sock = socket (PF_INET, SOCK_STREAM, 0)) < 0)
	{
		syslog (LOG_ERR, "hddtemp: could not create socket: %s", strerror (errno));
		return (-1);
	}

	/* connect to the hddtemp daemon */
	if (connect (sock, (const struct sockaddr *) &addr, sizeof (addr)))
	{
		syslog (LOG_ERR, "hddtemp: Could not connect to the hddtemp daemon: %s", strerror (errno));
		close (sock);
		return (-1);
	}

	/* receive data from the hddtemp daemon */
	memset (buffer, '\0', buffer_size);
	size = recv (sock, buffer, buffer_size, 0);

	if (size >= buffer_size)
	{
		syslog (LOG_WARNING, "hddtemp: Message from hddtemp has been truncated.");
		close (sock);
		return (-1);
	}
	/* FIXME: Since the server closes the connection this returns zero. At
	 * least my machine does. -octo */
	/*
	else if (size == 0)
	{
		syslog (LOG_WARNING, "hddtemp: Peer has unexpectedly shut down the socket. Buffer: `%s'", buffer);
		close (sock);
		return (-1);
	}
	*/
	else if (size < 0)
	{
		syslog (LOG_ERR, "hddtemp: Could not receive from the hddtemp daemon: %s", strerror (errno));
		close (sock);
		return (-1);
	}

	close (sock);
	return (0);
}

static void hddtemp_init (void)
{
	FILE *fh;
	char buf[BUFFER_SIZE];
	int buflen;

	char *fields[16];
	int num_fields;

	int major;
	int minor;
	char *name;
	hddname_t *next;
	hddname_t *entry;

	next = first_hddname;
	while (next != NULL)
	{
		entry = next;
		next = entry->next;

		free (entry->name);
		free (entry);
	}
	first_hddname = NULL;

	if ((fh = fopen ("/proc/partitions", "r")) != NULL)
	{
		while (fgets (buf, BUFFER_SIZE, fh) != NULL)
		{
			/* Delete trailing newlines */
			buflen = strlen (buf);
			while ((buflen > 0) && ((buf[buflen-1] == '\n') || (buf[buflen-1] == '\r')))
				buf[--buflen] = '\0';
			if (buflen == 0)
				continue;
			
			num_fields = strsplit (buf, fields, 16);

			if (num_fields != 4)
				continue;

			major = atoi (fields[0]);
			minor = atoi (fields[1]);

			/* I know that this makes `minor' redundant, but I want
			 * to be able to change this beavior in the future..
			 * And 4 or 8 bytes won't hurt anybody.. -octo */
			if (major == 0)
				continue;
			if (minor != 0)
				continue;

			if ((name = strdup (fields[3])) == NULL)
				continue;

			if ((entry = (hddname_t *) malloc (sizeof (hddname_t))) == NULL)
			{
				free (name);
				continue;
			}

			entry->major = major;
			entry->minor = minor;
			entry->name  = name;
			entry->next  = NULL;

			if (first_hddname == NULL)
			{
				first_hddname = entry;
			}
			else
			{
				entry->next = first_hddname;
				first_hddname = entry;
			}
		}
	}

	return;
}

static void hddtemp_write (char *host, char *inst, char *val)
{
	char filename[BUFFER_SIZE];
	int status;

	/* construct filename */
	status = snprintf (filename, BUFFER_SIZE, filename_format, inst);
	if (status < 1)
		return;
	else if (status >= BUFFER_SIZE)
		return;

	rrd_update_file (host, filename, val, ds_def, ds_num);
}

static char *hddtemp_get_name (char *drive)
{
	hddname_t *list;
	char *ret;

	for (list = first_hddname; list != NULL; list = list->next)
		if (strcmp (drive, list->name) == 0)
			break;

	if (list == NULL)
		return (strdup (drive));

	if ((ret = (char *) malloc (128 * sizeof (char))) == NULL)
		return (NULL);

	if (snprintf (ret, 128, "%i-%i", list->major, list->minor) >= 128)
	{
		free (ret);
		return (NULL);
	}

	return (ret);
}

static void hddtemp_submit (char *inst, double temperature)
{
	char buf[BUFFER_SIZE];

	if (snprintf (buf, BUFFER_SIZE, "%u:%.3f", (unsigned int) curtime, temperature) >= BUFFER_SIZE)
		return;

	plugin_submit (MODULE_NAME, inst, buf);
}

static void hddtemp_read (void)
{
	char buf[BUFFER_SIZE];
	char *fields[128];
	char *ptr;
	int num_fields;
	int num_disks;
	int i;

	static int wait_time = 1;
	static int wait_left = 0;

	if (wait_left >= 10)
	{
		wait_left -= 10;
		return;
	}

	/* get data from daemon */
	if (hddtemp_query_daemon (buf, BUFFER_SIZE) < 0)
	{
		/* This limit is reached in log2(86400) =~ 17 steps. Since
		 * there is a 2^n seconds wait between each step it will need
		 * roughly one day to reach this limit. -octo */
		
		wait_time *= 2;
		if (wait_time > 86400)
			wait_time = 86400;

		wait_left = wait_time;

		return;
	}
	else
	{
		wait_time = 1;
		wait_left = 0;
	}

	/* NB: strtok will eat up "||" and leading "|"'s */
	num_fields = 0;
	ptr = buf;
	while ((fields[num_fields] = strtok (ptr, "|")) != NULL)
	{
		ptr = NULL;
		num_fields++;

		if (num_fields >= 128)
			break;
	}

	num_disks = num_fields / 4;

	for (i = 0; i < num_disks; i++)
	{
		char *name, *submit_name;
		double temperature;
		char *mode;

		mode = fields[4*i + 3];

		/* Skip non-temperature information */
		if (mode[0] != 'C' && mode[0] != 'F')
			continue;

		name = basename (fields[4*i + 0]);
		temperature = atof (fields[4*i + 2]);

		/* Convert farenheit to celsius */
		if (mode[0] == 'F')
			temperature = (temperature - 32.0) * 5.0 / 9.0;

		if ((submit_name = hddtemp_get_name (name)) != NULL)
		{
			hddtemp_submit (submit_name, temperature);
			free (submit_name);
		}
		else
		{
			hddtemp_submit (name, temperature);
		}
	}
}

/* module_register
   Register collectd plugin. */
void module_register (void)
{
	plugin_register (MODULE_NAME, hddtemp_init, hddtemp_read, hddtemp_write);
}
