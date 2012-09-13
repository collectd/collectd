/**
 * collectd - src/network.c
 * Copyright (C) 2005-2010  Florian octo Forster
 * Copyright (C) 2009       Aman Gupta
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; only version 2.1 of the License is
 * applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 *   Aman Gupta <aman at tmm1.net>
 **/

#define _BSD_SOURCE /* For struct ip_mreq */

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_fbhash.h"
#include "utils_avltree.h"
#include "utils_cache.h"
#include "utils_complain.h"

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
#if HAVE_NET_IF_H
# include <net/if.h>
#endif

#if HAVE_LIBGCRYPT
# include <gcrypt.h>
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif

#ifndef IPV6_ADD_MEMBERSHIP
# ifdef IPV6_JOIN_GROUP
#  define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
# else
#  error "Neither IP_ADD_MEMBERSHIP nor IPV6_JOIN_GROUP is defined"
# endif
#endif /* !IP_ADD_MEMBERSHIP */

/*
 * Maximum size required for encryption / signing:
 *
 *    42 bytes for the encryption header
 * +  64 bytes for the username
 * -----------
 * = 106 bytes
 */
#define BUFF_SIG_SIZE 106

/*
 * Private data types
 */
#define SECURITY_LEVEL_NONE     0
#if HAVE_LIBGCRYPT
# define SECURITY_LEVEL_SIGN    1
# define SECURITY_LEVEL_ENCRYPT 2
#endif
struct sockent_client
{
	int fd;
	struct sockaddr_storage *addr;
	socklen_t                addrlen;
#if HAVE_LIBGCRYPT
	int security_level;
	char *username;
	char *password;
	gcry_cipher_hd_t cypher;
	unsigned char password_hash[32];
#endif
};

struct sockent_server
{
	int *fd;
	size_t fd_num;
#if HAVE_LIBGCRYPT
	int security_level;
	char *auth_file;
	fbhash_t *userdb;
	gcry_cipher_hd_t cypher;
#endif
};

typedef struct sockent
{
#define SOCKENT_TYPE_CLIENT 1
#define SOCKENT_TYPE_SERVER 2
	int type;

	char *node;
	char *service;
	int interface;

	union
	{
		struct sockent_client client;
		struct sockent_server server;
	} data;

	struct sockent *next;
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
/* Minimum size */
#define PART_SIGNATURE_SHA256_SIZE 36
struct part_signature_sha256_s
{
  part_header_t head;
  unsigned char hash[32];
  char *username;
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
/* Minimum size */
#define PART_ENCRYPTION_AES256_SIZE 42
struct part_encryption_aes256_s
{
  part_header_t head;
  uint16_t username_length;
  char *username;
  unsigned char iv[16];
  /* <encrypted> */
  unsigned char hash[20];
  /*   <payload /> */
  /* </encrypted> */
};
typedef struct part_encryption_aes256_s part_encryption_aes256_t;

struct receive_list_entry_s
{
  char *data;
  int  data_len;
  int  fd;
  struct receive_list_entry_s *next;
};
typedef struct receive_list_entry_s receive_list_entry_t;

/*
 * Private variables
 */
static int network_config_ttl = 0;
/* Ethernet - (IPv6 + UDP) = 1500 - (40 + 8) = 1452 */
static size_t network_config_packet_size = 1452;
static int network_config_forward = 0;
static int network_config_stats = 0;

static sockent_t *sending_sockets = NULL;

static receive_list_entry_t *receive_list_head = NULL;
static receive_list_entry_t *receive_list_tail = NULL;
static pthread_mutex_t       receive_list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t        receive_list_cond = PTHREAD_COND_INITIALIZER;
static uint64_t              receive_list_length = 0;

static sockent_t     *listen_sockets = NULL;
static struct pollfd *listen_sockets_pollfd = NULL;
static size_t         listen_sockets_num = 0;

/* The receive and dispatch threads will run as long as `listen_loop' is set to
 * zero. */
static int       listen_loop = 0;
static int       receive_thread_running = 0;
static pthread_t receive_thread_id;
static int       dispatch_thread_running = 0;
static pthread_t dispatch_thread_id;

/* Buffer in which to-be-sent network packets are constructed. */
static char            *send_buffer;
static char            *send_buffer_ptr;
static int              send_buffer_fill;
static value_list_t     send_buffer_vl = VALUE_LIST_STATIC;
static pthread_mutex_t  send_buffer_lock = PTHREAD_MUTEX_INITIALIZER;

/* XXX: These counters are incremented from one place only. The spot in which
 * the values are incremented is either only reachable by one thread (the
 * dispatch thread, for example) or locked by some lock (send_buffer_lock for
 * example). Only if neither is true, the stats_lock is acquired. The counters
 * are always read without holding a lock in the hope that writing 8 bytes to
 * memory is an atomic operation. */
static derive_t stats_octets_rx  = 0;
static derive_t stats_octets_tx  = 0;
static derive_t stats_packets_rx = 0;
static derive_t stats_packets_tx = 0;
static derive_t stats_values_dispatched = 0;
static derive_t stats_values_not_dispatched = 0;
static derive_t stats_values_sent = 0;
static derive_t stats_values_not_sent = 0;
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Private functions
 */
static _Bool check_receive_okay (const value_list_t *vl) /* {{{ */
{
  uint64_t time_sent = 0;
  int status;

  status = uc_meta_data_get_unsigned_int (vl,
      "network:time_sent", &time_sent);

  /* This is a value we already sent. Don't allow it to be received again in
   * order to avoid looping. */
  if ((status == 0) && (time_sent >= ((uint64_t) vl->time)))
    return (0);

  return (1);
} /* }}} _Bool check_receive_okay */

static _Bool check_send_okay (const value_list_t *vl) /* {{{ */
{
  _Bool received = 0;
  int status;

  if (network_config_forward != 0)
    return (1);

  if (vl->meta == NULL)
    return (1);

  status = meta_data_get_boolean (vl->meta, "network:received", &received);
  if (status == -ENOENT)
    return (1);
  else if (status != 0)
  {
    ERROR ("network plugin: check_send_okay: meta_data_get_boolean failed "
	"with status %i.", status);
    return (1);
  }

  /* By default, only *send* value lists that were not *received* by the
   * network plugin. */
  return (!received);
} /* }}} _Bool check_send_okay */

static _Bool check_notify_received (const notification_t *n) /* {{{ */
{
  notification_meta_t *ptr;

  for (ptr = n->meta; ptr != NULL; ptr = ptr->next)
    if ((strcmp ("network:received", ptr->name) == 0)
        && (ptr->type == NM_TYPE_BOOLEAN))
      return ((_Bool) ptr->nm_value.nm_boolean);

  return (0);
} /* }}} _Bool check_notify_received */

static _Bool check_send_notify_okay (const notification_t *n) /* {{{ */
{
  static c_complain_t complain_forwarding = C_COMPLAIN_INIT_STATIC;
  _Bool received = 0;

  if (n->meta == NULL)
    return (1);

  received = check_notify_received (n);

  if (network_config_forward && received)
  {
    c_complain_once (LOG_ERR, &complain_forwarding,
        "network plugin: A notification has been received via the network "
        "forwarding if enabled. Forwarding of notifications is currently "
        "not supported, because there is not loop-deteciton available. "
        "Please contact the collectd mailing list if you need this "
        "feature.");
  }

  /* By default, only *send* value lists that were not *received* by the
   * network plugin. */
  return (!received);
} /* }}} _Bool check_send_notify_okay */

static int network_dispatch_values (value_list_t *vl, /* {{{ */
    const char *username)
{
  int status;

  if ((vl->time <= 0)
      || (strlen (vl->host) <= 0)
      || (strlen (vl->plugin) <= 0)
      || (strlen (vl->type) <= 0))
    return (-EINVAL);

  if (!check_receive_okay (vl))
  {
#if COLLECT_DEBUG
    char name[6*DATA_MAX_NAME_LEN];
    FORMAT_VL (name, sizeof (name), vl);
    name[sizeof (name) - 1] = 0;
    DEBUG ("network plugin: network_dispatch_values: "
	"NOT dispatching %s.", name);
#endif
    stats_values_not_dispatched++;
    return (0);
  }

  assert (vl->meta == NULL);

  vl->meta = meta_data_create ();
  if (vl->meta == NULL)
  {
    ERROR ("network plugin: meta_data_create failed.");
    return (-ENOMEM);
  }

  status = meta_data_add_boolean (vl->meta, "network:received", 1);
  if (status != 0)
  {
    ERROR ("network plugin: meta_data_add_boolean failed.");
    meta_data_destroy (vl->meta);
    vl->meta = NULL;
    return (status);
  }

  if (username != NULL)
  {
    status = meta_data_add_string (vl->meta, "network:username", username);
    if (status != 0)
    {
      ERROR ("network plugin: meta_data_add_string failed.");
      meta_data_destroy (vl->meta);
      vl->meta = NULL;
      return (status);
    }
  }

  plugin_dispatch_values_secure (vl);
  stats_values_dispatched++;

  meta_data_destroy (vl->meta);
  vl->meta = NULL;

  return (0);
} /* }}} int network_dispatch_values */

static int network_dispatch_notification (notification_t *n) /* {{{ */
{
  int status;

  assert (n->meta == NULL);

  status = plugin_notification_meta_add_boolean (n, "network:received", 1);
  if (status != 0)
  {
    ERROR ("network plugin: plugin_notification_meta_add_boolean failed.");
    plugin_notification_meta_free (n->meta);
    n->meta = NULL;
    return (status);
  }

  status = plugin_dispatch_notification (n);

  plugin_notification_meta_free (n->meta);
  n->meta = NULL;

  return (status);
} /* }}} int network_dispatch_notification */

#if HAVE_LIBGCRYPT
static gcry_cipher_hd_t network_get_aes256_cypher (sockent_t *se, /* {{{ */
    const void *iv, size_t iv_size, const char *username)
{
  gcry_error_t err;
  gcry_cipher_hd_t *cyper_ptr;
  unsigned char password_hash[32];

  if (se->type == SOCKENT_TYPE_CLIENT)
  {
	  cyper_ptr = &se->data.client.cypher;
	  memcpy (password_hash, se->data.client.password_hash,
			  sizeof (password_hash));
  }
  else
  {
	  char *secret;

	  cyper_ptr = &se->data.server.cypher;

	  if (username == NULL)
		  return (NULL);

	  secret = fbh_get (se->data.server.userdb, username);
	  if (secret == NULL)
		  return (NULL);

	  gcry_md_hash_buffer (GCRY_MD_SHA256,
			  password_hash,
			  secret, strlen (secret));

	  sfree (secret);
  }

  if (*cyper_ptr == NULL)
  {
    err = gcry_cipher_open (cyper_ptr,
        GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_OFB, /* flags = */ 0);
    if (err != 0)
    {
      ERROR ("network plugin: gcry_cipher_open returned: %s",
          gcry_strerror (err));
      *cyper_ptr = NULL;
      return (NULL);
    }
  }
  else
  {
    gcry_cipher_reset (*cyper_ptr);
  }
  assert (*cyper_ptr != NULL);

  err = gcry_cipher_setkey (*cyper_ptr,
      password_hash, sizeof (password_hash));
  if (err != 0)
  {
    ERROR ("network plugin: gcry_cipher_setkey returned: %s",
        gcry_strerror (err));
    gcry_cipher_close (*cyper_ptr);
    *cyper_ptr = NULL;
    return (NULL);
  }

  err = gcry_cipher_setiv (*cyper_ptr, iv, iv_size);
  if (err != 0)
  {
    ERROR ("network plugin: gcry_cipher_setkey returned: %s",
        gcry_strerror (err));
    gcry_cipher_close (*cyper_ptr);
    *cyper_ptr = NULL;
    return (NULL);
  }

  return (*cyper_ptr);
} /* }}} int network_get_aes256_cypher */
#endif /* HAVE_LIBGCRYPT */

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
		pkg_values_types[i] = (uint8_t) ds->ds[i].type;
		switch (ds->ds[i].type)
		{
			case DS_TYPE_COUNTER:
				pkg_values[i].counter = htonll (vl->values[i].counter);
				break;

			case DS_TYPE_GAUGE:
				pkg_values[i].gauge = htond (vl->values[i].gauge);
				break;

			case DS_TYPE_DERIVE:
				pkg_values[i].derive = htonll (vl->values[i].derive);
				break;

			case DS_TYPE_ABSOLUTE:
				pkg_values[i].absolute = htonll (vl->values[i].absolute);
				break;

			default:
				free (pkg_values_types);
				free (pkg_values);
				ERROR ("network plugin: write_part_values: "
						"Unknown data source type: %i",
						ds->ds[i].type);
				return (-1);
		} /* switch (ds->ds[i].type) */
	} /* for (num_values) */

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
	if (buffer_len < exp_size)
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
		switch (pkg_types[i])
		{
		  case DS_TYPE_COUNTER:
		    pkg_values[i].counter = (counter_t) ntohll (pkg_values[i].counter);
		    break;

		  case DS_TYPE_GAUGE:
		    pkg_values[i].gauge = (gauge_t) ntohd (pkg_values[i].gauge);
		    break;

		  case DS_TYPE_DERIVE:
		    pkg_values[i].derive = (derive_t) ntohll (pkg_values[i].derive);
		    break;

		  case DS_TYPE_ABSOLUTE:
		    pkg_values[i].absolute = (absolute_t) ntohll (pkg_values[i].absolute);
		    break;

		  default:
		    NOTICE ("network plugin: parse_part_values: "
			"Don't know how to handle data source type %"PRIu8,
			pkg_types[i]);
		    sfree (pkg_types);
		    sfree (pkg_values);
		    return (-1);
		} /* switch (pkg_types[i]) */
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

	if (buffer_len < exp_size)
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
	/* pkg_type = ntohs (tmp16); */

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

	if (buffer_len < header_size)
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
	/* pkg_type = ntohs (tmp16); */

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
		void *buffer, size_t buffer_size, int flags,
		const char *username);

