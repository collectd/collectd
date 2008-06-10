/**
 * collectd - src/network.c
 * Copyright (C) 2005-2008  Florian octo Forster
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

struct receive_list_entry_s
{
  char data[BUFF_SIZE];
  int  data_len;
  struct receive_list_entry_s *next;
};
typedef struct receive_list_entry_s receive_list_entry_t;

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

static receive_list_entry_t *receive_list_head = NULL;
static receive_list_entry_t *receive_list_tail = NULL;
static pthread_mutex_t       receive_list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t        receive_list_cond = PTHREAD_COND_INITIALIZER;

static struct pollfd *listen_sockets = NULL;
static int listen_sockets_num = 0;

static int listen_loop = 0;
static pthread_t receive_thread_id = 0;
static pthread_t dispatch_thread_id = 0;

static char         send_buffer[BUFF_SIZE];
static char        *send_buffer_ptr;
static int          send_buffer_fill;
static value_list_t send_buffer_vl = VALUE_LIST_STATIC;
static char         send_buffer_type[DATA_MAX_NAME_LEN];
static pthread_mutex_t send_buffer_lock = PTHREAD_MUTEX_INITIALIZER;

static c_avl_tree_t      *cache_tree = NULL;
static pthread_mutex_t  cache_lock = PTHREAD_MUTEX_INITIALIZER;
static time_t           cache_flush_last = 0;
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
	char *packet_ptr;
	int packet_len;
	int num_values;

	part_header_t pkg_ph;
	uint16_t      pkg_num_values;
	uint8_t      *pkg_values_types;
	value_t      *pkg_values;

	int offset;
	int i;

	num_values = vl->values_len;
	packet_len = sizeof (part_header_t) + sizeof (uint16_t)
		+ (num_values * sizeof (uint8_t))
		+ (num_values * sizeof (value_t));

	if (*ret_buffer_len < packet_len)
		return (-1);

	pkg_values_types = (uint8_t *) malloc (num_values * sizeof (uint8_t));
	if (pkg_values_types == NULL)
	{
		ERROR ("network plugin: write_part_values: malloc failed.");
		return (-1);
	}

	pkg_values = (value_t *) malloc (num_values * sizeof (value_t));
	if (pkg_values == NULL)
	{
		free (pkg_values_types);
		ERROR ("network plugin: write_part_values: malloc failed.");
		return (-1);
	}

	pkg_ph.type = htons (TYPE_VALUES);
	pkg_ph.length = htons (packet_len);

	pkg_num_values = htons ((uint16_t) vl->values_len);

	for (i = 0; i < num_values; i++)
	{
		if (ds->ds[i].type == DS_TYPE_COUNTER)
		{
			pkg_values_types[i] = DS_TYPE_COUNTER;
			pkg_values[i].counter = htonll (vl->values[i].counter);
		}
		else
		{
			pkg_values_types[i] = DS_TYPE_GAUGE;
			pkg_values[i].gauge = htond (vl->values[i].gauge);
		}
	}

	/*
	 * Use `memcpy' to write everything to the buffer, because the pointer
	 * may be unaligned and some architectures, such as SPARC, can't handle
	 * that.
	 */
	packet_ptr = *ret_buffer;
	offset = 0;
	memcpy (packet_ptr + offset, &pkg_ph, sizeof (pkg_ph));
	offset += sizeof (pkg_ph);
	memcpy (packet_ptr + offset, &pkg_num_values, sizeof (pkg_num_values));
	offset += sizeof (pkg_num_values);
	memcpy (packet_ptr + offset, pkg_values_types, num_values * sizeof (uint8_t));
	offset += num_values * sizeof (uint8_t);
	memcpy (packet_ptr + offset, pkg_values, num_values * sizeof (value_t));
	offset += num_values * sizeof (value_t);

	assert (offset == packet_len);

	*ret_buffer = packet_ptr + packet_len;
	*ret_buffer_len -= packet_len;

	free (pkg_values_types);
	free (pkg_values);

	return (0);
} /* int write_part_values */

