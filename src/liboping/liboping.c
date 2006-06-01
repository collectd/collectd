/**
 * Object oriented C module to send ICMP and ICMPv6 `echo's.
 * Copyright (C) 2006  Florian octo Forster <octo at verplant.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if STDC_HEADERS
# include <stdlib.h>
# include <stdio.h>
# include <string.h>
# include <errno.h>
# include <assert.h>
#else
# error "You don't have the standard C99 header files installed"
#endif /* STDC_HEADERS */

#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_FCNTL_H
# include <fcntl.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#endif

#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#if HAVE_NETDB_H
# include <netdb.h>
#endif

#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
#if HAVE_NETINET_IP_ICMP_H
# include <netinet/ip_icmp.h>
#endif
#ifdef HAVE_NETINET_IP_VAR_H
# include <netinet/ip_var.h>
#endif
#if HAVE_NETINET_IP6_H
# include <netinet/ip6.h>
#endif
#if HAVE_NETINET_ICMP6_H
# include <netinet/icmp6.h>
#endif

#include "liboping.h"

#if DEBUG
# define dprintf(...) printf ("%s[%4i]: %-20s: ", __FILE__, __LINE__, __FUNCTION__); printf (__VA_ARGS__)
#else
# define dprintf(...) /**/
#endif

#define PING_DATA "Florian Forster <octo@verplant.org> http://verplant.org/"

/*
 * private (static) functions
 */
static int ping_timeval_add (struct timeval *tv1, struct timeval *tv2,
		struct timeval *res)
{
	res->tv_sec  = tv1->tv_sec  + tv2->tv_sec;
	res->tv_usec = tv1->tv_usec + tv2->tv_usec;

	while (res->tv_usec > 1000000)
	{
		res->tv_usec -= 1000000;
		res->tv_sec++;
	}

	return (0);
}

static int ping_timeval_sub (struct timeval *tv1, struct timeval *tv2,
		struct timeval *res)
{

	if ((tv1->tv_sec < tv2->tv_sec)
			|| ((tv1->tv_sec == tv2->tv_sec)
				&& (tv1->tv_usec < tv2->tv_usec)))
		return (-1);

	res->tv_sec  = tv1->tv_sec  - tv2->tv_sec;
	res->tv_usec = tv1->tv_usec - tv2->tv_usec;

	assert ((res->tv_sec > 0) || ((res->tv_sec == 0) && (res->tv_usec > 0)));

	while (res->tv_usec < 0)
	{
		res->tv_usec += 1000000;
		res->tv_sec--;
	}

	return (0);
}

static uint16_t ping_icmp4_checksum (char *buf, size_t len)
{
	uint32_t sum = 0;
	uint16_t ret = 0;

	uint16_t *ptr;

	for (ptr = (uint16_t *) buf; len > 1; ptr++, len -= 2)
		sum += *ptr;

	if (len == 1)
	{
		*(char *) &ret = *(char *) ptr;
		sum += ret;
	}

	/* Do this twice to get all possible carries.. */
	sum = (sum >> 16) + (sum & 0xFFFF);
	sum = (sum >> 16) + (sum & 0xFFFF);

	ret = ~sum;

	return (ret);
}