#define BUFFER_READ(p,s) do { \
  memcpy ((p), buffer + buffer_offset, (s)); \
  buffer_offset += (s); \
} while (0)

#if HAVE_LIBGCRYPT
static int parse_part_sign_sha256 (sockent_t *se, /* {{{ */
    void **ret_buffer, size_t *ret_buffer_len, int flags)
{
  static c_complain_t complain_no_users = C_COMPLAIN_INIT_STATIC;

  char *buffer;
  size_t buffer_len;
  size_t buffer_offset;

  size_t username_len;
  char *secret;

  part_signature_sha256_t pss;
  uint16_t pss_head_length;
  char hash[sizeof (pss.hash)];

  gcry_md_hd_t hd;
  gcry_error_t err;
  unsigned char *hash_ptr;

  buffer = *ret_buffer;
  buffer_len = *ret_buffer_len;
  buffer_offset = 0;

  if (se->data.server.userdb == NULL)
  {
    c_complain (LOG_NOTICE, &complain_no_users,
        "network plugin: Received signed network packet but can't verify it "
        "because no user DB has been configured. Will accept it.");
    return (0);
  }

  /* Check if the buffer has enough data for this structure. */
  if (buffer_len <= PART_SIGNATURE_SHA256_SIZE)
    return (-ENOMEM);

  /* Read type and length header */
  BUFFER_READ (&pss.head.type, sizeof (pss.head.type));
  BUFFER_READ (&pss.head.length, sizeof (pss.head.length));
  pss_head_length = ntohs (pss.head.length);

  /* Check if the `pss_head_length' is within bounds. */
  if ((pss_head_length <= PART_SIGNATURE_SHA256_SIZE)
      || (pss_head_length > buffer_len))
  {
    ERROR ("network plugin: HMAC-SHA-256 with invalid length received.");
    return (-1);
  }

  /* Copy the hash. */
  BUFFER_READ (pss.hash, sizeof (pss.hash));

  /* Calculate username length (without null byte) and allocate memory */
  username_len = pss_head_length - PART_SIGNATURE_SHA256_SIZE;
  pss.username = malloc (username_len + 1);
  if (pss.username == NULL)
    return (-ENOMEM);

  /* Read the username */
  BUFFER_READ (pss.username, username_len);
  pss.username[username_len] = 0;

  assert (buffer_offset == pss_head_length);

  /* Query the password */
  secret = fbh_get (se->data.server.userdb, pss.username);
  if (secret == NULL)
  {
    ERROR ("network plugin: Unknown user: %s", pss.username);
    sfree (pss.username);
    return (-ENOENT);
  }

  /* Create a hash device and check the HMAC */
  hd = NULL;
  err = gcry_md_open (&hd, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC);
  if (err != 0)
  {
    ERROR ("network plugin: Creating HMAC-SHA-256 object failed: %s",
        gcry_strerror (err));
    sfree (secret);
    sfree (pss.username);
    return (-1);
  }

  err = gcry_md_setkey (hd, secret, strlen (secret));
  if (err != 0)
  {
    ERROR ("network plugin: gcry_md_setkey failed: %s", gcry_strerror (err));
    gcry_md_close (hd);
    sfree (secret);
    sfree (pss.username);
    return (-1);
  }

  gcry_md_write (hd,
      buffer     + PART_SIGNATURE_SHA256_SIZE,
      buffer_len - PART_SIGNATURE_SHA256_SIZE);
  hash_ptr = gcry_md_read (hd, GCRY_MD_SHA256);
  if (hash_ptr == NULL)
  {
    ERROR ("network plugin: gcry_md_read failed.");
    gcry_md_close (hd);
    sfree (secret);
    sfree (pss.username);
    return (-1);
  }
  memcpy (hash, hash_ptr, sizeof (hash));

  /* Clean up */
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
        flags | PP_SIGNED, pss.username);
  }

  sfree (secret);
  sfree (pss.username);

  *ret_buffer = buffer + buffer_len;
  *ret_buffer_len = 0;

  return (0);
} /* }}} int parse_part_sign_sha256 */
/* #endif HAVE_LIBGCRYPT */

