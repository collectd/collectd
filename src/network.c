/**
 * collectd - src/network.c
 * Copyright (C) 2005-2007  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
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
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_avltree.h"

#include "network.h"

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#if HAVE_NETDB_H
# include <netdb.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#if HAVE_POLL_H
# include <poll.h>
#endif

/* 1500 - 40 - 8  =  Ethernet packet - IPv6 header - UDP header */
/* #define BUFF_SIZE 1452 */

#ifndef IPV6_ADD_MEMBERSHIP
# ifdef IPV6_JOIN_GROUP
#  define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
# else
#  error "Neither IP_ADD_MEMBERSHIP nor IPV6_JOIN_GROUP is defined"
# endif
#endif /* !IP_ADD_MEMBERSHIP */

#define BUFF_SIZE 1024

/*
 * Private data types
 */
typedef struct sockent
{
	int                      fd;
	struct sockaddr_storage *addr;
	socklen_t                addrlen;
	struct sockent          *next;
} sockent_t;

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------+-----------------------+-------------------------------+
 * ! Ver.  !                       ! Length                        !
 * +-------+-----------------------+-------------------------------+
 */
struct part_header_s
{
	uint16_t type;
	uint16_t length;
};
typedef struct part_header_s part_header_t;

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------------------------------+-------------------------------+
 * ! Type                          ! Length                        !
 * +-------------------------------+-------------------------------+
 * : (Length - 4) Bytes                                            :
 * +---------------------------------------------------------------+
 */
struct part_string_s
{
	part_header_t *head;
	char *value;
};
typedef struct part_string_s part_string_t;

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------------------------------+-------------------------------+
 * ! Type                          ! Length                        !
 * +-------------------------------+-------------------------------+
 * : (Length - 4 == 2 || 4 || 8) Bytes                             :
 * +---------------------------------------------------------------+
 */
struct part_number_s
{
	part_header_t *head;
	uint64_t *value;
};
typedef struct part_number_s part_number_t;

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------------------------------+-------------------------------+
 * ! Type                          ! Length                        !
 * +-------------------------------+---------------+---------------+
 * ! Num of values                 ! Type0         ! Type1         !
 * +-------------------------------+---------------+---------------+
 * ! Value0                                                        !
 * !                                                               !
 * +---------------------------------------------------------------+
 * ! Value1                                                        !
 * !                                                               !
 * +---------------------------------------------------------------+
 */
struct part_values_s
{
	part_header_t *head;
	uint16_t *num_values;
	uint8_t  *values_types;
	value_t  *values;
};
typedef struct part_values_s part_values_t;

/*
 * Private variables
 */