static pinghost_t *ping_receive_ipv4 (pinghost_t *ph, char *buffer, size_t buffer_len)
{
	struct ip *ip_hdr;
	struct icmp *icmp_hdr;

	size_t ip_hdr_len;

	uint16_t recv_checksum;
	uint16_t calc_checksum;

	uint16_t ident;
	uint16_t seq;

	pinghost_t *ptr;

	if (buffer_len < sizeof (struct ip))
		return (NULL);

	ip_hdr     = (struct ip *) buffer;
	ip_hdr_len = ip_hdr->ip_hl << 2;

	if (buffer_len < ip_hdr_len)
		return (NULL);

	buffer     += ip_hdr_len;
	buffer_len -= ip_hdr_len;

	if (buffer_len < sizeof (struct icmp))
		return (NULL);

	icmp_hdr = (struct icmp *) buffer;
	buffer     += sizeof (struct icmp);
	buffer_len -= sizeof (struct icmp);

	if (icmp_hdr->icmp_type != ICMP_ECHOREPLY)
	{
		dprintf ("Unexpected ICMP type: %i\n", icmp_hdr->icmp_type);
		return (NULL);
	}

	recv_checksum = icmp_hdr->icmp_cksum;
	icmp_hdr->icmp_cksum = 0;
	calc_checksum = ping_icmp4_checksum ((char *) icmp_hdr,
			sizeof (struct icmp) + buffer_len);

	if (recv_checksum != calc_checksum)
	{
		dprintf ("Checksum missmatch: Got 0x%04x, calculated 0x%04x\n",
				recv_checksum, calc_checksum);
		return (NULL);
	}

	ident = ntohs (icmp_hdr->icmp_id);
	seq   = ntohs (icmp_hdr->icmp_seq);

	for (ptr = ph; ptr != NULL; ptr = ptr->next)
	{
		dprintf ("hostname = %s, ident = 0x%04x, seq = %i\n",
				ptr->hostname, ptr->ident, ((ptr->sequence - 1) & 0xFFFF));

		if (ptr->addrfamily != AF_INET)
			continue;

		if (!timerisset (ptr->timer))
			continue;

		if (ptr->ident != ident)
			continue;

		if (((ptr->sequence - 1) & 0xFFFF) != seq)
			continue;

		dprintf ("Match found: hostname = %s, ident = 0x%04x, seq = %i\n",
				ptr->hostname, ident, seq);

		break;
	}

	if (ptr == NULL)
	{
		dprintf ("No match found for ident = 0x%04x, seq = %i\n",
				ident, seq);
	}

	return (ptr);
}

static pinghost_t *ping_receive_ipv6 (pinghost_t *ph, char *buffer, size_t buffer_len)
{
	struct icmp6_hdr *icmp_hdr;

	uint16_t ident;
	uint16_t seq;

	pinghost_t *ptr;

	if (buffer_len < sizeof (struct icmp6_hdr))
		return (NULL);

	icmp_hdr = (struct icmp6_hdr *) buffer;
	buffer     += sizeof (struct icmp);
	buffer_len -= sizeof (struct icmp);

	if (icmp_hdr->icmp6_type != ICMP6_ECHO_REPLY)
	{
		dprintf ("Unexpected ICMP type: %02x\n", icmp_hdr->icmp6_type);
		return (NULL);
	}

	if (icmp_hdr->icmp6_code != 0)
	{
		dprintf ("Unexpected ICMP code: %02x\n", icmp_hdr->icmp6_code);
		return (NULL);
	}

	ident = ntohs (icmp_hdr->icmp6_id);
	seq   = ntohs (icmp_hdr->icmp6_seq);

	for (ptr = ph; ptr != NULL; ptr = ptr->next)
	{
		dprintf ("hostname = %s, ident = 0x%04x, seq = %i\n",
				ptr->hostname, ptr->ident, ((ptr->sequence - 1) & 0xFFFF));

		if (ptr->addrfamily != AF_INET6)
			continue;

		if (!timerisset (ptr->timer))
			continue;

		if (ptr->ident != ident)
			continue;

		if (((ptr->sequence - 1) & 0xFFFF) != seq)
			continue;

		dprintf ("Match found: hostname = %s, ident = 0x%04x, seq = %i\n",
				ptr->hostname, ident, seq);

		break;
	}

	if (ptr == NULL)
	{
		dprintf ("No match found for ident = 0x%04x, seq = %i\n",
				ident, seq);
	}

	return (ptr);
}