#else /* if !HAVE_LIBGCRYPT */
static int parse_part_sign_sha256 (sockent_t *se, /* {{{ */
    void **ret_buffer, size_t *ret_buffer_size, int flags)
{
  static int warning_has_been_printed = 0;

  char *buffer;
  size_t buffer_size;
  size_t buffer_offset;
  uint16_t part_len;

  part_signature_sha256_t pss;

  buffer = *ret_buffer;
  buffer_size = *ret_buffer_size;
  buffer_offset = 0;

  if (buffer_size <= PART_SIGNATURE_SHA256_SIZE)
    return (-ENOMEM);

  BUFFER_READ (&pss.head.type, sizeof (pss.head.type));
  BUFFER_READ (&pss.head.length, sizeof (pss.head.length));
  part_len = ntohs (pss.head.length);

  if ((part_len <= PART_SIGNATURE_SHA256_SIZE)
      || (part_len > buffer_size))
    return (-EINVAL);

  if (warning_has_been_printed == 0)
  {
    WARNING ("network plugin: Received signed packet, but the network "
        "plugin was not linked with libgcrypt, so I cannot "
        "verify the signature. The packet will be accepted.");
    warning_has_been_printed = 1;
  }

  parse_packet (se, buffer + part_len, buffer_size - part_len, flags,
      /* username = */ NULL);

  *ret_buffer = buffer + buffer_size;
  *ret_buffer_size = 0;

  return (0);
} /* }}} int parse_part_sign_sha256 */
#endif /* !HAVE_LIBGCRYPT */

#if HAVE_LIBGCRYPT
static int parse_part_encr_aes256 (sockent_t *se, /* {{{ */
		void **ret_buffer, size_t *ret_buffer_len,
		int flags)
{
  char  *buffer = *ret_buffer;
  size_t buffer_len = *ret_buffer_len;
  size_t payload_len;
  size_t part_size;
  size_t buffer_offset;
  uint16_t username_len;
  part_encryption_aes256_t pea;
  unsigned char hash[sizeof (pea.hash)];

  gcry_cipher_hd_t cypher;
  gcry_error_t err;

  /* Make sure at least the header if available. */
  if (buffer_len <= PART_ENCRYPTION_AES256_SIZE)
  {
    NOTICE ("network plugin: parse_part_encr_aes256: "
        "Discarding short packet.");
    return (-1);
  }

  buffer_offset = 0;

  /* Copy the unencrypted information into `pea'. */
  BUFFER_READ (&pea.head.type, sizeof (pea.head.type));
  BUFFER_READ (&pea.head.length, sizeof (pea.head.length));

  /* Check the `part size'. */
  part_size = ntohs (pea.head.length);
  if ((part_size <= PART_ENCRYPTION_AES256_SIZE)
      || (part_size > buffer_len))
  {
    NOTICE ("network plugin: parse_part_encr_aes256: "
        "Discarding part with invalid size.");
    return (-1);
  }

  /* Read the username */
  BUFFER_READ (&username_len, sizeof (username_len));
  username_len = ntohs (username_len);

  if ((username_len <= 0)
      || (username_len > (part_size - (PART_ENCRYPTION_AES256_SIZE + 1))))
  {
    NOTICE ("network plugin: parse_part_encr_aes256: "
        "Discarding part with invalid username length.");
    return (-1);
  }

  assert (username_len > 0);
  pea.username = malloc (username_len + 1);
  if (pea.username == NULL)
    return (-ENOMEM);
  BUFFER_READ (pea.username, username_len);
  pea.username[username_len] = 0;

  /* Last but not least, the initialization vector */
  BUFFER_READ (pea.iv, sizeof (pea.iv));

  /* Make sure we are at the right position */
  assert (buffer_offset == (username_len +
        PART_ENCRYPTION_AES256_SIZE - sizeof (pea.hash)));

  cypher = network_get_aes256_cypher (se, pea.iv, sizeof (pea.iv),
      pea.username);
  if (cypher == NULL)
  {
    sfree (pea.username);
    return (-1);
  }

  payload_len = part_size - (PART_ENCRYPTION_AES256_SIZE + username_len);
  assert (payload_len > 0);

  /* Decrypt the packet in-place */
  err = gcry_cipher_decrypt (cypher,
      buffer    + buffer_offset,
      part_size - buffer_offset,
      /* in = */ NULL, /* in len = */ 0);
  if (err != 0)
  {
    sfree (pea.username);
    ERROR ("network plugin: gcry_cipher_decrypt returned: %s",
        gcry_strerror (err));
    return (-1);
  }

  /* Read the hash */
  BUFFER_READ (pea.hash, sizeof (pea.hash));

  /* Make sure we're at the right position - again */
  assert (buffer_offset == (username_len + PART_ENCRYPTION_AES256_SIZE));
  assert (buffer_offset == (part_size - payload_len));

  /* Check hash sum */
  memset (hash, 0, sizeof (hash));
  gcry_md_hash_buffer (GCRY_MD_SHA1, hash,
      buffer + buffer_offset, payload_len);
  if (memcmp (hash, pea.hash, sizeof (hash)) != 0)
  {
    sfree (pea.username);
    ERROR ("network plugin: Decryption failed: Checksum mismatch.");
    return (-1);
  }

  parse_packet (se, buffer + buffer_offset, payload_len,
      flags | PP_ENCRYPTED, pea.username);

  /* XXX: Free pea.username?!? */

  /* Update return values */
  *ret_buffer =     buffer     + part_size;
  *ret_buffer_len = buffer_len - part_size;

  sfree (pea.username);

  return (0);
} /* }}} int parse_part_encr_aes256 */
/* #endif HAVE_LIBGCRYPT */

