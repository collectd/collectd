/**
 * collectd - src/pinba.c (based on code from pinba_engine 0.0.5)
 * Copyright (c) 2007-2009  Antony Dovgal
 * Copyright (C) 2010       Phoenix Kayo
 * Copyright (C) 2010       Florian Forster
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
 *   Antony Dovgal <tony at daylessday.org>
 *   Phoenix Kayo <kayo.k11.4 at gmail.com>
 *   Florian Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

#include "pinba.pb-c.h"

/*
 * Defines
 */
#ifndef PINBA_UDP_BUFFER_SIZE
# define PINBA_UDP_BUFFER_SIZE 65536
#endif

#ifndef PINBA_DEFAULT_NODE
# define PINBA_DEFAULT_NODE "::0"
#endif

#ifndef PINBA_DEFAULT_SERVICE
# define PINBA_DEFAULT_SERVICE "30002"
#endif

#ifndef PINBA_MAX_SOCKETS
# define PINBA_MAX_SOCKETS 16
#endif

/*
 * Private data structures
 */
/* {{{ */
struct pinba_socket_s
{
  struct pollfd fd[PINBA_MAX_SOCKETS];
  nfds_t fd_num;
};
typedef struct pinba_socket_s pinba_socket_t;

/* Fixed point counter value. n is the decimal part multiplied by 10^9. */
struct float_counter_s
{
  uint64_t i;
  uint64_t n; /* nanos */
};
typedef struct float_counter_s float_counter_t;

struct pinba_statnode_s
{
  /* collector name, used as plugin instance */
  char *name;

  /* query data */
  char *host;
  char *server;
  char *script;

  derive_t req_count;

  float_counter_t req_time;
  float_counter_t ru_utime;
  float_counter_t ru_stime;

  derive_t doc_size;
  gauge_t mem_peak;
};
typedef struct pinba_statnode_s pinba_statnode_t;
/* }}} */

/*
 * Module global variables
 */
/* {{{ */
static pinba_statnode_t *stat_nodes = NULL;
static unsigned int stat_nodes_num = 0;
static pthread_mutex_t stat_nodes_lock;

static char *conf_node = NULL;
static char *conf_service = NULL;

static _Bool collector_thread_running = 0;
static _Bool collector_thread_do_shutdown = 0;
static pthread_t collector_thread_id;
/* }}} */

/*
 * Functions
 */
static void float_counter_add (float_counter_t *fc, float val) /* {{{ */
{
  uint64_t tmp;

  if (val < 0.0)
    return;

  tmp = (uint64_t) val;
  val -= (double) tmp;

  fc->i += tmp;
  fc->n += (uint64_t) ((val * 1000000000.0) + .5);

  if (fc->n >= 1000000000)
  {
    fc->i += 1;
    fc->n -= 1000000000;
    assert (fc->n < 1000000000);
  }
} /* }}} void float_counter_add */

static derive_t float_counter_get (const float_counter_t *fc, /* {{{ */
    uint64_t factor)
{
  derive_t ret;

  ret = (derive_t) (fc->i * factor);
  ret += (derive_t) (fc->n / (1000000000 / factor));

  return (ret);
} /* }}} derive_t float_counter_get */

static void strset (char **str, const char *new) /* {{{ */
{
  char *tmp;

  if (!str || !new)
    return;

  tmp = strdup (new);
  if (tmp == NULL)
    return;

  sfree (*str);
  *str = tmp;
} /* }}} void strset */

static void service_statnode_add(const char *name, /* {{{ */
    const char *host,
    const char *server,
    const char *script)
{
  pinba_statnode_t *node;
  
  node = realloc (stat_nodes,
      sizeof (*stat_nodes) * (stat_nodes_num + 1));
  if (node == NULL)
  {
    ERROR ("pinba plugin: realloc failed");
    return;
  }
  stat_nodes = node;

  node = stat_nodes + stat_nodes_num;
  memset (node, 0, sizeof (*node));
  
  /* reset strings */
  node->name   = NULL;
  node->host   = NULL;
  node->server = NULL;
  node->script = NULL;

  node->mem_peak = NAN;
  
  /* fill query data */
  strset (&node->name, name);
  strset (&node->host, host);
  strset (&node->server, server);
  strset (&node->script, script);
  
  /* increment counter */
  stat_nodes_num++;
} /* }}} void service_statnode_add */

/* Copy the data from the global "stat_nodes" list into the buffer pointed to
 * by "res", doing the derivation in the process. Returns the next index or
 * zero if the end of the list has been reached. */