static int ping_receive_one (int fd, pinghost_t *ph, struct timeval *now)
{
	char   buffer[4096];
	size_t buffer_len;

	struct timeval diff;

	pinghost_t *host = NULL;

	struct sockaddr_storage sa;
	socklen_t               sa_len;

	sa_len = sizeof (sa);

	buffer_len = recvfrom (fd, buffer, sizeof (buffer), 0,
			(struct sockaddr *) &sa, &sa_len);
	if (buffer_len == -1)
	{
		dprintf ("recvfrom: %s\n", strerror (errno));
		return (-1);
	}

	dprintf ("Read %i bytes from fd = %i\n", buffer_len, fd);

	if (sa.ss_family == AF_INET)
	{
		if ((host = ping_receive_ipv4 (ph, buffer, buffer_len)) == NULL)
			return (-1);
	}
	else if (sa.ss_family == AF_INET6)
	{
		if ((host = ping_receive_ipv6 (ph, buffer, buffer_len)) == NULL)
			return (-1);
	}

	dprintf ("rcvd: %12i.%06i\n",
			(int) now->tv_sec,
			(int) now->tv_usec);
	dprintf ("sent: %12i.%06i\n",
			(int) host->timer->tv_sec,
			(int) host->timer->tv_usec);

	if (ping_timeval_sub (now, host->timer, &diff) < 0)
	{
		timerclear (host->timer);
		return (-1);
	}

	dprintf ("diff: %12i.%06i\n",
			(int) diff.tv_sec,
			(int) diff.tv_usec);

	host->latency  = ((double) diff.tv_usec) / 1000.0;
	host->latency += ((double) diff.tv_sec)  * 1000.0;

	timerclear (host->timer);

	return (0);
}

