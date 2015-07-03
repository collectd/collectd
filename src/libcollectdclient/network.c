/**
 * collectd - src/libcollectdclient/network.c
 * Copyright (C) 2005-2015  Florian Forster
 * Copyright (C) 2010       Max Henkel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
 *   Max Henkel <henkel at gmx.at>
 **/

#include "collectd.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif

#if HAVE_NET_IF_H
# include <net/if.h>
#endif

#include "collectd/network.h"
#include "collectd/network_buffer.h"

/*
 * Private data types
 */
struct lcc_network_s
{
  lcc_server_t *servers;
};

struct lcc_server_s
{
  char *node;
  char *service;

  int ttl;
  lcc_security_level_t security_level;
  char *username;
  char *password;

  int fd;
  struct sockaddr *sa;
  socklen_t sa_len;

  lcc_network_buffer_t *buffer;

  lcc_server_t *next;
};

/*
 * Private functions
 */
static int server_close_socket (lcc_server_t *srv) /* {{{ */
{
  if (srv == NULL)
    return (EINVAL);

  if (srv->fd < 0)
    return (0);

  close (srv->fd);
  free (srv->sa);
  srv->sa = NULL;
  srv->sa_len = 0;

  return (0);
} /* }}} int server_close_socket */

static void int_server_destroy (lcc_server_t *srv) /* {{{ */
{
  lcc_server_t *next;

  if (srv == NULL)
    return;

  server_close_socket (srv);

  next = srv->next;

  if (srv->fd >= 0)
  {
    close (srv->fd);
    srv->fd = -1;
  }

  free (srv->node);
  free (srv->service);
  free (srv->username);
  free (srv->password);
  free (srv);

  int_server_destroy (next);
} /* }}} void int_server_destroy */

