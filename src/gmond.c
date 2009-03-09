/**
 * collectd - src/gmond.c
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

#include <gm_protocol.h>

#ifndef IPV6_ADD_MEMBERSHIP
# ifdef IPV6_JOIN_GROUP
#  define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
# else
#  error "Neither IP_ADD_MEMBERSHIP nor IPV6_JOIN_GROUP is defined"
# endif
#endif /* !IP_ADD_MEMBERSHIP */

#ifdef GANGLIA_MAX_MESSAGE_LEN
# define BUFF_SIZE GANGLIA_MAX_MESSAGE_LEN
#else
# define BUFF_SIZE 1400
#endif

struct socket_entry_s
{
  int                     fd;
  struct sockaddr_storage addr;
  socklen_t               addrlen;
};
typedef struct socket_entry_s socket_entry_t;

struct staging_entry_s
{
  char key[2 * DATA_MAX_NAME_LEN];
  value_list_t vl;
  int flags;
};
typedef struct staging_entry_s staging_entry_t;

struct metric_map_s
{
  const char *ganglia_name;
  const char *type;
  const char *type_instance;
  int ds_type;
  size_t value_index;
};
typedef struct metric_map_s metric_map_t;

static struct pollfd *mc_receive_sockets = NULL;
static size_t         mc_receive_sockets_num = 0;

static socket_entry_t  *mc_send_sockets = NULL;
static size_t           mc_send_sockets_num = 0;
static pthread_mutex_t  mc_send_sockets_lock = PTHREAD_MUTEX_INITIALIZER;

static int            mc_receive_thread_loop    = 0;
static int            mc_receive_thread_running = 0;
static pthread_t      mc_receive_thread_id;

static metric_map_t metric_map[] =
{
  { "load_one",     "load",       "",         DS_TYPE_GAUGE,   0 },
  { "load_five",    "load",       "",         DS_TYPE_GAUGE,   1 },
  { "load_fifteen", "load",       "",         DS_TYPE_GAUGE,   2 },
  { "cpu_user",     "cpu",        "user",     DS_TYPE_COUNTER, 0 },
  { "cpu_system",   "cpu",        "system",   DS_TYPE_COUNTER, 0 },
  { "cpu_idle",     "cpu",        "idle",     DS_TYPE_COUNTER, 0 },
  { "cpu_nice",     "cpu",        "nice",     DS_TYPE_COUNTER, 0 },
  { "cpu_wio",      "cpu",        "wait",     DS_TYPE_COUNTER, 0 },
  { "mem_free",     "memory",     "free",     DS_TYPE_GAUGE,   0 },
  { "mem_shared",   "memory",     "shared",   DS_TYPE_GAUGE,   0 },
  { "mem_buffers",  "memory",     "buffered", DS_TYPE_GAUGE,   0 },
  { "mem_cached",   "memory",     "cached",   DS_TYPE_GAUGE,   0 },
  { "mem_total",    "memory",     "total",    DS_TYPE_GAUGE,   0 },
  { "bytes_in",     "if_octets",  "",         DS_TYPE_COUNTER, 0 },
  { "bytes_out",    "if_octets",  "",         DS_TYPE_COUNTER, 1 },
  { "pkts_in",      "if_packets", "",         DS_TYPE_COUNTER, 0 },
  { "pkts_out",     "if_packets", "",         DS_TYPE_COUNTER, 1 }
};
static size_t metric_map_len = STATIC_ARRAY_SIZE (metric_map);

static c_avl_tree_t   *staging_tree;
static pthread_mutex_t staging_lock = PTHREAD_MUTEX_INITIALIZER;

