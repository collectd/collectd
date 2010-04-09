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
 *  Service declaration section
 */
#ifndef PINBA_UDP_BUFFER_SIZE
# define PINBA_UDP_BUFFER_SIZE 65536
#endif

#ifndef PINBA_DEFAULT_ADDRESS
# define PINBA_DEFAULT_ADDRESS "127.0.0.1" /* FIXME */
#endif

#ifndef PINBA_DEFAULT_PORT
# define PINBA_DEFAULT_PORT 12345 /* FIXME */
#endif

#ifndef PINBA_MAX_SOCKETS
# define PINBA_MAX_SOCKETS 16
#endif

#ifndef NI_MAXSERV
# define NI_MAXSERV 32
#endif

/*
 * Private data structures
 */
typedef struct _pinba_statres_ pinba_statres;
struct _pinba_statres_ {
  const char *name;
  double req_per_sec;
  double req_time;
  double ru_utime;
  double ru_stime;
  double doc_size;
  double mem_peak;
};

struct pinba_socket_s {
  struct pollfd fd[PINBA_MAX_SOCKETS];
  nfds_t fd_num;
};
typedef struct pinba_socket_s pinba_socket_t;

typedef double pinba_time_t;
typedef uint32_t pinba_size_t;

typedef struct pinba_statnode_s pinba_statnode_t;
struct pinba_statnode_s
{
  /* collector name */
  char *name;
  /* query data */
  char *host;
  char *server;
  char *script;
  /* collected data */
  pinba_time_t last_coll;
  pinba_size_t req_count;
  pinba_time_t req_time;
  pinba_time_t ru_utime;
  pinba_time_t ru_stime;
  pinba_size_t doc_size;
  pinba_size_t mem_peak;
};

static pinba_statnode_t *stat_nodes = NULL;
static unsigned int stat_nodes_num = 0;
static pthread_mutex_t stat_nodes_lock;

char service_status=0;
char *service_address = PINBA_DEFAULT_ADDRESS;
unsigned int service_port=PINBA_DEFAULT_PORT;

static _Bool collector_thread_running = 0;
static _Bool collector_thread_do_shutdown = 0;
static pthread_t collector_thread_id;

static pinba_time_t now (void) /* {{{ */
{
  static struct timeval tv;
  
  gettimeofday (&tv, /* tz = */ NULL);
  
  return (double)tv.tv_sec+((double)tv.tv_usec/(double)1000000);
} /* }}} */

static void service_statnode_reset (pinba_statnode_t *node) /* {{{ */
{
  node->last_coll=now();
  node->req_count=0;
  node->req_time=0.0;
  node->ru_utime=0.0;
  node->ru_stime=0.0;
  node->doc_size=0;
  node->mem_peak=0;
} /* }}} void service_statnode_reset */

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
  DEBUG("adding node `%s' to collector { %s, %s, %s }", name, host?host:"", server?server:"", script?script:"");
  
  stat_nodes=realloc(stat_nodes, sizeof(pinba_statnode_t)*(stat_nodes_num+1));
  if(!stat_nodes){
    ERROR("Realloc failed!");
    exit(-1);
  }
  
  node=&stat_nodes[stat_nodes_num];
  
  /* reset stat data */
  service_statnode_reset(node);
  
  /* reset strings */
  node->name=NULL;
  node->host=NULL;
  node->server=NULL;
  node->script=NULL;
  
  /* fill query data */
  strset(&node->name, name);
  strset(&node->host, host);
  strset(&node->server, server);
  strset(&node->script, script);
  
  /* increment counter */
  stat_nodes_num++;
} /* }}} void service_statnode_add */

static void service_statnode_free (void)
{
  unsigned int i;

  if(stat_nodes_num < 1)
    return;

  for (i = 0; i < stat_nodes_num; i++)
  {
    sfree (stat_nodes[i].name);
    sfree (stat_nodes[i].host);
    sfree (stat_nodes[i].server);
    sfree (stat_nodes[i].script);
  }

  sfree (stat_nodes);
  stat_nodes_num = 0;

  pthread_mutex_destroy (&stat_nodes_lock);
}