static int server_open_socket (lcc_server_t *srv) /* {{{ */
{
  struct addrinfo ai_hints = { 0 };
  struct addrinfo *ai_list = NULL;
  struct addrinfo *ai_ptr;
  int status;

  if (srv == NULL)
    return (EINVAL);

  if (srv->fd >= 0)
    server_close_socket (srv);

#ifdef AI_ADDRCONFIG
  ai_hints.ai_flags |= AI_ADDRCONFIG;
#endif
  ai_hints.ai_family   = AF_UNSPEC;
  ai_hints.ai_socktype = SOCK_DGRAM;

  status = getaddrinfo (srv->node, srv->service, &ai_hints, &ai_list);
  if (status != 0)
    return (status);
  assert (ai_list != NULL);

  for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
  {
    srv->fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
    if (srv->fd < 0)
      continue;

    if (ai_ptr->ai_family == AF_INET)
    {

      struct sockaddr_in *addr = (struct sockaddr_in *) ai_ptr->ai_addr;
      int optname;

      if (IN_MULTICAST (ntohl (addr->sin_addr.s_addr)))
        optname = IP_MULTICAST_TTL;
      else
        optname = IP_TTL;

      setsockopt (srv->fd, IPPROTO_IP, optname,
          &srv->ttl,
          sizeof (srv->ttl));
    }
    else if (ai_ptr->ai_family == AF_INET6)
    {
      /* Useful example: http://gsyc.escet.urjc.es/~eva/IPv6-web/examples/mcast.html */
      struct sockaddr_in6 *addr = (struct sockaddr_in6 *) ai_ptr->ai_addr;
      int optname;

      if (IN6_IS_ADDR_MULTICAST (&addr->sin6_addr))
        optname = IPV6_MULTICAST_HOPS;
      else
        optname = IPV6_UNICAST_HOPS;

      setsockopt (srv->fd, IPPROTO_IPV6, optname,
          &srv->ttl,
          sizeof (srv->ttl));
    }

    srv->sa = malloc (ai_ptr->ai_addrlen);
    if (srv->sa == NULL)
    {
      close (srv->fd);
      srv->fd = -1;
      continue;
    }

    memcpy (srv->sa, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    srv->sa_len = ai_ptr->ai_addrlen;
    break;
  }

  freeaddrinfo (ai_list);

  if (srv->fd < 0)
    return (-1);
  return (0);
} /* }}} int server_open_socket */

static int server_send_buffer (lcc_server_t *srv) /* {{{ */
{
  char buffer[LCC_NETWORK_BUFFER_SIZE_DEFAULT];
  size_t buffer_size;
  int status;

  if (srv->fd < 0)
  {
    status = server_open_socket (srv);
    if (status != 0)
      return (status);
  }

  memset (buffer, 0, sizeof (buffer));
  buffer_size = sizeof (buffer);

  status = lcc_network_buffer_finalize (srv->buffer);
  if (status != 0)
  {
    lcc_network_buffer_initialize (srv->buffer);
    return (status);
  }

  status = lcc_network_buffer_get (srv->buffer, buffer, &buffer_size);
  lcc_network_buffer_initialize (srv->buffer);

  if (status != 0)
    return (status);

  if (buffer_size > sizeof (buffer))
    buffer_size = sizeof (buffer);

  while (42)
  {
    assert (srv->fd >= 0);
    assert (srv->sa != NULL);
    status = (int) sendto (srv->fd, buffer, buffer_size, /* flags = */ 0,
        srv->sa, srv->sa_len);
    if ((status < 0) && ((errno == EINTR) || (errno == EAGAIN)))
      continue;

    break;
  }

  if (status < 0)
    return (status);
  return (0);
} /* }}} int server_send_buffer */

static int server_value_add (lcc_server_t *srv, /* {{{ */
    const lcc_value_list_t *vl)
{
  int status;

  status = lcc_network_buffer_add_value (srv->buffer, vl);
  if (status == 0)
    return (0);

  server_send_buffer (srv);
  return (lcc_network_buffer_add_value (srv->buffer, vl));
} /* }}} int server_value_add */

/*
 * Public functions
 */
lcc_network_t *lcc_network_create (void) /* {{{ */
{
  lcc_network_t *net;

  net = malloc (sizeof (*net));
  if (net == NULL)
    return (NULL);
  memset (net, 0, sizeof (*net));

  net->servers = NULL;

  return (net);
} /* }}} lcc_network_t *lcc_network_create */

void lcc_network_destroy (lcc_network_t *net) /* {{{ */
{
  if (net == NULL)
    return;
  int_server_destroy (net->servers);
  free (net);
} /* }}} void lcc_network_destroy */

lcc_server_t *lcc_server_create (lcc_network_t *net, /* {{{ */
    const char *node, const char *service)
{
  lcc_server_t *srv;

  if ((net == NULL) || (node == NULL))
    return (NULL);
  if (service == NULL)
    service = NET_DEFAULT_PORT;

  srv = malloc (sizeof (*srv));
  if (srv == NULL)
    return (NULL);
  memset (srv, 0, sizeof (*srv));

  srv->fd = -1;
  srv->security_level = NONE;
  srv->username = NULL;
  srv->password = NULL;
  srv->next = NULL;

  srv->node = strdup (node);
  if (srv->node == NULL)
  {
    free (srv);
    return (NULL);
  }

  srv->service = strdup (service);
  if (srv->service == NULL)
  {
    free (srv->node);
    free (srv);
    return (NULL);
  }

  srv->buffer = lcc_network_buffer_create (/* size = */ 0);
  if (srv->buffer == NULL)
  {
    free (srv->service);
    free (srv->node);
    free (srv);
    return (NULL);
  }

  if (net->servers == NULL)
  {
    net->servers = srv;
  }
  else
  {
    lcc_server_t *last = net->servers;

    while (last->next != NULL)
      last = last->next;

    last->next = srv;
  }

  return (srv);
} /* }}} lcc_server_t *lcc_server_create */

int lcc_server_destroy (lcc_network_t *net, lcc_server_t *srv) /* {{{ */
{
  if ((net == NULL) || (srv == NULL))
    return (EINVAL);

  if (net->servers == srv)
  {
    net->servers = srv->next;
    srv->next = NULL;
  }
  else
  {
    lcc_server_t *prev = net->servers;

    while ((prev != NULL) && (prev->next != srv))
      prev = prev->next;

    if (prev == NULL)
      return (ENOENT);

    prev->next = srv->next;
    srv->next = NULL;
  }

  int_server_destroy (srv);

  return (0);
} /* }}} int lcc_server_destroy */

int lcc_server_set_ttl (lcc_server_t *srv, uint8_t ttl) /* {{{ */
{
  if (srv == NULL)
    return (EINVAL);

  srv->ttl = (int) ttl;

  return (0);
} /* }}} int lcc_server_set_ttl */

int lcc_server_set_interface (lcc_server_t *srv, char const *interface) /* {{{ */
{
  int if_index;
  int status;

  if ((srv == NULL) || (interface == NULL))
    return (EINVAL);

  if_index = if_nametoindex (interface);
  if (if_index == 0)
    return (ENOENT);

  /* IPv4 multicast */
  if (srv->sa->sa_family == AF_INET)
  {
    struct sockaddr_in *addr = (struct sockaddr_in *) srv->sa;

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
      mreq.imr_ifindex = if_index;
#else
      struct ip_mreq mreq;

      memset (&mreq, 0, sizeof (mreq));
      mreq.imr_multiaddr.s_addr = addr->sin_addr.s_addr;
      mreq.imr_interface.s_addr = ntohl (INADDR_ANY);
#endif

      status = setsockopt (srv->fd, IPPROTO_IP, IP_MULTICAST_IF,
          &mreq, sizeof (mreq));
      if (status != 0)
        return (status);

      return (0);
    }
  }

  /* IPv6 multicast */
  if (srv->sa->sa_family == AF_INET6)
  {
    struct sockaddr_in6 *addr = (struct sockaddr_in6 *) srv->sa;

    if (IN6_IS_ADDR_MULTICAST (&addr->sin6_addr))
    {
      status = setsockopt (srv->fd, IPPROTO_IPV6, IPV6_MULTICAST_IF,
          &if_index, sizeof (if_index));
      if (status != 0)
        return (status);

      return (0);
    }
  }

  /* else: Not a multicast interface. */
#if defined(SO_BINDTODEVICE)
  status = setsockopt (srv->fd, SOL_SOCKET, SO_BINDTODEVICE,
      interface, strlen (interface) + 1);
  if (status != 0)
    return (-1);
#endif

  return (0);
} /* }}} int lcc_server_set_interface */

int lcc_server_set_security_level (lcc_server_t *srv, /* {{{ */
    lcc_security_level_t level,
    const char *username, const char *password)
{
  return (lcc_network_buffer_set_security_level (srv->buffer,
        level, username, password));
} /* }}} int lcc_server_set_security_level */

int lcc_network_values_send (lcc_network_t *net, /* {{{ */
    const lcc_value_list_t *vl)
{
  lcc_server_t *srv;

  if ((net == NULL) || (vl == NULL))
    return (EINVAL);

  for (srv = net->servers; srv != NULL; srv = srv->next)
    server_value_add (srv, vl);

  return (0);
} /* }}} int lcc_network_values_send */

/* vim: set sw=2 sts=2 et fdm=marker : */