static int create_sockets (socket_entry_t **ret_sockets, /* {{{ */
    size_t *ret_sockets_num,
    const char *node, const char *service, int listen)
{
  struct addrinfo  ai_hints;
  struct addrinfo *ai_list;
  struct addrinfo *ai_ptr;
  int              ai_return;

  socket_entry_t *sockets;
  size_t          sockets_num;

  int status;
    
  sockets     = *ret_sockets;
  sockets_num = *ret_sockets_num;

  memset (&ai_hints, 0, sizeof (ai_hints));
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
    ERROR ("gmond plugin: getaddrinfo (%s, %s) failed: %s",
        (node == NULL) ? "(null)" : node,
        (service == NULL) ? "(null)" : service,
        (ai_return == EAI_SYSTEM)
        ? sstrerror (errno, errbuf, sizeof (errbuf))
        : gai_strerror (ai_return));
    return (-1);
  }

  for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next) /* {{{ */
  {
    socket_entry_t *tmp;

    tmp = realloc (sockets, (sockets_num + 1) * sizeof (*sockets));
    if (tmp == NULL)
    {
      ERROR ("gmond plugin: realloc failed.");
      continue;
    }
    sockets = tmp;

    sockets[sockets_num].fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype,
        ai_ptr->ai_protocol);
    if (sockets[sockets_num].fd < 0)
    {
      char errbuf[1024];
      ERROR ("gmond plugin: socket failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      continue;
    }

    assert (sizeof (sockets[sockets_num].addr) >= ai_ptr->ai_addrlen);
    memcpy (&sockets[sockets_num].addr, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    sockets[sockets_num].addrlen = ai_ptr->ai_addrlen;

    /* Sending socket: Open only one socket and don't bind it. */
    if (listen == 0)
    {
      sockets_num++;
      break;
    }
    else
    {
      int yes = 1;

      setsockopt (sockets[sockets_num].fd, SOL_SOCKET, SO_REUSEADDR,
          (void *) &yes, sizeof (yes));
    }

    status = bind (sockets[sockets_num].fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    if (status != 0)
    {
      char errbuf[1024];
      ERROR ("gmond plugin: bind failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      close (sockets[sockets_num].fd);
      continue;
    }

    if (ai_ptr->ai_family == AF_INET)
    {
      struct sockaddr_in *addr;
      struct ip_mreq mreq;
      int loop;

      addr = (struct sockaddr_in *) ai_ptr->ai_addr;

      if (!IN_MULTICAST (ntohl (addr->sin_addr.s_addr)))
      {
        sockets_num++;
        continue;
      }

      loop = 1;
      setsockopt (sockets[sockets_num].fd, IPPROTO_IP, IP_MULTICAST_LOOP,
          (void *) &loop, sizeof (loop));

      memset (&mreq, 0, sizeof (mreq));
      mreq.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
      mreq.imr_interface.s_addr = htonl (INADDR_ANY);
      setsockopt (sockets[sockets_num].fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
          (void *) &mreq, sizeof (mreq));
    } /* if (ai_ptr->ai_family == AF_INET) */
    else if (ai_ptr->ai_family == AF_INET6)
    {
      struct sockaddr_in6 *addr;
      struct ipv6_mreq mreq;
      int loop;

      addr = (struct sockaddr_in6 *) ai_ptr->ai_addr;

      if (!IN6_IS_ADDR_MULTICAST (&addr->sin6_addr))
      {
        sockets_num++;
        continue;
      }

      loop = 1;
      setsockopt (sockets[sockets_num].fd, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
          (void *) &loop, sizeof (loop));

      memset (&mreq, 0, sizeof (mreq));
      memcpy (&mreq.ipv6mr_multiaddr,
          &addr->sin6_addr, sizeof (addr->sin6_addr));
      mreq.ipv6mr_interface = 0; /* any */
      setsockopt (sockets[sockets_num].fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
          (void *) &mreq, sizeof (mreq));
    } /* if (ai_ptr->ai_family == AF_INET6) */

    sockets_num++;
  } /* }}} for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next) */

  freeaddrinfo (ai_list);

  if ((*ret_sockets_num) >= sockets_num)
    return (-1);

  *ret_sockets = sockets;
  *ret_sockets_num = sockets_num;
  return (0);
} /* }}} int create_sockets */

static int request_meta_data (const char *host, const char *name) /* {{{ */
{
  Ganglia_metadata_msg msg;
  char buffer[BUFF_SIZE];
  unsigned int buffer_size;
  XDR xdr;
  size_t i;

  memset (&msg, 0, sizeof (msg));

  msg.id = gmetadata_request;
  msg.Ganglia_metadata_msg_u.grequest.metric_id.host = strdup (host);
  msg.Ganglia_metadata_msg_u.grequest.metric_id.name = strdup (name);

  if ((msg.Ganglia_metadata_msg_u.grequest.metric_id.host == NULL)
      || (msg.Ganglia_metadata_msg_u.grequest.metric_id.name == NULL))
  {
    sfree (msg.Ganglia_metadata_msg_u.grequest.metric_id.host);
    sfree (msg.Ganglia_metadata_msg_u.grequest.metric_id.name);
    return (-1);
  }

  memset (buffer, 0, sizeof (buffer));
  xdrmem_create (&xdr, buffer, sizeof (buffer), XDR_ENCODE);

  if (!xdr_Ganglia_metadata_msg (&xdr, &msg))
  {
    sfree (msg.Ganglia_metadata_msg_u.grequest.metric_id.host);
    sfree (msg.Ganglia_metadata_msg_u.grequest.metric_id.name);
    return (-1);
  }

  buffer_size = xdr_getpos (&xdr);

  DEBUG ("gmond plugin: Requesting meta data for %s/%s.",
      host, name);

  pthread_mutex_lock (&mc_send_sockets_lock);
  for (i = 0; i < mc_send_sockets_num; i++)
    sendto (mc_send_sockets[i].fd, buffer, (size_t) buffer_size,
        /* flags = */ 0,
        (struct sockaddr *) &mc_send_sockets[i].addr,
        mc_send_sockets[i].addrlen);
  pthread_mutex_unlock (&mc_send_sockets_lock);

  sfree (msg.Ganglia_metadata_msg_u.grequest.metric_id.host);
  sfree (msg.Ganglia_metadata_msg_u.grequest.metric_id.name);
  return (0);
} /* }}} int request_meta_data */

static staging_entry_t *staging_entry_get (const char *host, /* {{{ */
    const char *name,
    const char *type, const char *type_instance,
    int values_len)
{
  char key[2 * DATA_MAX_NAME_LEN];
  staging_entry_t *se;
  int status;

  if (staging_tree == NULL)
    return (NULL);

  ssnprintf (key, sizeof (key), "%s/%s/%s", host, type, type_instance);

  se = NULL;
  status = c_avl_get (staging_tree, key, (void *) &se);
  if (status == 0)
    return (se);

  /* insert new entry */
  se = (staging_entry_t *) malloc (sizeof (*se));
  if (se == NULL)
    return (NULL);
  memset (se, 0, sizeof (*se));

  sstrncpy (se->key, key, sizeof (se->key));
  se->flags = 0;

  se->vl.values = (value_t *) calloc (values_len, sizeof (*se->vl.values));
  if (se->vl.values == NULL)
  {
    sfree (se);
    return (NULL);
  }
  se->vl.values_len = values_len;

  se->vl.time = 0;
  se->vl.interval = 0;
  sstrncpy (se->vl.host, host, sizeof (se->vl.host));
  sstrncpy (se->vl.plugin, "gmond", sizeof (se->vl.plugin));
  sstrncpy (se->vl.type, type, sizeof (se->vl.type));
  if (type_instance != NULL)
    sstrncpy (se->vl.type_instance, type_instance,
        sizeof (se->vl.type_instance));

  status = c_avl_insert (staging_tree, se->key, se);
  if (status != 0)
  {
    ERROR ("gmond plugin: c_avl_insert failed.");
    sfree (se->vl.values);
    sfree (se);
    return (NULL);
  }

  return (se);
} /* }}} staging_entry_t *staging_entry_get */

static int staging_entry_submit (const char *host, const char *name, /* {{{ */
    staging_entry_t *se)
{
  value_list_t vl;
  value_t values[se->vl.values_len];

  if (se->vl.interval == 0)
  {
    /* No meta data has been received for this metric yet. */
    se->flags = 0;
    pthread_mutex_unlock (&staging_lock);
    request_meta_data (host, name);
    return (0);
  }

  se->flags = 0;

  memcpy (values, se->vl.values, sizeof (values));
  memcpy (&vl, &se->vl, sizeof (vl));

  /* Unlock before calling `plugin_dispatch_values'.. */
  pthread_mutex_unlock (&staging_lock);

  vl.values = values;

  plugin_dispatch_values (&vl);

  return (0);
} /* }}} int staging_entry_submit */

static int staging_entry_update (const char *host, const char *name, /* {{{ */
    const char *type, const char *type_instance,
    int value_index, int ds_type, value_t value)
{
  const data_set_t *ds;
  staging_entry_t *se;

  ds = plugin_get_ds (type);
  if (ds == NULL)
  {
    ERROR ("gmond plugin: Looking up type %s failed.", type);
    return (-1);
  }

  if (ds->ds_num <= value_index)
  {
    ERROR ("gmond plugin: Invalid index %i: %s has only %i data source(s).",
        value_index, ds->type, ds->ds_num);
    return (-1);
  }

  pthread_mutex_lock (&staging_lock);

  se = staging_entry_get (host, name, type, type_instance, ds->ds_num);
  if (se == NULL)
  {
    pthread_mutex_unlock (&staging_lock);
    ERROR ("gmond plugin: staging_entry_get failed.");
    return (-1);
  }
  if (se->vl.values_len != ds->ds_num)
  {
    pthread_mutex_unlock (&staging_lock);
    return (-1);
  }

  if (ds_type == DS_TYPE_COUNTER)
    se->vl.values[value_index].counter += value.counter;
  else if (ds_type == DS_TYPE_GAUGE)
    se->vl.values[value_index].gauge = value.gauge;
  se->flags |= (0x01 << value_index);

  DEBUG ("gmond plugin: key = %s; flags = %i;",
      se->key, se->flags);

  /* Check if all values have been set and submit if so. */
  if (se->flags == ((0x01 << se->vl.values_len) - 1))
  {
    /* `staging_lock' is unlocked in `staging_entry_submit'. */
    staging_entry_submit (host, name, se);
  }
  else
  {
    pthread_mutex_unlock (&staging_lock);
  }

  return (0);
} /* }}} int staging_entry_update */

static int mc_handle_value_msg (Ganglia_value_msg *msg) /* {{{ */
{
  const char *host;
  const char *name;

  value_t value_counter;
  value_t value_gauge;

  size_t i;

  /* Fill in `host', `name', `value_counter', and `value_gauge' according to
   * the value type, or return with an error. */
  switch (msg->id) /* {{{ */
  {
    case gmetric_uint:
    {
      Ganglia_gmetric_uint msg_uint;

      msg_uint = msg->Ganglia_value_msg_u.gu_int;

      host = msg_uint.metric_id.host;
      name = msg_uint.metric_id.name;
      value_counter.counter = (counter_t) msg_uint.ui;
      value_gauge.gauge = (gauge_t) msg_uint.ui;
      break;
    }

    case gmetric_string:
    {
      Ganglia_gmetric_string msg_string;
      char *endptr;

      msg_string = msg->Ganglia_value_msg_u.gstr;

      host = msg_string.metric_id.host;
      name = msg_string.metric_id.name;

      endptr = NULL;
      errno = 0;
      value_counter.counter = (counter_t) strtoll (msg_string.str,
          &endptr, /* base = */ 0);
      if ((endptr == msg_string.str) || (errno != 0))
        value_counter.counter = -1;

      endptr = NULL;
      errno = 0;
      value_gauge.gauge = (gauge_t) strtod (msg_string.str, &endptr);
      if ((endptr == msg_string.str) || (errno != 0))
        value_gauge.gauge = NAN;

      break;
    }

    case gmetric_float:
    {
      Ganglia_gmetric_float msg_float;

      msg_float = msg->Ganglia_value_msg_u.gf;

      host = msg_float.metric_id.host;
      name = msg_float.metric_id.name;
      value_counter.counter = (counter_t) msg_float.f;
      value_gauge.gauge = (gauge_t) msg_float.f;
      break;
    }

    case gmetric_double:
    {
      Ganglia_gmetric_double msg_double;

      msg_double = msg->Ganglia_value_msg_u.gd;

      host = msg_double.metric_id.host;
      name = msg_double.metric_id.name;
      value_counter.counter = (counter_t) msg_double.d;
      value_gauge.gauge = (gauge_t) msg_double.d;
      break;
    }
    default:
      DEBUG ("gmond plugin: Value type not handled: %i", msg->id);
      return (-1);
  } /* }}} switch (msg->id) */

  assert (host != NULL);
  assert (name != NULL);

  for (i = 0; i < metric_map_len; i++)
  {
    if (strcmp (name, metric_map[i].ganglia_name) != 0)
      continue;

    return (staging_entry_update (host, name,
          metric_map[i].type, metric_map[i].type_instance,
          metric_map[i].value_index, metric_map[i].ds_type,
          (metric_map[i].ds_type == DS_TYPE_COUNTER)
          ? value_counter
          : value_gauge));
  }

  DEBUG ("gmond plugin: Cannot find a translation for %s.", name);

  return (-1);
} /* }}} int mc_handle_value_msg */

static int mc_handle_metadata_msg (Ganglia_metadata_msg *msg) /* {{{ */
{
  switch (msg->id)
  {
    case gmetadata_full:
    {
      Ganglia_metadatadef msg_meta;
      staging_entry_t *se;
      const data_set_t *ds;
      size_t i;

      msg_meta = msg->Ganglia_metadata_msg_u.gfull;

      if (msg_meta.metric.tmax <= 0)
        return (-1);

      for (i = 0; i < metric_map_len; i++)
      {
        if (strcmp (msg_meta.metric_id.name, metric_map[i].ganglia_name) == 0)
          break;
      }

      if (i >= metric_map_len)
      {
        DEBUG ("gmond plugin: Not handling meta data %s.",
            msg_meta.metric_id.name);
        return (0);
      }

      ds = plugin_get_ds (metric_map[i].type);
      if (ds == NULL)
      {
        WARNING ("gmond plugin: Could not find data set %s.",
            metric_map[i].type);
        return (-1);
      }

      DEBUG ("gmond plugin: Received meta data for %s/%s.",
          msg_meta.metric_id.host, msg_meta.metric_id.name);

      pthread_mutex_lock (&staging_lock);
      se = staging_entry_get (msg_meta.metric_id.host,
          msg_meta.metric_id.name,
          metric_map[i].type, metric_map[i].type_instance,
          ds->ds_num);
      if (se != NULL)
        se->vl.interval = (int) msg_meta.metric.tmax;
      pthread_mutex_unlock (&staging_lock);

      if (se == NULL)
      {
        ERROR ("gmond plugin: staging_entry_get failed.");
        return (-1);
      }

      break;
    }

    default:
    {
      return (-1);
    }
  }

  return (0);
} /* }}} int mc_handle_metadata_msg */

static int mc_handle_metric (void *buffer, size_t buffer_size) /* {{{ */
{
  XDR xdr;
  Ganglia_msg_formats format;

  xdrmem_create (&xdr, buffer, buffer_size, XDR_DECODE);

  xdr_Ganglia_msg_formats (&xdr, &format);
  xdr_setpos (&xdr, 0);

  switch (format)
  {
    case gmetric_ushort:
    case gmetric_short:
    case gmetric_int:
    case gmetric_uint:
    case gmetric_string:
    case gmetric_float:
    case gmetric_double:
    {
      Ganglia_value_msg msg;

      memset (&msg, 0, sizeof (msg));
      if (xdr_Ganglia_value_msg (&xdr, &msg))
        mc_handle_value_msg (&msg);
      break;
    }

    case gmetadata_full:
    case gmetadata_request:
    {
      Ganglia_metadata_msg msg;
      memset (&msg, 0, sizeof (msg));
      if (xdr_Ganglia_metadata_msg (&xdr, &msg))
        mc_handle_metadata_msg (&msg);
      break;
    }

    default:
      DEBUG ("gmond plugin: Unknown format: %i", format);
      return (-1);
  } /* switch (format) */


  return (0);
} /* }}} int mc_handle_metric */

static int mc_handle_socket (struct pollfd *p) /* {{{ */
{
  char buffer[BUFF_SIZE];
  ssize_t buffer_size;

  if ((p->revents & (POLLIN | POLLPRI)) == 0)
  {
    p->revents = 0;
    return (-1);
  }

  buffer_size = recv (p->fd, buffer, sizeof (buffer), /* flags = */ 0);
  if (buffer_size <= 0)
  {
    char errbuf[1024];
    ERROR ("gmond plugin: recv failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    p->revents = 0;
    return (-1);
  }

  mc_handle_metric (buffer, (size_t) buffer_size);
  return (0);
} /* }}} int mc_handle_socket */

static void *mc_receive_thread (void *arg) /* {{{ */
{
  socket_entry_t *mc_receive_socket_entries;
  int status;
  size_t i;

  mc_receive_socket_entries = NULL;
  status = create_sockets (&mc_receive_socket_entries, &mc_receive_sockets_num,
      "239.2.11.71", "8649", /* listen = */ 1);
  if (status != 0)
  {
    ERROR ("gmond plugin: create_sockets failed.");
    return ((void *) -1);
  }

  mc_receive_sockets = (struct pollfd *) calloc (mc_receive_sockets_num,
      sizeof (*mc_receive_sockets));
  if (mc_receive_sockets == NULL)
  {
    ERROR ("gmond plugin: calloc failed.");
    for (i = 0; i < mc_receive_sockets_num; i++)
      close (mc_receive_socket_entries[i].fd);
    free (mc_receive_socket_entries);
    mc_receive_socket_entries = NULL;
    mc_receive_sockets_num = 0;
    return ((void *) -1);
  }

  for (i = 0; i < mc_receive_sockets_num; i++)
  {
    mc_receive_sockets[i].fd = mc_receive_socket_entries[i].fd;
    mc_receive_sockets[i].events = POLLIN | POLLPRI;
    mc_receive_sockets[i].revents = 0;
  }

  while (mc_receive_thread_loop != 0)
  {
    status = poll (mc_receive_sockets, mc_receive_sockets_num, -1);
    if (status <= 0)
    {
      char errbuf[1024];
      if (errno == EINTR)
        continue;
      ERROR ("gmond plugin: poll failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      break;
    }

    for (i = 0; i < mc_receive_sockets_num; i++)
    {
      if (mc_receive_sockets[i].revents != 0)
        mc_handle_socket (mc_receive_sockets + i);
    }
  } /* while (mc_receive_thread_loop != 0) */

  return ((void *) 0);
} /* }}} void *mc_receive_thread */

static int mc_receive_thread_start (void) /* {{{ */
{
  int status;

  if (mc_receive_thread_running != 0)
    return (-1);

  mc_receive_thread_loop = 1;

  status = pthread_create (&mc_receive_thread_id, /* attr = */ NULL,
      mc_receive_thread, /* args = */ NULL);
  if (status != 0)
  {
    ERROR ("gmond plugin: Starting receive thread failed.");
    mc_receive_thread_loop = 0;
    return (-1);
  }

  mc_receive_thread_running = 1;
  return (0);
} /* }}} int start_receive_thread */

static int mc_receive_thread_stop (void) /* {{{ */
{
  if (mc_receive_thread_running == 0)
    return (-1);

  mc_receive_thread_loop = 0;

  INFO ("gmond plugin: Stopping receive thread.");
  pthread_kill (mc_receive_thread_id, SIGTERM);
  pthread_join (mc_receive_thread_id, /* return value = */ NULL);
  memset (&mc_receive_thread_id, 0, sizeof (mc_receive_thread_id));

  mc_receive_thread_running = 0;

  return (0);
} /* }}} int mc_receive_thread_stop */

/* 
 * TODO: Config:
 *
 * <Plugin gmond>
 *   MCReceiveFrom "239.2.11.71" "8649"
 *   MCSendTo "239.2.11.71" "8649"
 *   <Metric "load_one">
 *     Type "load"
 *     [TypeInstance "foo"]
 *     [Index 0]
 *   </Metric>
 * </Plugin>
 */

static int gmond_init (void)
{
  create_sockets (&mc_send_sockets, &mc_send_sockets_num,
      "239.2.11.71", "8649", /* listen = */ 0);

  staging_tree = c_avl_create ((void *) strcmp);
  if (staging_tree == NULL)
  {
    ERROR ("gmond plugin: c_avl_create failed.");
    return (-1);
  }

  mc_receive_thread_start ();

  return (0);
} /* int gmond_init */

static int gmond_shutdown (void)
{
  size_t i;

  mc_receive_thread_stop ();

  pthread_mutex_lock (&mc_send_sockets_lock);
  for (i = 0; i < mc_send_sockets_num; i++)
  {
    close (mc_send_sockets[i].fd);
    mc_send_sockets[i].fd = -1;
  }
  sfree (mc_send_sockets);
  mc_send_sockets_num = 0;
  pthread_mutex_unlock (&mc_send_sockets_lock);


  return (0);
} /* int gmond_shutdown */

void module_register (void)
{
  plugin_register_init ("gmond", gmond_init);
  plugin_register_shutdown ("gmond", gmond_shutdown);
}

/* vim: set sw=2 sts=2 et fdm=marker : */