static unsigned int service_statnode_collect (pinba_statnode_t *res, /* {{{ */
    unsigned int index)
{
  pinba_statnode_t *node;
  
  if (stat_nodes_num == 0)
    return 0;
  
  /* begin collecting */
  if (index == 0)
    pthread_mutex_lock (&stat_nodes_lock);
  
  /* end collecting */
  if (index >= stat_nodes_num)
  {
    pthread_mutex_unlock (&stat_nodes_lock);
    return 0;
  }

  node = stat_nodes + index;
  memcpy (res, node, sizeof (*res));

  /* reset node */
  node->mem_peak = NAN;
  
  return (index + 1);
} /* }}} unsigned int service_statnode_collect */

static void service_statnode_process (pinba_statnode_t *node, /* {{{ */
    Pinba__Request* request)
{
  node->req_count++;

  float_counter_add (&node->req_time, request->request_time);
  float_counter_add (&node->ru_utime, request->ru_utime);
  float_counter_add (&node->ru_stime, request->ru_stime);

  node->doc_size += request->document_size;

  if (isnan (node->mem_peak)
      || (node->mem_peak < ((gauge_t) request->memory_peak)))
    node->mem_peak = (gauge_t) request->memory_peak;

} /* }}} void service_statnode_process */

static void service_process_request (Pinba__Request *request) /* {{{ */
{
  unsigned int i;

  pthread_mutex_lock (&stat_nodes_lock);
  
  for (i = 0; i < stat_nodes_num; i++)
  {
    if ((stat_nodes[i].host != NULL)
        && (strcmp (request->hostname, stat_nodes[i].host) != 0))
      continue;

    if ((stat_nodes[i].server != NULL)
      && (strcmp (request->server_name, stat_nodes[i].server) != 0))
      continue;

    if ((stat_nodes[i].script != NULL)
      && (strcmp (request->script_name, stat_nodes[i].script) != 0))
      continue;

    service_statnode_process(&stat_nodes[i], request);
  }
  
  pthread_mutex_unlock(&stat_nodes_lock);
} /* }}} void service_process_request */

static int pb_del_socket (pinba_socket_t *s, /* {{{ */
    nfds_t index)
{
  if (index >= s->fd_num)
    return (EINVAL);

  close (s->fd[index].fd);
  s->fd[index].fd = -1;

  /* When deleting the last element in the list, no memmove is necessary. */
  if (index < (s->fd_num - 1))
  {
    memmove (&s->fd[index], &s->fd[index + 1],
        sizeof (s->fd[0]) * (s->fd_num - (index + 1)));
  }

  s->fd_num--;
  return (0);
} /* }}} int pb_del_socket */

