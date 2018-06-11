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

#ifndef LIBCOLLECTD_SERVER_H
#define LIBCOLLECTD_SERVER_H 1

#include "collectd/lcc_features.h"

#include "collectd/network.h"       /* for lcc_security_level_t */
#include "collectd/network_parse.h" /* for lcc_network_parse_options_t */
#include "collectd/types.h"

#include <stdint.h>

#ifndef LCC_NETWORK_BUFFER_SIZE
#define LCC_NETWORK_BUFFER_SIZE 1452
#endif

LCC_BEGIN_DECLS

/* lcc_network_parser_t is a callback that parses received network packets. It
 * is expected to call lcc_network_parse_options_t.writer with each
 * lcc_value_list_t it parses that has the required security level. */
typedef int (*lcc_network_parser_t)(void *payload, size_t payload_size,
                                    lcc_network_parse_options_t opts);

/* lcc_listener_t holds parameters for running a collectd server. */
typedef struct {
  /* conn is a UDP socket for the server to listen on. If set to <0 node and
   * service will be used to open a new UDP socket. If >=0, it is the caller's
   * job to clean up the socket. */
  int conn;

  /* node is the local address to listen on if conn is <0. Defaults to "::" (any
   * address). */
  char *node;

  /* service is the local address to listen on if conn is <0. Defaults to
   * LCC_DEFAULT_PORT. */
  char *service;

  /* parser is the callback used to parse incoming network packets. Defaults to
   * lcc_network_parse() if set to NULL. */
  lcc_network_parser_t parser;

  /* parse_options contains options for parser and is passed on verbatimely. */
  lcc_network_parse_options_t parse_options;

  /* buffer_size determines the maximum packet size to accept. Defaults to
   * LCC_NETWORK_BUFFER_SIZE if set to zero. */
  uint16_t buffer_size;

  /* interface is the name of the interface to use when subscribing to a
   * multicast group. Has no effect when using unicast. */
  char *iface;
} lcc_listener_t;

/* lcc_listen_and_write listens on the provided UDP socket (or opens one using
 * srv.addr if srv.conn is less than zero), parses the received packets and
 * writes them to the provided lcc_value_list_writer_t. Returns non-zero on
 * failure and does not return otherwise. */
int lcc_listen_and_write(lcc_listener_t srv);

LCC_END_DECLS

#endif /* LIBCOLLECTD_SERVER_H */
