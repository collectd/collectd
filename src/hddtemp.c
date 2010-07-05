/**
 * collectd - src/hddtemp.c
 * Copyright (C) 2005,2006  Vincent Stehlé
 * Copyright (C) 2006-2010  Florian octo Forster
 * Copyright (C) 2008       Sebastian Harl
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
 *   Sebastian Harl <sh at tokkee.org>
 *
 * TODO:
 *   Do a pass, some day, and spare some memory. We consume too much for now
 *   in string buffers and the like.
 *
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

# include <netdb.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <libgen.h> /* for basename */

#if HAVE_LINUX_MAJOR_H
# include <linux/major.h>
#endif

#define HDDTEMP_DEF_HOST "127.0.0.1"
#define HDDTEMP_DEF_PORT "7634"

static const char *config_keys[] =
{
	"Host",
	"Port"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char *hddtemp_host = NULL;
static char hddtemp_port[16];

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
 *  Hm, maybe we can re-use the `sockaddr' structure? -octo
 */
static int hddtemp_query_daemon (char *buffer, int buffer_size)
{
	int fd;
	ssize_t status;
	int buffer_fill;

	const char *host;
	const char *port;

	struct addrinfo  ai_hints;
	struct addrinfo *ai_list, *ai_ptr;
	int              ai_return;

	memset (&ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_flags    = 0;
#ifdef AI_ADDRCONFIG
	ai_hints.ai_flags   |= AI_ADDRCONFIG;
#endif
	ai_hints.ai_family   = PF_UNSPEC;
	ai_hints.ai_socktype = SOCK_STREAM;
	ai_hints.ai_protocol = IPPROTO_TCP;

	host = hddtemp_host;
	if (host == NULL)
		host = HDDTEMP_DEF_HOST;

	port = hddtemp_port;
	if (strlen (port) == 0)
		port = HDDTEMP_DEF_PORT;

	if ((ai_return = getaddrinfo (host, port, &ai_hints, &ai_list)) != 0)
	{
		char errbuf[1024];
		ERROR ("hddtemp plugin: getaddrinfo (%s, %s): %s",
				host, port,
				(ai_return == EAI_SYSTEM)
				? sstrerror (errno, errbuf, sizeof (errbuf))
				: gai_strerror (ai_return));
		return (-1);
	}

	fd = -1;
	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		/* create our socket descriptor */
		fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype,
				ai_ptr->ai_protocol);
		if (fd < 0)
		{
			char errbuf[1024];
			ERROR ("hddtemp plugin: socket: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			continue;
		}

		/* connect to the hddtemp daemon */
		if (connect (fd, (struct sockaddr *) ai_ptr->ai_addr,
					ai_ptr->ai_addrlen))
		{
			char errbuf[1024];
			INFO ("hddtemp plugin: connect (%s, %s) failed: %s",
					host, port,
					sstrerror (errno, errbuf, sizeof (errbuf)));
			close (fd);
			fd = -1;
			continue;
		}

		/* A socket could be opened and connecting succeeded. We're
		 * done. */
		break;
	}

	freeaddrinfo (ai_list);

	if (fd < 0)
	{
		ERROR ("hddtemp plugin: Could not connect to daemon.");
		return (-1);
	}

	/* receive data from the hddtemp daemon */
	memset (buffer, '\0', buffer_size);

	buffer_fill = 0;
	while ((status = read (fd, buffer + buffer_fill, buffer_size - buffer_fill)) != 0)
	{
		if (status == -1)
		{
			char errbuf[1024];

			if ((errno == EAGAIN) || (errno == EINTR))
				continue;

			ERROR ("hddtemp plugin: Error reading from socket: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			close (fd);
			return (-1);
		}
		buffer_fill += status;

		if (buffer_fill >= buffer_size)
			break;
	}

	if (buffer_fill >= buffer_size)
	{
		buffer[buffer_size - 1] = '\0';
		WARNING ("hddtemp plugin: Message from hddtemp has been "
				"truncated.");
	}
	else if (buffer_fill == 0)
	{
		WARNING ("hddtemp plugin: Peer has unexpectedly shut down "
				"the socket. Buffer: `%s'", buffer);
		close (fd);
		return (-1);
	}

	close (fd);
	return (0);
}

static int hddtemp_config (const char *key, const char *value)
{
	if (strcasecmp (key, "Host") == 0)
	{
		if (hddtemp_host != NULL)
			free (hddtemp_host);
		hddtemp_host = strdup (value);
	}
	else if (strcasecmp (key, "Port") == 0)
	{
		int port = (int) (atof (value));
		if ((port > 0) && (port <= 65535))
			ssnprintf (hddtemp_port, sizeof (hddtemp_port),
					"%i", port);
		else
			sstrncpy (hddtemp_port, value, sizeof (hddtemp_port));
	}
	else
	{
		return (-1);
	}

	return (0);
}

static void hddtemp_submit (char *type_instance, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "hddtemp", sizeof (vl.plugin));
	sstrncpy (vl.type, "temperature", sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static int hddtemp_read (void)
{
	char buf[1024];
	char *fields[128];
	char *ptr;
	char *saveptr;
	int num_fields;
	int num_disks;
	int i;

	/* get data from daemon */
	if (hddtemp_query_daemon (buf, sizeof (buf)) < 0)
		return (-1);

	/* NB: strtok_r will eat up "||" and leading "|"'s */
	num_fields = 0;
	ptr = buf;
	saveptr = NULL;
	while ((fields[num_fields] = strtok_r (ptr, "|", &saveptr)) != NULL)
	{
		ptr = NULL;
		num_fields++;

		if (num_fields >= 128)
			break;
	}

	num_disks = num_fields / 4;

	for (i = 0; i < num_disks; i++)
	{
		char *name;
		double temperature;
		char *mode;

		mode = fields[4*i + 3];
		name = basename (fields[4*i + 0]);

		/* Skip non-temperature information */
		if (mode[0] != 'C' && mode[0] != 'F')
			continue;

		temperature = atof (fields[4*i + 2]);

		/* Convert farenheit to celsius */
		if (mode[0] == 'F')
			temperature = (temperature - 32.0) * 5.0 / 9.0;

		hddtemp_submit (name, temperature);
	}
	
	return (0);
} /* int hddtemp_read */

/* module_register
   Register collectd plugin. */
void module_register (void)
{
	plugin_register_config ("hddtemp", hddtemp_config,
			config_keys, config_keys_num);
	plugin_register_read ("hddtemp", hddtemp_read);
}
