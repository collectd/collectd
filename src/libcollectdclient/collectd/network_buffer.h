/**
 * collectd - src/libcollectdclient/collectd/network_buffer.h
 * Copyright (C) 2010-2012  Florian octo Forster
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