#else /* if !HAVE_LIBGCRYPT */
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

  if ((ph_length <= PART_ENCRYPTION_AES256_SIZE)
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
#endif /* !HAVE_LIBGCRYPT */

#undef BUFFER_READ

static int parse_packet (sockent_t *se, /* {{{ */
		void *buffer, size_t buffer_size, int flags,
		const char *username)
{
	int status;

	value_list_t vl = VALUE_LIST_INIT;
	notification_t n;

#if HAVE_LIBGCRYPT
	int packet_was_signed = (flags & PP_SIGNED);
        int packet_was_encrypted = (flags & PP_ENCRYPTED);
	int printed_ignore_warning = 0;
#endif /* HAVE_LIBGCRYPT */


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
#if HAVE_LIBGCRYPT
		else if ((se->data.server.security_level == SECURITY_LEVEL_ENCRYPT)
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
#endif /* HAVE_LIBGCRYPT */
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
#if HAVE_LIBGCRYPT
		else if ((se->data.server.security_level == SECURITY_LEVEL_SIGN)
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
#endif /* HAVE_LIBGCRYPT */
		else if (pkg_type == TYPE_VALUES)
		{
			status = parse_part_values (&buffer, &buffer_size,
					&vl.values, &vl.values_len);
			if (status != 0)
				break;

			network_dispatch_values (&vl, username);

			sfree (vl.values);
		}
		else if (pkg_type == TYPE_TIME)
		{
			uint64_t tmp = 0;
			status = parse_part_number (&buffer, &buffer_size,
					&tmp);
			if (status == 0)
			{
				vl.time = TIME_T_TO_CDTIME_T (tmp);
				n.time  = TIME_T_TO_CDTIME_T (tmp);
			}
		}
		else if (pkg_type == TYPE_TIME_HR)
		{
			uint64_t tmp = 0;
			status = parse_part_number (&buffer, &buffer_size,
					&tmp);
			if (status == 0)
			{
				vl.time = (cdtime_t) tmp;
				n.time  = (cdtime_t) tmp;
			}
		}
		else if (pkg_type == TYPE_INTERVAL)
		{
			uint64_t tmp = 0;
			status = parse_part_number (&buffer, &buffer_size,
					&tmp);
			if (status == 0)
				vl.interval = TIME_T_TO_CDTIME_T (tmp);
		}
		else if (pkg_type == TYPE_INTERVAL_HR)
		{
			uint64_t tmp = 0;
			status = parse_part_number (&buffer, &buffer_size,
					&tmp);
			if (status == 0)
				vl.interval = (cdtime_t) tmp;
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
				network_dispatch_notification (&n);
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

	if (status == 0 && buffer_size > 0)
		WARNING ("network plugin: parse_packet: Received truncated "
				"packet, try increasing `MaxPacketSize'");

	return (status);
} /* }}} int parse_packet */

static void free_sockent_client (struct sockent_client *sec) /* {{{ */
{
  if (sec->fd >= 0)
  {
    close (sec->fd);
    sec->fd = -1;
  }
  sfree (sec->addr);
#if HAVE_LIBGCRYPT
  sfree (sec->username);
  sfree (sec->password);
  if (sec->cypher != NULL)
    gcry_cipher_close (sec->cypher);
#endif
} /* }}} void free_sockent_client */

static void free_sockent_server (struct sockent_server *ses) /* {{{ */
{
  size_t i;

  for (i = 0; i < ses->fd_num; i++)
  {
    if (ses->fd[i] >= 0)
    {
      close (ses->fd[i]);
      ses->fd[i] = -1;
    }
  }

  sfree (ses->fd);
#if HAVE_LIBGCRYPT
  sfree (ses->auth_file);
  fbh_destroy (ses->userdb);
  if (ses->cypher != NULL)
    gcry_cipher_close (ses->cypher);
#endif
} /* }}} void free_sockent_server */

static void sockent_destroy (sockent_t *se) /* {{{ */
{
  sockent_t *next;

  DEBUG ("network plugin: sockent_destroy (se = %p);", (void *) se);

  while (se != NULL)
  {
    next = se->next;

    sfree (se->node);
    sfree (se->service);

    if (se->type == SOCKENT_TYPE_CLIENT)
      free_sockent_client (&se->data.client);
    else
      free_sockent_server (&se->data.server);

    sfree (se);
    se = next;
  }
} /* }}} void sockent_destroy */

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

        assert (se->type == SOCKENT_TYPE_CLIENT);

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

		if (setsockopt (se->data.client.fd, IPPROTO_IP, optname,
					&network_config_ttl,
					sizeof (network_config_ttl)) != 0)
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

		if (setsockopt (se->data.client.fd, IPPROTO_IPV6, optname,
					&network_config_ttl,
					sizeof (network_config_ttl)) != 0)
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

static int network_set_interface (const sockent_t *se, const struct addrinfo *ai) /* {{{ */
{
	DEBUG ("network plugin: network_set_interface: interface index = %i;",
			se->interface);

        assert (se->type == SOCKENT_TYPE_CLIENT);

	if (ai->ai_family == AF_INET)
	{
		struct sockaddr_in *addr = (struct sockaddr_in *) ai->ai_addr;

		if (IN_MULTICAST (ntohl (addr->sin_addr.s_addr)))
		{
#if HAVE_STRUCT_IP_MREQN_IMR_IFINDEX
			/* If possible, use the "ip_mreqn" structure which has
			 * an "interface index" member. Using the interface
			 * index is preferred here, because of its similarity
			 * to the way IPv6 handles this. Unfortunately, it
			 * appears not to be portable. */
			struct ip_mreqn mreq;

			memset (&mreq, 0, sizeof (mreq));
			mreq.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
			mreq.imr_address.s_addr = ntohl (INADDR_ANY);
			mreq.imr_ifindex = se->interface;
#else
			struct ip_mreq mreq;

			memset (&mreq, 0, sizeof (mreq));
			mreq.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
			mreq.imr_interface.s_addr = ntohl (INADDR_ANY);
#endif

			if (setsockopt (se->data.client.fd, IPPROTO_IP, IP_MULTICAST_IF,
						&mreq, sizeof (mreq)) != 0)
			{
				char errbuf[1024];
				ERROR ("setsockopt: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
				return (-1);
			}

			return (0);
		}
	}
	else if (ai->ai_family == AF_INET6)
	{
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) ai->ai_addr;

		if (IN6_IS_ADDR_MULTICAST (&addr->sin6_addr))
		{
			if (setsockopt (se->data.client.fd, IPPROTO_IPV6, IPV6_MULTICAST_IF,
						&se->interface,
						sizeof (se->interface)) != 0)
			{
				char errbuf[1024];
				ERROR ("setsockopt: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}

			return (0);
		}
	}

	/* else: Not a multicast interface. */
	if (se->interface != 0)
	{
#if defined(HAVE_IF_INDEXTONAME) && HAVE_IF_INDEXTONAME && defined(SO_BINDTODEVICE)
		char interface_name[IFNAMSIZ];

		if (if_indextoname (se->interface, interface_name) == NULL)
			return (-1);

		DEBUG ("network plugin: Binding socket to interface %s", interface_name);

		if (setsockopt (se->data.client.fd, SOL_SOCKET, SO_BINDTODEVICE,
					interface_name,
					sizeof(interface_name)) == -1 )
		{
			char errbuf[1024];
			ERROR ("setsockopt: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			return (-1);
		}
/* #endif HAVE_IF_INDEXTONAME && SO_BINDTODEVICE */

#else
		WARNING ("network plugin: Cannot set the interface on a unicast "
			"socket because "
# if !defined(SO_BINDTODEVICE)
			"the \"SO_BINDTODEVICE\" socket option "
# else
			"the \"if_indextoname\" function "
# endif
			"is not available on your system.");
#endif

	}

	return (0);
} /* }}} network_set_interface */

static int network_bind_socket (int fd, const struct addrinfo *ai, const int interface_idx)
{
	int loop = 0;
	int yes  = 1;

	/* allow multiple sockets to use the same PORT number */
	if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR,
				&yes, sizeof(yes)) == -1) {
                char errbuf[1024];
                ERROR ("setsockopt: %s", 
                                sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	DEBUG ("fd = %i; calling `bind'", fd);

	if (bind (fd, ai->ai_addr, ai->ai_addrlen) == -1)
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
#if HAVE_STRUCT_IP_MREQN_IMR_IFINDEX
			struct ip_mreqn mreq;
#else
			struct ip_mreq mreq;
#endif

			DEBUG ("fd = %i; IPv4 multicast address found", fd);

			mreq.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
#if HAVE_STRUCT_IP_MREQN_IMR_IFINDEX
			/* Set the interface using the interface index if
			 * possible (available). Unfortunately, the struct
			 * ip_mreqn is not portable. */
			mreq.imr_address.s_addr = ntohl (INADDR_ANY);
			mreq.imr_ifindex = interface_idx;
#else
			mreq.imr_interface.s_addr = ntohl (INADDR_ANY);
#endif

			if (setsockopt (fd, IPPROTO_IP, IP_MULTICAST_LOOP,
						&loop, sizeof (loop)) == -1)
			{
				char errbuf[1024];
				ERROR ("setsockopt: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}

			if (setsockopt (fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
						&mreq, sizeof (mreq)) == -1)
			{
				char errbuf[1024];
				ERROR ("setsockopt: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}

			return (0);
		}
	}
	else if (ai->ai_family == AF_INET6)
	{
		/* Useful example: http://gsyc.escet.urjc.es/~eva/IPv6-web/examples/mcast.html */
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) ai->ai_addr;
		if (IN6_IS_ADDR_MULTICAST (&addr->sin6_addr))
		{
			struct ipv6_mreq mreq;

			DEBUG ("fd = %i; IPv6 multicast address found", fd);

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
			mreq.ipv6mr_interface = interface_idx;

			if (setsockopt (fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
						&loop, sizeof (loop)) == -1)
			{
				char errbuf[1024];
				ERROR ("setsockopt: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}

			if (setsockopt (fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
						&mreq, sizeof (mreq)) == -1)
			{
				char errbuf[1024];
				ERROR ("setsockopt: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}

			return (0);
		}
	}

#if defined(HAVE_IF_INDEXTONAME) && HAVE_IF_INDEXTONAME && defined(SO_BINDTODEVICE)
	/* if a specific interface was set, bind the socket to it. But to avoid
 	 * possible problems with multicast routing, only do that for non-multicast
	 * addresses */
	if (interface_idx != 0)
	{
		char interface_name[IFNAMSIZ];

		if (if_indextoname (interface_idx, interface_name) == NULL)
			return (-1);

		DEBUG ("fd = %i; Binding socket to interface %s", fd, interface_name);

		if (setsockopt (fd, SOL_SOCKET, SO_BINDTODEVICE,
					interface_name,
					sizeof(interface_name)) == -1 )
		{
			char errbuf[1024];
			ERROR ("setsockopt: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			return (-1);
		}
	}
#endif /* HAVE_IF_INDEXTONAME && SO_BINDTODEVICE */

	return (0);
} /* int network_bind_socket */

/* Initialize a sockent structure. `type' must be either `SOCKENT_TYPE_CLIENT'
 * or `SOCKENT_TYPE_SERVER' */
static int sockent_init (sockent_t *se, int type) /* {{{ */
{
	if (se == NULL)
		return (-1);

	memset (se, 0, sizeof (*se));

	se->type = SOCKENT_TYPE_CLIENT;
	se->node = NULL;
	se->service = NULL;
	se->interface = 0;
	se->next = NULL;

	if (type == SOCKENT_TYPE_SERVER)
	{
		se->type = SOCKENT_TYPE_SERVER;
		se->data.server.fd = NULL;
#if HAVE_LIBGCRYPT
		se->data.server.security_level = SECURITY_LEVEL_NONE;
		se->data.server.auth_file = NULL;
		se->data.server.userdb = NULL;
		se->data.server.cypher = NULL;
#endif
	}
	else
	{
		se->data.client.fd = -1;
		se->data.client.addr = NULL;
#if HAVE_LIBGCRYPT
		se->data.client.security_level = SECURITY_LEVEL_NONE;
		se->data.client.username = NULL;
		se->data.client.password = NULL;
		se->data.client.cypher = NULL;
#endif
	}

	return (0);
} /* }}} int sockent_init */

/* Open the file descriptors for a initialized sockent structure. */
static int sockent_open (sockent_t *se) /* {{{ */
{
	struct addrinfo  ai_hints;
	struct addrinfo *ai_list, *ai_ptr;
	int              ai_return;

        const char *node;
        const char *service;

	if (se == NULL)
		return (-1);

	/* Set up the security structures. */
#if HAVE_LIBGCRYPT /* {{{ */
	if (se->type == SOCKENT_TYPE_CLIENT)
	{
		if (se->data.client.security_level > SECURITY_LEVEL_NONE)
		{
			if ((se->data.client.username == NULL)
					|| (se->data.client.password == NULL))
			{
				ERROR ("network plugin: Client socket with "
						"security requested, but no "
						"credentials are configured.");
				return (-1);
			}
			gcry_md_hash_buffer (GCRY_MD_SHA256,
					se->data.client.password_hash,
					se->data.client.password,
					strlen (se->data.client.password));
		}
	}
	else /* (se->type == SOCKENT_TYPE_SERVER) */
	{
		if (se->data.server.security_level > SECURITY_LEVEL_NONE)
		{
			if (se->data.server.auth_file == NULL)
			{
				ERROR ("network plugin: Server socket with "
						"security requested, but no "
						"password file is configured.");
				return (-1);
			}
		}
		if (se->data.server.auth_file != NULL)
		{
			se->data.server.userdb = fbh_create (se->data.server.auth_file);
			if (se->data.server.userdb == NULL)
			{
				ERROR ("network plugin: Reading password file "
						"`%s' failed.",
						se->data.server.auth_file);
				if (se->data.server.security_level > SECURITY_LEVEL_NONE)
					return (-1);
			}
		}
	}
#endif /* }}} HAVE_LIBGCRYPT */

        node = se->node;
        service = se->service;

        if (service == NULL)
          service = NET_DEFAULT_PORT;

        DEBUG ("network plugin: sockent_open: node = %s; service = %s;",
            node, service);

	memset (&ai_hints, 0, sizeof (ai_hints));
	ai_hints.ai_flags  = 0;
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
		ERROR ("network plugin: getaddrinfo (%s, %s) failed: %s",
				(se->node == NULL) ? "(null)" : se->node,
				(se->service == NULL) ? "(null)" : se->service,
				gai_strerror (ai_return));
		return (-1);
	}

	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		int status;

		if (se->type == SOCKENT_TYPE_SERVER) /* {{{ */
		{
			int *tmp;

			tmp = realloc (se->data.server.fd,
					sizeof (*tmp) * (se->data.server.fd_num + 1));
			if (tmp == NULL)
			{
				ERROR ("network plugin: realloc failed.");
				continue;
			}
			se->data.server.fd = tmp;
			tmp = se->data.server.fd + se->data.server.fd_num;

			*tmp = socket (ai_ptr->ai_family, ai_ptr->ai_socktype,
					ai_ptr->ai_protocol);
			if (*tmp < 0)
			{
				char errbuf[1024];
				ERROR ("network plugin: socket(2) failed: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				continue;
			}

			status = network_bind_socket (*tmp, ai_ptr, se->interface);
			if (status != 0)
			{
				close (*tmp);
				*tmp = -1;
				continue;
			}

			se->data.server.fd_num++;
			continue;
		} /* }}} if (se->type == SOCKENT_TYPE_SERVER) */
		else /* if (se->type == SOCKENT_TYPE_CLIENT) {{{ */
		{
			se->data.client.fd = socket (ai_ptr->ai_family,
					ai_ptr->ai_socktype,
					ai_ptr->ai_protocol);
			if (se->data.client.fd < 0)
			{
				char errbuf[1024];
				ERROR ("network plugin: socket(2) failed: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				continue;
			}

			se->data.client.addr = malloc (sizeof (*se->data.client.addr));
			if (se->data.client.addr == NULL)
			{
				ERROR ("network plugin: malloc failed.");
				close (se->data.client.fd);
				se->data.client.fd = -1;
				continue;
			}

			memset (se->data.client.addr, 0, sizeof (*se->data.client.addr));
			assert (sizeof (*se->data.client.addr) >= ai_ptr->ai_addrlen);
			memcpy (se->data.client.addr, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
			se->data.client.addrlen = ai_ptr->ai_addrlen;

			network_set_ttl (se, ai_ptr);
			network_set_interface (se, ai_ptr);

			/* We don't open more than one write-socket per
			 * node/service pair.. */
			break;
		} /* }}} if (se->type == SOCKENT_TYPE_CLIENT) */
	} /* for (ai_list) */

	freeaddrinfo (ai_list);

	/* Check if all went well. */
	if (se->type == SOCKENT_TYPE_SERVER)
	{
		if (se->data.server.fd_num <= 0)
			return (-1);
	}
	else /* if (se->type == SOCKENT_TYPE_CLIENT) */
	{
		if (se->data.client.fd < 0)
			return (-1);
	}

	return (0);
} /* }}} int sockent_open */

/* Add a sockent to the global list of sockets */
static int sockent_add (sockent_t *se) /* {{{ */
{
	sockent_t *last_ptr;

	if (se == NULL)
		return (-1);

	if (se->type == SOCKENT_TYPE_SERVER)
	{
		struct pollfd *tmp;
		size_t i;

		tmp = realloc (listen_sockets_pollfd,
				sizeof (*tmp) * (listen_sockets_num
					+ se->data.server.fd_num));
		if (tmp == NULL)
		{
			ERROR ("network plugin: realloc failed.");
			return (-1);
		}
		listen_sockets_pollfd = tmp;
		tmp = listen_sockets_pollfd + listen_sockets_num;

		for (i = 0; i < se->data.server.fd_num; i++)
		{
			memset (tmp + i, 0, sizeof (*tmp));
			tmp[i].fd = se->data.server.fd[i];
			tmp[i].events = POLLIN | POLLPRI;
			tmp[i].revents = 0;
		}

		listen_sockets_num += se->data.server.fd_num;

		if (listen_sockets == NULL)
		{
			listen_sockets = se;
			return (0);
		}
		last_ptr = listen_sockets;
	}
	else /* if (se->type == SOCKENT_TYPE_CLIENT) */
	{
		if (sending_sockets == NULL)
		{
			sending_sockets = se;
			return (0);
		}
		last_ptr = sending_sockets;
	}

	while (last_ptr->next != NULL)
		last_ptr = last_ptr->next;
	last_ptr->next = se;

	return (0);
} /* }}} int sockent_add */

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
    receive_list_length--;
    pthread_mutex_unlock (&receive_list_lock);

    /* Check whether we are supposed to exit. We do NOT check `listen_loop'
     * because we dispatch all missing packets before shutting down. */
    if (ent == NULL)
      break;

    /* Look for the correct `sockent_t' */
    se = listen_sockets;
    while (se != NULL)
    {
      size_t i;

      for (i = 0; i < se->data.server.fd_num; i++)
        if (se->data.server.fd[i] == ent->fd)
          break;

      if (i < se->data.server.fd_num)
        break;

      se = se->next;
    }

    if (se == NULL)
    {
      ERROR ("network plugin: Got packet from FD %i, but can't "
          "find an appropriate socket entry.",
          ent->fd);
      sfree (ent->data);
      sfree (ent);
      continue;
    }

    parse_packet (se, ent->data, ent->data_len, /* flags = */ 0,
	/* username = */ NULL);
    sfree (ent->data);
    sfree (ent);
  } /* while (42) */

  return (NULL);
} /* }}} void *dispatch_thread */

static int network_receive (void) /* {{{ */
{
	char buffer[network_config_packet_size];
	int  buffer_len;

	int i;
	int status;

	receive_list_entry_t *private_list_head;
	receive_list_entry_t *private_list_tail;
	uint64_t              private_list_length;

        assert (listen_sockets_num > 0);

	private_list_head = NULL;
	private_list_tail = NULL;
	private_list_length = 0;

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

			stats_octets_rx += ((uint64_t) buffer_len);
			stats_packets_rx++;

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
			ent->data = malloc (network_config_packet_size);
			if (ent->data == NULL)
			{
				sfree (ent);
				ERROR ("network plugin: malloc failed.");
				return (-1);
			}
			ent->fd = listen_sockets_pollfd[i].fd;
			ent->next = NULL;

			memcpy (ent->data, buffer, buffer_len);
			ent->data_len = buffer_len;

			if (private_list_head == NULL)
				private_list_head = ent;
			else
				private_list_tail->next = ent;
			private_list_tail = ent;
			private_list_length++;

			/* Do not block here. Blocking here has led to
			 * insufficient performance in the past. */
			if (pthread_mutex_trylock (&receive_list_lock) == 0)
			{
				assert (((receive_list_head == NULL) && (receive_list_length == 0))
						|| ((receive_list_head != NULL) && (receive_list_length != 0)));

				if (receive_list_head == NULL)
					receive_list_head = private_list_head;
				else
					receive_list_tail->next = private_list_head;
				receive_list_tail = private_list_tail;
				receive_list_length += private_list_length;

				pthread_cond_signal (&receive_list_cond);
				pthread_mutex_unlock (&receive_list_lock);

				private_list_head = NULL;
				private_list_tail = NULL;
				private_list_length = 0;
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
		receive_list_length += private_list_length;

		private_list_head = NULL;
		private_list_tail = NULL;
		private_list_length = 0;

		pthread_cond_signal (&receive_list_cond);
		pthread_mutex_unlock (&receive_list_lock);
	}

	return (0);
} /* }}} int network_receive */

static void *receive_thread (void __attribute__((unused)) *arg)
{
	return (network_receive () ? (void *) 1 : (void *) 0);
} /* void *receive_thread */

static void network_init_buffer (void)
{
	memset (send_buffer, 0, network_config_packet_size);
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
		status = sendto (se->data.client.fd, buffer, buffer_size,
                    /* flags = */ 0,
                    (struct sockaddr *) se->data.client.addr,
                    se->data.client.addrlen);
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

#if HAVE_LIBGCRYPT
#define BUFFER_ADD(p,s) do { \
  memcpy (buffer + buffer_offset, (p), (s)); \
  buffer_offset += (s); \
} while (0)

static void networt_send_buffer_signed (const sockent_t *se, /* {{{ */
		const char *in_buffer, size_t in_buffer_size)
{
  part_signature_sha256_t ps;
  char buffer[BUFF_SIG_SIZE + in_buffer_size];
  size_t buffer_offset;
  size_t username_len;

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

  err = gcry_md_setkey (hd, se->data.client.password,
      strlen (se->data.client.password));
  if (err != 0)
  {
    ERROR ("network plugin: gcry_md_setkey failed: %s",
        gcry_strerror (err));
    gcry_md_close (hd);
    return;
  }

  username_len = strlen (se->data.client.username);
  if (username_len > (BUFF_SIG_SIZE - PART_SIGNATURE_SHA256_SIZE))
  {
    ERROR ("network plugin: Username too long: %s",
        se->data.client.username);
    return;
  }

  memcpy (buffer + PART_SIGNATURE_SHA256_SIZE,
      se->data.client.username, username_len);
  memcpy (buffer + PART_SIGNATURE_SHA256_SIZE + username_len,
      in_buffer, in_buffer_size);

  /* Initialize the `ps' structure. */
  memset (&ps, 0, sizeof (ps));
  ps.head.type = htons (TYPE_SIGN_SHA256);
  ps.head.length = htons (PART_SIGNATURE_SHA256_SIZE + username_len);

  /* Calculate the hash value. */
  gcry_md_write (hd, buffer + PART_SIGNATURE_SHA256_SIZE,
      username_len + in_buffer_size);
  hash = gcry_md_read (hd, GCRY_MD_SHA256);
  if (hash == NULL)
  {
    ERROR ("network plugin: gcry_md_read failed.");
    gcry_md_close (hd);
    return;
  }
  memcpy (ps.hash, hash, sizeof (ps.hash));

  /* Add the header */
  buffer_offset = 0;

  BUFFER_ADD (&ps.head.type, sizeof (ps.head.type));
  BUFFER_ADD (&ps.head.length, sizeof (ps.head.length));
  BUFFER_ADD (ps.hash, sizeof (ps.hash));

  assert (buffer_offset == PART_SIGNATURE_SHA256_SIZE);

  gcry_md_close (hd);
  hd = NULL;

  buffer_offset = PART_SIGNATURE_SHA256_SIZE + username_len + in_buffer_size;
  networt_send_buffer_plain (se, buffer, buffer_offset);
} /* }}} void networt_send_buffer_signed */

static void networt_send_buffer_encrypted (sockent_t *se, /* {{{ */
		const char *in_buffer, size_t in_buffer_size)
{
  part_encryption_aes256_t pea;
  char buffer[BUFF_SIG_SIZE + in_buffer_size];
  size_t buffer_size;
  size_t buffer_offset;
  size_t header_size;
  size_t username_len;
  gcry_error_t err;
  gcry_cipher_hd_t cypher;

  /* Initialize the header fields */
  memset (&pea, 0, sizeof (pea));
  pea.head.type = htons (TYPE_ENCR_AES256);

  pea.username = se->data.client.username;

  username_len = strlen (pea.username);
  if ((PART_ENCRYPTION_AES256_SIZE + username_len) > BUFF_SIG_SIZE)
  {
    ERROR ("network plugin: Username too long: %s", pea.username);
    return;
  }

  buffer_size = PART_ENCRYPTION_AES256_SIZE + username_len + in_buffer_size;
  header_size = PART_ENCRYPTION_AES256_SIZE + username_len
    - sizeof (pea.hash);

  assert (buffer_size <= sizeof (buffer));
  DEBUG ("network plugin: networt_send_buffer_encrypted: "
      "buffer_size = %zu;", buffer_size);

  pea.head.length = htons ((uint16_t) (PART_ENCRYPTION_AES256_SIZE
        + username_len + in_buffer_size));
  pea.username_length = htons ((uint16_t) username_len);

  /* Chose a random initialization vector. */
  gcry_randomize ((void *) &pea.iv, sizeof (pea.iv), GCRY_STRONG_RANDOM);

  /* Create hash of the payload */
  gcry_md_hash_buffer (GCRY_MD_SHA1, pea.hash, in_buffer, in_buffer_size);

  /* Initialize the buffer */
  buffer_offset = 0;
  memset (buffer, 0, sizeof (buffer));


  BUFFER_ADD (&pea.head.type, sizeof (pea.head.type));
  BUFFER_ADD (&pea.head.length, sizeof (pea.head.length));
  BUFFER_ADD (&pea.username_length, sizeof (pea.username_length));
  BUFFER_ADD (pea.username, username_len);
  BUFFER_ADD (pea.iv, sizeof (pea.iv));
  assert (buffer_offset == header_size);
  BUFFER_ADD (pea.hash, sizeof (pea.hash));
  BUFFER_ADD (in_buffer, in_buffer_size);

  assert (buffer_offset == buffer_size);

  cypher = network_get_aes256_cypher (se, pea.iv, sizeof (pea.iv),
      se->data.client.password);
  if (cypher == NULL)
    return;

  /* Encrypt the buffer in-place */
  err = gcry_cipher_encrypt (cypher,
      buffer      + header_size,
      buffer_size - header_size,
      /* in = */ NULL, /* in len = */ 0);
  if (err != 0)
  {
    ERROR ("network plugin: gcry_cipher_encrypt returned: %s",
        gcry_strerror (err));
    return;
  }

  /* Send it out without further modifications */
  networt_send_buffer_plain (se, buffer, buffer_size);
} /* }}} void networt_send_buffer_encrypted */
#undef BUFFER_ADD
#endif /* HAVE_LIBGCRYPT */

static void network_send_buffer (char *buffer, size_t buffer_len) /* {{{ */
{
  sockent_t *se;

  DEBUG ("network plugin: network_send_buffer: buffer_len = %zu", buffer_len);

  for (se = sending_sockets; se != NULL; se = se->next)
  {
#if HAVE_LIBGCRYPT
    if (se->data.client.security_level == SECURITY_LEVEL_ENCRYPT)
      networt_send_buffer_encrypted (se, buffer, buffer_len);
    else if (se->data.client.security_level == SECURITY_LEVEL_SIGN)
      networt_send_buffer_signed (se, buffer, buffer_len);
    else /* if (se->data.client.security_level == SECURITY_LEVEL_NONE) */
#endif /* HAVE_LIBGCRYPT */
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
		if (write_part_number (&buffer, &buffer_size, TYPE_TIME_HR,
					(uint64_t) vl->time))
			return (-1);
		vl_def->time = vl->time;
	}

	if (vl_def->interval != vl->interval)
	{
		if (write_part_number (&buffer, &buffer_size, TYPE_INTERVAL_HR,
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

	stats_octets_tx += ((uint64_t) send_buffer_fill);
	stats_packets_tx++;

	network_init_buffer ();
}

static int network_write (const data_set_t *ds, const value_list_t *vl,
		user_data_t __attribute__((unused)) *user_data)
{
	int status;

	if (!check_send_okay (vl))
	{
#if COLLECT_DEBUG
	  char name[6*DATA_MAX_NAME_LEN];
	  FORMAT_VL (name, sizeof (name), vl);
	  name[sizeof (name) - 1] = 0;
	  DEBUG ("network plugin: network_write: "
	      "NOT sending %s.", name);
#endif
	  /* Counter is not protected by another lock and may be reached by
	   * multiple threads */
	  pthread_mutex_lock (&stats_lock);
	  stats_values_not_sent++;
	  pthread_mutex_unlock (&stats_lock);
	  return (0);
	}

	uc_meta_data_add_unsigned_int (vl,
	    "network:time_sent", (uint64_t) vl->time);

	pthread_mutex_lock (&send_buffer_lock);

	status = add_to_buffer (send_buffer_ptr,
			network_config_packet_size - (send_buffer_fill + BUFF_SIG_SIZE),
			&send_buffer_vl,
			ds, vl);
	if (status >= 0)
	{
		/* status == bytes added to the buffer */
		send_buffer_fill += status;
		send_buffer_ptr  += status;

		stats_values_sent++;
	}
	else
	{
		flush_buffer ();

		status = add_to_buffer (send_buffer_ptr,
				network_config_packet_size - (send_buffer_fill + BUFF_SIG_SIZE),
				&send_buffer_vl,
				ds, vl);

		if (status >= 0)
		{
			send_buffer_fill += status;
			send_buffer_ptr  += status;

			stats_values_sent++;
		}
	}

	if (status < 0)
	{
		ERROR ("network plugin: Unable to append to the "
				"buffer for some weird reason");
	}
	else if ((network_config_packet_size - send_buffer_fill) < 15)
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

    if (IS_TRUE (str))
      *retval = 1;
    else if (IS_FALSE (str))
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

static int network_config_set_interface (const oconfig_item_t *ci, /* {{{ */
    int *interface)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("network plugin: The `Interface' config option needs exactly "
        "one string argument.");
    return (-1);
  }

  if (interface == NULL)
    return (-1);

  *interface = if_nametoindex (ci->values[0].value.string);

  return (0);
} /* }}} int network_config_set_interface */

static int network_config_set_buffer_size (const oconfig_item_t *ci) /* {{{ */
{
  int tmp;
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    WARNING ("network plugin: The `MaxPacketSize' config option needs exactly "
        "one numeric argument.");
    return (-1);
  }

  tmp = (int) ci->values[0].value.number;
  if ((tmp >= 1024) && (tmp <= 65535))
    network_config_packet_size = tmp;

  return (0);
} /* }}} int network_config_set_buffer_size */

#if HAVE_LIBGCRYPT
static int network_config_set_string (const oconfig_item_t *ci, /* {{{ */
    char **ret_string)
{
  char *tmp;
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("network plugin: The `%s' config option needs exactly "
        "one string argument.", ci->key);
    return (-1);
  }

  tmp = strdup (ci->values[0].value.string);
  if (tmp == NULL)
    return (-1);

  sfree (*ret_string);
  *ret_string = tmp;

  return (0);
} /* }}} int network_config_set_string */
#endif /* HAVE_LIBGCRYPT */

#if HAVE_LIBGCRYPT
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
#endif /* HAVE_LIBGCRYPT */

static int network_config_add_listen (const oconfig_item_t *ci) /* {{{ */
{
  sockent_t *se;
  int status;
  int i;

  if ((ci->values_num < 1) || (ci->values_num > 2)
      || (ci->values[0].type != OCONFIG_TYPE_STRING)
      || ((ci->values_num > 1) && (ci->values[1].type != OCONFIG_TYPE_STRING)))
  {
    ERROR ("network plugin: The `%s' config option needs "
        "one or two string arguments.", ci->key);
    return (-1);
  }

  se = malloc (sizeof (*se));
  if (se == NULL)
  {
    ERROR ("network plugin: malloc failed.");
    return (-1);
  }
  sockent_init (se, SOCKENT_TYPE_SERVER);

  se->node = strdup (ci->values[0].value.string);
  if (ci->values_num >= 2)
    se->service = strdup (ci->values[1].value.string);

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

#if HAVE_LIBGCRYPT
    if (strcasecmp ("AuthFile", child->key) == 0)
      network_config_set_string (child, &se->data.server.auth_file);
    else if (strcasecmp ("SecurityLevel", child->key) == 0)
      network_config_set_security_level (child,
          &se->data.server.security_level);
    else
#endif /* HAVE_LIBGCRYPT */
    if (strcasecmp ("Interface", child->key) == 0)
      network_config_set_interface (child,
          &se->interface);
    else
    {
      WARNING ("network plugin: Option `%s' is not allowed here.",
          child->key);
    }
  }

#if HAVE_LIBGCRYPT
  if ((se->data.server.security_level > SECURITY_LEVEL_NONE)
      && (se->data.server.auth_file == NULL))
  {
    ERROR ("network plugin: A security level higher than `none' was "
        "requested, but no AuthFile option was given. Cowardly refusing to "
        "open this socket!");
    sockent_destroy (se);
    return (-1);
  }
#endif /* HAVE_LIBGCRYPT */

  status = sockent_open (se);
  if (status != 0)
  {
    ERROR ("network plugin: network_config_add_listen: sockent_open failed.");
    sockent_destroy (se);
    return (-1);
  }

  status = sockent_add (se);
  if (status != 0)
  {
    ERROR ("network plugin: network_config_add_listen: sockent_add failed.");
    sockent_destroy (se);
    return (-1);
  }

  return (0);
} /* }}} int network_config_add_listen */

static int network_config_add_server (const oconfig_item_t *ci) /* {{{ */
{
  sockent_t *se;
  int status;
  int i;

  if ((ci->values_num < 1) || (ci->values_num > 2)
      || (ci->values[0].type != OCONFIG_TYPE_STRING)
      || ((ci->values_num > 1) && (ci->values[1].type != OCONFIG_TYPE_STRING)))
  {
    ERROR ("network plugin: The `%s' config option needs "
        "one or two string arguments.", ci->key);
    return (-1);
  }

  se = malloc (sizeof (*se));
  if (se == NULL)
  {
    ERROR ("network plugin: malloc failed.");
    return (-1);
  }
  sockent_init (se, SOCKENT_TYPE_CLIENT);

  se->node = strdup (ci->values[0].value.string);
  if (ci->values_num >= 2)
    se->service = strdup (ci->values[1].value.string);

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

#if HAVE_LIBGCRYPT
    if (strcasecmp ("Username", child->key) == 0)
      network_config_set_string (child, &se->data.client.username);
    else if (strcasecmp ("Password", child->key) == 0)
      network_config_set_string (child, &se->data.client.password);
    else if (strcasecmp ("SecurityLevel", child->key) == 0)
      network_config_set_security_level (child,
          &se->data.client.security_level);
    else
#endif /* HAVE_LIBGCRYPT */
    if (strcasecmp ("Interface", child->key) == 0)
      network_config_set_interface (child,
          &se->interface);
    else
    {
      WARNING ("network plugin: Option `%s' is not allowed here.",
          child->key);
    }
  }

#if HAVE_LIBGCRYPT
  if ((se->data.client.security_level > SECURITY_LEVEL_NONE)
      && ((se->data.client.username == NULL)
        || (se->data.client.password == NULL)))
  {
    ERROR ("network plugin: A security level higher than `none' was "
        "requested, but no Username or Password option was given. "
        "Cowardly refusing to open this socket!");
    sockent_destroy (se);
    return (-1);
  }
#endif /* HAVE_LIBGCRYPT */

  status = sockent_open (se);
  if (status != 0)
  {
    ERROR ("network plugin: network_config_add_server: sockent_open failed.");
    sockent_destroy (se);
    return (-1);
  }

  status = sockent_add (se);
  if (status != 0)
  {
    ERROR ("network plugin: network_config_add_server: sockent_add failed.");
    sockent_destroy (se);
    return (-1);
  }

  return (0);
} /* }}} int network_config_add_server */

static int network_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Listen", child->key) == 0)
      network_config_add_listen (child);
    else if (strcasecmp ("Server", child->key) == 0)
      network_config_add_server (child);
    else if (strcasecmp ("TimeToLive", child->key) == 0)
      network_config_set_ttl (child);
    else if (strcasecmp ("MaxPacketSize", child->key) == 0)
      network_config_set_buffer_size (child);
    else if (strcasecmp ("Forward", child->key) == 0)
      network_config_set_boolean (child, &network_config_forward);
    else if (strcasecmp ("ReportStats", child->key) == 0)
      network_config_set_boolean (child, &network_config_stats);
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
  char  buffer[network_config_packet_size];
  char *buffer_ptr = buffer;
  int   buffer_free = sizeof (buffer);
  int   status;

  if (!check_send_notify_okay (n))
    return (0);

  memset (buffer, 0, sizeof (buffer));

  status = write_part_number (&buffer_ptr, &buffer_free, TYPE_TIME_HR,
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

	sockent_destroy (listen_sockets);

	if (send_buffer_fill > 0)
		flush_buffer ();

	sfree (send_buffer);

	/* TODO: Close `sending_sockets' */

	plugin_unregister_config ("network");
	plugin_unregister_init ("network");
	plugin_unregister_write ("network");
	plugin_unregister_shutdown ("network");

	return (0);
} /* int network_shutdown */

static int network_stats_read (void) /* {{{ */
{
	derive_t copy_octets_rx;
	derive_t copy_octets_tx;
	derive_t copy_packets_rx;
	derive_t copy_packets_tx;
	derive_t copy_values_dispatched;
	derive_t copy_values_not_dispatched;
	derive_t copy_values_sent;
	derive_t copy_values_not_sent;
	derive_t copy_receive_list_length;
	value_list_t vl = VALUE_LIST_INIT;
	value_t values[2];

	copy_octets_rx = stats_octets_rx;
	copy_octets_tx = stats_octets_tx;
	copy_packets_rx = stats_packets_rx;
	copy_packets_tx = stats_packets_tx;
	copy_values_dispatched = stats_values_dispatched;
	copy_values_not_dispatched = stats_values_not_dispatched;
	copy_values_sent = stats_values_sent;
	copy_values_not_sent = stats_values_not_sent;
	copy_receive_list_length = receive_list_length;

	/* Initialize `vl' */
	vl.values = values;
	vl.values_len = 2;
	vl.time = 0;
	vl.interval = interval_g;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "network", sizeof (vl.plugin));

	/* Octets received / sent */
	vl.values[0].derive = (derive_t) copy_octets_rx;
	vl.values[1].derive = (derive_t) copy_octets_tx;
	sstrncpy (vl.type, "if_octets", sizeof (vl.type));
	plugin_dispatch_values_secure (&vl);

	/* Packets received / send */
	vl.values[0].derive = (derive_t) copy_packets_rx;
	vl.values[1].derive = (derive_t) copy_packets_tx;
	sstrncpy (vl.type, "if_packets", sizeof (vl.type));
	plugin_dispatch_values_secure (&vl);

	/* Values (not) dispatched and (not) send */
	sstrncpy (vl.type, "total_values", sizeof (vl.type));
	vl.values_len = 1;

	vl.values[0].derive = (derive_t) copy_values_dispatched;
	sstrncpy (vl.type_instance, "dispatch-accepted",
			sizeof (vl.type_instance));
	plugin_dispatch_values_secure (&vl);

	vl.values[0].derive = (derive_t) copy_values_not_dispatched;
	sstrncpy (vl.type_instance, "dispatch-rejected",
			sizeof (vl.type_instance));
	plugin_dispatch_values_secure (&vl);

	vl.values[0].derive = (derive_t) copy_values_sent;
	sstrncpy (vl.type_instance, "send-accepted",
			sizeof (vl.type_instance));
	plugin_dispatch_values_secure (&vl);

	vl.values[0].derive = (derive_t) copy_values_not_sent;
	sstrncpy (vl.type_instance, "send-rejected",
			sizeof (vl.type_instance));
	plugin_dispatch_values_secure (&vl);

	/* Receive queue length */
	vl.values[0].gauge = (gauge_t) copy_receive_list_length;
	sstrncpy (vl.type, "queue_length", sizeof (vl.type));
	vl.type_instance[0] = 0;
	plugin_dispatch_values_secure (&vl);

	return (0);
} /* }}} int network_stats_read */

static int network_init (void)
{
	static _Bool have_init = 0;

	/* Check if we were already initialized. If so, just return - there's
	 * nothing more to do (for now, that is). */
	if (have_init)
		return (0);
	have_init = 1;

#if HAVE_LIBGCRYPT
	gcry_control (GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
	gcry_control (GCRYCTL_INIT_SECMEM, 32768, 0);
	gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);
#endif

	if (network_config_stats != 0)
		plugin_register_read ("network", network_stats_read);

	plugin_register_shutdown ("network", network_shutdown);

	send_buffer = malloc (network_config_packet_size);
	if (send_buffer == NULL)
	{
		ERROR ("network plugin: malloc failed.");
		return (-1);
	}
	network_init_buffer ();

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
static int network_flush (__attribute__((unused)) cdtime_t timeout,
		__attribute__((unused)) const char *identifier,
		__attribute__((unused)) user_data_t *user_data)
{
	pthread_mutex_lock (&send_buffer_lock);

	if (send_buffer_fill > 0)
	  flush_buffer ();

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
