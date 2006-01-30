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
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <errno.h>

#include "network.h"
#include "common.h"
#include "configfile.h"
#include "utils_debug.h"

/* 1500 - 40 - 8  =  Ethernet packet - IPv6 header - UDP header */
/* #define BUFF_SIZE 1452 */

#define BUFF_SIZE 4096

#ifdef HAVE_LIBRRD
extern int operating_mode;
#else
static int operating_mode = MODE_CLIENT;
#endif

typedef struct sockent
{
	int                      fd;
	int                      mode;
	struct sockaddr_storage *addr;
	socklen_t                addrlen;
	struct sockent          *next;
} sockent_t;

static sockent_t *socklist_head = NULL;

static int network_bind_socket (const sockent_t *se, const struct addrinfo *ai)
{
	int loop = 1;

	char *ttl_str;
	int   ttl_int;

	ttl_str = cf_get_option ("MulticastTTL", NULL);
	ttl_int = 0;
	if (ttl_str != NULL)
		ttl_int = atoi (ttl_str);
	if ((ttl_int < 1) || (ttl_int > 255))
		ttl_int = NET_DEFAULT_MC_TTL;

	DBG ("fd = %i; calling `bind'", se->fd);

	if (bind (se->fd, ai->ai_addr, ai->ai_addrlen) == -1)
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

			DBG ("fd = %i; IPv4 multicast address found", se->fd);

			mreq.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
			mreq.imr_interface.s_addr = htonl (INADDR_ANY);

			if (setsockopt (se->fd, IPPROTO_IP, IP_MULTICAST_LOOP,
						&loop, sizeof (loop)) == -1)
			{
				syslog (LOG_ERR, "setsockopt: %s", strerror (errno));
				return (-1);
			}

			/* IP_MULTICAST_TTL */
			if (setsockopt (se->fd, IPPROTO_IP, IP_MULTICAST_TTL,
						&ttl_int, sizeof (ttl_int)) == -1)
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

			DBG ("fd = %i; IPv6 multicast address found", se->fd);

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
			mreq.ipv6mr_interface = 0;

			if (setsockopt (se->fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
						&loop, sizeof (loop)) == -1)
			{
				syslog (LOG_ERR, "setsockopt: %s", strerror (errno));
				return (-1);
			}

			if (setsockopt (se->fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
						&ttl_int, sizeof (ttl_int)) == -1)
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

		assert (sizeof (struct sockaddr_storage) >= ai_ptr->ai_addrlen);
		memset (se->addr, '\0', sizeof (struct sockaddr_storage));
		memcpy (se->addr, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
		se->addrlen = ai_ptr->ai_addrlen;

		se->mode = operating_mode;
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
			if (network_bind_socket (se, ai_ptr) != 0)
			{
				free (se->addr);
				free (se);
				continue;
			}

		if (socklist_tail == NULL)
		{
			socklist_head = se;
			socklist_tail = se;
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

static int network_connect_default (void)
{
	int ret;

	if (socklist_head != NULL)
		return (0);

	DBG ("socklist_head is NULL");

	ret = 0;

	if (network_create_socket (NET_DEFAULT_V6_ADDR, NET_DEFAULT_PORT) > 0)
		ret++;

	/* Don't use IPv4 and IPv6 in parallel by default.. */
	if ((operating_mode == MODE_CLIENT) && (ret != 0))
		return (ret);

	if (network_create_socket (NET_DEFAULT_V4_ADDR, NET_DEFAULT_PORT) > 0)
		ret++;

	if (ret == 0)
		ret = -1;

	return (ret);
}

static int network_get_listen_socket (void)
{
	int fd;
	int max_fd;
	int status;

	fd_set readfds;
	sockent_t *se;

	if (socklist_head == NULL)
		network_connect_default ();

	FD_ZERO (&readfds);
	max_fd = -1;
	for (se = socklist_head; se != NULL; se = se->next)
	{
		if (se->mode != operating_mode)
			continue;

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

	if (status == -1)
	{
		if (errno != EINTR)
			syslog (LOG_ERR, "select: %s", strerror (errno));
		return (-1);
	}

	fd = -1;
	for (se = socklist_head; se != NULL; se = se->next)
	{
		if (se->mode != operating_mode)
			continue;

		if (FD_ISSET (se->fd, &readfds))
		{
			fd = se->fd;
			break;
		}
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
	socklen_t               addrlen;
	int status;

	char *fields[4];

	assert (operating_mode == MODE_SERVER);

	*host  = NULL;
	*type  = NULL;
	*inst  = NULL;
	*value = NULL;

	if ((fd = network_get_listen_socket ()) < 0)
		return (-1);

	addrlen = sizeof (addr);
	if (recvfrom (fd, buffer, BUFF_SIZE, 0, (struct sockaddr *) &addr, &addrlen) == -1)
	{
		syslog (LOG_ERR, "recvfrom: %s", strerror (errno));
		return (-1);
	}

	if ((*host = (char *) malloc (BUFF_SIZE)) == NULL)
	{
		syslog (LOG_EMERG, "malloc: %s", strerror (errno));
		return (-1);
	}

	status = getnameinfo ((struct sockaddr *) &addr, addrlen,
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
		syslog (LOG_EMERG, "strdup: %s", strerror (errno));
		free (*host); *host = NULL;
		return (-1);
	}

	if ((*inst = strdup (fields[1])) == NULL)
	{
		syslog (LOG_EMERG, "strdup: %s", strerror (errno));
		free (*host); *host = NULL;
		free (*type); *type = NULL;
		return (-1);
	}

	if ((*value = strdup (fields[2])) == NULL)
	{
		syslog (LOG_EMERG, "strdup: %s", strerror (errno));
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

	if (socklist_head == NULL)
		network_connect_default ();

	ret = 0;
	for (se = socklist_head; se != NULL; se = se->next)
	{
		if (se->mode != operating_mode)
			continue;

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
					ret = -1;
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
