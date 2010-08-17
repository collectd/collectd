/**
 * collectd - src/libcollectdclient/network.h
 * Copyright (C) 2005-2010  Florian octo Forster
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
int lcc_server_set_security_level (lcc_server_t *srv,
    lcc_security_level_t level);
int lcc_server_set_credentials (lcc_server_t *srv,
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
