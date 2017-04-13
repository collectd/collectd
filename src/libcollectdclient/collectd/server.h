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

#include "collectd/types.h"
#include "collectd/network.h" /* for lcc_security_level_t */

#include <stdint.h>

LCC_BEGIN_DECLS

/* lcc_value_list_writer_t is a write callback to which value lists are
 * dispatched. */
typedef int (*lcc_value_list_writer_t)(lcc_value_list_t const *);

/* lcc_password_lookup_t is a callback for looking up the password for a given
 * user. Must return NULL if the user is not known. */
typedef char const *(*lcc_password_lookup_t)(char const *);

/* lcc_listener_t holds parameters for running a collectd server. */
typedef struct {
  /* conn is a UDP socket for the server to listen on. */
  int conn;

  /* node is the local address to listen on if conn is <0. Defaults to "::" (any
   * address). */
  char *node;

  /* service is the local address to listen on if conn is <0. Defaults to
   * LCC_DEFAULT_PORT. */
  char *service;

  /* writer is the callback used to send incoming lcc_value_list_t to. */
  lcc_value_list_writer_t writer;

  /* buffer_size determines the maximum packet size to accept. */
  uint16_t buffer_size;

  /* password_lookup is used to look up the password for a given username. */
  lcc_password_lookup_t password_lookup;

  /* security_level is the minimal required security level. */
  lcc_security_level_t security_level;

  /* interface is the name of the interface to use when subscribing to a
   * multicast group. Has no effect when using unicast. */
  char *interface;
} lcc_listener_t;

/* lcc_listen_and_write listens on the provided UDP socket (or opens one using
 * srv.addr if srv.conn is less than zero), parses the received packets and
 * writes them to the provided lcc_value_list_writer_t. Returns non-zero on
 * failure and does not return otherwise. */
int lcc_listen_and_write(lcc_listener_t srv);

typedef struct {
  /* writer is the callback used to send incoming lcc_value_list_t to. */
  lcc_value_list_writer_t writer;

  /* password_lookup is used to look up the password for a given username. */
  lcc_password_lookup_t password_lookup;

  /* security_level is the minimal required security level. */
  lcc_security_level_t security_level;
} lcc_network_parse_options_t;

/* lcc_network_parse parses data received from the network and calls "w" with
 * the parsed lcc_value_list_ts. */
/* TODO(octo): the Go code returns a []api.ValueList. Should we return a
 * value_list_t** here? */
int lcc_network_parse(void *buffer, size_t buffer_size,
                      lcc_network_parse_options_t opts);

LCC_END_DECLS

#endif /* LIBCOLLECTD_SERVER_H */