static int write_part_number (char **ret_buffer, int *ret_buffer_len,
		int type, uint64_t value)
{
	char *packet_ptr;
	int packet_len;

	part_header_t pkg_head;
	uint64_t pkg_value;
	
	int offset;

	packet_len = sizeof (pkg_head) + sizeof (pkg_value);

	if (*ret_buffer_len < packet_len)
		return (-1);

	pkg_head.type = htons (type);
	pkg_head.length = htons (packet_len);
	pkg_value = htonll (value);

	packet_ptr = *ret_buffer;
	offset = 0;
	memcpy (packet_ptr + offset, &pkg_head, sizeof (pkg_head));
	offset += sizeof (pkg_head);
	memcpy (packet_ptr + offset, &pkg_value, sizeof (pkg_value));
	offset += sizeof (pkg_value);

	assert (offset == packet_len);

	*ret_buffer = packet_ptr + packet_len;
	*ret_buffer_len -= packet_len;

	return (0);
} /* int write_part_number */

static int write_part_string (char **ret_buffer, int *ret_buffer_len,
		int type, const char *str, int str_len)
{
	char *buffer;
	int buffer_len;

	uint16_t pkg_type;
	uint16_t pkg_length;

	int offset;

	buffer_len = 2 * sizeof (uint16_t) + str_len + 1;
	if (*ret_buffer_len < buffer_len)
		return (-1);

	pkg_type = htons (type);
	pkg_length = htons (buffer_len);

	buffer = *ret_buffer;
	offset = 0;
	memcpy (buffer + offset, (void *) &pkg_type, sizeof (pkg_type));
	offset += sizeof (pkg_type);
	memcpy (buffer + offset, (void *) &pkg_length, sizeof (pkg_length));
	offset += sizeof (pkg_length);
	memcpy (buffer + offset, str, str_len);
	offset += str_len;
	memset (buffer + offset, '\0', 1);
	offset += 1;

	assert (offset == buffer_len);

	*ret_buffer = buffer + buffer_len;
	*ret_buffer_len -= buffer_len;

	return (0);
} /* int write_part_string */

static int parse_part_values (void **ret_buffer, int *ret_buffer_len,
		value_t **ret_values, int *ret_num_values)
{
	char *buffer = *ret_buffer;
	int   buffer_len = *ret_buffer_len;

	uint16_t tmp16;
	size_t exp_size;
	int   i;

	uint16_t pkg_length;
	uint16_t pkg_type;
	uint16_t pkg_numval;

	uint8_t *pkg_types;
	value_t *pkg_values;

	if (buffer_len < (15))
	{
		DEBUG ("network plugin: packet is too short: buffer_len = %i",
				buffer_len);
		return (-1);
	}

	memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
	buffer += sizeof (tmp16);
	pkg_type = ntohs (tmp16);

	memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
	buffer += sizeof (tmp16);
	pkg_length = ntohs (tmp16);

	memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
	buffer += sizeof (tmp16);
	pkg_numval = ntohs (tmp16);

	assert (pkg_type == TYPE_VALUES);

	exp_size = 3 * sizeof (uint16_t)
		+ pkg_numval * (sizeof (uint8_t) + sizeof (value_t));
	if (buffer_len < exp_size)
	{
		WARNING ("network plugin: parse_part_values: "
				"Packet too short: "
				"Chunk of size %u expected, "
				"but buffer has only %i bytes left.",
				(unsigned int) exp_size, buffer_len);
		return (-1);
	}

	if (pkg_length != exp_size)
	{
		WARNING ("network plugin: parse_part_values: "
				"Length and number of values "
				"in the packet don't match.");
		return (-1);
	}

	pkg_types = (uint8_t *) malloc (pkg_numval * sizeof (uint8_t));
	pkg_values = (value_t *) malloc (pkg_numval * sizeof (value_t));
	if ((pkg_types == NULL) || (pkg_values == NULL))
	{
		sfree (pkg_types);
		sfree (pkg_values);
		ERROR ("network plugin: parse_part_values: malloc failed.");
		return (-1);
	}

	memcpy ((void *) pkg_types, (void *) buffer, pkg_numval * sizeof (uint8_t));
	buffer += pkg_numval * sizeof (uint8_t);
	memcpy ((void *) pkg_values, (void *) buffer, pkg_numval * sizeof (value_t));
	buffer += pkg_numval * sizeof (value_t);

	for (i = 0; i < pkg_numval; i++)
	{
		if (pkg_types[i] == DS_TYPE_COUNTER)
			pkg_values[i].counter = ntohll (pkg_values[i].counter);
		else if (pkg_types[i] == DS_TYPE_GAUGE)
			pkg_values[i].gauge = ntohd (pkg_values[i].gauge);
	}

	*ret_buffer     = buffer;
	*ret_buffer_len = buffer_len - pkg_length;
	*ret_num_values = pkg_numval;
	*ret_values     = pkg_values;

	sfree (pkg_types);

	return (0);
} /* int parse_part_values */

