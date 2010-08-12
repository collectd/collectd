/**
 * collectd - src/libcollectdclient/network_buffer.h
 * Copyright (C) 2010  Florian octo Forster
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

#ifndef LIBCOLLECTDCLIENT_NETWORK_BUFFER_H
#define LIBCOLLECTDCLIENT_NETWORK_BUFFER_H 1

/* FIXME */
#include "client.h"
#include "network.h"

/* Ethernet frame - (IPv6 header + UDP header) */
#define LCC_NETWORK_BUFFER_SIZE_DEFAULT 1452

struct lcc_network_buffer_s;
typedef struct lcc_network_buffer_s lcc_network_buffer_t;

lcc_network_buffer_t *lcc_network_buffer_create (size_t size);
void lcc_network_buffer_destroy (lcc_network_buffer_t *nb);

int lcc_network_buffer_set_security_level (lcc_network_buffer_t *nb,
    lcc_security_level_t level,
    const char *user, const char *password);

int lcc_network_buffer_initialize (lcc_network_buffer_t *nb);
int lcc_network_buffer_finalize (lcc_network_buffer_t *nb);

int lcc_network_buffer_add_value (lcc_network_buffer_t *nb,
    const lcc_value_list_t *vl);

int lcc_network_buffer_get (lcc_network_buffer_t *nb,
    void *buffer, size_t *buffer_size);

#endif /* LIBCOLLECTDCLIENT_NETWORK_BUFFER_H */
/* vim: set sw=2 sts=2 et : */