static int ping_receive_all (pingobj_t *obj)
{
	fd_set readfds;
	int num_readfds;
	int max_readfds;

	pinghost_t *ph;
	pinghost_t *ptr;

	struct timeval endtime;
	struct timeval nowtime;
	struct timeval timeout;
	int status;

	int ret;

	ph = obj->head;
	ret = 0;

	for (ptr = ph; ptr != NULL; ptr = ptr->next)
		ptr->latency = -1.0;

	if (gettimeofday (&nowtime, NULL) == -1)
		return (-1);

	/* Set up timeout */
	timeout.tv_sec = (time_t) obj->timeout;
	timeout.tv_usec = (suseconds_t) (1000000 * (obj->timeout - ((double) timeout.tv_sec)));

	dprintf ("Set timeout to %i.%06i seconds\n",
			(int) timeout.tv_sec,
			(int) timeout.tv_usec);

	ping_timeval_add (&nowtime, &timeout, &endtime);

	while (1)
	{
		FD_ZERO (&readfds);
		num_readfds =  0;
		max_readfds = -1;

		for (ptr = ph; ptr != NULL; ptr = ptr->next)
		{
			if (!timerisset (ptr->timer))
				continue;

			FD_SET (ptr->fd, &readfds);
			num_readfds++;

			if (max_readfds < ptr->fd)
				max_readfds = ptr->fd;
		}

		if (num_readfds == 0)
			break;

		if (gettimeofday (&nowtime, NULL) == -1)
			return (-1);

		if (ping_timeval_sub (&endtime, &nowtime, &timeout) == -1)
			break;

		dprintf ("Waiting on %i sockets for %i.%06i seconds\n", num_readfds,
				(int) timeout.tv_sec,
				(int) timeout.tv_usec);

		status = select (max_readfds + 1, &readfds, NULL, NULL, &timeout);

		if (gettimeofday (&nowtime, NULL) == -1)
			return (-1);
		
		if ((status == -1) && (errno == EINTR))
		{
			dprintf ("select was interrupted by signal..\n");
			continue;
		}
		else if (status < 0)
		{
			dprintf ("select: %s\n", strerror (errno));
			break;
		}
		else if (status == 0)
		{
			dprintf ("select timed out\n");
			break;
		}

		for (ptr = ph; ptr != NULL; ptr = ptr->next)
		{
			if (FD_ISSET (ptr->fd, &readfds))
				if (ping_receive_one (ptr->fd, ph, &nowtime) == 0)
					ret++;
		}
	} /* while (1) */
	
	return (ret);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Sending functions:                                                        *
 *                                                                           *
 * ping_send_all                                                             *
 * +-> ping_send_one_ipv4                                                    *
 * `-> ping_send_one_ipv6                                                    *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
ssize_t ping_sendto (pinghost_t *ph, const void *buf, size_t buflen)
{
	ssize_t ret;

	if (gettimeofday (ph->timer, NULL) == -1)
	{
		timerclear (ph->timer);
		return (-1);
	}

	ret = sendto (ph->fd, buf, buflen, 0,
			(struct sockaddr *) ph->addr, ph->addrlen);

	return (ret);
}

static int ping_send_one_ipv4 (pinghost_t *ph)
{
	struct icmp *icmp4;
	int status;

	char buf[4096];
	int  buflen;

	char *data;
	int   datalen;

	dprintf ("ph->hostname = %s\n", ph->hostname);

	memset (buf, '\0', sizeof (buf));
	icmp4 = (struct icmp *) buf;
	data  = (char *) (icmp4 + 1);

	icmp4->icmp_type  = ICMP_ECHO;
	icmp4->icmp_code  = 0;
	icmp4->icmp_cksum = 0;
	icmp4->icmp_id    = htons (ph->ident);
	icmp4->icmp_seq   = htons (ph->sequence);

	strcpy (data, PING_DATA);
	datalen = strlen (data);

	buflen = datalen + sizeof (struct icmp);

	icmp4->icmp_cksum = ping_icmp4_checksum (buf, buflen);

	dprintf ("Sending ICMPv4 package with ID 0x%04x\n", ph->ident);

	status = ping_sendto (ph, buf, buflen);
	if (status < 0)
	{
		perror ("ping_sendto");
		return (-1);
	}

	dprintf ("sendto: status = %i\n", status);

	return (0);
}

static int ping_send_one_ipv6 (pinghost_t *ph)
{
	struct icmp6_hdr *icmp6;
	int status;

	char buf[4096];
	int  buflen;

	char *data;
	int   datalen;

	dprintf ("ph->hostname = %s\n", ph->hostname);

	memset (buf, '\0', sizeof (buf));
	icmp6 = (struct icmp6_hdr *) buf;
	data  = (char *) (icmp6 + 1);

	icmp6->icmp6_type  = ICMP6_ECHO_REQUEST;
	icmp6->icmp6_code  = 0;
	/* The checksum will be calculated by the TCP/IP stack.  */
	icmp6->icmp6_cksum = 0;
	icmp6->icmp6_id    = htons (ph->ident);
	icmp6->icmp6_seq   = htons (ph->sequence);

	strcpy (data, PING_DATA);
	datalen = strlen (data);

	buflen = datalen + sizeof (struct icmp6_hdr);

	dprintf ("Sending ICMPv6 package with ID 0x%04x\n", ph->ident);

	status = ping_sendto (ph, buf, buflen);
	if (status < 0)
	{
		perror ("ping_sendto");
		return (-1);
	}

	dprintf ("sendto: status = %i\n", status);

	return (0);
}

static int ping_send_all (pinghost_t *ph)
{
	pinghost_t *ptr;

	for (ptr = ph; ptr != NULL; ptr = ptr->next)
	{
		/* start timer.. The GNU `ping6' starts the timer before
		 * sending the packet, so I will do that too */
		if (gettimeofday (ptr->timer, NULL) == -1)
		{
			dprintf ("gettimeofday: %s\n", strerror (errno));
			timerclear (ptr->timer);
			continue;
		}
		else
		{
			dprintf ("timer set for hostname = %s\n", ptr->hostname);
		}

		if (ptr->addrfamily == AF_INET6)
		{	
			dprintf ("Sending ICMPv6 echo request to `%s'\n", ptr->hostname);
			if (ping_send_one_ipv6 (ptr) != 0)
			{
				timerclear (ptr->timer);
				continue;
			}
		}
		else if (ptr->addrfamily == AF_INET)
		{
			dprintf ("Sending ICMPv4 echo request to `%s'\n", ptr->hostname);
			if (ping_send_one_ipv4 (ptr) != 0)
			{
				timerclear (ptr->timer);
				continue;
			}
		}
		else /* this should not happen */
		{
			dprintf ("Unknown address family: %i\n", ptr->addrfamily);
			timerclear (ptr->timer);
			continue;
		}

		ptr->sequence++;
	}

	/* FIXME */
	return (0);
}

/*
 * Set the TTL of a socket protocol independently.
 */
static int ping_set_ttl (pinghost_t *ph, int ttl)
{
	int ret = -2;

	if (ph->addrfamily == AF_INET)
	{
		ret = setsockopt (ph->fd, IPPROTO_IP, IP_TTL, &ttl, sizeof (ttl));
	}
	else if (ph->addrfamily == AF_INET6)
	{
		ret = setsockopt (ph->fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof (ttl));
	}

	return (ret);
}