static void service_statnode_init (void)
{
  /* only total info collect by default */
  service_statnode_free();
  
  DEBUG("initializing collector..");
  pthread_mutex_init(&stat_nodes_lock, 0);
}

static void service_statnode_begin (void)
{
  service_statnode_init();
  pthread_mutex_lock(&stat_nodes_lock);
  
  service_statnode_add("total", NULL, NULL, NULL);
}

static void service_statnode_end (void)
{
  pthread_mutex_unlock(&stat_nodes_lock);
}

static unsigned int service_statnode_collect (pinba_statres *res, /* {{{ */
    unsigned int index)
{
  pinba_statnode_t* node;
  pinba_time_t delta;
  
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
  delta = now() - node->last_coll;
  
  res->name = node->name;
  res->req_per_sec = node->req_count / delta;
  
  if (node->req_count == 0)
    node->req_count = 1;

  res->req_time = node->req_time / node->req_count;
  res->ru_utime = node->ru_utime / node->req_count;
  res->ru_stime = node->ru_stime / node->req_count;
  res->ru_stime = node->ru_stime / node->req_count;
  res->doc_size = node->doc_size / node->req_count;
  res->mem_peak = node->mem_peak / node->req_count;
  
  service_statnode_reset (node);
  return (index + 1);
} /* }}} unsigned int service_statnode_collect */

static void service_statnode_process (pinba_statnode_t *node,
    Pinba__Request* request)
{
  node->req_count++;
  node->req_time+=request->request_time;
  node->ru_utime+=request->ru_utime;
  node->ru_stime+=request->ru_stime;
  node->doc_size+=request->document_size;
  node->mem_peak+=request->memory_peak;
}

