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
#include <assert.h>

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

#define BUFF_SIZE 4096

#ifdef HAVE_LIBRRD
extern int operating_mode;
#else
static int operating_mode = MODE_CLIENT;
#endif

typedef struct sockent
{
	int                      fd;
	struct sockaddr_storage *addr;
	socklen_t                addrlen;
	struct sockent          *next;
} sockent_t;

static sockent_t *socklist_head = NULL;

static int network_bind_socket (int fd, const struct addrinfo *ai, const sockent_t *se)
{
	int loop = 1;

	if (bind (fd, ai->ai_addr, ai->ai_addrlen) == -1)
	{
		syslog (LOG_ERR, "bind: %s", strerror (errno));
		return (-1);
	}

	if (ai->ai_family == AF_INET)
	{
		struct sockaddr_in *addr = (struct sockaddr_in *) ai->ai_addr;
		if (IN_MULTICAST (ntohl (addr->sin_addr.s_addr)))
		{
			struct ip_mreq mreq;

			mreq.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
			mreq.imr_interface.s_addr = htonl (INADDR_ANY);

			if (setsockopt (se->fd, IPPROTO_IP, IP_MULTICAST_LOOP,
						&loop, sizeof (loop)) == -1)
			{
				syslog (LOG_ERR, "setsockopt: %s", strerror (errno));
				return (-1);
			}

			if (setsockopt (se->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
						&mreq, sizeof (mreq)) == -1)
			{
				syslog (LOG_ERR, "setsockopt: %s", strerror (errno));
				return (-1);
			}
		}
	}
	else if (ai->ai_family == AF_INET6)
	{
		/* Useful example: http://gsyc.escet.urjc.es/~eva/IPv6-web/examples/mcast.html */
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) ai->ai_addr;
		if (IN6_IS_ADDR_MULTICAST (&addr->sin6_addr))
		{
			struct ipv6_mreq mreq;

			memcpy (&mreq.ipv6mr_multiaddr,
					&addr->sin6_addr,
					sizeof (addr->sin6_addr));

			/* http://developer.apple.com/documentation/Darwin/Reference/ManPages/man4/ip6.4.html
			 * ipv6mr_interface may be set to zeroes to
			 * choose the default multicast interface or to
			 * the index of a particular multicast-capable
			 * interface if the host is multihomed.
			 * Membership is associ-associated with a
			 * single interface; programs running on
			 * multihomed hosts may need to join the same
			 * group on more than one interface.*/
			mreq6.ipv6mr_interface = 0;

			if (setsockopt (se->fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
						&loop, sizeof (loop)) == -1)
			{
				syslog (LOG_ERR, "setsockopt: %s", strerror (errno));
				return (-1);
			}

			if (setsockopt (se->fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
						&mreq, sizeof (mreq)) == -1)
			{
				syslog (LOG_ERR, "setsockopt: %s", strerror (errno));
				return (-1);
			}
		}
	}

	return (0);
}