static int parse_part_number (void **ret_buffer, int *ret_buffer_len,
		uint64_t *value)
{
	char *buffer = *ret_buffer;
	int buffer_len = *ret_buffer_len;

	uint16_t tmp16;
	uint64_t tmp64;
	size_t exp_size = 2 * sizeof (uint16_t) + sizeof (uint64_t);

	uint16_t pkg_length;
	uint16_t pkg_type;

	if (buffer_len < exp_size)
	{
		WARNING ("network plugin: parse_part_number: "
				"Packet too short: "
				"Chunk of size %u expected, "
				"but buffer has only %i bytes left.",
				(unsigned int) exp_size, buffer_len);
		return (-1);
	}

	memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
	buffer += sizeof (tmp16);
	pkg_type = ntohs (tmp16);

	memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
	buffer += sizeof (tmp16);
	pkg_length = ntohs (tmp16);

	memcpy ((void *) &tmp64, buffer, sizeof (tmp64));
	buffer += sizeof (tmp64);
	*value = ntohll (tmp64);

	*ret_buffer = buffer;
	*ret_buffer_len = buffer_len - pkg_length;

	return (0);
} /* int parse_part_number */

static int parse_part_string (void **ret_buffer, int *ret_buffer_len,
		char *output, int output_len)
{
	char *buffer = *ret_buffer;
	int   buffer_len = *ret_buffer_len;

	uint16_t tmp16;
	size_t header_size = 2 * sizeof (uint16_t);

	uint16_t pkg_length;
	uint16_t pkg_type;

	if (buffer_len < header_size)
	{
		WARNING ("network plugin: parse_part_string: "
				"Packet too short: "
				"Chunk of at least size %u expected, "
				"but buffer has only %i bytes left.",
				(unsigned int) header_size, buffer_len);
		return (-1);
	}

	memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
	buffer += sizeof (tmp16);
	pkg_type = ntohs (tmp16);

	memcpy ((void *) &tmp16, buffer, sizeof (tmp16));
	buffer += sizeof (tmp16);
	pkg_length = ntohs (tmp16);

	/* Check that packet fits in the input buffer */
	if (pkg_length > buffer_len)
	{
		WARNING ("network plugin: parse_part_string: "
				"Packet too big: "
				"Chunk of size %hu received, "
				"but buffer has only %i bytes left.",
				pkg_length, buffer_len);
		return (-1);
	}

	/* Check that pkg_length is in the valid range */
	if (pkg_length <= header_size)
	{
		WARNING ("network plugin: parse_part_string: "
				"Packet too short: "
				"Header claims this packet is only %hu "
				"bytes long.", pkg_length);
		return (-1);
	}

	/* Check that the package data fits into the output buffer.
	 * The previous if-statement ensures that:
	 * `pkg_length > header_size' */
	if ((pkg_length - header_size) > output_len)
	{
		WARNING ("network plugin: parse_part_string: "
				"Output buffer too small.");
		return (-1);
	}

	/* All sanity checks successfull, let's copy the data over */
	output_len = pkg_length - header_size;
	memcpy ((void *) output, (void *) buffer, output_len);
	buffer += output_len;

	/* For some very weird reason '\0' doesn't do the trick on SPARC in
	 * this statement. */
	if (output[output_len - 1] != 0)
	{
		WARNING ("network plugin: parse_part_string: "
				"Received string does not end "
				"with a NULL-byte.");
		return (-1);
	}

	*ret_buffer = buffer;
	*ret_buffer_len = buffer_len - pkg_length;

	return (0);
} /* int parse_part_string */