static const char *config_keys[] =
{
	"CacheFlush",
	"Listen",
	"Server",
	"TimeToLive",
	"Forward"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int network_config_ttl = 0;
static int network_config_forward = 0;

static sockent_t *sending_sockets = NULL;

static struct pollfd *listen_sockets = NULL;
static int listen_sockets_num = 0;
static pthread_t listen_thread = 0;
static int listen_loop = 0;

static char         send_buffer[BUFF_SIZE];
static char        *send_buffer_ptr;
static int          send_buffer_fill;
static value_list_t send_buffer_vl = VALUE_LIST_STATIC;
static char         send_buffer_type[DATA_MAX_NAME_LEN];
static pthread_mutex_t send_buffer_lock = PTHREAD_MUTEX_INITIALIZER;

static c_avl_tree_t      *cache_tree = NULL;
static pthread_mutex_t  cache_lock = PTHREAD_MUTEX_INITIALIZER;
static time_t           cache_flush_last;
static int              cache_flush_interval = 1800;

/*
 * Private functions
 */
static int cache_flush (void)
{
	char **keys = NULL;
	int    keys_num = 0;

	char **tmp;
	int    i;

	char   *key;
	time_t *value;
	c_avl_iterator_t *iter;

	time_t curtime = time (NULL);

	iter = c_avl_get_iterator (cache_tree);
	while (c_avl_iterator_next (iter, (void *) &key, (void *) &value) == 0)
	{
		if ((curtime - *value) <= cache_flush_interval)
			continue;
		tmp = (char **) realloc (keys,
				(keys_num + 1) * sizeof (char *));
		if (tmp == NULL)
		{
			sfree (keys);
			c_avl_iterator_destroy (iter);
			ERROR ("network plugin: cache_flush: realloc"
					" failed.");
			return (-1);
		}
		keys = tmp;
		keys[keys_num] = key;
		keys_num++;
	} /* while (c_avl_iterator_next) */
	c_avl_iterator_destroy (iter);

	for (i = 0; i < keys_num; i++)
	{
		if (c_avl_remove (cache_tree, keys[i], (void *) &key,
					(void *) &value) != 0)
		{
			WARNING ("network plugin: cache_flush: c_avl_remove"
					" (%s) failed.", keys[i]);
			continue;
		}

		sfree (key);
		sfree (value);
	}

	sfree (keys);

	DEBUG ("network plugin: cache_flush: Removed %i %s",
			keys_num, (keys_num == 1) ? "entry" : "entries");
	cache_flush_last = curtime;
	return (0);
} /* int cache_flush */

static int cache_check (const char *type, const value_list_t *vl)
{
	char key[1024];
	time_t *value = NULL;
	int retval = -1;

	if (cache_tree == NULL)
		return (-1);

	if (format_name (key, sizeof (key), vl->host, vl->plugin,
				vl->plugin_instance, type, vl->type_instance))
		return (-1);

	pthread_mutex_lock (&cache_lock);

	if (c_avl_get (cache_tree, key, (void *) &value) == 0)
	{
		if (*value < vl->time)
		{
			*value = vl->time;
			retval = 0;
		}
		else
		{
			DEBUG ("network plugin: cache_check: *value = %i >= vl->time = %i",
					(int) *value, (int) vl->time);
			retval = 1;
		}
	}
	else
	{
		char *key_copy = strdup (key);
		value = malloc (sizeof (time_t));
		if ((key_copy != NULL) && (value != NULL))
		{
			*value = vl->time;
			c_avl_insert (cache_tree, key_copy, value);
			retval = 0;
		}
		else
		{
			sfree (key_copy);
			sfree (value);
		}
	}

	if ((time (NULL) - cache_flush_last) > cache_flush_interval)
		cache_flush ();

	pthread_mutex_unlock (&cache_lock);

	DEBUG ("network plugin: cache_check: key = %s; time = %i; retval = %i",
			key, (int) vl->time, retval);

	return (retval);
} /* int cache_check */

static int write_part_values (char **ret_buffer, int *ret_buffer_len,
		const data_set_t *ds, const value_list_t *vl)
{
	part_values_t pv;
	int i;

	i = 6 + (9 * vl->values_len);
	if (*ret_buffer_len < i)
		return (-1);
	*ret_buffer_len -= i;

	pv.head = (part_header_t *) *ret_buffer;
	pv.num_values = (uint16_t *) (pv.head + 1);
	pv.values_types = (uint8_t *) (pv.num_values + 1);
	pv.values = (value_t *) (pv.values_types + vl->values_len);
	*ret_buffer = (void *) (pv.values + vl->values_len);

	pv.head->type = htons (TYPE_VALUES);
	pv.head->length = htons (6 + (9 * vl->values_len));
	*pv.num_values = htons ((uint16_t) vl->values_len);
	
	for (i = 0; i < vl->values_len; i++)
	{
		if (ds->ds[i].type == DS_TYPE_COUNTER)
		{
			pv.values_types[i] = DS_TYPE_COUNTER;
			pv.values[i].counter = htonll (vl->values[i].counter);
		}
		else
		{
			pv.values_types[i] = DS_TYPE_GAUGE;
			pv.values[i].gauge = vl->values[i].gauge;
		}
	} /* for (values) */

	return (0);
} /* int write_part_values */

static int write_part_number (char **ret_buffer, int *ret_buffer_len,
		int type, uint64_t value)
{
	part_number_t pn;

	if (*ret_buffer_len < 12)
		return (-1);

	pn.head = (part_header_t *) *ret_buffer;
	pn.value = (uint64_t *) (pn.head + 1);

	pn.head->type = htons (type);
	pn.head->length = htons (12);
	*pn.value = htonll (value);

	*ret_buffer = (char *) (pn.value + 1);
	*ret_buffer_len -= 12;

	return (0);
} /* int write_part_number */

static int write_part_string (char **ret_buffer, int *ret_buffer_len,
		int type, const char *str, int str_len)
{
	part_string_t ps;
	int len;

	len = 4 + str_len + 1;
	if (*ret_buffer_len < len)
		return (-1);
	*ret_buffer_len -= len;

	ps.head = (part_header_t *) *ret_buffer;
	ps.value = (char *) (ps.head + 1);

	ps.head->type = htons ((uint16_t) type);
	ps.head->length = htons ((uint16_t) str_len + 5);
	if (str_len > 0)
		memcpy (ps.value, str, str_len);
	ps.value[str_len] = '\0';
	*ret_buffer = (void *) (ps.value + (str_len + 1));

	return (0);
} /* int write_part_string */

static int parse_part_values (void **ret_buffer, int *ret_buffer_len,
		value_t **ret_values, int *ret_num_values)
{
	char *buffer = *ret_buffer;
	int   buffer_len = *ret_buffer_len;
	part_values_t pv;
	int   i;

	uint16_t h_length;
	uint16_t h_type;
	uint16_t h_num;

	if (buffer_len < (15))
	{
		DEBUG ("network plugin: packet is too short: buffer_len = %i",
				buffer_len);
		return (-1);
	}

	pv.head = (part_header_t *) buffer;
	h_length = ntohs (pv.head->length);
	h_type = ntohs (pv.head->type);

	assert (h_type == TYPE_VALUES);

	pv.num_values = (uint16_t *) (pv.head + 1);
	h_num = ntohs (*pv.num_values);

	if (h_num != ((h_length - 6) / 9))
	{
		DEBUG ("`length' and `num of values' don't match");
		return (-1);
	}

	pv.values_types = (uint8_t *) (pv.num_values + 1);
	pv.values = (value_t *) (pv.values_types + h_num);

	for (i = 0; i < h_num; i++)
		if (pv.values_types[i] == DS_TYPE_COUNTER)
			pv.values[i].counter = ntohll (pv.values[i].counter);

	*ret_buffer     = (void *) (pv.values + h_num);
	*ret_buffer_len = buffer_len - h_length;
	*ret_num_values = h_num;
	*ret_values     = pv.values;

	return (0);
} /* int parse_part_values */

static int parse_part_number (void **ret_buffer, int *ret_buffer_len,
		uint64_t *value)
{
	part_number_t pn;
	uint16_t len;

	pn.head = (part_header_t *) *ret_buffer;
	pn.value = (uint64_t *) (pn.head + 1);

	len = ntohs (pn.head->length);
	if (len != 12)
		return (-1);
	if (len > *ret_buffer_len)
		return (-1);
	*value = ntohll (*pn.value);

	*ret_buffer = (void *) (pn.value + 1);
	*ret_buffer_len -= len;

	return (0);
} /* int parse_part_number */

static int parse_part_string (void **ret_buffer, int *ret_buffer_len,
		char *output, int output_len)
{
	char *buffer = *ret_buffer;
	int   buffer_len = *ret_buffer_len;
	part_string_t ps;

	uint16_t h_length;
	uint16_t h_type;

	DEBUG ("network plugin: parse_part_string: ret_buffer = %p;"
			" ret_buffer_len = %i; output = %p; output_len = %i;",
			*ret_buffer, *ret_buffer_len,
			(void *) output, output_len);

	ps.head = (part_header_t *) buffer;

	h_length = ntohs (ps.head->length);
	h_type = ntohs (ps.head->type);

	DEBUG ("network plugin: parse_part_string: length = %hu; type = %hu;",
			h_length, h_type);

	if (buffer_len < h_length)
	{
		DEBUG ("packet is too short");
		return (-1);
	}
	assert ((h_type == TYPE_HOST)
			|| (h_type == TYPE_PLUGIN)
			|| (h_type == TYPE_PLUGIN_INSTANCE)
			|| (h_type == TYPE_TYPE)
			|| (h_type == TYPE_TYPE_INSTANCE));

	ps.value = buffer + 4;
	if (ps.value[h_length - 5] != '\0')
	{
		DEBUG ("String does not end with a nullbyte");
		return (-1);
	}

	if (output_len < (h_length - 4))
	{
		DEBUG ("output buffer is too small");
		return (-1);
	}
	strcpy (output, ps.value);

	DEBUG ("network plugin: parse_part_string: output = %s", output);

	*ret_buffer = (void *) (buffer + h_length);
	*ret_buffer_len = buffer_len - h_length;

	return (0);
} /* int parse_part_string */

static int parse_packet (void *buffer, int buffer_len)
{
	part_header_t *header;
	int status;

	value_list_t vl = VALUE_LIST_INIT;
	char type[DATA_MAX_NAME_LEN];

	DEBUG ("network plugin: parse_packet: buffer = %p; buffer_len = %i;",
			buffer, buffer_len);

	memset (&vl, '\0', sizeof (vl));
	memset (&type, '\0', sizeof (type));
	status = 0;

	while ((status == 0) && (buffer_len > sizeof (part_header_t)))
	{
		header = (part_header_t *) buffer;

		if (ntohs (header->length) > buffer_len)
			break;
		/* Assure that this loop terminates eventually */
		if (ntohs (header->length) < 4)
			break;

		if (ntohs (header->type) == TYPE_VALUES)
		{
			status = parse_part_values (&buffer, &buffer_len,
					&vl.values, &vl.values_len);

			if (status != 0)
			{
				DEBUG ("parse_part_values failed.");
				break;
			}

			if ((vl.time > 0)
					&& (strlen (vl.host) > 0)
					&& (strlen (vl.plugin) > 0)
					&& (strlen (type) > 0)
					&& (cache_check (type, &vl) == 0))
			{
				DEBUG ("network plugin: parse_packet:"
						" dispatching values");
				plugin_dispatch_values (type, &vl);
			}
			else
			{
				DEBUG ("network plugin: parse_packet:"
						" NOT dispatching values");
			}
		}
		else if (ntohs (header->type) == TYPE_TIME)
		{
			uint64_t tmp = 0;
			status = parse_part_number (&buffer, &buffer_len, &tmp);
			if (status == 0)
				vl.time = (time_t) tmp;
		}
		else if (ntohs (header->type) == TYPE_INTERVAL)
		{
			uint64_t tmp = 0;
			status = parse_part_number (&buffer, &buffer_len, &tmp);
			if (status == 0)
				vl.interval = (int) tmp;
		}
		else if (ntohs (header->type) == TYPE_HOST)
		{
			status = parse_part_string (&buffer, &buffer_len,
					vl.host, sizeof (vl.host));
			DEBUG ("network plugin: parse_packet: vl.host = %s", vl.host);
		}
		else if (ntohs (header->type) == TYPE_PLUGIN)
		{
			status = parse_part_string (&buffer, &buffer_len,
					vl.plugin, sizeof (vl.plugin));
			DEBUG ("network plugin: parse_packet: vl.plugin = %s", vl.plugin);
		}
		else if (ntohs (header->type) == TYPE_PLUGIN_INSTANCE)
		{
			status = parse_part_string (&buffer, &buffer_len,
					vl.plugin_instance, sizeof (vl.plugin_instance));
			DEBUG ("network plugin: parse_packet: vl.plugin_instance = %s", vl.plugin_instance);
		}
		else if (ntohs (header->type) == TYPE_TYPE)
		{
			status = parse_part_string (&buffer, &buffer_len,
					type, sizeof (type));
			DEBUG ("network plugin: parse_packet: type = %s", type);
		}
		else if (ntohs (header->type) == TYPE_TYPE_INSTANCE)
		{
			status = parse_part_string (&buffer, &buffer_len,
					vl.type_instance, sizeof (vl.type_instance));
			DEBUG ("network plugin: parse_packet: vl.type_instance = %s", vl.type_instance);
		}
		else
		{
			DEBUG ("network plugin: parse_packet: Unknown part"
					" type: 0x%0hx", ntohs (header->type));
			buffer = ((char *) buffer) + ntohs (header->length);
		}
	} /* while (buffer_len > sizeof (part_header_t)) */

	return (0);
} /* int parse_packet */

static void free_sockent (sockent_t *se)
{
	sockent_t *next;
	while (se != NULL)
	{
		next = se->next;
		free (se->addr);
		free (se);
		se = next;
	}
} /* void free_sockent */

/*
 * int network_set_ttl
 *
 * Set the `IP_MULTICAST_TTL', `IP_TTL', `IPV6_MULTICAST_HOPS' or
 * `IPV6_UNICAST_HOPS', depending on which option is applicable.
 *
 * The `struct addrinfo' is used to destinguish between unicast and multicast
 * sockets.
 */
static int network_set_ttl (const sockent_t *se, const struct addrinfo *ai)
{
	if ((network_config_ttl < 1) || (network_config_ttl > 255))
		return (-1);

	DEBUG ("ttl = %i", network_config_ttl);

	if (ai->ai_family == AF_INET)
	{
		struct sockaddr_in *addr = (struct sockaddr_in *) ai->ai_addr;
		int optname;

		if (IN_MULTICAST (ntohl (addr->sin_addr.s_addr)))
			optname = IP_MULTICAST_TTL;
		else
			optname = IP_TTL;

		if (setsockopt (se->fd, IPPROTO_IP, optname,
					&network_config_ttl,
					sizeof (network_config_ttl)) == -1)
		{
			char errbuf[1024];
			ERROR ("setsockopt: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			return (-1);
		}
	}
	else if (ai->ai_family == AF_INET6)
	{
		/* Useful example: http://gsyc.escet.urjc.es/~eva/IPv6-web/examples/mcast.html */
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) ai->ai_addr;
		int optname;

		if (IN6_IS_ADDR_MULTICAST (&addr->sin6_addr))
			optname = IPV6_MULTICAST_HOPS;
		else
			optname = IPV6_UNICAST_HOPS;

		if (setsockopt (se->fd, IPPROTO_IPV6, optname,
					&network_config_ttl,
					sizeof (network_config_ttl)) == -1)
		{
			char errbuf[1024];
			ERROR ("setsockopt: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			return (-1);
		}
	}

	return (0);
} /* int network_set_ttl */

static int network_bind_socket (const sockent_t *se, const struct addrinfo *ai)
{
	int loop = 0;
	int yes  = 1;

	/* allow multiple sockets to use the same PORT number */
	if (setsockopt(se->fd, SOL_SOCKET, SO_REUSEADDR,
				&yes, sizeof(yes)) == -1) {
                char errbuf[1024];
                ERROR ("setsockopt: %s", 
                                sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	DEBUG ("fd = %i; calling `bind'", se->fd);

	if (bind (se->fd, ai->ai_addr, ai->ai_addrlen) == -1)
	{
		char errbuf[1024];
		ERROR ("bind: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	if (ai->ai_family == AF_INET)
	{
		struct sockaddr_in *addr = (struct sockaddr_in *) ai->ai_addr;
		if (IN_MULTICAST (ntohl (addr->sin_addr.s_addr)))
		{
			struct ip_mreq mreq;

			DEBUG ("fd = %i; IPv4 multicast address found", se->fd);

			mreq.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
			mreq.imr_interface.s_addr = htonl (INADDR_ANY);

			if (setsockopt (se->fd, IPPROTO_IP, IP_MULTICAST_LOOP,
						&loop, sizeof (loop)) == -1)
			{
				char errbuf[1024];
				ERROR ("setsockopt: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}

			if (setsockopt (se->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
						&mreq, sizeof (mreq)) == -1)
			{
				char errbuf[1024];
				ERROR ("setsockopt: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
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

			DEBUG ("fd = %i; IPv6 multicast address found", se->fd);

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
				char errbuf[1024];
				ERROR ("setsockopt: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}

			if (setsockopt (se->fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
						&mreq, sizeof (mreq)) == -1)
			{
				char errbuf[1024];
				ERROR ("setsockopt: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}
		}
	}

	return (0);
} /* int network_bind_socket */

static sockent_t *network_create_socket (const char *node,
		const char *service,
		int listen)
{
	struct addrinfo  ai_hints;
	struct addrinfo *ai_list, *ai_ptr;
	int              ai_return;

	sockent_t *se_head = NULL;
	sockent_t *se_tail = NULL;

	DEBUG ("node = %s, service = %s", node, service);

	memset (&ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_flags    = 0;
#ifdef AI_PASSIVE
	ai_hints.ai_flags |= AI_PASSIVE;
#endif
#ifdef AI_ADDRCONFIG
	ai_hints.ai_flags |= AI_ADDRCONFIG;
#endif
	ai_hints.ai_family   = AF_UNSPEC;
	ai_hints.ai_socktype = SOCK_DGRAM;
	ai_hints.ai_protocol = IPPROTO_UDP;

	ai_return = getaddrinfo (node, service, &ai_hints, &ai_list);
	if (ai_return != 0)
	{
		char errbuf[1024];
		ERROR ("getaddrinfo (%s, %s): %s",
				(node == NULL) ? "(null)" : node,
				(service == NULL) ? "(null)" : service,
				(ai_return == EAI_SYSTEM)
				? sstrerror (errno, errbuf, sizeof (errbuf))
				: gai_strerror (ai_return));
		return (NULL);
	}

	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		sockent_t *se;

		if ((se = (sockent_t *) malloc (sizeof (sockent_t))) == NULL)
		{
			char errbuf[1024];
			ERROR ("malloc: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			continue;
		}

		if ((se->addr = (struct sockaddr_storage *) malloc (sizeof (struct sockaddr_storage))) == NULL)
		{
			char errbuf[1024];
			ERROR ("malloc: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			free (se);
			continue;
		}

		assert (sizeof (struct sockaddr_storage) >= ai_ptr->ai_addrlen);
		memset (se->addr, '\0', sizeof (struct sockaddr_storage));
		memcpy (se->addr, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
		se->addrlen = ai_ptr->ai_addrlen;

		se->fd   = socket (ai_ptr->ai_family,
				ai_ptr->ai_socktype,
				ai_ptr->ai_protocol);
		se->next = NULL;

		if (se->fd == -1)
		{
			char errbuf[1024];
			ERROR ("socket: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			free (se->addr);
			free (se);
			continue;
		}

		if (listen != 0)
		{
			if (network_bind_socket (se, ai_ptr) != 0)
			{
				close (se->fd);
				free (se->addr);
				free (se);
				continue;
			}
		}
		else /* listen == 0 */
		{
			network_set_ttl (se, ai_ptr);
		}

		if (se_tail == NULL)
		{
			se_head = se;
			se_tail = se;
		}
		else
		{
			se_tail->next = se;
			se_tail = se;
		}

		/* We don't open more than one write-socket per node/service pair.. */
		if (listen == 0)
			break;
	}

	freeaddrinfo (ai_list);

	return (se_head);
} /* sockent_t *network_create_socket */

static sockent_t *network_create_default_socket (int listen)
{
	sockent_t *se_ptr  = NULL;
	sockent_t *se_head = NULL;
	sockent_t *se_tail = NULL;

	se_ptr = network_create_socket (NET_DEFAULT_V6_ADDR,
			NET_DEFAULT_PORT, listen);

	/* Don't send to the same machine in IPv6 and IPv4 if both are available. */
	if ((listen == 0) && (se_ptr != NULL))
		return (se_ptr);

	if (se_ptr != NULL)
	{
		se_head = se_ptr;
		se_tail = se_ptr;
		while (se_tail->next != NULL)
			se_tail = se_tail->next;
	}

	se_ptr = network_create_socket (NET_DEFAULT_V4_ADDR, NET_DEFAULT_PORT, listen);

	if (se_tail == NULL)
		return (se_ptr);

	se_tail->next = se_ptr;
	return (se_head);
} /* sockent_t *network_create_default_socket */

static int network_add_listen_socket (const char *node, const char *service)
{
	sockent_t *se;
	sockent_t *se_ptr;
	int se_num = 0;

	if (service == NULL)
		service = NET_DEFAULT_PORT;

	if (node == NULL)
		se = network_create_default_socket (1 /* listen == true */);
	else
		se = network_create_socket (node, service, 1 /* listen == true */);

	if (se == NULL)
		return (-1);

	for (se_ptr = se; se_ptr != NULL; se_ptr = se_ptr->next)
		se_num++;

	listen_sockets = (struct pollfd *) realloc (listen_sockets,
			(listen_sockets_num + se_num)
			* sizeof (struct pollfd));

	for (se_ptr = se; se_ptr != NULL; se_ptr = se_ptr->next)
	{
		listen_sockets[listen_sockets_num].fd = se_ptr->fd;
		listen_sockets[listen_sockets_num].events = POLLIN | POLLPRI;
		listen_sockets[listen_sockets_num].revents = 0;
		listen_sockets_num++;
	} /* for (se) */

	free_sockent (se);
	return (0);
} /* int network_add_listen_socket */

static int network_add_sending_socket (const char *node, const char *service)
{
	sockent_t *se;
	sockent_t *se_ptr;

	if (service == NULL)
		service = NET_DEFAULT_PORT;

	if (node == NULL)
		se = network_create_default_socket (0 /* listen == false */);
	else
		se = network_create_socket (node, service, 0 /* listen == false */);

	if (se == NULL)
		return (-1);

	if (sending_sockets == NULL)
	{
		sending_sockets = se;
		return (0);
	}

	for (se_ptr = sending_sockets; se_ptr->next != NULL; se_ptr = se_ptr->next)
		/* seek end */;

	se_ptr->next = se;
	return (0);
} /* int network_get_listen_socket */

static int network_receive (void)
{
	char buffer[BUFF_SIZE];
	int  buffer_len;

	int i;
	int status;

	if (listen_sockets_num == 0)
		network_add_listen_socket (NULL, NULL);

	if (listen_sockets_num == 0)
	{
		ERROR ("network: Failed to open a listening socket.");
		return (-1);
	}

	while (listen_loop == 0)
	{
		status = poll (listen_sockets, listen_sockets_num, -1);

		if (status <= 0)
		{
			char errbuf[1024];
			if (errno == EINTR)
				continue;
			ERROR ("poll failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			return (-1);
		}

		for (i = 0; (i < listen_sockets_num) && (status > 0); i++)
		{
			if ((listen_sockets[i].revents & (POLLIN | POLLPRI)) == 0)
				continue;
			status--;

			buffer_len = recv (listen_sockets[i].fd,
					buffer, sizeof (buffer),
					0 /* no flags */);
			if (buffer_len < 0)
			{
				char errbuf[1024];
				ERROR ("recv failed: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}

			parse_packet (buffer, buffer_len);
		} /* for (listen_sockets) */
	} /* while (listen_loop == 0) */

	return (0);
}

static void *receive_thread (void *arg)
{
	return (network_receive () ? (void *) 1 : (void *) 0);
} /* void *receive_thread */

static void network_send_buffer (const char *buffer, int buffer_len)
{
	sockent_t *se;
	int status;

	DEBUG ("network plugin: network_send_buffer: buffer_len = %i", buffer_len);

	for (se = sending_sockets; se != NULL; se = se->next)
	{
		while (42)
		{
			status = sendto (se->fd, buffer, buffer_len, 0 /* no flags */,
					(struct sockaddr *) se->addr, se->addrlen);
			if (status < 0)
			{
				char errbuf[1024];
				if (errno == EINTR)
					continue;
				ERROR ("network plugin: sendto failed: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				break;
			}

			break;
		} /* while (42) */
	} /* for (sending_sockets) */
} /* void network_send_buffer */

static int add_to_buffer (char *buffer, int buffer_size,
		value_list_t *vl_def, char *type_def,
		const data_set_t *ds, const value_list_t *vl)
{
	char *buffer_orig = buffer;

	if (strcmp (vl_def->host, vl->host) != 0)
	{
		if (write_part_string (&buffer, &buffer_size, TYPE_HOST,
					vl->host, strlen (vl->host)) != 0)
			return (-1);
		strcpy (vl_def->host, vl->host);
	}

	if (vl_def->time != vl->time)
	{
		if (write_part_number (&buffer, &buffer_size, TYPE_TIME,
					(uint64_t) vl->time))
			return (-1);
		vl_def->time = vl->time;
	}

	if (vl_def->interval != vl->interval)
	{
		if (write_part_number (&buffer, &buffer_size, TYPE_INTERVAL,
					(uint64_t) vl->interval))
			return (-1);
		vl_def->interval = vl->interval;
	}

	if (strcmp (vl_def->plugin, vl->plugin) != 0)
	{
		if (write_part_string (&buffer, &buffer_size, TYPE_PLUGIN,
					vl->plugin, strlen (vl->plugin)) != 0)
			return (-1);
		strcpy (vl_def->plugin, vl->plugin);
	}

	if (strcmp (vl_def->plugin_instance, vl->plugin_instance) != 0)
	{
		if (write_part_string (&buffer, &buffer_size, TYPE_PLUGIN_INSTANCE,
					vl->plugin_instance,
					strlen (vl->plugin_instance)) != 0)
			return (-1);
		strcpy (vl_def->plugin_instance, vl->plugin_instance);
	}

	if (strcmp (type_def, ds->type) != 0)
	{
		if (write_part_string (&buffer, &buffer_size, TYPE_TYPE,
					ds->type, strlen (ds->type)) != 0)
			return (-1);
		strcpy (type_def, ds->type);
	}

	if (strcmp (vl_def->type_instance, vl->type_instance) != 0)
	{
		if (write_part_string (&buffer, &buffer_size, TYPE_TYPE_INSTANCE,
					vl->type_instance,
					strlen (vl->type_instance)) != 0)
			return (-1);
		strcpy (vl_def->type_instance, vl->type_instance);
	}
	
	if (write_part_values (&buffer, &buffer_size, ds, vl) != 0)
		return (-1);

	return (buffer - buffer_orig);
} /* int add_to_buffer */

static void flush_buffer (void)
{
	DEBUG ("network plugin: flush_buffer: send_buffer_fill = %i",
			send_buffer_fill);

	network_send_buffer (send_buffer, send_buffer_fill);
	send_buffer_ptr  = send_buffer;
	send_buffer_fill = 0;
	memset (&send_buffer_vl, '\0', sizeof (send_buffer_vl));
	memset (send_buffer_type, '\0', sizeof (send_buffer_type));
}

static int network_write (const data_set_t *ds, const value_list_t *vl)
{
	int status;

	/* If the value is already in the cache, we have received it via the
	 * network. We write it again if forwarding is activated. It's then in
	 * the cache and should we receive it again we will ignore it. */
	status = cache_check (ds->type, vl);
	if ((network_config_forward == 0)
			&& (status != 0))
		return (0);

	pthread_mutex_lock (&send_buffer_lock);

	status = add_to_buffer (send_buffer_ptr,
			sizeof (send_buffer) - send_buffer_fill,
			&send_buffer_vl, send_buffer_type,
			ds, vl);
	if (status >= 0)
	{
		/* status == bytes added to the buffer */
		send_buffer_fill += status;
		send_buffer_ptr  += status;
	}
	else
	{
		flush_buffer ();

		status = add_to_buffer (send_buffer_ptr,
				sizeof (send_buffer) - send_buffer_fill,
				&send_buffer_vl, send_buffer_type,
				ds, vl);

		if (status >= 0)
		{
			send_buffer_fill += status;
			send_buffer_ptr  += status;
		}
	}

	if (status < 0)
	{
		ERROR ("network plugin: Unable to append to the "
				"buffer for some weird reason");
	}
	else if ((sizeof (send_buffer) - send_buffer_fill) < 15)
	{
		flush_buffer ();
	}

	pthread_mutex_unlock (&send_buffer_lock);

	return ((status < 0) ? -1 : 0);
} /* int network_write */

static int network_config (const char *key, const char *val)
{
	char *node;
	char *service;

	char *fields[3];
	int   fields_num;

	if ((strcasecmp ("Listen", key) == 0)
			|| (strcasecmp ("Server", key) == 0))
	{
		char *val_cpy = strdup (val);
		if (val_cpy == NULL)
			return (1);

		service = NET_DEFAULT_PORT;
		fields_num = strsplit (val_cpy, fields, 3);
		if ((fields_num != 1)
				&& (fields_num != 2))
			return (1);
		else if (fields_num == 2)
		{
			if ((service = strchr (fields[1], '.')) != NULL)
				*service = '\0';
			service = fields[1];
		}
		node = fields[0];

		if (strcasecmp ("Listen", key) == 0)
			network_add_listen_socket (node, service);
		else
			network_add_sending_socket (node, service);
	}
	else if (strcasecmp ("TimeToLive", key) == 0)
	{
		int tmp = atoi (val);
		if ((tmp > 0) && (tmp < 256))
			network_config_ttl = tmp;
		else
			return (1);
	}
	else if (strcasecmp ("Forward", key) == 0)
	{
		if ((strcasecmp ("true", val) == 0)
				|| (strcasecmp ("yes", val) == 0)
				|| (strcasecmp ("on", val) == 0))
			network_config_forward = 1;
		else
			network_config_forward = 0;
	}
	else if (strcasecmp ("CacheFlush", key) == 0)
	{
		int tmp = atoi (val);
		if (tmp > 0)
			cache_flush_interval = tmp;
		else return (1);
	}
	else
	{
		return (-1);
	}
	return (0);
} /* int network_config */

static int network_shutdown (void)
{
	listen_loop++;

	if (listen_thread != (pthread_t) 0)
	{
		pthread_kill (listen_thread, SIGTERM);
		pthread_join (listen_thread, NULL /* no return value */);
		listen_thread = (pthread_t) 0;
	}

	if (send_buffer_fill > 0)
		flush_buffer ();

	if (cache_tree != NULL)
	{
		void *key;
		void *value;

		while (c_avl_pick (cache_tree, &key, &value) == 0)
		{
			sfree (key);
			sfree (value);
		}
		c_avl_destroy (cache_tree);
		cache_tree = NULL;
	}

	/* TODO: Close `sending_sockets' */

	plugin_unregister_config ("network");
	plugin_unregister_init ("network");
	plugin_unregister_write ("network");
	plugin_unregister_shutdown ("network");

	return (0);
} /* int network_shutdown */

static int network_init (void)
{
	plugin_register_shutdown ("network", network_shutdown);

	send_buffer_ptr  = send_buffer;
	send_buffer_fill = 0;
	memset (&send_buffer_vl, '\0', sizeof (send_buffer_vl));
	memset (send_buffer_type, '\0', sizeof (send_buffer_type));

	cache_tree = c_avl_create ((int (*) (const void *, const void *)) strcmp);
	cache_flush_last = time (NULL);

	/* setup socket(s) and so on */
	if (sending_sockets != NULL)
		plugin_register_write ("network", network_write);

	if ((listen_sockets_num != 0) && (listen_thread == 0))
	{
		int status;

		status = pthread_create (&listen_thread, NULL /* no attributes */,
				receive_thread, NULL /* no argument */);

		if (status != 0)
		{
			char errbuf[1024];
			ERROR ("network: pthread_create failed: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
		}
	}
	return (0);
} /* int network_init */

void module_register (void)
{
	plugin_register_config ("network", network_config,
			config_keys, config_keys_num);
	plugin_register_init   ("network", network_init);
} /* void module_register */