int network_create_socket (const char *node, const char *service)
{
	sockent_t *socklist_tail;

	struct addrinfo  ai_hints;
	struct addrinfo *ai_list, *ai_ptr;
	int              ai_return;

	int num_added = 0;

	DBG ("node = %s, service = %s", node, service);

	if (operating_mode == MODE_LOCAL)
		return (-1);

	socklist_tail = socklist_head;
	while ((socklist_tail != NULL) && (socklist_tail->next != NULL))
		socklist_tail = socklist_tail->next;

	memset (&ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_flags    = AI_PASSIVE | AI_ADDRCONFIG;
	ai_hints.ai_family   = PF_UNSPEC;
	ai_hints.ai_socktype = SOCK_DGRAM;
	ai_hints.ai_protocol = IPPROTO_UDP; /* XXX is this right here?!? */

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
		sockent_t *se;

		if ((se = (sockent_t *) malloc (sizeof (sockent_t))) == NULL)
		{
			syslog (LOG_EMERG, "malloc: %s", strerror (errno));
			continue;
		}

		if ((se->addr = (struct sockaddr_storage *) malloc (sizeof (struct sockaddr_storage))) == NULL)
		{
			syslog (LOG_EMERG, "malloc: %s", strerror (errno));
			free (se);
			continue;
		}

		assert (sizeof (struct sockaddr_storage) >= ai_ptr->addrlen);
		memset (se->addr, '\0', sizeof (struct sockaddr_storage));
		memcpy (se->addr, ai_ptr->ai_addr, ai_ptr->addrlen);
		se->addrlen = ai_ptr->addrlen;

		se->fd   = socket (ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
		se->next = NULL;

		if (se->fd == -1)
		{
			syslog (LOG_ERR, "socket: %s", strerror (errno));
			free (se->addr);
			free (se);
			continue;
		}

		if (operating_mode == MODE_SERVER)
			if (network_bind_socket (se->fd, ai_ptr, se->addr) != 0)
			{
				free (se->addr);
				free (se);
				continue;
			}

		if (socklist_tail == NULL)
		{
			socklist_head = socklist_tail = se;
		}
		else
		{
			socklist_tail->next = se;
			socklist_tail = se;
		}

		num_added++;

		/* We don't open more than one write-socket per node/service pair.. */
		if (operating_mode == MODE_CLIENT)
			break;
	}

	freeaddrinfo (ai_list);

	return (num_added);
}

static int network_get_listen_socket (void)
{
	int    fd;
	int    max_fd;

	fd_set readfds;
	sockent_t *se;

	while (1)
	{
		FD_ZERO (&readfds);
		max_fd = -1;
		for (se = socklist_head; se != NULL; se = se->next)
		{
			FD_SET (se->fd, &readfds);
			if (se->fd >= max_fd)
				max_fd = se->fd + 1;
		}

		if (max_fd == -1)
		{
			syslog (LOG_WARNING, "No listen sockets found!");
			return (-1);
		}

		status = select (max_fd, &readfds, NULL, NULL, NULL);

		if ((status == -1) && (errno == EINTR))
			continue;
		else if (status == -1)
		{
			syslog (LOG_ERR, "select: %s", strerror (errno));
			return (-1);
		}
		else
			break;
	} /* while (true) */

	fd = -1;
	for (se = socklist_head; se != NULL; se = se->next)
		if (FD_ISSET (se->fd, &readfds))
		{
			fd = se->fd;
			break;
		}

	if (fd == -1)
		syslog (LOG_WARNING, "No socket ready..?");

	DBG ("fd = %i", fd);
	return (fd);
}

int network_receive (char **host, char **type, char **inst, char **value)
{
	int fd;
	char buffer[BUFF_SIZE];

	struct sockaddr_storage addr;
	int status;

	char *fields[4];

	assert (operating_mode == MODE_SERVER);

	*host  = NULL;
	*type  = NULL;
	*inst  = NULL;
	*value = NULL;

	if ((fd = network_get_listen_socket ()) < 0)
		return (-1);

	if (recvfrom (fd, buffer, BUFF_SIZE, 0, (struct sockaddr *) &addr, sizeof (addr)) == -1)
	{
		syslog (LOG_ERR, "recvfrom: %s", strerror (errno));
		return (-1);
	}

	if ((*host = (char *) malloc (BUFF_SIZE)) == NULL)
	{
		syslog (LOG_EMERG, "malloc: %s", strerror (errno));
		return (-1);
	}

	status = getnameinfo ((struct sockaddr *) &addr, sizeof (addr),
			*host, BUFF_SIZE, NULL, 0, 0);
	if (status != 0)
	{
		free (*host); *host = NULL;
		syslog (LOG_ERR, "getnameinfo: %s",
				status == EAI_SYSTEM ? strerror (errno) : gai_strerror (status));
		return (-1);
	}

	if (strsplit (buffer, fields, 4) != 3)
	{
		syslog (LOG_WARNING, "Invalid message from `%s'", *host);
		free (*host); *host = NULL;
		return (-1);
	}

	if ((*type = strdup (fields[0])) == NULL)
	{
		syslog (LOG_EMERG, "strdup: %s", strerror ());
		free (*host); *host = NULL;
		return (-1);
	}

	if ((*inst = strdup (fields[1])) == NULL)
	{
		syslog (LOG_EMERG, "strdup: %s", strerror ());
		free (*host); *host = NULL;
		free (*type); *type = NULL;
		return (-1);
	}

	if ((*value = strdup (fields[2])) == NULL)
	{
		syslog (LOG_EMERG, "strdup: %s", strerror ());
		free (*host); *host = NULL;
		free (*type); *type = NULL;
		free (*inst); *inst = NULL;
		return (-1);
	}

	DBG ("host = %s, type = %s, inst = %s, value = %s",
			*host, *type, *inst, *value);

	return (0);
}

int network_send (char *type, char *inst, char *value)
{
	char buf[BUFF_SIZE];
	int buflen;

	sockent_t *se;

	int ret;
	int status;

	DBG ("type = %s, inst = %s, value = %s", type, inst, value);

	assert (operating_mode == MODE_CLIENT);

	buflen = snprintf (buf, BUFF_SIZE, "%s %s %s", type, inst, value);
	if ((buflen >= BUFF_SIZE) || (buflen < 1))
	{
		syslog (LOG_WARNING, "network_send: snprintf failed..");
		return (-1);
	}
	buf[buflen] = '\0';
	buflen++;

	ret = 0;
	for (se = socklist_head; se != NULL; se = se->next)
	{
		DBG ("fd = %i", se->fd);

		while (1)
		{
			status = sendto (se->fd, buf, buflen, 0,
					(struct sockaddr *) se->addr, se->addrlen);

			if (status == -1)
			{
				if (errno == EINTR)
				{
					DBG ("sendto was interrupted");
					continue;
				}
				else
				{
					syslog (LOG_ERR, "sendto: %s", strerror (errno));
					break;
				}
			}
			else if (ret >= 0)
				ret++;
			break;
		}
	}

	if (ret == 0)
		syslog (LOG_WARNING, "Message wasn't sent to anybody..");

	return (ret);
}
