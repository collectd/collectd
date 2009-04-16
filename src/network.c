/**
 * collectd - src/network.c
 * Copyright (C) 2005-2009  Florian octo Forster
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

#if HAVE_GCRYPT_H
# include <gcrypt.h>
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

/* Buffer size to allocate. */
#define BUFF_SIZE 1024

/*
 * Maximum size required for encryption / signing:
 * Type/length:       4
 * IV                16
 * Hash/orig length: 22
 * Padding (up to):  15
 * --------------------
 *                   57
 */
#define BUFF_SIG_SIZE 57

/*
 * Private data types
 */
typedef struct sockent
{
	int                      fd;
	struct sockaddr_storage *addr;
	socklen_t                addrlen;

#define SECURITY_LEVEL_NONE     0
#if HAVE_GCRYPT_H
# define SECURITY_LEVEL_SIGN    1
# define SECURITY_LEVEL_ENCRYPT 2
	int security_level;
	char *shared_secret;
	unsigned char shared_secret_hash[32];
	gcry_cipher_hd_t cypher;
#endif /* HAVE_GCRYPT_H */

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

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------------------------------+-------------------------------+
 * ! Type                          ! Length                        !
 * +-------------------------------+-------------------------------+
 * ! Hash (Bits   0 -  31)                                         !
 * : :                                                             :
 * ! Hash (Bits 224 - 255)                                         !
 * +---------------------------------------------------------------+
 */
#define PART_SIGNATURE_SHA256_SIZE 36
struct part_signature_sha256_s
{
  part_header_t head;
  unsigned char hash[32];
};
typedef struct part_signature_sha256_s part_signature_sha256_t;

/*                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-------------------------------+-------------------------------+
 * ! Type                          ! Length                        !
 * +-------------------------------+-------------------------------+
 * ! Original length               ! Padding (0 - 15 bytes)        !
 * +-------------------------------+-------------------------------+
 * ! Hash (Bits   0 -  31)                                         !
 * : :                                                             :
 * ! Hash (Bits 128 - 159)                                         !
 * +---------------------------------------------------------------+
 */
/* Size without padding */
#define PART_ENCRYPTION_AES256_SIZE 42
#define PART_ENCRYPTION_AES256_UNENCR_SIZE 20
struct part_encryption_aes256_s
{
  part_header_t head;
  unsigned char iv[16];
  uint16_t orig_length;
  unsigned char hash[20];
  unsigned char padding[15];
};
typedef struct part_encryption_aes256_s part_encryption_aes256_t;

struct receive_list_entry_s
{
  char data[BUFF_SIZE];
  int  data_len;
  int  fd;
  struct receive_list_entry_s *next;
};
typedef struct receive_list_entry_s receive_list_entry_t;

/*
 * Private variables
 */
static int network_config_ttl = 0;
static int network_config_forward = 0;

static sockent_t *sending_sockets = NULL;

static receive_list_entry_t *receive_list_head = NULL;
static receive_list_entry_t *receive_list_tail = NULL;
static pthread_mutex_t       receive_list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t        receive_list_cond = PTHREAD_COND_INITIALIZER;

static sockent_t     *listen_sockets = NULL;
static struct pollfd *listen_sockets_pollfd = NULL;
static int            listen_sockets_num = 0;

/* The receive and dispatch threads will run as long as `listen_loop' is set to
 * zero. */
static int       listen_loop = 0;
static int       receive_thread_running = 0;
static pthread_t receive_thread_id;
static int       dispatch_thread_running = 0;
static pthread_t dispatch_thread_id;

/* Buffer in which to-be-sent network packets are constructed. */
static char             send_buffer[BUFF_SIZE];
static char            *send_buffer_ptr;
static int              send_buffer_fill;
static value_list_t     send_buffer_vl = VALUE_LIST_STATIC;
static pthread_mutex_t  send_buffer_lock = PTHREAD_MUTEX_INITIALIZER;

/* In this cache we store all the values we received, so we can send out only
 * those values which were *not* received via the network plugin, too. This is
 * used for the `Forward false' option. */
static c_avl_tree_t    *cache_tree = NULL;
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

static int cache_check (const value_list_t *vl)
{
	char key[1024];
	time_t *value = NULL;
	int retval = -1;

	if (cache_tree == NULL)
		return (-1);

	if (format_name (key, sizeof (key), vl->host, vl->plugin,
				vl->plugin_instance, vl->type, vl->type_instance))
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

	return (retval);
} /* int cache_check */

#if HAVE_GCRYPT_H
static gcry_cipher_hd_t network_get_aes256_cypher (sockent_t *se, /* {{{ */
    const void *iv, size_t iv_size)
{
  gcry_error_t err;

  if (se->cypher == NULL)
  {
    err = gcry_cipher_open (&se->cypher,
        GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_CBC, /* flags = */ 0);
    if (err != 0)
    {
      ERROR ("network plugin: gcry_cipher_open returned: %s",
          gcry_strerror (err));
      se->cypher = NULL;
      return (NULL);
    }
  }
  else
  {
    gcry_cipher_reset (se->cypher);
  }
  assert (se->cypher != NULL);

  err = gcry_cipher_setkey (se->cypher,
      se->shared_secret_hash, sizeof (se->shared_secret_hash));
  if (err != 0)
  {
    ERROR ("network plugin: gcry_cipher_setkey returned: %s",
        gcry_strerror (err));
    gcry_cipher_close (se->cypher);
    se->cypher = NULL;
    return (NULL);
  }

  err = gcry_cipher_setiv (se->cypher, iv, iv_size);
  if (err != 0)
  {
    ERROR ("network plugin: gcry_cipher_setkey returned: %s",
        gcry_strerror (err));
    gcry_cipher_close (se->cypher);
    se->cypher = NULL;
    return (NULL);
  }

  return (se->cypher);
} /* }}} int network_get_aes256_cypher */
#endif /* HAVE_GCRYPT_H */

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

static int parse_part_values (void **ret_buffer, size_t *ret_buffer_len,
		value_t **ret_values, int *ret_num_values)
{
	char *buffer = *ret_buffer;
	size_t buffer_len = *ret_buffer_len;

	uint16_t tmp16;
	size_t exp_size;
	int   i;

	uint16_t pkg_length;
	uint16_t pkg_type;
	uint16_t pkg_numval;

	uint8_t *pkg_types;
	value_t *pkg_values;

	if (buffer_len < 15)
	{
		NOTICE ("network plugin: packet is too short: "
				"buffer_len = %zu", buffer_len);
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
	if ((buffer_len < 0) || (buffer_len < exp_size))
	{
		WARNING ("network plugin: parse_part_values: "
				"Packet too short: "
				"Chunk of size %zu expected, "
				"but buffer has only %zu bytes left.",
				exp_size, buffer_len);
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

static int parse_part_number (void **ret_buffer, size_t *ret_buffer_len,
		uint64_t *value)
{
	char *buffer = *ret_buffer;
	size_t buffer_len = *ret_buffer_len;

	uint16_t tmp16;
	uint64_t tmp64;
	size_t exp_size = 2 * sizeof (uint16_t) + sizeof (uint64_t);

	uint16_t pkg_length;
	uint16_t pkg_type;

	if ((buffer_len < 0) || ((size_t) buffer_len < exp_size))
	{
		WARNING ("network plugin: parse_part_number: "
				"Packet too short: "
				"Chunk of size %zu expected, "
				"but buffer has only %zu bytes left.",
				exp_size, buffer_len);
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

static int parse_part_string (void **ret_buffer, size_t *ret_buffer_len,
		char *output, int output_len)
{
	char *buffer = *ret_buffer;
	size_t buffer_len = *ret_buffer_len;

	uint16_t tmp16;
	size_t header_size = 2 * sizeof (uint16_t);

	uint16_t pkg_length;
	uint16_t pkg_type;

	if ((buffer_len < 0) || (buffer_len < header_size))
	{
		WARNING ("network plugin: parse_part_string: "
				"Packet too short: "
				"Chunk of at least size %zu expected, "
				"but buffer has only %zu bytes left.",
				header_size, buffer_len);
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
				"Chunk of size %"PRIu16" received, "
				"but buffer has only %zu bytes left.",
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
	if ((output_len < 0)
			|| ((size_t) output_len < ((size_t) pkg_length - header_size)))
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

/* Forward declaration: parse_part_sign_sha256 and parse_part_encr_aes256 call
 * parse_packet and vice versa. */
#define PP_SIGNED    0x01
#define PP_ENCRYPTED 0x02
static int parse_packet (sockent_t *se,
		void *buffer, size_t buffer_size, int flags);

#define BUFFER_READ(p,s) do { \
  memcpy ((p), buffer + buffer_offset, (s)); \
  buffer_offset += (s); \
} while (0)

#if HAVE_GCRYPT_H
static int parse_part_sign_sha256 (sockent_t *se, /* {{{ */
    void **ret_buffer, size_t *ret_buffer_len, int flags)
{
  char *buffer;
  size_t buffer_len;
  size_t buffer_offset;

  part_signature_sha256_t pss;
  char hash[sizeof (pss.hash)];

  gcry_md_hd_t hd;
  gcry_error_t err;
  unsigned char *hash_ptr;

  buffer = *ret_buffer;
  buffer_len = *ret_buffer_len;
  buffer_offset = 0;

  if (se->shared_secret == NULL)
  {
    NOTICE ("network plugin: Received signed network packet but can't verify "
        "it because no shared secret has been configured. Will accept it.");
    return (0);
  }

  if (buffer_len < PART_SIGNATURE_SHA256_SIZE)
    return (-ENOMEM);

  BUFFER_READ (&pss.head.type, sizeof (pss.head.type));
  BUFFER_READ (&pss.head.length, sizeof (pss.head.length));
  BUFFER_READ (pss.hash, sizeof (pss.hash));

  assert (buffer_offset == PART_SIGNATURE_SHA256_SIZE);

  if (ntohs (pss.head.length) != PART_SIGNATURE_SHA256_SIZE)
  {
    ERROR ("network plugin: HMAC-SHA-256 with invalid length received.");
    return (-1);
  }

  hd = NULL;
  err = gcry_md_open (&hd, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC);
  if (err != 0)
  {
    ERROR ("network plugin: Creating HMAC-SHA-256 object failed: %s",
        gcry_strerror (err));
    return (-1);
  }

  err = gcry_md_setkey (hd, se->shared_secret,
      strlen (se->shared_secret));
  if (err != 0)
  {
    ERROR ("network plugin: gcry_md_setkey failed: %s",
        gcry_strerror (err));
    gcry_md_close (hd);
    return (-1);
  }

  gcry_md_write (hd, buffer + buffer_offset, buffer_len - buffer_offset);
  hash_ptr = gcry_md_read (hd, GCRY_MD_SHA256);
  if (hash_ptr == NULL)
  {
    ERROR ("network plugin: gcry_md_read failed.");
    gcry_md_close (hd);
    return (-1);
  }
  memcpy (hash, hash_ptr, sizeof (hash));

  gcry_md_close (hd);
  hd = NULL;

  if (memcmp (pss.hash, hash, sizeof (pss.hash)) != 0)
  {
    WARNING ("network plugin: Verifying HMAC-SHA-256 signature failed: "
        "Hash mismatch.");
  }
  else
  {
    parse_packet (se, buffer + buffer_offset, buffer_len - buffer_offset,
        flags | PP_SIGNED);
  }

  *ret_buffer = buffer + buffer_len;
  *ret_buffer_len = 0;

  return (0);
} /* }}} int parse_part_sign_sha256 */
/* #endif HAVE_GCRYPT_H */

#else /* if !HAVE_GCRYPT_H */
static int parse_part_sign_sha256 (sockent_t *se, /* {{{ */
    void **ret_buffer, size_t *ret_buffer_size, int flags)
{
  static int warning_has_been_printed = 0;

  char *buffer;
  size_t buffer_size;
  size_t buffer_offset;

  part_signature_sha256_t pss;

  buffer = *ret_buffer;
  buffer_size = *ret_buffer_size;
  buffer_offset = 0;

  if (buffer_size < PART_SIGNATURE_SHA256_SIZE)
    return (-ENOMEM);

  BUFFER_READ (&pss.head.type, sizeof (pss.head.type));
  BUFFER_READ (&pss.head.length, sizeof (pss.head.length));
  BUFFER_READ (pss.hash, sizeof (pss.hash));

  assert (buffer_offset == PART_SIGNATURE_SHA256_SIZE);

  if (ntohs (pss.head.length) != PART_SIGNATURE_SHA256_SIZE)
  {
    ERROR ("network plugin: HMAC-SHA-256 with invalid length received.");
    return (-1);
  }

  if (warning_has_been_printed == 0)
  {
    WARNING ("network plugin: Received signed packet, but the network "
        "plugin was not linked with libgcrypt, so I cannot "
        "verify the signature. The packet will be accepted.");
    warning_has_been_printed = 1;
  }

  parse_packet (se, buffer + buffer_offset, buffer_size - buffer_offset,
      flags);

  *ret_buffer = buffer + buffer_size;
  *ret_buffer_size = 0;

  return (0);
} /* }}} int parse_part_sign_sha256 */
#endif /* !HAVE_GCRYPT_H */

#if HAVE_GCRYPT_H
static int parse_part_encr_aes256 (sockent_t *se, /* {{{ */
		void **ret_buffer, size_t *ret_buffer_len,
		int flags)
{
  char  *buffer = *ret_buffer;
  size_t buffer_len = *ret_buffer_len;
  size_t orig_buffer_len;
  size_t part_size;
  size_t buffer_offset;
  size_t padding_size;
  part_encryption_aes256_t pea;
  unsigned char hash[sizeof (pea.hash)];

  gcry_cipher_hd_t cypher;
  gcry_error_t err;

  /* Make sure at least the header if available. */
  if (buffer_len < sizeof (pea))
  {
    NOTICE ("network plugin: parse_part_encr_aes256: "
        "Discarding short packet.");
    return (-1);
  }

  buffer_offset = 0;

  /* Copy the unencrypted information into `pea'. */
  BUFFER_READ (&pea.head.type, sizeof (pea.head.type));
  BUFFER_READ (&pea.head.length, sizeof (pea.head.length));
  BUFFER_READ (pea.iv, sizeof (pea.iv));

  /* Check the `part size'. */
  part_size = ntohs (pea.head.length);
  if (part_size > buffer_len)
  {
    NOTICE ("network plugin: parse_part_encr_aes256: "
        "Discarding large part.");
    return (-1);
  }

  cypher = network_get_aes256_cypher (se, pea.iv, sizeof (pea.iv));
  if (cypher == NULL)
    return (-1);

  /* Decrypt the packet in-place */
  err = gcry_cipher_decrypt (cypher,
      buffer    + PART_ENCRYPTION_AES256_UNENCR_SIZE,
      part_size - PART_ENCRYPTION_AES256_UNENCR_SIZE,
      /* in = */ NULL, /* in len = */ 0);
  if (err != 0)
  {
    ERROR ("network plugin: gcry_cipher_decrypt returned: %s",
        gcry_strerror (err));
    return (-1);
  }

  /* Figure out the length of the payload and the length of the padding. */
  BUFFER_READ (&pea.orig_length, sizeof (pea.orig_length));

  orig_buffer_len = ntohs (pea.orig_length);
  if (orig_buffer_len > (part_size - PART_ENCRYPTION_AES256_SIZE))
  {
    ERROR ("network plugin: Decryption failed: Invalid original length.");
    return (-1);
  }

  /* Calculate the size of the `padding' field. */
  padding_size = part_size - (orig_buffer_len + PART_ENCRYPTION_AES256_SIZE);
  if (padding_size > sizeof (pea.padding))
  {
    ERROR ("network plugin: Part- and original length "
        "differ more than %zu bytes.", sizeof (pea.padding));
    return (-1);
  }

  BUFFER_READ (pea.hash, sizeof (pea.hash));

  /* Read the padding. */
  BUFFER_READ (pea.padding, padding_size);

  /* Check hash sum */
  memset (hash, 0, sizeof (hash));
  gcry_md_hash_buffer (GCRY_MD_SHA1, hash,
      buffer + buffer_offset, orig_buffer_len);
  
  if (memcmp (hash, pea.hash, sizeof (hash)) != 0)
  {
    ERROR ("network plugin: Decryption failed: Checksum mismatch.");
    return (-1);
  }

  assert ((PART_ENCRYPTION_AES256_SIZE + padding_size + orig_buffer_len)
		  == part_size);

  parse_packet (se, buffer + PART_ENCRYPTION_AES256_SIZE + padding_size,
		  orig_buffer_len, flags | PP_ENCRYPTED);

  /* Update return values */
  *ret_buffer =     buffer     + part_size;
  *ret_buffer_len = buffer_len - part_size;

  return (0);
} /* }}} int parse_part_encr_aes256 */
/* #endif HAVE_GCRYPT_H */

#else /* if !HAVE_GCRYPT_H */
static int parse_part_encr_aes256 (sockent_t *se, /* {{{ */
    void **ret_buffer, size_t *ret_buffer_size, int flags)
{
  static int warning_has_been_printed = 0;

  char *buffer;
  size_t buffer_size;
  size_t buffer_offset;

  part_header_t ph;
  size_t ph_length;

  buffer = *ret_buffer;
  buffer_size = *ret_buffer_size;
  buffer_offset = 0;

  /* parse_packet assures this minimum size. */
  assert (buffer_size >= (sizeof (ph.type) + sizeof (ph.length)));

  BUFFER_READ (&ph.type, sizeof (ph.type));
  BUFFER_READ (&ph.length, sizeof (ph.length));
  ph_length = ntohs (ph.length);

  if ((ph_length < PART_ENCRYPTION_AES256_SIZE)
      || (ph_length > buffer_size))
  {
    ERROR ("network plugin: AES-256 encrypted part "
        "with invalid length received.");
    return (-1);
  }

  if (warning_has_been_printed == 0)
  {
    WARNING ("network plugin: Received encrypted packet, but the network "
        "plugin was not linked with libgcrypt, so I cannot "
        "decrypt it. The part will be discarded.");
    warning_has_been_printed = 1;
  }

  *ret_buffer += ph_length;
  *ret_buffer_size -= ph_length;

  return (0);
} /* }}} int parse_part_encr_aes256 */
#endif /* !HAVE_GCRYPT_H */

#undef BUFFER_READ

static int parse_packet (sockent_t *se, /* {{{ */
		void *buffer, size_t buffer_size, int flags)
{
	int status;

	value_list_t vl = VALUE_LIST_INIT;
	notification_t n;

#if HAVE_GCRYPT_H
	int packet_was_signed = (flags & PP_SIGNED);
        int packet_was_encrypted = (flags & PP_ENCRYPTED);
	int printed_ignore_warning = 0;
#endif /* HAVE_GCRYPT_H */


	memset (&vl, '\0', sizeof (vl));
	memset (&n, '\0', sizeof (n));
	status = 0;

	while ((status == 0) && (0 < buffer_size)
			&& ((unsigned int) buffer_size > sizeof (part_header_t)))
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

		if (pkg_length > buffer_size)
			break;
		/* Ensure that this loop terminates eventually */
		if (pkg_length < (2 * sizeof (uint16_t)))
			break;

		if (pkg_type == TYPE_ENCR_AES256)
		{
			status = parse_part_encr_aes256 (se,
					&buffer, &buffer_size, flags);
			if (status != 0)
			{
				ERROR ("network plugin: Decrypting AES256 "
						"part failed "
						"with status %i.", status);
				break;
			}
		}
#if HAVE_GCRYPT_H
		else if ((se->security_level == SECURITY_LEVEL_ENCRYPT)
				&& (packet_was_encrypted == 0))
		{
			if (printed_ignore_warning == 0)
			{
				INFO ("network plugin: Unencrypted packet or "
						"part has been ignored.");
				printed_ignore_warning = 1;
			}
			buffer = ((char *) buffer) + pkg_length;
			continue;
		}
#endif /* HAVE_GCRYPT_H */
		else if (pkg_type == TYPE_SIGN_SHA256)
		{
			status = parse_part_sign_sha256 (se,
                                        &buffer, &buffer_size, flags);
			if (status != 0)
			{
				ERROR ("network plugin: Verifying HMAC-SHA-256 "
						"signature failed "
						"with status %i.", status);
				break;
			}
		}
#if HAVE_GCRYPT_H
		else if ((se->security_level == SECURITY_LEVEL_SIGN)
				&& (packet_was_encrypted == 0)
				&& (packet_was_signed == 0))
		{
			if (printed_ignore_warning == 0)
			{
				INFO ("network plugin: Unsigned packet or "
						"part has been ignored.");
				printed_ignore_warning = 1;
			}
			buffer = ((char *) buffer) + pkg_length;
			continue;
		}
#endif /* HAVE_GCRYPT_H */
		else if (pkg_type == TYPE_VALUES)
		{
			status = parse_part_values (&buffer, &buffer_size,
					&vl.values, &vl.values_len);

			if (status != 0)
				break;

			if ((vl.time > 0)
					&& (strlen (vl.host) > 0)
					&& (strlen (vl.plugin) > 0)
					&& (strlen (vl.type) > 0)
					&& (cache_check (&vl) == 0))
			{
				plugin_dispatch_values (&vl);
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
			status = parse_part_number (&buffer, &buffer_size,
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
			status = parse_part_number (&buffer, &buffer_size,
					&tmp);
			if (status == 0)
				vl.interval = (int) tmp;
		}
		else if (pkg_type == TYPE_HOST)
		{
			status = parse_part_string (&buffer, &buffer_size,
					vl.host, sizeof (vl.host));
			if (status == 0)
				sstrncpy (n.host, vl.host, sizeof (n.host));
		}
		else if (pkg_type == TYPE_PLUGIN)
		{
			status = parse_part_string (&buffer, &buffer_size,
					vl.plugin, sizeof (vl.plugin));
			if (status == 0)
				sstrncpy (n.plugin, vl.plugin,
						sizeof (n.plugin));
		}
		else if (pkg_type == TYPE_PLUGIN_INSTANCE)
		{
			status = parse_part_string (&buffer, &buffer_size,
					vl.plugin_instance,
					sizeof (vl.plugin_instance));
			if (status == 0)
				sstrncpy (n.plugin_instance,
						vl.plugin_instance,
						sizeof (n.plugin_instance));
		}
		else if (pkg_type == TYPE_TYPE)
		{
			status = parse_part_string (&buffer, &buffer_size,
					vl.type, sizeof (vl.type));
			if (status == 0)
				sstrncpy (n.type, vl.type, sizeof (n.type));
		}
		else if (pkg_type == TYPE_TYPE_INSTANCE)
		{
			status = parse_part_string (&buffer, &buffer_size,
					vl.type_instance,
					sizeof (vl.type_instance));
			if (status == 0)
				sstrncpy (n.type_instance, vl.type_instance,
						sizeof (n.type_instance));
		}
		else if (pkg_type == TYPE_MESSAGE)
		{
			status = parse_part_string (&buffer, &buffer_size,
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
			status = parse_part_number (&buffer, &buffer_size,
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
	} /* while (buffer_size > sizeof (part_header_t)) */

	return (status);
} /* }}} int parse_packet */

static void free_sockent (sockent_t *se) /* {{{ */
{
	sockent_t *next;
	while (se != NULL)
	{
		next = se->next;

#if HAVE_GCRYPT_H
		if (se->cypher != NULL)
		{
			gcry_cipher_close (se->cypher);
			se->cypher = NULL;
		}
		free (se->shared_secret);
#endif /* HAVE_GCRYPT_H */

		free (se->addr);
		free (se);

		se = next;
	}
} /* }}} void free_sockent */

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
	DEBUG ("network plugin: network_set_ttl: network_config_ttl = %i;",
			network_config_ttl);

	if ((network_config_ttl < 1) || (network_config_ttl > 255))
		return (-1);

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

#define CREATE_SOCKET_FLAGS_LISTEN    0x0001
static sockent_t *network_create_socket (const char *node, /* {{{ */
		const char *service,
		const char *shared_secret,
                int security_level,
		int flags)
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
		int status;

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

		if ((flags & CREATE_SOCKET_FLAGS_LISTEN) != 0)
		{
			status = network_bind_socket (se, ai_ptr);
			if (status != 0)
			{
				close (se->fd);
				free (se->addr);
				free (se);
				continue;
			}
		}
		else /* sending socket */
		{
			network_set_ttl (se, ai_ptr);
		}

#if HAVE_GCRYPT_H
		se->security_level = security_level;
		se->shared_secret = NULL;
		se->cypher = NULL;
		if (shared_secret != NULL)
		{
			se->shared_secret = sstrdup (shared_secret);
			assert (se->shared_secret != NULL);

			memset (se->shared_secret_hash, 0,
					sizeof (se->shared_secret_hash));
			gcry_md_hash_buffer (GCRY_MD_SHA256,
					se->shared_secret_hash,
					se->shared_secret,
					strlen (se->shared_secret));
		}
#else
		/* Make compiler happy */
		security_level = 0;
		shared_secret = NULL;
#endif /* HAVE_GCRYPT_H */

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
		if ((flags & CREATE_SOCKET_FLAGS_LISTEN) == 0)
			break;
	}

	freeaddrinfo (ai_list);

	return (se_head);
} /* }}} sockent_t *network_create_socket */

static sockent_t *network_create_default_socket (int flags) /* {{{ */
{
	sockent_t *se_ptr  = NULL;
	sockent_t *se_head = NULL;
	sockent_t *se_tail = NULL;

	se_ptr = network_create_socket (NET_DEFAULT_V6_ADDR, NET_DEFAULT_PORT,
			/* shared secret = */ NULL, SECURITY_LEVEL_NONE,
                        flags);

	/* Don't send to the same machine in IPv6 and IPv4 if both are available. */
	if (((flags & CREATE_SOCKET_FLAGS_LISTEN) == 0) && (se_ptr != NULL))
		return (se_ptr);

	if (se_ptr != NULL)
	{
		se_head = se_ptr;
		se_tail = se_ptr;
		while (se_tail->next != NULL)
			se_tail = se_tail->next;
	}

	se_ptr = network_create_socket (NET_DEFAULT_V4_ADDR, NET_DEFAULT_PORT,
			/* shared secret = */ NULL, SECURITY_LEVEL_NONE,
                        flags);

	if (se_tail == NULL)
		return (se_ptr);

	se_tail->next = se_ptr;
	return (se_head);
} /* }}} sockent_t *network_create_default_socket */

static int network_add_listen_socket (const char *node, /* {{{ */
    const char *service, const char *shared_secret, int security_level)
{
	sockent_t *se;
	sockent_t *se_ptr;
	int se_num = 0;

        int flags;

        flags = CREATE_SOCKET_FLAGS_LISTEN;

	if (service == NULL)
		service = NET_DEFAULT_PORT;

	if (node == NULL)
		se = network_create_default_socket (flags);
	else
		se = network_create_socket (node, service,
                    shared_secret, security_level, flags);

	if (se == NULL)
		return (-1);

	for (se_ptr = se; se_ptr != NULL; se_ptr = se_ptr->next)
		se_num++;

	listen_sockets_pollfd = realloc (listen_sockets_pollfd,
			(listen_sockets_num + se_num)
			* sizeof (struct pollfd));

	for (se_ptr = se; se_ptr != NULL; se_ptr = se_ptr->next)
	{
		listen_sockets_pollfd[listen_sockets_num].fd = se_ptr->fd;
		listen_sockets_pollfd[listen_sockets_num].events = POLLIN | POLLPRI;
		listen_sockets_pollfd[listen_sockets_num].revents = 0;
		listen_sockets_num++;
	} /* for (se) */

	se_ptr = listen_sockets;
	while ((se_ptr != NULL) && (se_ptr->next != NULL))
		se_ptr = se_ptr->next;

	if (se_ptr == NULL)
		listen_sockets = se;
	else
		se_ptr->next = se;

	return (0);
} /* }}} int network_add_listen_socket */

static int network_add_sending_socket (const char *node, /* {{{ */
    const char *service, const char *shared_secret, int security_level)
{
	sockent_t *se;
	sockent_t *se_ptr;

	if (service == NULL)
		service = NET_DEFAULT_PORT;

	if (node == NULL)
		se = network_create_default_socket (/* flags = */ 0);
	else
		se = network_create_socket (node, service,
				shared_secret, security_level,
                                /* flags = */ 0);

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
} /* }}} int network_add_sending_socket */

static void *dispatch_thread (void __attribute__((unused)) *arg) /* {{{ */
{
  while (42)
  {
    receive_list_entry_t *ent;
    sockent_t *se;

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

    /* Look for the correct `sockent_t' */
    se = listen_sockets;
    while ((se != NULL) && (se->fd != ent->fd))
	    se = se->next;

    if (se == NULL)
    {
	    ERROR ("network plugin: Got packet from FD %i, but can't "
			    "find an appropriate socket entry.",
			    ent->fd);
	    sfree (ent);
	    continue;
    }

    parse_packet (se, ent->data, ent->data_len, /* flags = */ 0);
    sfree (ent);
  } /* while (42) */

  return (NULL);
} /* }}} void *dispatch_thread */

static int network_receive (void)
{
	char buffer[BUFF_SIZE];
	int  buffer_len;

	int i;
	int status;

	receive_list_entry_t *private_list_head;
	receive_list_entry_t *private_list_tail;

	if (listen_sockets_num == 0)
		network_add_listen_socket (/* node = */ NULL,
				/* service = */ NULL,
				/* shared secret = */ NULL,
				/* encryption = */ 0);

	if (listen_sockets_num == 0)
	{
		ERROR ("network: Failed to open a listening socket.");
		return (-1);
	}

	private_list_head = NULL;
	private_list_tail = NULL;

	while (listen_loop == 0)
	{
		status = poll (listen_sockets_pollfd, listen_sockets_num, -1);

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

			if ((listen_sockets_pollfd[i].revents
						& (POLLIN | POLLPRI)) == 0)
				continue;
			status--;

			buffer_len = recv (listen_sockets_pollfd[i].fd,
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

			/* TODO: Possible performance enhancement: Do not free
			 * these entries in the dispatch thread but put them in
			 * another list, so we don't have to allocate more and
			 * more of these structures. */
			ent = malloc (sizeof (receive_list_entry_t));
			if (ent == NULL)
			{
				ERROR ("network plugin: malloc failed.");
				return (-1);
			}
			memset (ent, 0, sizeof (receive_list_entry_t));
			ent->fd = listen_sockets_pollfd[i].fd;
			ent->next = NULL;

			/* Hopefully this be optimized out by the compiler. It
			 * might help prevent stupid bugs in the future though.
			 */
			assert (sizeof (ent->data) == sizeof (buffer));

			memcpy (ent->data, buffer, buffer_len);
			ent->data_len = buffer_len;

			if (private_list_head == NULL)
				private_list_head = ent;
			else
				private_list_tail->next = ent;
			private_list_tail = ent;

			/* Do not block here. Blocking here has led to
			 * insufficient performance in the past. */
			if (pthread_mutex_trylock (&receive_list_lock) == 0)
			{
				if (receive_list_head == NULL)
					receive_list_head = private_list_head;
				else
					receive_list_tail->next = private_list_head;
				receive_list_tail = private_list_tail;

				private_list_head = NULL;
				private_list_tail = NULL;

				pthread_cond_signal (&receive_list_cond);
				pthread_mutex_unlock (&receive_list_lock);
			}
		} /* for (listen_sockets_pollfd) */
	} /* while (listen_loop == 0) */

	/* Make sure everything is dispatched before exiting. */
	if (private_list_head != NULL)
	{
		pthread_mutex_lock (&receive_list_lock);

		if (receive_list_head == NULL)
			receive_list_head = private_list_head;
		else
			receive_list_tail->next = private_list_head;
		receive_list_tail = private_list_tail;

		private_list_head = NULL;
		private_list_tail = NULL;

		pthread_cond_signal (&receive_list_cond);
		pthread_mutex_unlock (&receive_list_lock);
	}

	return (0);
} /* int network_receive */

static void *receive_thread (void __attribute__((unused)) *arg)
{
	return (network_receive () ? (void *) 1 : (void *) 0);
} /* void *receive_thread */

static void network_init_buffer (void)
{
	memset (send_buffer, 0, sizeof (send_buffer));
	send_buffer_ptr = send_buffer;
	send_buffer_fill = 0;

	memset (&send_buffer_vl, 0, sizeof (send_buffer_vl));
} /* int network_init_buffer */

static void networt_send_buffer_plain (const sockent_t *se, /* {{{ */
		const char *buffer, size_t buffer_size)
{
	int status;

	while (42)
	{
		status = sendto (se->fd, buffer, buffer_size, 0 /* no flags */,
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
} /* }}} void networt_send_buffer_plain */

#if HAVE_GCRYPT_H
static void networt_send_buffer_signed (const sockent_t *se, /* {{{ */
		const char *in_buffer, size_t in_buffer_size)
{
	part_signature_sha256_t ps;
	char buffer[sizeof (ps) + in_buffer_size];

	gcry_md_hd_t hd;
	gcry_error_t err;
	unsigned char *hash;

	hd = NULL;
	err = gcry_md_open (&hd, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC);
	if (err != 0)
	{
		ERROR ("network plugin: Creating HMAC object failed: %s",
				gcry_strerror (err));
		return;
	}

	err = gcry_md_setkey (hd, se->shared_secret,
			strlen (se->shared_secret));
	if (err != 0)
	{
		ERROR ("network plugin: gcry_md_setkey failed: %s",
				gcry_strerror (err));
		gcry_md_close (hd);
		return;
	}

	/* Initialize the `ps' structure. */
	memset (&ps, 0, sizeof (ps));
	ps.head.type = htons (TYPE_SIGN_SHA256);
	ps.head.length = htons ((uint16_t) sizeof (ps));

	/* Calculate the hash value. */
	gcry_md_write (hd, in_buffer, in_buffer_size);
	hash = gcry_md_read (hd, GCRY_MD_SHA256);
	if (hash == NULL)
	{
		ERROR ("network plugin: gcry_md_read failed.");
		gcry_md_close (hd);
		return;
	}

	/* Add the signature and fill the rest of the buffer. */
        memcpy (ps.hash, hash, sizeof (ps.hash));
	memcpy (buffer, &ps, sizeof (ps));
	memcpy (buffer + sizeof (ps), in_buffer, in_buffer_size);

	gcry_md_close (hd);
	hd = NULL;

	networt_send_buffer_plain (se, buffer, sizeof (buffer));
} /* }}} void networt_send_buffer_signed */

static void networt_send_buffer_encrypted (sockent_t *se, /* {{{ */
		const char *in_buffer, size_t in_buffer_size)
{
  part_encryption_aes256_t pea;
  char buffer[sizeof (pea) + in_buffer_size];
  size_t buffer_size;
  size_t buffer_offset;
  size_t padding_size;
  gcry_error_t err;
  gcry_cipher_hd_t cypher;

  /* Round to the next multiple of 16, because AES has a block size of 128 bit.
   * the first 20 bytes of `pea' are not encrypted and must be subtracted. */
  buffer_size = in_buffer_size + 
    (PART_ENCRYPTION_AES256_SIZE - PART_ENCRYPTION_AES256_UNENCR_SIZE);
  padding_size = buffer_size;
  /* Round to the next multiple of 16. */
  buffer_size = (buffer_size + 15) / 16;
  buffer_size = buffer_size * 16;
  /* Calculate padding_size */
  padding_size = buffer_size - padding_size;
  assert (padding_size <= sizeof (pea.padding));
  /* Now add the unencrypted bytes. */
  buffer_size += PART_ENCRYPTION_AES256_UNENCR_SIZE;

  DEBUG ("network plugin: networt_send_buffer_encrypted: "
      "buffer_size = %zu;", buffer_size);

  /* Initialize the header fields */
  memset (&pea, 0, sizeof (pea));
  pea.head.type = htons (TYPE_ENCR_AES256);
  pea.head.length = htons ((uint16_t) buffer_size);
  pea.orig_length = htons ((uint16_t) in_buffer_size);

  /* Chose a random initialization vector. */
  gcry_randomize (&pea.iv, sizeof (pea.iv), GCRY_STRONG_RANDOM);

  /* Create hash of the payload */
  gcry_md_hash_buffer (GCRY_MD_SHA1, pea.hash, in_buffer, in_buffer_size);

  /* Fill the extra field with random values. Some entropy in the encrypted
   * data is usually not a bad thing, I hope. */
  if (padding_size > 0)
    gcry_randomize (&pea.padding, padding_size, GCRY_STRONG_RANDOM);

  /* Initialize the buffer */
  buffer_offset = 0;
  memset (buffer, 0, sizeof (buffer));

#define BUFFER_ADD(p,s) do { \
  memcpy (buffer + buffer_offset, (p), (s)); \
  buffer_offset += (s); \
} while (0)

  BUFFER_ADD (&pea.head.type, sizeof (pea.head.type));
  BUFFER_ADD (&pea.head.length, sizeof (pea.head.length));
  BUFFER_ADD (pea.iv, sizeof (pea.iv));
  BUFFER_ADD (&pea.orig_length, sizeof (pea.orig_length));
  BUFFER_ADD (pea.hash, sizeof (pea.hash));

  if (padding_size > 0)
    BUFFER_ADD (pea.padding, padding_size);

  BUFFER_ADD (in_buffer, in_buffer_size);

  assert (buffer_offset == buffer_size);

  cypher = network_get_aes256_cypher (se, pea.iv, sizeof (pea.iv));
  if (cypher == NULL)
    return;

  /* Encrypt the buffer in-place */
  err = gcry_cipher_encrypt (cypher,
      buffer      + PART_ENCRYPTION_AES256_UNENCR_SIZE,
      buffer_size - PART_ENCRYPTION_AES256_UNENCR_SIZE,
      /* in = */ NULL, /* in len = */ 0);
  if (err != 0)
  {
    ERROR ("network plugin: gcry_cipher_encrypt returned: %s",
        gcry_strerror (err));
    return;
  }

  /* Send it out without further modifications */
  networt_send_buffer_plain (se, buffer, buffer_size);
#undef BUFFER_ADD
} /* }}} void networt_send_buffer_encrypted */
#endif /* HAVE_GCRYPT_H */

static void network_send_buffer (char *buffer, size_t buffer_len) /* {{{ */
{
  sockent_t *se;

  DEBUG ("network plugin: network_send_buffer: buffer_len = %zu", buffer_len);

  for (se = sending_sockets; se != NULL; se = se->next)
  {
#if HAVE_GCRYPT_H
    if (se->security_level == SECURITY_LEVEL_ENCRYPT)
      networt_send_buffer_encrypted (se, buffer, buffer_len);
    else if (se->security_level == SECURITY_LEVEL_SIGN)
      networt_send_buffer_signed (se, buffer, buffer_len);
    else /* if (se->security_level == SECURITY_LEVEL_NONE) */
#endif /* HAVE_GCRYPT_H */
      networt_send_buffer_plain (se, buffer, buffer_len);
  } /* for (sending_sockets) */
} /* }}} void network_send_buffer */

static int add_to_buffer (char *buffer, int buffer_size, /* {{{ */
		value_list_t *vl_def,
		const data_set_t *ds, const value_list_t *vl)
{
	char *buffer_orig = buffer;

	if (strcmp (vl_def->host, vl->host) != 0)
	{
		if (write_part_string (&buffer, &buffer_size, TYPE_HOST,
					vl->host, strlen (vl->host)) != 0)
			return (-1);
		sstrncpy (vl_def->host, vl->host, sizeof (vl_def->host));
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
		sstrncpy (vl_def->plugin, vl->plugin, sizeof (vl_def->plugin));
	}

	if (strcmp (vl_def->plugin_instance, vl->plugin_instance) != 0)
	{
		if (write_part_string (&buffer, &buffer_size, TYPE_PLUGIN_INSTANCE,
					vl->plugin_instance,
					strlen (vl->plugin_instance)) != 0)
			return (-1);
		sstrncpy (vl_def->plugin_instance, vl->plugin_instance, sizeof (vl_def->plugin_instance));
	}

	if (strcmp (vl_def->type, vl->type) != 0)
	{
		if (write_part_string (&buffer, &buffer_size, TYPE_TYPE,
					vl->type, strlen (vl->type)) != 0)
			return (-1);
		sstrncpy (vl_def->type, ds->type, sizeof (vl_def->type));
	}

	if (strcmp (vl_def->type_instance, vl->type_instance) != 0)
	{
		if (write_part_string (&buffer, &buffer_size, TYPE_TYPE_INSTANCE,
					vl->type_instance,
					strlen (vl->type_instance)) != 0)
			return (-1);
		sstrncpy (vl_def->type_instance, vl->type_instance, sizeof (vl_def->type_instance));
	}
	
	if (write_part_values (&buffer, &buffer_size, ds, vl) != 0)
		return (-1);

	return (buffer - buffer_orig);
} /* }}} int add_to_buffer */

static void flush_buffer (void)
{
	DEBUG ("network plugin: flush_buffer: send_buffer_fill = %i",
			send_buffer_fill);

	network_send_buffer (send_buffer, (size_t) send_buffer_fill);
	network_init_buffer ();
}

static int network_write (const data_set_t *ds, const value_list_t *vl,
		user_data_t __attribute__((unused)) *user_data)
{
	int status;

	/* If the value is already in the cache, we have received it via the
	 * network. We write it again if forwarding is activated. It's then in
	 * the cache and should we receive it again we will ignore it. */
	status = cache_check (vl);
	if ((network_config_forward == 0)
			&& (status != 0))
		return (0);

	pthread_mutex_lock (&send_buffer_lock);

	status = add_to_buffer (send_buffer_ptr,
			sizeof (send_buffer) - (send_buffer_fill + BUFF_SIG_SIZE),
			&send_buffer_vl,
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
				sizeof (send_buffer) - (send_buffer_fill + BUFF_SIG_SIZE),
				&send_buffer_vl,
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

static int network_config_set_boolean (const oconfig_item_t *ci, /* {{{ */
    int *retval)
{
  if ((ci->values_num != 1)
      || ((ci->values[0].type != OCONFIG_TYPE_BOOLEAN)
        && (ci->values[0].type != OCONFIG_TYPE_STRING)))
  {
    ERROR ("network plugin: The `%s' config option needs "
        "exactly one boolean argument.", ci->key);
    return (-1);
  }

  if (ci->values[0].type == OCONFIG_TYPE_BOOLEAN)
  {
    if (ci->values[0].value.boolean)
      *retval = 1;
    else
      *retval = 0;
  }
  else
  {
    char *str = ci->values[0].value.string;

    if ((strcasecmp ("true", str) == 0)
        || (strcasecmp ("yes", str) == 0)
        || (strcasecmp ("on", str) == 0))
      *retval = 1;
    else if ((strcasecmp ("false", str) == 0)
        || (strcasecmp ("no", str) == 0)
        || (strcasecmp ("off", str) == 0))
      *retval = 0;
    else
    {
      ERROR ("network plugin: Cannot parse string value `%s' of the `%s' "
          "option as boolean value.",
          str, ci->key);
      return (-1);
    }
  }

  return (0);
} /* }}} int network_config_set_boolean */

static int network_config_set_ttl (const oconfig_item_t *ci) /* {{{ */
{
  int tmp;
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    WARNING ("network plugin: The `TimeToLive' config option needs exactly "
        "one numeric argument.");
    return (-1);
  }

  tmp = (int) ci->values[0].value.number;
  if ((tmp > 0) && (tmp <= 255))
    network_config_ttl = tmp;

  return (0);
} /* }}} int network_config_set_ttl */

#if HAVE_GCRYPT_H
static int network_config_set_security_level (oconfig_item_t *ci, /* {{{ */
    int *retval)
{
  char *str;
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("network plugin: The `SecurityLevel' config option needs exactly "
        "one string argument.");
    return (-1);
  }

  str = ci->values[0].value.string;
  if (strcasecmp ("Encrypt", str) == 0)
    *retval = SECURITY_LEVEL_ENCRYPT;
  else if (strcasecmp ("Sign", str) == 0)
    *retval = SECURITY_LEVEL_SIGN;
  else if (strcasecmp ("None", str) == 0)
    *retval = SECURITY_LEVEL_NONE;
  else
  {
    WARNING ("network plugin: Unknown security level: %s.", str);
    return (-1);
  }

  return (0);
} /* }}} int network_config_set_security_level */
#endif /* HAVE_GCRYPT_H */

static int network_config_listen_server (const oconfig_item_t *ci) /* {{{ */
{
  char *node;
  char *service;
  char *shared_secret = NULL;
  int security_level = SECURITY_LEVEL_NONE;
  int i;

  if ((ci->values_num < 1) || (ci->values_num > 2)
      || (ci->values[0].type != OCONFIG_TYPE_STRING)
      || ((ci->values_num > 1) && (ci->values[1].type != OCONFIG_TYPE_STRING)))
  {
    ERROR ("network plugin: The `%s' config option needs "
        "one or two string arguments.", ci->key);
    return (-1);
  }

  node = ci->values[0].value.string;
  if (ci->values_num >= 2)
    service = ci->values[1].value.string;
  else
    service = NULL;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

#if HAVE_GCRYPT_H
    if (strcasecmp ("Secret", child->key) == 0)
    {
      if ((child->values_num == 1)
          && (child->values[0].type == OCONFIG_TYPE_STRING))
        shared_secret = child->values[0].value.string;
      else
        ERROR ("network plugin: The `Secret' option needs exactly one string "
            "argument.");
    }
    else if (strcasecmp ("SecurityLevel", child->key) == 0)
      network_config_set_security_level (child, &security_level);
    else
#endif /* HAVE_GCRYPT_H */
    {
      WARNING ("network plugin: Option `%s' is not allowed here.",
          child->key);
    }
  }

  if ((security_level > SECURITY_LEVEL_NONE) && (shared_secret == NULL))
  {
    ERROR ("network plugin: A security level higher than `none' was "
        "requested, but no shared key was given. Cowardly refusing to open "
        "this socket!");
    return (-1);
  }

  if (strcasecmp ("Listen", ci->key) == 0)
    network_add_listen_socket (node, service, shared_secret, security_level);
  else
    network_add_sending_socket (node, service, shared_secret, security_level);

  return (0);
} /* }}} int network_config_listen_server */

static int network_config_set_cache_flush (const oconfig_item_t *ci) /* {{{ */
{
  int tmp;
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    WARNING ("network plugin: The `CacheFlush' config option needs exactly "
        "one numeric argument.");
    return (-1);
  }

  tmp = (int) ci->values[0].value.number;
  if (tmp > 0)
    network_config_ttl = tmp;

  return (0);
} /* }}} int network_config_set_cache_flush */

static int network_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if ((strcasecmp ("Listen", child->key) == 0)
        || (strcasecmp ("Server", child->key) == 0))
      network_config_listen_server (child);
    else if (strcasecmp ("TimeToLive", child->key) == 0)
      network_config_set_ttl (child);
    else if (strcasecmp ("Forward", child->key) == 0)
      network_config_set_boolean (child, &network_config_forward);
    else if (strcasecmp ("CacheFlush", child->key) == 0)
      network_config_set_cache_flush (child);
    else
    {
      WARNING ("network plugin: Option `%s' is not allowed here.",
          child->key);
    }
  }

  return (0);
} /* }}} int network_config */

static int network_notification (const notification_t *n,
		user_data_t __attribute__((unused)) *user_data)
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
	if (receive_thread_running != 0)
	{
		INFO ("network plugin: Stopping receive thread.");
		pthread_kill (receive_thread_id, SIGTERM);
		pthread_join (receive_thread_id, NULL /* no return value */);
		memset (&receive_thread_id, 0, sizeof (receive_thread_id));
		receive_thread_running = 0;
	}

	/* Shutdown the dispatching thread */
	if (dispatch_thread_running != 0)
	{
		INFO ("network plugin: Stopping dispatch thread.");
		pthread_mutex_lock (&receive_list_lock);
		pthread_cond_broadcast (&receive_list_cond);
		pthread_mutex_unlock (&receive_list_lock);
		pthread_join (dispatch_thread_id, /* ret = */ NULL);
		dispatch_thread_running = 0;
	}

	free_sockent (listen_sockets);

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

	network_init_buffer ();

	cache_tree = c_avl_create ((int (*) (const void *, const void *)) strcmp);
	cache_flush_last = time (NULL);

	/* setup socket(s) and so on */
	if (sending_sockets != NULL)
	{
		plugin_register_write ("network", network_write,
				/* user_data = */ NULL);
		plugin_register_notification ("network", network_notification,
				/* user_data = */ NULL);
	}

	/* If no threads need to be started, return here. */
	if ((listen_sockets_num == 0)
			|| ((dispatch_thread_running != 0)
				&& (receive_thread_running != 0)))
		return (0);

	if (dispatch_thread_running == 0)
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
		else
		{
			dispatch_thread_running = 1;
		}
	}

	if (receive_thread_running == 0)
	{
		int status;
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
		else
		{
			receive_thread_running = 1;
		}
	}

	return (0);
} /* int network_init */

/* 
 * The flush option of the network plugin cannot flush individual identifiers.
 * All the values are added to a buffer and sent when the buffer is full, the
 * requested value may or may not be in there, it's not worth finding out. We
 * just send the buffer if `flush'  is called - if the requested value was in
 * there, good. If not, well, then there is nothing to flush.. -octo
 */
static int network_flush (int timeout,
		const char __attribute__((unused)) *identifier,
		user_data_t __attribute__((unused)) *user_data)
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
	plugin_register_complex_config ("network", network_config);
	plugin_register_init   ("network", network_init);
	plugin_register_flush   ("network", network_flush,
			/* user_data = */ NULL);
} /* void module_register */

/* vim: set fdm=marker : */