static int pb_add_socket (pinba_socket_t *s, /* {{{ */
    const struct addrinfo *ai)
{
  int fd;
  int tmp;
  int status;

  if (s->fd_num == PINBA_MAX_SOCKETS)
  {
    WARNING ("pinba plugin: Sorry, you have hit the built-in limit of "
        "%i sockets. Please complain to the collectd developers so we can "
        "raise the limit.", PINBA_MAX_SOCKETS);
    return (-1);
  }

  fd = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (fd < 0)
  {
    char errbuf[1024];
    ERROR ("pinba plugin: socket(2) failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (0);
  }

  tmp = 1;
  status = setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof (tmp));
  if (status != 0)
  {
    char errbuf[1024];
    WARNING ("pinba plugin: setsockopt(SO_REUSEADDR) failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
  }

  status = bind (fd, ai->ai_addr, ai->ai_addrlen);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("pinba plugin: bind(2) failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (0);
  }

  s->fd[s->fd_num].fd = fd;
  s->fd[s->fd_num].events = POLLIN | POLLPRI;
  s->fd[s->fd_num].revents = 0;
  s->fd_num++;

  return (0);
} /* }}} int pb_add_socket */

static pinba_socket_t *pinba_socket_open (const char *node, /* {{{ */
    const char *service)
{
  pinba_socket_t *s;
  struct addrinfo *ai_list;
  struct addrinfo *ai_ptr;
  struct addrinfo  ai_hints;
  int status;

  memset (&ai_hints, 0, sizeof (ai_hints));
  ai_hints.ai_flags = AI_PASSIVE;
  ai_hints.ai_family = AF_UNSPEC;
  ai_hints.ai_socktype = SOCK_DGRAM;
  ai_hints.ai_addr = NULL;
  ai_hints.ai_canonname = NULL;
  ai_hints.ai_next = NULL;

  if (node == NULL)
    node = PINBA_DEFAULT_NODE;

  if (service == NULL)
    service = PINBA_DEFAULT_SERVICE;

  ai_list = NULL;
  status = getaddrinfo (node, service,
      &ai_hints, &ai_list);
  if (status != 0)
  {
    ERROR ("pinba plugin: getaddrinfo(3) failed: %s",
        gai_strerror (status));
    return (NULL);
  }
  assert (ai_list != NULL);

  s = malloc (sizeof (*s));
  if (s == NULL)
  {
    freeaddrinfo (ai_list);
    ERROR ("pinba plugin: malloc failed.");
    return (NULL);
  }
  memset (s, 0, sizeof (*s));

  for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
  {
    status = pb_add_socket (s, ai_ptr);
    if (status != 0)
      break;
  } /* for (ai_list) */
  
  freeaddrinfo (ai_list);

  if (s->fd_num < 1)
  {
    WARNING ("pinba plugin: Unable to open socket for address %s.", node);
    sfree (s);
    s = NULL;
  }

  return (s);
} /* }}} pinba_socket_open */

static void pinba_socket_free (pinba_socket_t *socket) /* {{{ */
{
  nfds_t i;

  if (!socket)
    return;
  
  for (i = 0; i < socket->fd_num; i++)
  {
    if (socket->fd[i].fd < 0)
      continue;
    close (socket->fd[i].fd);
    socket->fd[i].fd = -1;
  }
  
  sfree(socket);
} /* }}} void pinba_socket_free */

static int pinba_process_stats_packet (const uint8_t *buffer, /* {{{ */
    size_t buffer_size)
{
  Pinba__Request *request;  
  
  request = pinba__request__unpack (NULL, buffer_size, buffer);
  
  if (!request)
    return (-1);

  service_process_request(request);
  pinba__request__free_unpacked (request, NULL);
    
  return (0);
} /* }}} int pinba_process_stats_packet */

static int pinba_udp_read_callback_fn (int sock) /* {{{ */
{
  uint8_t buffer[PINBA_UDP_BUFFER_SIZE];
  size_t buffer_size;
  int status;

  while (42)
  {
    buffer_size = sizeof (buffer);
    status = recvfrom (sock, buffer, buffer_size - 1, MSG_DONTWAIT, /* from = */ NULL, /* from len = */ 0);
    if (status < 0)
    {
      char errbuf[1024];

      if ((errno == EINTR)
#ifdef EWOULDBLOCK
          || (errno == EWOULDBLOCK)
#endif
          || (errno == EAGAIN))
      {
        continue;
      }

      WARNING("pinba plugin: recvfrom(2) failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      return (-1);
    }
    else if (status == 0)
    {
      DEBUG ("pinba plugin: recvfrom(2) returned unexpected status zero.");
      return (-1);
    }
    else /* if (status > 0) */
    {
      assert (((size_t) status) < buffer_size);
      buffer_size = (size_t) status;
      buffer[buffer_size] = 0;

      status = pinba_process_stats_packet (buffer, buffer_size);
      if (status != 0)
        DEBUG("pinba plugin: Parsing packet failed.");
      return (status);
    }
  } /* while (42) */

  /* not reached */
  assert (23 == 42);
  return (-1);
} /* }}} void pinba_udp_read_callback_fn */

static int receive_loop (void) /* {{{ */
{
  pinba_socket_t *s;

  s = pinba_socket_open (conf_node, conf_service);
  if (s == NULL)
  {
    ERROR ("pinba plugin: Collector thread is exiting prematurely.");
    return (-1);
  }

  while (!collector_thread_do_shutdown)
  {
    int status;
    nfds_t i;

    if (s->fd_num < 1)
      break;

    status = poll (s->fd, s->fd_num, /* timeout = */ 1000);
    if (status == 0) /* timeout */
    {
      continue;
    }
    else if (status < 0)
    {
      char errbuf[1024];

      if ((errno == EINTR) || (errno == EAGAIN))
        continue;

      ERROR ("pinba plugin: poll(2) failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      pinba_socket_free (s);
      return (-1);
    }

    for (i = 0; i < s->fd_num; i++)
    {
      if (s->fd[i].revents & (POLLERR | POLLHUP | POLLNVAL))
      {
        pb_del_socket (s, i);
        i--;
      }
      else if (s->fd[i].revents & (POLLIN | POLLPRI))
      {
        pinba_udp_read_callback_fn (s->fd[i].fd);
      }
    } /* for (s->fd) */
  } /* while (!collector_thread_do_shutdown) */

  pinba_socket_free (s);
  s = NULL;

  return (0);
} /* }}} int receive_loop */

static void *collector_thread (void *arg) /* {{{ */
{
  receive_loop ();

  memset (&collector_thread_id, 0, sizeof (collector_thread_id));
  collector_thread_running = 0;
  pthread_exit (NULL);
  return (NULL);
} /* }}} void *collector_thread */

/*
 * Plugin declaration section
 */
static int pinba_config_view (const oconfig_item_t *ci) /* {{{ */
{
  char *name   = NULL;
  char *host   = NULL;
  char *server = NULL;
  char *script = NULL;
  int status;
  int i;

  status = cf_util_get_string (ci, &name);
  if (status != 0)
    return (status);

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Host", child->key) == 0)
      status = cf_util_get_string (child, &host);
    else if (strcasecmp ("Server", child->key) == 0)
      status = cf_util_get_string (child, &server);
    else if (strcasecmp ("Script", child->key) == 0)
      status = cf_util_get_string (child, &script);
    else
    {
      WARNING ("pinba plugin: Unknown config option: %s", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status == 0)
    service_statnode_add (name, host, server, script);

  sfree (name);
  sfree (host);
  sfree (server);
  sfree (script);

  return (status);
} /* }}} int pinba_config_view */

static int plugin_config (oconfig_item_t *ci) /* {{{ */
{
  int i;
  
  /* The lock should not be necessary in the config callback, but let's be
   * sure.. */
  pthread_mutex_lock (&stat_nodes_lock);

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Address", child->key) == 0)
      cf_util_get_string (child, &conf_node);
    else if (strcasecmp ("Port", child->key) == 0)
      cf_util_get_service (child, &conf_service);
    else if (strcasecmp ("View", child->key) == 0)
      pinba_config_view (child);
    else
      WARNING ("pinba plugin: Unknown config option: %s", child->key);
  }

  pthread_mutex_unlock(&stat_nodes_lock);
  
  return (0);
} /* }}} int pinba_config */

static int plugin_init (void) /* {{{ */
{
  int status;

  if (stat_nodes == NULL)
  {
    /* Collect the "total" data by default. */
    service_statnode_add ("total",
        /* host   = */ NULL,
        /* server = */ NULL,
        /* script = */ NULL);
  }

  if (collector_thread_running)
    return (0);

  status = pthread_create (&collector_thread_id,
      /* attrs = */ NULL,
      collector_thread,
      /* args = */ NULL);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("pinba plugin: pthread_create(3) failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }
  collector_thread_running = 1;

  return (0);
} /* }}} */

static int plugin_shutdown (void) /* {{{ */
{
  if (collector_thread_running)
  {
    int status;

    DEBUG ("pinba plugin: Shutting down collector thread.");
    collector_thread_do_shutdown = 1;

    status = pthread_join (collector_thread_id, /* retval = */ NULL);
    if (status != 0)
    {
      char errbuf[1024];
      ERROR ("pinba plugin: pthread_join(3) failed: %s",
          sstrerror (status, errbuf, sizeof (errbuf)));
    }

    collector_thread_running = 0;
    collector_thread_do_shutdown = 0;
  } /* if (collector_thread_running) */

  return (0);
} /* }}} int plugin_shutdown */

static int plugin_submit (const pinba_statnode_t *res) /* {{{ */
{
  value_t value;
  value_list_t vl = VALUE_LIST_INIT;
  
  vl.values = &value;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "pinba", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, res->name, sizeof (vl.plugin_instance));

  value.derive = res->req_count;
  sstrncpy (vl.type, "total_requests", sizeof (vl.type)); 
  plugin_dispatch_values (&vl);

  value.derive = float_counter_get (&res->req_time, /* factor = */ 1000);
  sstrncpy (vl.type, "total_time_in_ms", sizeof (vl.type)); 
  plugin_dispatch_values (&vl);

  value.derive = res->doc_size;
  sstrncpy (vl.type, "total_bytes", sizeof (vl.type)); 
  plugin_dispatch_values (&vl);

  value.derive = float_counter_get (&res->ru_utime, /* factor = */ 100);
  sstrncpy (vl.type, "cpu", sizeof (vl.type));
  sstrncpy (vl.type_instance, "user", sizeof (vl.type_instance));
  plugin_dispatch_values (&vl);

  value.derive = float_counter_get (&res->ru_stime, /* factor = */ 100);
  sstrncpy (vl.type, "cpu", sizeof (vl.type));
  sstrncpy (vl.type_instance, "system", sizeof (vl.type_instance));
  plugin_dispatch_values (&vl);

  value.gauge = res->mem_peak;
  sstrncpy (vl.type, "memory", sizeof (vl.type));
  sstrncpy (vl.type_instance, "peak", sizeof (vl.type_instance));
  plugin_dispatch_values (&vl);

  return (0);
} /* }}} int plugin_submit */

static int plugin_read (void) /* {{{ */
{
  unsigned int i=0;
  pinba_statnode_t data;
  
  while ((i = service_statnode_collect (&data, i)) != 0)
  {
    plugin_submit (&data);
  }
  
  return 0;
} /* }}} int plugin_read */

void module_register (void) /* {{{ */
{
  plugin_register_complex_config ("pinba", plugin_config);
  plugin_register_init ("pinba", plugin_init);
  plugin_register_read ("pinba", plugin_read);
  plugin_register_shutdown ("pinba", plugin_shutdown);
} /* }}} void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