static void service_process_request (Pinba__Request *request)
{
  unsigned int i;

  pthread_mutex_lock (&stat_nodes_lock);
  
  for (i = 0; i < stat_nodes_num; i++)
  {
    if(stat_nodes[i].host && strcmp(request->hostname, stat_nodes[i].host))
      continue;
    if(stat_nodes[i].server && strcmp(request->server_name, stat_nodes[i].server))
      continue;
    if(stat_nodes[i].script && strcmp(request->script_name, stat_nodes[i].script))
      continue;

    service_statnode_process(&stat_nodes[i], request);
  }
  
  pthread_mutex_unlock(&stat_nodes_lock);
}

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
    int listen_port)
{
  pinba_socket_t *s;
  struct addrinfo *ai_list;
  struct addrinfo *ai_ptr;
  struct addrinfo  ai_hints;
  int status;

  char service[NI_MAXSERV]; /* FIXME */
  snprintf (service, sizeof (service), "%i", listen_port);

  memset (&ai_hints, 0, sizeof (ai_hints));
  ai_hints.ai_flags = AI_PASSIVE;
  ai_hints.ai_family = AF_UNSPEC;
  ai_hints.ai_socktype = SOCK_DGRAM;
  ai_hints.ai_addr = NULL;
  ai_hints.ai_canonname = NULL;
  ai_hints.ai_next = NULL;

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
  if (s != NULL)
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

  s = pinba_socket_open (service_address, service_port);
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

static int config_set (char **var, const char *value)
{
  /* code from nginx plugin for collectd */
  if (*var != NULL) {
    free (*var);
    *var = NULL;
  }
  
  if ((*var = strdup (value)) == NULL) return (1);
  else return (0);
}

static int plugin_config (oconfig_item_t *ci)
{
  unsigned int i, o;
  int pinba_port = 0;
  char *pinba_address = NULL;
  
  INFO("Pinba Configure..");
  
  service_statnode_begin();
  
  /* Set default values */
  config_set(&pinba_address, PINBA_DEFAULT_ADDRESS);
  pinba_port = PINBA_DEFAULT_PORT;
  
  for (i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp ("Address", child->key) == 0) {
      if ((child->values_num != 1) || (child->values[0].type != OCONFIG_TYPE_STRING)){
	WARNING ("pinba plugin: `Address' needs exactly one string argument.");
	return (-1);
      }
      config_set(&pinba_address, child->values[0].value.string);
    } else if (strcasecmp ("Port", child->key) == 0) {
      if ((child->values_num != 1) || (child->values[0].type != OCONFIG_TYPE_NUMBER)){
	WARNING ("pinba plugin: `Port' needs exactly one number argument.");
	return (-1);
      }
      pinba_port=child->values[0].value.number;
    } else if (strcasecmp ("View", child->key) == 0) {
      const char *name=NULL, *host=NULL, *server=NULL, *script=NULL;
      if ((child->values_num != 1) || (child->values[0].type != OCONFIG_TYPE_STRING) || strlen(child->values[0].value.string)==0){
	WARNING ("pinba plugin: `View' needs exactly one non-empty string argument.");
	return (-1);
      }
      name = child->values[0].value.string;
      for(o=0; o<child->children_num; o++){
	oconfig_item_t *node = child->children + o;
	if (strcasecmp ("Host", node->key) == 0) {
	  if ((node->values_num != 1) || (node->values[0].type != OCONFIG_TYPE_STRING) || strlen(node->values[0].value.string)==0){
	    WARNING ("pinba plugin: `View->Host' needs exactly one non-empty string argument.");
	    return (-1);
	  }
	  host = node->values[0].value.string;
	} else if (strcasecmp ("Server", node->key) == 0) {
	  if ((node->values_num != 1) || (node->values[0].type != OCONFIG_TYPE_STRING) || strlen(node->values[0].value.string)==0){
	    WARNING ("pinba plugin: `View->Server' needs exactly one non-empty string argument.");
	    return (-1);
	  }
	  server = node->values[0].value.string;
	} else if (strcasecmp ("Script", node->key) == 0) {
	  if ((node->values_num != 1) || (node->values[0].type != OCONFIG_TYPE_STRING) || strlen(node->values[0].value.string)==0){
	    WARNING ("pinba plugin: `View->Script' needs exactly one non-empty string argument.");
	    return (-1);
	  }
	  script = node->values[0].value.string;
	} else {
	  WARNING ("pinba plugin: In `<View>' context allowed only `Host', `Server' and `Script' options but not the `%s'.", node->key);
	  return (-1);
	}
      }
      /* add new statnode */
      service_statnode_add(name, host, server, script);
    } else {
      WARNING ("pinba plugin: In `<Plugin pinba>' context allowed only `Address', `Port' and `Observe' options but not the `%s'.", child->key);
      return (-1);
    }
  }
  
  service_statnode_end();
  
  return (0);
} /* int pinba_config */

static int plugin_init (void) /* {{{ */
{
  int status;

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

static int plugin_submit (const char *plugin_instance,
	       const char *type,
	       const pinba_statres *res) {
  value_t values[6];
  value_list_t vl = VALUE_LIST_INIT;
  
  values[0].gauge = res->req_per_sec;
  values[1].gauge = res->req_time;
  values[2].gauge = res->ru_utime;
  values[3].gauge = res->ru_stime;
  values[4].gauge = res->doc_size;
  values[5].gauge = res->mem_peak;
  
  vl.values = values;
  vl.values_len = 6;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "pinba", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, plugin_instance,
	    sizeof(vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));
  INFO("Pinba Dispatch");
  plugin_dispatch_values (&vl);

  return (0);
}

static int plugin_read (void)
{
  unsigned int i=0;
  static pinba_statres res;
  
  while ((i = service_statnode_collect (&res, i)) != 0)
  {
    plugin_submit(res.name, "pinba_view", &res);
  }
  
  return 0;
}

void module_register (void)
{
  plugin_register_complex_config ("pinba", plugin_config);
  plugin_register_init ("pinba", plugin_init);
  plugin_register_read ("pinba", plugin_read);
  plugin_register_shutdown ("pinba", plugin_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