static int parse_packet (void *buffer, int buffer_len)
{
	int status;

	value_list_t vl = VALUE_LIST_INIT;
	char type[DATA_MAX_NAME_LEN];
	notification_t n;

	DEBUG ("network plugin: parse_packet: buffer = %p; buffer_len = %i;",
			buffer, buffer_len);

	memset (&vl, '\0', sizeof (vl));
	memset (&type, '\0', sizeof (type));
	memset (&n, '\0', sizeof (n));
	status = 0;

	while ((status == 0) && (0 < buffer_len)
			&& ((unsigned int) buffer_len > sizeof (part_header_t)))
	{
		uint16_t pkg_length;
		uint16_t pkg_type;

		memcpy ((void *) &pkg_type,
				(void *) buffer,
				sizeof (pkg_type));
		memcpy ((void *) &pkg_length,
				(void *) (buffer + sizeof (pkg_type)),
				sizeof (pkg_length));

		pkg_length = ntohs (pkg_length);
		pkg_type = ntohs (pkg_type);

		if (pkg_length > buffer_len)
			break;
		/* Ensure that this loop terminates eventually */
		if (pkg_length < (2 * sizeof (uint16_t)))
			break;

		if (pkg_type == TYPE_VALUES)
		{
			status = parse_part_values (&buffer, &buffer_len,
					&vl.values, &vl.values_len);

			if (status != 0)
				break;

			if ((vl.time > 0)
					&& (strlen (vl.host) > 0)
					&& (strlen (vl.plugin) > 0)
					&& (strlen (type) > 0)
					&& (cache_check (type, &vl) == 0))
			{
				plugin_dispatch_values (type, &vl);
			}
			else
			{
				DEBUG ("network plugin: parse_packet:"
						" NOT dispatching values");
			}

			sfree (vl.values);
		}
		else if (pkg_type == TYPE_TIME)
		{
			uint64_t tmp = 0;
			status = parse_part_number (&buffer, &buffer_len,
					&tmp);
			if (status == 0)
			{
				vl.time = (time_t) tmp;
				n.time = (time_t) tmp;
			}
		}
		else if (pkg_type == TYPE_INTERVAL)
		{
			uint64_t tmp = 0;
			status = parse_part_number (&buffer, &buffer_len,
					&tmp);
			if (status == 0)
				vl.interval = (int) tmp;
		}
		else if (pkg_type == TYPE_HOST)
		{
			status = parse_part_string (&buffer, &buffer_len,
					vl.host, sizeof (vl.host));
			if (status == 0)
				sstrncpy (n.host, vl.host, sizeof (n.host));
		}
		else if (pkg_type == TYPE_PLUGIN)
		{
			status = parse_part_string (&buffer, &buffer_len,
					vl.plugin, sizeof (vl.plugin));
			if (status == 0)
				sstrncpy (n.plugin, vl.plugin,
						sizeof (n.plugin));
		}
		else if (pkg_type == TYPE_PLUGIN_INSTANCE)
		{
			status = parse_part_string (&buffer, &buffer_len,
					vl.plugin_instance,
					sizeof (vl.plugin_instance));
			if (status == 0)
				sstrncpy (n.plugin_instance,
						vl.plugin_instance,
						sizeof (n.plugin_instance));
		}
		else if (pkg_type == TYPE_TYPE)
		{
			status = parse_part_string (&buffer, &buffer_len,
					type, sizeof (type));
			if (status == 0)
				sstrncpy (n.type, type, sizeof (n.type));
		}
		else if (pkg_type == TYPE_TYPE_INSTANCE)
		{
			status = parse_part_string (&buffer, &buffer_len,
					vl.type_instance,
					sizeof (vl.type_instance));
			if (status == 0)
				sstrncpy (n.type_instance, vl.type_instance,
						sizeof (n.type_instance));
		}
		else if (pkg_type == TYPE_MESSAGE)
		{
			status = parse_part_string (&buffer, &buffer_len,
					n.message, sizeof (n.message));

			if (status != 0)
			{
				/* do nothing */
			}
			else if ((n.severity != NOTIF_FAILURE)
					&& (n.severity != NOTIF_WARNING)
					&& (n.severity != NOTIF_OKAY))
			{
				INFO ("network plugin: "
						"Ignoring notification with "
						"unknown severity %i.",
						n.severity);
			}
			else if (n.time <= 0)
			{
				INFO ("network plugin: "
						"Ignoring notification with "
						"time == 0.");
			}
			else if (strlen (n.message) <= 0)
			{
				INFO ("network plugin: "
						"Ignoring notification with "
						"an empty message.");
			}
			else
			{
				plugin_dispatch_notification (&n);
			}
		}
		else if (pkg_type == TYPE_SEVERITY)
		{
			uint64_t tmp = 0;
			status = parse_part_number (&buffer, &buffer_len,
					&tmp);
			if (status == 0)
				n.severity = (int) tmp;
		}
		else
		{
			DEBUG ("network plugin: parse_packet: Unknown part"
					" type: 0x%04hx", pkg_type);
			buffer = ((char *) buffer) + pkg_length;
		}
	} /* while (buffer_len > sizeof (part_header_t)) */

	return (status);
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

static void *dispatch_thread (void *arg)
{
  while (42)
  {
    receive_list_entry_t *ent;

    /* Lock and wait for more data to come in */
    pthread_mutex_lock (&receive_list_lock);
    while ((listen_loop == 0)
	&& (receive_list_head == NULL))
      pthread_cond_wait (&receive_list_cond, &receive_list_lock);

    /* Remove the head entry and unlock */
    ent = receive_list_head;
    if (ent != NULL)
      receive_list_head = ent->next;
    pthread_mutex_unlock (&receive_list_lock);

    /* Check whether we are supposed to exit. We do NOT check `listen_loop'
     * because we dispatch all missing packets before shutting down. */
    if (ent == NULL)
      break;

    parse_packet (ent->data, ent->data_len);

    sfree (ent);
  } /* while (42) */

  return (NULL);
} /* void *receive_thread */

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
			receive_list_entry_t *ent;

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

			ent = malloc (sizeof (receive_list_entry_t));
			if (ent == NULL)
			{
				ERROR ("network plugin: malloc failed.");
				return (-1);
			}
			memset (ent, '\0', sizeof (receive_list_entry_t));

			/* Hopefully this be optimized out by the compiler. It
			 * might help prevent stupid bugs in the future though.
			 */
			assert (sizeof (ent->data) == sizeof (buffer));

			memcpy (ent->data, buffer, buffer_len);
			ent->data_len = buffer_len;

			pthread_mutex_lock (&receive_list_lock);
			if (receive_list_head == NULL)
			{
				receive_list_head = ent;
				receive_list_tail = ent;
			}
			else
			{
				receive_list_tail->next = ent;
				receive_list_tail = ent;
			}
			pthread_cond_signal (&receive_list_cond);
			pthread_mutex_unlock (&receive_list_lock);
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

static int network_notification (const notification_t *n)
{
  char  buffer[BUFF_SIZE];
  char *buffer_ptr = buffer;
  int   buffer_free = sizeof (buffer);
  int   status;

  memset (buffer, '\0', sizeof (buffer));


  status = write_part_number (&buffer_ptr, &buffer_free, TYPE_TIME,
      (uint64_t) n->time);
  if (status != 0)
    return (-1);

  status = write_part_number (&buffer_ptr, &buffer_free, TYPE_SEVERITY,
      (uint64_t) n->severity);
  if (status != 0)
    return (-1);

  if (strlen (n->host) > 0)
  {
    status = write_part_string (&buffer_ptr, &buffer_free, TYPE_HOST,
	n->host, strlen (n->host));
    if (status != 0)
      return (-1);
  }

  if (strlen (n->plugin) > 0)
  {
    status = write_part_string (&buffer_ptr, &buffer_free, TYPE_PLUGIN,
	n->plugin, strlen (n->plugin));
    if (status != 0)
      return (-1);
  }

  if (strlen (n->plugin_instance) > 0)
  {
    status = write_part_string (&buffer_ptr, &buffer_free,
	TYPE_PLUGIN_INSTANCE,
	n->plugin_instance, strlen (n->plugin_instance));
    if (status != 0)
      return (-1);
  }

  if (strlen (n->type) > 0)
  {
    status = write_part_string (&buffer_ptr, &buffer_free, TYPE_TYPE,
	n->type, strlen (n->type));
    if (status != 0)
      return (-1);
  }

  if (strlen (n->type_instance) > 0)
  {
    status = write_part_string (&buffer_ptr, &buffer_free, TYPE_TYPE_INSTANCE,
	n->type_instance, strlen (n->type_instance));
    if (status != 0)
      return (-1);
  }

  status = write_part_string (&buffer_ptr, &buffer_free, TYPE_MESSAGE,
      n->message, strlen (n->message));
  if (status != 0)
    return (-1);

  network_send_buffer (buffer, sizeof (buffer) - buffer_free);

  return (0);
} /* int network_notification */

static int network_shutdown (void)
{
	listen_loop++;

	/* Kill the listening thread */
	if (receive_thread_id != (pthread_t) 0)
	{
		pthread_kill (receive_thread_id, SIGTERM);
		pthread_join (receive_thread_id, NULL /* no return value */);
		receive_thread_id = (pthread_t) 0;
	}

	/* Shutdown the dispatching thread */
	if (dispatch_thread_id != (pthread_t) 0)
		pthread_cond_broadcast (&receive_list_cond);

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

	/* Let the init function do it's move again ;) */
	cache_flush_last = 0;

	return (0);
} /* int network_shutdown */

static int network_init (void)
{
	/* Check if we were already initialized. If so, just return - there's
	 * nothing more to do (for now, that is). */
	if (cache_flush_last != 0)
		return (0);

	plugin_register_shutdown ("network", network_shutdown);

	send_buffer_ptr  = send_buffer;
	send_buffer_fill = 0;
	memset (&send_buffer_vl, '\0', sizeof (send_buffer_vl));
	memset (send_buffer_type, '\0', sizeof (send_buffer_type));

	cache_tree = c_avl_create ((int (*) (const void *, const void *)) strcmp);
	cache_flush_last = time (NULL);

	/* setup socket(s) and so on */
	if (sending_sockets != NULL)
	{
		plugin_register_write ("network", network_write);
		plugin_register_notification ("network", network_notification);
	}

	if ((listen_sockets_num != 0) && (receive_thread_id == 0))
	{
		int status;

		status = pthread_create (&dispatch_thread_id,
				NULL /* no attributes */,
				dispatch_thread,
				NULL /* no argument */);
		if (status != 0)
		{
			char errbuf[1024];
			ERROR ("network: pthread_create failed: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
		}

		status = pthread_create (&receive_thread_id,
				NULL /* no attributes */,
				receive_thread,
				NULL /* no argument */);
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

static int network_flush (int timeout)
{
	pthread_mutex_lock (&send_buffer_lock);

	if (((time (NULL) - cache_flush_last) >= timeout)
			&& (send_buffer_fill > 0))
	{
		flush_buffer ();
	}

	pthread_mutex_unlock (&send_buffer_lock);

	return (0);
} /* int network_flush */

void module_register (void)
{
	plugin_register_config ("network", network_config,
			config_keys, config_keys_num);
	plugin_register_init   ("network", network_init);
	plugin_register_flush   ("network", network_flush);
} /* void module_register */
