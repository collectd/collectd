/**
 * collectd - src/network.c
 * Copyright (C) 2006  Florian octo Forster
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

#include "network.h"
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

#define IPV4_MCAST_GROUP "239.192.74.66"
#define UDP_PORT 25826

/* 1500 - 40 - 8  =  Ethernet packet - IPv6 header - UDP header */
#define BUFF_SIZE 1452

typedef struct socklist
{
	int              fd;
	struct socklist *next;
} socklist_t;

static socklist_t *listen_socks_head = NULL;

uint16_t get_port (void)
{
	char *port_str;
	int   port_int;
	uint16_t ret;

	port_str = cf_get_option ("Port", NULL);
	port_int = 0;

	if (port_str != NULL)
		port_int = atoi (port_str);

	if (port_int == 0)
		port_int = UDP_PORT;

	ret = htons (port_int);
	return (ret);
}

int network_create_listen_socket (const char *node, const char *service)
{
	socklist_t *socklist_tail;

	struct addrinfo  ai_hints;
	struct addrinfo *ai_list, *ai_ptr;
	int              ai_return;

	int num_added = 0;

	socklist_tail = listen_socks_head;
	while ((socklist_tail != NULL) && (socklist_tail->next != NULL))
		socklist_tail = socklist_tail->next;

	memset (&ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_flags    = AI_PASSIVE | AI_ADDRCONFIG;
	ai_hints.ai_family   = AF_UNSPEC;
	ai_hints.ai_socktype = SOCK_DGRAM;
	ai_hints.ai_protocol = IPPROTO_UDP;

	if ((ai_return = getaddrinfo (node, service, &ai_hints, &ai_list)) != 0)
	{
		syslog (LOG_ERR, "getaddrinfo (%s, %s): %s",
				node == NULL ? "(null)" : node,
				service == NULL ? "(null)" : service,
				ai_return == EAI_SYSTEM ? strerror (errno) : gai_strerror (ai_return));
		return (-1);
	}

	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		socklist_t *socklist_ent;

		if ((socklist_ent = (socklist_t *) malloc (sizeof (socklist_t))) == NULL)
		{
			syslog (LOG_EMERG, "malloc: %s", strerror (errno));
			continue;
		}

		socklist_ent->fd   = socket (ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
		socklist_ent->next = NULL;

		if (socklist_ent->fd == -1)
		{
			syslog (LOG_ERR, "socket: %s", strerror (errno));
			free (socklist_ent);
			continue;
		}

		if (bind (socklist_ent->fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen) == -1)
		{
			syslog (LOG_ERR, "bind: %s", strerror (errno));
			close (socklist_ent->fd);
			free (socklist_ent);
			continue;
		}

		if (ai_ptr->ai_family == AF_INET)
		{
			struct sockaddr_in *addr = (struct sockaddr_in *) ai_ptr->ai_addr;
			if (IN_MULTICAST (ntohl (addr->sin_addr.s_addr)))
			{
				struct ip_mreq mreq;

				mreq.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
				mreq.imr_interface.s_addr = htonl (INADDR_ANY);

				if (setsockopt (socklist_ent->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
							&mreq, sizeof (mreq)) == -1)
				{
					syslog (LOG_ERR, "setsockopt: %s", strerror (errno));
					close (socklist_ent->fd);
					free (socklist_ent);
					continue;
				}
			}
		}
		else if (ai_ptr->ai_family == AF_INET6)
		{
			/* Useful example: http://gsyc.escet.urjc.es/~eva/IPv6-web/examples/mcast.html */
			struct sockaddr_in6 *addr = (struct sockaddr_in6 *) ai_ptr->ai_addr;
			if (IN6_IS_ADDR_MULTICAST (&addr->sin6_addr))
			{
				struct ipv6_mreq mreq;

				memcpy (&mreq.ipv6mr_multiaddr,
						&addr->sin6_addr,
						sizeof (addr->sin6_addr));

				/* FIXME What do I need here? `netdevice(7)'
				 * doesn't tell me either.. */
				mreq6.ipv6mr_interface = 0;

				if (setsockopt (socklist_ent->fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
							&mreq, sizeof (mreq)) == -1)
				{
					syslog (LOG_ERR, "setsockopt: %s", strerror (errno));
					close (socklist_ent->fd);
					free (socklist_ent);
					continue;
				}
			}
		}

		if (socklist_tail == NULL)
		{
			listen_socks_head = socklist_tail = socklist_ent;
		}
		else
		{
			socklist_tail->next = socklist_ent;
			socklist_tail = socklist_ent;
		}
		num_added++;
	}

	freeaddrinfo (ai_list);

	return (num_added);
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

int network_receive (char **host, char **type, char **instance, char **value)
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

int network_send (char *type, char *instance, char *value)
{
	int sd = get_write_socket ();
	struct sockaddr_in addr;

	char buf[BUFF_SIZE];
	int buflen;

	if (sd == -1)
		return (-1);

	if ((buflen = snprintf (buf, BUFF_SIZE, "%s %s %s", type, instance, value)) >= BUFF_SIZE)
	{
		syslog (LOG_WARNING, "network_send: Output truncated..");
		return (-1);
	}
	buf[buflen++] = '\0';

	memset(&addr, '\0', sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr (IPV4_MCAST_GROUP);
	addr.sin_port = get_port ();

	return (sendto (sd, buf, buflen, 0, (struct sockaddr *) &addr, sizeof (addr)));
}
