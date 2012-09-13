/**
 * collectd - src/mbmon.c
 * Copyright (C) 2006       Flavio Stanchina
 * Copyright (C) 2006-2007  Florian octo Forster
 * Based on the hddtemp plugin.
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
 *   Flavio Stanchina <flavio at stanchina.net>
 *   Florian Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define MBMON_DEF_HOST "127.0.0.1"
#define MBMON_DEF_PORT "411" /* the default for Debian */

static const char *config_keys[] =
{
	"Host",
	"Port",
	NULL
};
static int config_keys_num = 2;

static char *mbmon_host = NULL;
static char *mbmon_port = NULL;

/*
 * NAME
 *  mbmon_query_daemon
 *
 * DESCRIPTION
 * Connect to the mbmon daemon and receive data.
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
 *    TEMP0 : 27.0
 *    TEMP1 : 31.0
 *    TEMP2 : 29.5
 *    FAN0  : 4411
 *    FAN1  : 4470
 *    FAN2  : 4963
 *    VC0   :  +1.68
 *    VC1   :  +1.73
 *
 * FIXME:
 *  we need to create a new socket each time. Is there another way?
 *  Hm, maybe we can re-use the `sockaddr' structure? -octo
 */
static int mbmon_query_daemon (char *buffer, int buffer_size)
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

	host = mbmon_host;
	if (host == NULL)
		host = MBMON_DEF_HOST;

	port = mbmon_port;
	if (port == NULL)
		port = MBMON_DEF_PORT;

	if ((ai_return = getaddrinfo (host, port, &ai_hints, &ai_list)) != 0)
	{
		char errbuf[1024];
		ERROR ("mbmon: getaddrinfo (%s, %s): %s",
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
		if ((fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol)) < 0)
		{
			char errbuf[1024];
			ERROR ("mbmon: socket: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			continue;
		}

		/* connect to the mbmon daemon */
		if (connect (fd, (struct sockaddr *) ai_ptr->ai_addr, ai_ptr->ai_addrlen))
		{
			char errbuf[1024];
			INFO ("mbmon: connect (%s, %s): %s", host, port,
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
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
		ERROR ("mbmon: Could not connect to daemon.");
		return (-1);
	}

	/* receive data from the mbmon daemon */
	memset (buffer, '\0', buffer_size);

	buffer_fill = 0;
	while ((status = read (fd, buffer + buffer_fill, buffer_size - buffer_fill)) != 0)
	{
		if (status == -1)
		{
			char errbuf[1024];

			if ((errno == EAGAIN) || (errno == EINTR))
				continue;

			ERROR ("mbmon: Error reading from socket: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
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
		WARNING ("mbmon: Message from mbmon has been truncated.");
	}
	else if (buffer_fill == 0)
	{
		WARNING ("mbmon: Peer has unexpectedly shut down the socket. "
				"Buffer: `%s'", buffer);
		close (fd);
		return (-1);
	}

	close (fd);
	return (0);
}

static int mbmon_config (const char *key, const char *value)
{
	if (strcasecmp (key, "host") == 0)
	{
		if (mbmon_host != NULL)
			free (mbmon_host);
		mbmon_host = strdup (value);
	}
	else if (strcasecmp (key, "port") == 0)
	{
		if (mbmon_port != NULL)
			free (mbmon_port);
		mbmon_port = strdup (value);
	}
	else
	{
		return (-1);
	}

	return (0);
}

static void mbmon_submit (const char *type, const char *type_instance,
		double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "mbmon", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void mbmon_submit */

/* Trim trailing whitespace from a string. */
static void trim_spaces (char *s)
{
	size_t l;

	for (l = strlen (s) - 1; (l > 0) && isspace ((int) s[l]); l--)
		s[l] = '\0';
}

static int mbmon_read (void)
{
	char buf[1024];
	char *s, *t;

	/* get data from daemon */
	if (mbmon_query_daemon (buf, sizeof (buf)) < 0)
		return (-1);

	s = buf;
	while ((t = strchr (s, ':')) != NULL)
	{
		double value;
		char *nextc;

		char *type;
		char *inst;

		*t++ = '\0';
		trim_spaces (s);

		value = strtod (t, &nextc);
		if ((*nextc != '\n') && (*nextc != '\0'))
		{
			ERROR ("mbmon: value for `%s' contains invalid characters: `%s'", s, t);
			break;
		}

		if (strncmp (s, "TEMP", 4) == 0)
		{
			inst = s + 4;
			type = "temperature";
		}
		else if (strncmp (s, "FAN", 3) == 0)
		{
			inst = s + 3;
			type = "fanspeed";
		}
		else if (strncmp (s, "V", 1) == 0)
		{
			inst = s + 1;
			type = "voltage";
		}
		else
		{
			continue;
		}

		mbmon_submit (type, inst, value);

		if (*nextc == '\0')
			break;

		s = nextc + 1;
	}

	return (0);
} /* void mbmon_read */

/* module_register
   Register collectd plugin. */
void module_register (void)
{
	plugin_register_config ("mbmon", mbmon_config, config_keys, config_keys_num);
	plugin_register_read ("mbmon", mbmon_read);
} /* void module_register */