static int ping_get_ident (void)
{
	int fd;
	static int did_seed = 0;

	int retval;

	if (did_seed == 0)
	{
		if ((fd = open ("/dev/urandom", O_RDONLY)) != -1)
		{
			unsigned int seed;

			if (read (fd, &seed, sizeof (seed)) != -1)
			{
				did_seed = 1;
				dprintf ("Random seed: %i\n", seed);
				srandom (seed);
			}

			close (fd);
		}
		else
		{
			dprintf ("open (/dev/urandom): %s\n", strerror (errno));
		}
	}

	retval = (int) random ();

	dprintf ("Random number: %i\n", retval);
	
	return (retval);
}

static pinghost_t *ping_alloc (void)
{
	pinghost_t *ph;
	size_t      ph_size;

	ph_size = sizeof (pinghost_t)
		+ sizeof (struct sockaddr_storage)
		+ sizeof (struct timeval);

	ph = (pinghost_t *) malloc (ph_size);
	if (ph == NULL)
		return (NULL);

	memset (ph, '\0', ph_size);

	ph->timer   = (struct timeval *) (ph + 1);
	ph->addr    = (struct sockaddr_storage *) (ph->timer + 1);

	ph->addrlen = sizeof (struct sockaddr_storage);
	ph->latency = -1.0;
	ph->ident   = ping_get_ident () & 0xFFFF;

	return (ph);
}

static void ping_free (pinghost_t *ph)
{
	if (ph->hostname != NULL)
		free (ph->hostname);

	free (ph);
}

/*
 * public methods
 */
pingobj_t *ping_construct (void)
{
	pingobj_t *obj;

	if ((obj = (pingobj_t *) malloc (sizeof (pingobj_t))) == NULL)
		return (NULL);
	memset (obj, '\0', sizeof (pingobj_t));

	obj->timeout    = PING_DEF_TIMEOUT;
	obj->ttl        = PING_DEF_TTL;
	obj->addrfamily = PING_DEF_AF;

	return (obj);
}

void ping_destroy (pingobj_t *obj)
{
	pinghost_t *current;
	pinghost_t *next;

	current = obj->head;
	next = NULL;

	while (current != NULL)
	{
		next = current->next;
		ping_free (current);
		current = next;
	}

	free (obj);

	return;
}

int ping_setopt (pingobj_t *obj, int option, void *value)
{
	int ret = 0;

	switch (option)
	{
		case PING_OPT_TIMEOUT:
			obj->timeout = *((double *) value);
			if (obj->timeout < 0.0)
			{
				obj->timeout = PING_DEF_TIMEOUT;
				ret = -1;
			}
			break;

		case PING_OPT_TTL:
			obj->ttl = *((int *) value);
			if ((obj->ttl < 1) || (obj->ttl > 255))
			{
				obj->ttl = PING_DEF_TTL;
				ret = -1;
			}
			break;

		case PING_OPT_AF:
			obj->addrfamily = *((int *) value);
			if ((obj->addrfamily != AF_UNSPEC)
					&& (obj->addrfamily != AF_INET)
					&& (obj->addrfamily != AF_INET6))
			{
				obj->addrfamily = PING_DEF_AF;
				ret = -1;
			}
			break;

		default:
			ret = -2;
	} /* switch (option) */

	return (ret);
} /* int ping_setopt */


int ping_send (pingobj_t *obj)
{
	int ret;

	if (ping_send_all (obj->head) < 0)
		return (-1);

	if ((ret = ping_receive_all (obj)) < 0)
		return (-2);

	return (ret);
}

static pinghost_t *ping_host_search (pinghost_t *ph, const char *host)
{
	while (ph != NULL)
	{
		if (strcasecmp (ph->hostname, host) == 0)
			break;

		ph = ph->next;
	}

	return (ph);
}

