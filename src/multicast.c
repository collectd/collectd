/**
 * collectd - src/multicast.c
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <errno.h>

#include "multicast.h"
#include "common.h"

/*
 * From RFC2365:
 *
 * The IPv4 Organization Local Scope -- 239.192.0.0/14
 *
 * 239.192.0.0/14 is defined to be the IPv4 Organization Local Scope, and is
 * the space from which an organization should allocate sub-ranges when
 * defining scopes for private use.
 *
 * Port 25826 is not assigned as of 2005-09-12
 */

#define MCAST_GROUP "239.192.74.66"
#define UDP_PORT 25826

/* 1500 - 40 - 8  =  Ethernet packet - IPv6 header - UDP header */
#define BUFF_SIZE 1452

int get_read_socket (void)
{
	static int sd = -1; /* socket descriptor */
	int optval;

	struct sockaddr_in addr;
	struct ip_mreq mreq;

	if (sd != -1)
		return (sd);

	/* Create UDP sicket */
	if ((sd = socket (PF_INET, SOCK_DGRAM, 0)) == -1)
	{
		syslog (LOG_ERR, "socket: %s", strerror (errno));
		return (-1);
	}

	optval = 1;
	if (setsockopt (sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
	{
		syslog (LOG_ERR, "setsockopt: %s", strerror (errno));
		shutdown (sd, SHUT_RD);
		sd = -1;
		return (-1);
	}

	memset (&addr, '\0', sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_ANY);
	addr.sin_port = htons (UDP_PORT);
	if (bind (sd, (struct sockaddr *) &addr, sizeof (addr)) == -1)
	{
		syslog (LOG_ERR, "bind: %s", strerror (errno));
		shutdown (sd, SHUT_RD);
		sd = -1;
		return (-1);
	}

	mreq.imr_multiaddr.s_addr = inet_addr (MCAST_GROUP);
	mreq.imr_interface.s_addr = htonl (INADDR_ANY);
	if (setsockopt (sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof (mreq)) == -1)
	{
		syslog (LOG_ERR, "setsockopt: %s", strerror (errno));
		shutdown (sd, SHUT_RD);
		sd = -1;
		return (-1);
	}

	return (sd);
}

int get_write_socket (void)
{
	static int sd = -1;

	if (sd != -1)
		return (sd);

	if ((sd = socket (AF_INET, SOCK_DGRAM, 0)) == -1)
	{
		syslog (LOG_ERR, "socket: %s", strerror (errno));
		return (-1);
	}

	return (sd);
}

char *addr_to_host (struct sockaddr_in *addr)
{
	char *host;
	struct hostent *he;

	if ((he = gethostbyaddr ((char *) &addr->sin_addr, sizeof (addr->sin_addr), AF_INET)) != NULL)
	{
		host = strdup (he->h_name);
	}
	else
	{
		char *tmp = inet_ntoa (addr->sin_addr);
		host = strdup (tmp);
	}

	return (host);
}

int multicast_receive (char **host, char **type, char **instance, char **value)
{
	int sd = get_read_socket ();

	char buffer[BUFF_SIZE];

	struct sockaddr_in addr;
	socklen_t addr_size;

	char *fields[4];

	*host     = NULL;
	*type     = NULL;
	*instance = NULL;
	*value    = NULL;

	if (sd == -1)
		return (-1);

	addr_size = sizeof (addr);

	if (recvfrom (sd, buffer, BUFF_SIZE, 0, (struct sockaddr *) &addr, &addr_size) == -1)
	{
		syslog (LOG_ERR, "recvfrom: %s", strerror (errno));
		return (-1);
	}

	if (strsplit (buffer, fields, 4) != 3)
		return (-1);

	*host     = addr_to_host (&addr);
	*type     = strdup (fields[0]);
	*instance = strdup (fields[1]);
	*value    = strdup (fields[2]);

	if (*host == NULL || *type == NULL || *instance == NULL || *value == NULL)
		return (-1);

	return (0);
}

int multicast_send (char *type, char *instance, char *value)
{
	int sd = get_write_socket ();
	struct sockaddr_in addr;

	char buf[BUFF_SIZE];
	int buflen;

	if (sd == -1)
		return (-1);

	if ((buflen = snprintf (buf, BUFF_SIZE, "%s %s %s", type, instance, value)) >= BUFF_SIZE)
	{
		syslog (LOG_WARNING, "multicast_send: Output truncated..");
		return (-1);
	}
	buf[buflen++] = '\0';

	memset(&addr, '\0', sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr (MCAST_GROUP);
	addr.sin_port = htons (UDP_PORT);

	return (sendto (sd, buf, buflen, 0, (struct sockaddr *) &addr, sizeof (addr)));
}
