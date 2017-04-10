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

#if HAVE_CONFIG_H
#include "config.h"
#endif

#if !defined(__GNUC__) || !__GNUC__
#define __attribute__(x) /**/
#endif

#include "collectd/lcc_features.h"
#include "collectd/server.h"

#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <net/if.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>
#define DEBUG(...) printf(__VA_ARGS__)

static _Bool is_multicast(struct addrinfo const *ai) {
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
        .imr_ifindex = if_nametoindex(srv->interface),
    };
#else
    struct ip_mreq mreq = {
        .imr_address.s_addr = INADDR_ANY, .imr_multiaddr.s_addr = sa->s_addr,
    };
#endif
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
        .ipv6mr_interface = if_nametoindex(srv->interface),
    };
    memcpy(&mreq6.ipv6mr_multiaddr, &sa->sin6_addr, sizeof(struct in6_addr));

    status = setsockopt(srv->conn, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mreq6,
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
  _Bool close_socket = 0;

  if (srv.conn < 0) {
    int status = server_open(&srv);
    if (status != 0)
      return status;
    close_socket = 1;
  }

  if (srv.buffer_size == 0)
    /* TODO(octo): this should be a define. */
    srv.buffer_size = 1452;

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

    /* TODO(octo): implement parse(). */
    (void)lcc_network_parse(buffer, (size_t)len, srv.writer);
  }

  if (close_socket) {
    close(srv.conn);
    srv.conn = -1;
  }

  return ret;
}

typedef struct {
  uint8_t *data;
  size_t len;
} buffer_t;

static int buffer_next(buffer_t *b, void *out, size_t n) {
  if (b->len < n) {
    return -1;
  }
  memmove(out, b->data, n);

  b->data += n;
  b->len -= n;

  return 0;
}

static int buffer_uint16(buffer_t *b, uint16_t *out) {
  uint16_t tmp;
  if (buffer_next(b, &tmp, sizeof(tmp)) != 0)
    return -1;

  *out = be16toh(tmp);
  return 0;
}

#define TYPE_HOST 0x0000
#define TYPE_TIME 0x0001
#define TYPE_TIME_HR 0x0008
#define TYPE_PLUGIN 0x0002
#define TYPE_PLUGIN_INSTANCE 0x0003
#define TYPE_TYPE 0x0004
#define TYPE_TYPE_INSTANCE 0x0005
#define TYPE_VALUES 0x0006
#define TYPE_INTERVAL 0x0007
#define TYPE_INTERVAL_HR 0x0009

static int parse_int(void *payload, size_t payload_size, uint64_t *out) {
  uint64_t tmp;

  if (payload_size != sizeof(tmp))
    return EINVAL;

  memmove(&tmp, payload, sizeof(tmp));
  *out = be64toh(tmp);
  return 0;
}

static int parse_string(void *payload, size_t payload_size, char *out,
                        size_t out_size) {
  char *in = payload;

  if ((payload_size < 1) || (in[payload_size - 1] != 0) ||
      (payload_size > out_size))
    return EINVAL;

  strncpy(out, in, out_size);
  return 0;
}

static int parse_identifier(uint16_t type, void *payload, size_t payload_size,
                            lcc_value_list_t *state) {
  char buf[LCC_NAME_LEN];

  if (parse_string(payload, payload_size, buf, sizeof(buf)) != 0)
    return EINVAL;

  switch (type) {
  case TYPE_HOST:
    memmove(state->identifier.host, buf, LCC_NAME_LEN);
    break;
  case TYPE_PLUGIN:
    memmove(state->identifier.plugin, buf, LCC_NAME_LEN);
    break;
  case TYPE_PLUGIN_INSTANCE:
    memmove(state->identifier.plugin_instance, buf, LCC_NAME_LEN);
    break;
  case TYPE_TYPE:
    memmove(state->identifier.type, buf, LCC_NAME_LEN);
    break;
  case TYPE_TYPE_INSTANCE:
    memmove(state->identifier.type_instance, buf, LCC_NAME_LEN);
    break;
  default:
    return EINVAL;
  }

  return 0;
}

static int parse_time(uint16_t type, void *payload, size_t payload_size,
                      lcc_value_list_t *state) {
  uint64_t tmp = 0;
  if (parse_int(payload, payload_size, &tmp))
    return EINVAL;

  double t = (double)tmp;
  switch (type) {
  case TYPE_INTERVAL:
    state->interval = t;
    break;
  case TYPE_INTERVAL_HR:
    state->interval = t / 1073741824.0;
    break;
  case TYPE_TIME:
    state->time = t;
    break;
  case TYPE_TIME_HR:
    state->time = t / 1073741824.0;
    break;
  default:
    return EINVAL;
  }

  return 0;
}