int ping_host_add (pingobj_t *obj, const char *host)
{
	pinghost_t *ph;

	struct sockaddr_storage sockaddr;
	socklen_t               sockaddr_len;

	struct addrinfo  ai_hints;
	struct addrinfo *ai_list, *ai_ptr;
	int              ai_return;

	dprintf ("host = %s\n", host);

	if (ping_host_search (obj->head, host) != NULL)
		return (0);

	memset (&ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_flags     = 0;
#ifdef AI_ADDRCONFIG
	ai_hints.ai_flags    |= AI_ADDRCONFIG;
#endif
	ai_hints.ai_family    = obj->addrfamily;
	ai_hints.ai_socktype  = SOCK_RAW;

	if ((ph = ping_alloc ()) == NULL)
	{
		dprintf ("Out of memory!\n");
		return (-1);
	}

	if ((ph->hostname = strdup (host)) == NULL)
	{
		dprintf ("Out of memory!\n");
		ping_free (ph);
		return (-1);
	}

	if ((ai_return = getaddrinfo (host, NULL, &ai_hints, &ai_list)) != 0)
	{
		dprintf ("getaddrinfo failed\n");
		ping_free (ph);
		return (-1);
	}

	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		ph->fd = -1;

		sockaddr_len = sizeof (sockaddr);
		memset (&sockaddr, '\0', sockaddr_len);

		if (ai_ptr->ai_family == AF_INET)
		{
			struct sockaddr_in *si;

			si = (struct sockaddr_in *) &sockaddr;
			si->sin_family = AF_INET;
			si->sin_port   = htons (ph->ident);
			si->sin_addr.s_addr = htonl (INADDR_ANY);

			ai_ptr->ai_protocol = IPPROTO_ICMP;
		}
		else if (ai_ptr->ai_family == AF_INET6)
		{
			struct sockaddr_in6 *si;

			si = (struct sockaddr_in6 *) &sockaddr;
			si->sin6_family = AF_INET6;
			si->sin6_port   = htons (ph->ident);
			si->sin6_addr   = in6addr_any;

			ai_ptr->ai_protocol = IPPROTO_ICMPV6;
		}
		else
		{
			dprintf ("Unknown `ai_family': %i\n", ai_ptr->ai_family);
			continue;
		}

		ph->fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
		if (ph->fd == -1)
		{
			dprintf ("socket: %s\n", strerror (errno));
			continue;
		}

#if 0
		if (bind (ph->fd, (struct sockaddr *) &sockaddr, sockaddr_len) == -1)
		{
			dprintf ("bind: %s\n", strerror (errno));
			close (ph->fd);
			ph->fd = -1;
			continue;
		}
#endif

		assert (sizeof (struct sockaddr_storage) >= ai_ptr->ai_addrlen);
		memset (ph->addr, '\0', sizeof (struct sockaddr_storage));
		memcpy (ph->addr, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
		ph->addrlen = ai_ptr->ai_addrlen;
		ph->addrfamily = ai_ptr->ai_family;

		break;
	}

	freeaddrinfo (ai_list);

	if (ph->fd < 0)
	{
		free (ph->hostname);
		free (ph);
		return (-1);
	}

	ph->next  = obj->head;
	obj->head = ph;

	ping_set_ttl (ph, obj->ttl);

	return (0);
}

int ping_host_remove (pingobj_t *obj, const char *host)
{
	pinghost_t *pre, *cur;

	pre = NULL;
	cur = obj->head;

	while (cur != NULL)
	{
		if (strcasecmp (host, cur->hostname))
			break;

		pre = cur;
		cur = cur->next;
	}

	if (cur == NULL)
		return (-1);

	if (pre == NULL)
		obj->head = cur->next;
	else
		pre->next = cur->next;
	
	if (cur->fd >= 0)
		close (cur->fd);

	ping_free (cur);

	return (0);
}

pingobj_iter_t *ping_iterator_get (pingobj_t *obj)
{
	return ((pingobj_iter_t *) obj->head);
}

pingobj_iter_t *ping_iterator_next (pingobj_iter_t *iter)
{
	return ((pingobj_iter_t *) iter->next);
}

const char *ping_iterator_get_host (pingobj_iter_t *iter)
{
	return (iter->hostname);
}

double ping_iterator_get_latency (pingobj_iter_t *iter)
{
	return (iter->latency);
}
