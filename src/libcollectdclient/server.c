/**
 * Copyright 2017 Florian Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#ifdef WIN32
#include "gnulib_config.h"
#endif

#include "config.h"

#if !defined(__GNUC__) || !__GNUC__
#define __attribute__(x) /**/
#endif

#include "collectd/lcc_features.h"
#include "collectd/network_parse.h" /* for lcc_network_parse_options_t */
#include "collectd/server.h"

// clang-format off
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
// clang-format on

#include <stdio.h>
#define DEBUG(...) printf(__VA_ARGS__)

#ifdef WIN32
#include <ws2tcpip.h>
#define AI_ADDRCONFIG 0
#endif

static bool is_multicast(struct addrinfo const *ai) {
  if (ai->ai_family == AF_INET) {
    struct sockaddr_in *addr = (struct sockaddr_in *)ai->ai_addr;
    return IN_MULTICAST(ntohl(addr->sin_addr.s_addr));
  } else if (ai->ai_family == AF_INET6) {
    struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ai->ai_addr;
    return IN6_IS_ADDR_MULTICAST(&addr->sin6_addr);
  }
  return 0;
}

static int server_multicast_join(lcc_listener_t *srv,
                                 struct sockaddr_storage *group, int loop_back,
                                 int ttl) {
  if (group->ss_family == AF_INET) {
    struct sockaddr_in *sa = (struct sockaddr_in *)group;

    int status = setsockopt(srv->conn, IPPROTO_IP, IP_MULTICAST_LOOP,
                            &loop_back, sizeof(loop_back));
    if (status == -1) {
      DEBUG("setsockopt(IP_MULTICAST_LOOP, %d) = %d\n", loop_back, errno);
      return errno;
    }

    status =
        setsockopt(srv->conn, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    if (status == -1)
      return errno;

#if HAVE_STRUCT_IP_MREQN_IMR_IFINDEX
    struct ip_mreqn mreq = {
        .imr_address.s_addr = INADDR_ANY,
        .imr_multiaddr.s_addr = sa->sin_addr.s_addr,
        .imr_ifindex = if_nametoindex(srv->iface),
    };
#else
#ifdef WIN32
    struct ip_mreq mreq = {
        .imr_interface.s_addr = INADDR_ANY,
        .imr_multiaddr.s_addr = sa->sin_addr.s_addr,
    };
#else
    struct ip_mreq mreq = {
        .imr_multiaddr.s_addr = sa->sin_addr.s_addr,
    };
#endif /* WIN32 */
#endif /* HAVE_STRUCT_IP_MREQN_IMR_IFINDEX */
    status = setsockopt(srv->conn, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
                        sizeof(mreq));
    if (status == -1)
      return errno;
  } else if (group->ss_family == AF_INET6) {
    struct sockaddr_in6 *sa = (struct sockaddr_in6 *)group;

    int status = setsockopt(srv->conn, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
                            &loop_back, sizeof(loop_back));
    if (status == -1)
      return errno;

    status = setsockopt(srv->conn, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl,
                        sizeof(ttl));
    if (status == -1)
      return errno;

    struct ipv6_mreq mreq6 = {
        .ipv6mr_interface = if_nametoindex(srv->iface),
    };
    memmove(&mreq6.ipv6mr_multiaddr, &sa->sin6_addr, sizeof(struct in6_addr));

    status = setsockopt(srv->conn, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6,
                        sizeof(mreq6));
    if (status == -1)
      return errno;
  } else {
    return EINVAL;
  }

  return 0;
}

static int server_bind_socket(lcc_listener_t *srv, struct addrinfo const *ai) {
  /* allow multiple sockets to use the same PORT number */
  if (setsockopt(srv->conn, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) ==
      -1) {
    return errno;
  }

  if (bind(srv->conn, ai->ai_addr, ai->ai_addrlen) == -1) {
    return -1;
  }

  if (is_multicast(ai)) {
    int status = server_multicast_join(srv, (void *)ai->ai_addr, /* loop = */ 1,
                                       /* ttl = */ 16);
    if (status != 0)
      return status;
  }

  return 0;
}

static int server_open(lcc_listener_t *srv) {
  struct addrinfo *res = NULL;
  int status = getaddrinfo(srv->node ? srv->node : "::",
                           srv->service ? srv->service : LCC_DEFAULT_PORT,
                           &(struct addrinfo){
                               .ai_flags = AI_ADDRCONFIG,
                               .ai_family = AF_UNSPEC,
                               .ai_socktype = SOCK_DGRAM,
                           },
                           &res);
  if (status != 0)
    return status;

  for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
    srv->conn = socket(ai->ai_family, ai->ai_socktype, 0);
    if (srv->conn == -1)
      continue;

    status = server_bind_socket(srv, ai);
    if (status != 0) {
      close(srv->conn);
      srv->conn = -1;
      continue;
    }

    break;
  }

  freeaddrinfo(res);

  if (srv->conn >= 0)
    return 0;
  return status != 0 ? status : -1;
}

int lcc_listen_and_write(lcc_listener_t srv) {
  bool close_socket = 0;

  if (srv.conn < 0) {
    int status = server_open(&srv);
    if (status != 0)
      return status;
    close_socket = 1;
  }

  if (srv.buffer_size == 0)
    srv.buffer_size = LCC_NETWORK_BUFFER_SIZE;

  if (srv.parser == NULL)
    srv.parser = lcc_network_parse;

  int ret = 0;
  while (42) {
    char buffer[srv.buffer_size];
    ssize_t len = recv(srv.conn, buffer, sizeof(buffer), /* flags = */ 0);
    if (len == -1) {
      ret = errno;
      break;
    } else if (len == 0) {
      break;
    }

    (void)srv.parser(buffer, (size_t)len, srv.parse_options);
  }

  if (close_socket) {
    close(srv.conn);
    srv.conn = -1;
  }

  return ret;
}