static double ntohd(double val) /* {{{ */
{
  static int config = 0;

  union {
    uint8_t byte[8];
    double floating;
  } in = {
      .floating = val,
  };
  union {
    uint8_t byte[8];
    double floating;
  } out = {
      .byte = {0},
  };

  if (config == 0) {
    double d = 8.642135e130;
    uint8_t b[8];

    memcpy(b, &d, sizeof(b));

    if ((b[0] == 0x2f) && (b[1] == 0x25) && (b[2] == 0xc0) && (b[3] == 0xc7) &&
        (b[4] == 0x43) && (b[5] == 0x2b) && (b[6] == 0x1f) && (b[7] == 0x5b))
      config = 1; /* need nothing */
    else if ((b[7] == 0x2f) && (b[6] == 0x25) && (b[5] == 0xc0) &&
             (b[4] == 0xc7) && (b[3] == 0x43) && (b[2] == 0x2b) &&
             (b[1] == 0x1f) && (b[0] == 0x5b))
      config = 2; /* endian flip */
    else if ((b[4] == 0x2f) && (b[5] == 0x25) && (b[6] == 0xc0) &&
             (b[7] == 0xc7) && (b[0] == 0x43) && (b[1] == 0x2b) &&
             (b[2] == 0x1f) && (b[3] == 0x5b))
      config = 3; /* int swap */
    else
      config = 4;
  }

  if (memcmp((char[]){0, 0, 0, 0, 0, 0, 0xf8, 0x7f}, in.byte, 8) == 0) {
    return NAN;
  } else if (config == 1) {
    return val;
  } else if (config == 2) {
    in.floating = val;
    out.byte[0] = in.byte[7];
    out.byte[1] = in.byte[6];
    out.byte[2] = in.byte[5];
    out.byte[3] = in.byte[4];
    out.byte[4] = in.byte[3];
    out.byte[5] = in.byte[2];
    out.byte[6] = in.byte[1];
    out.byte[7] = in.byte[0];
    return (out.floating);
  } else if (config == 3) {
    in.floating = val;
    out.byte[0] = in.byte[4];
    out.byte[1] = in.byte[5];
    out.byte[2] = in.byte[6];
    out.byte[3] = in.byte[7];
    out.byte[4] = in.byte[0];
    out.byte[5] = in.byte[1];
    out.byte[6] = in.byte[2];
    out.byte[7] = in.byte[3];
    return out.floating;
  } else {
    /* If in doubt, just copy the value back to the caller. */
    return val;
  }
} /* }}} double ntohd */

static int parse_values(void *payload, size_t payload_size,
                        lcc_value_list_t *state) {
  buffer_t *b = &(buffer_t){
      .data = payload, .len = payload_size,
  };

  uint16_t n;
  if (buffer_uint16(b, &n))
    return EINVAL;

  if (((size_t)n * 9) != b->len)
    return EINVAL;

  state->values_len = (size_t)n;
  state->values = calloc(sizeof(*state->values), state->values_len);
  state->values_types = calloc(sizeof(*state->values_types), state->values_len);
  if ((state->values == NULL) || (state->values_types == NULL)) {
    free(state->values);
    free(state->values_types);
    return ENOMEM;
  }

  for (uint16_t i = 0; i < n; i++) {
    uint8_t tmp;
    if (buffer_next(b, &tmp, sizeof(tmp)))
      return EINVAL;
    state->values_types[i] = (int)tmp;
  }

  for (uint16_t i = 0; i < n; i++) {
    uint64_t tmp;
    if (buffer_next(b, &tmp, sizeof(tmp)))
      return EINVAL;

    if (state->values_types[i] == LCC_TYPE_GAUGE) {
      union {
        uint64_t i;
        double d;
      } conv = {.i = tmp};
      state->values[i].gauge = ntohd(conv.d);
      continue;
    }

    tmp = be64toh(tmp);
    switch (state->values_types[i]) {
    case LCC_TYPE_COUNTER:
      state->values[i].counter = (counter_t)tmp;
      break;
    case LCC_TYPE_DERIVE:
      state->values[i].derive = (derive_t)tmp;
      break;
    case LCC_TYPE_ABSOLUTE:
      state->values[i].absolute = (absolute_t)tmp;
      break;
    default:
      return EINVAL;
    }
  }

  return 0;
}

int lcc_network_parse(void *data, size_t data_size, lcc_value_list_writer_t w) {
  buffer_t *b = &(buffer_t){
      .data = data, .len = data_size,
  };

  lcc_value_list_t state = {0};

  while (b->len > 0) {
    uint16_t type = 0, sz = 0;
    if (buffer_uint16(b, &type) || buffer_uint16(b, &sz)) {
      DEBUG("lcc_network_parse(): reading type and/or length failed.\n");
      return EINVAL;
    }

    if ((sz < 5) || (((size_t)sz - 4) > b->len)) {
      DEBUG("lcc_network_parse(): invalid 'sz' field: sz = %" PRIu16
            ", b->len = %zu\n",
            sz, b->len);
      return EINVAL;
    }
    sz -= 4;

    uint8_t payload[sz];
    if (buffer_next(b, payload, sizeof(payload)))
      return EINVAL;

    switch (type) {
    case TYPE_HOST:
    case TYPE_PLUGIN:
    case TYPE_PLUGIN_INSTANCE:
    case TYPE_TYPE:
    case TYPE_TYPE_INSTANCE: {
      if (parse_identifier(type, payload, sizeof(payload), &state)) {
        DEBUG("lcc_network_parse(): parse_identifier failed.\n");
        return EINVAL;
      }
      break;
    }

    case TYPE_INTERVAL:
    case TYPE_INTERVAL_HR:
    case TYPE_TIME:
    case TYPE_TIME_HR: {
      if (parse_time(type, payload, sizeof(payload), &state)) {
        DEBUG("lcc_network_parse(): parse_time failed.\n");
        return EINVAL;
      }
      break;
    }

    case TYPE_VALUES: {
      lcc_value_list_t vl = state;
      if (parse_values(payload, sizeof(payload), &vl)) {
        DEBUG("lcc_network_parse(): parse_values failed.\n");
        return EINVAL;
      }

      /* TODO(octo): skip if current_security_level < required_security_level */

      int status = w(&vl);

      free(vl.values);
      free(vl.values_types);

      if (status != 0)
        return status;
      break;
    }

    default: {
      DEBUG("lcc_network_parse(): ignoring unknown type %" PRIu16 "\n", type);
      return EINVAL;
    }
    }
  }

  return 0;
}
