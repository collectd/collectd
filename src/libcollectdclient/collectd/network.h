/**
 * collectd - src/libcollectdclient/collectd/network.h
 * Copyright (C) 2005-2012  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#ifndef LIBCOLLECTDCLIENT_NETWORK_H
#define LIBCOLLECTDCLIENT_NETWORK_H 1

#include <stdint.h>
#include <inttypes.h>

#include "client.h"

#define NET_DEFAULT_V4_ADDR "239.192.74.66"
#define NET_DEFAULT_V6_ADDR "ff18::efc0:4a42"
#define NET_DEFAULT_PORT    "25826"

struct lcc_network_s;
typedef struct lcc_network_s lcc_network_t;

struct lcc_server_s;
typedef struct lcc_server_s lcc_server_t;

enum lcc_security_level_e
{
  NONE,
  SIGN,
  ENCRYPT
};
typedef enum lcc_security_level_e lcc_security_level_t;

/*
 * Create / destroy object
 */
lcc_network_t *lcc_network_create (void);
void lcc_network_destroy (lcc_network_t *net);

/* 
 * Add servers
 */
lcc_server_t *lcc_server_create (lcc_network_t *net,
    const char *node, const char *service);
int lcc_server_destroy (lcc_network_t *net, lcc_server_t *srv);

/* Configure servers */
int lcc_server_set_ttl (lcc_server_t *srv, uint8_t ttl);
int lcc_server_set_interface (lcc_server_t *srv, char const *interface);
int lcc_server_set_security_level (lcc_server_t *srv,
    lcc_security_level_t level,
    const char *username, const char *password);

/*
 * Send data
 */
int lcc_network_values_send (lcc_network_t *net,
    const lcc_value_list_t *vl);
#if 0
int lcc_network_notification_send (lcc_network_t *net,
    const lcc_notification_t *notif);
#endif

/* vim: set sw=2 sts=2 et : */
#endif /* LIBCOLLECTDCLIENT_NETWORK_H */
