/**
 * libcollectdclient - src/libcollectdclient/collectd/client.h
 * Copyright (C) 2008-2012  Florian octo Forster
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

#ifndef LIBCOLLECTD_COLLECTDCLIENT_H
#define LIBCOLLECTD_COLLECTDCLIENT_H 1

#include "collectd/lcc_features.h"
#include "collectd/types.h"

/* COLLECTD_TRACE is the environment variable used to control trace output. When
 * set to something non-zero, all lines sent to / received from the daemon are
 * printed to STDOUT. */
#ifndef LCC_TRACE_ENV
#define LCC_TRACE_ENV "COLLECTD_TRACE"
#endif

/*
 * Includes (for data types)
 */
#include <inttypes.h>
#include <stdint.h>
#include <time.h>

LCC_BEGIN_DECLS

struct lcc_connection_s;
typedef struct lcc_connection_s lcc_connection_t;

/*
 * Functions
 */
int lcc_connect(const char *address, lcc_connection_t **ret_con);
int lcc_disconnect(lcc_connection_t *c);
#define LCC_DESTROY(c)                                                         \
  do {                                                                         \
    lcc_disconnect(c);                                                         \
    (c) = NULL;                                                                \
  } while (0)

int lcc_getval(lcc_connection_t *c, lcc_identifier_t *ident,
               size_t *ret_values_num, gauge_t **ret_values,
               char ***ret_values_names);

int lcc_putval(lcc_connection_t *c, const lcc_value_list_t *vl);

int lcc_flush(lcc_connection_t *c, const char *plugin, lcc_identifier_t *ident,
              int timeout);

int lcc_listval(lcc_connection_t *c, lcc_identifier_t **ret_ident,
                size_t *ret_ident_num);

/* TODO: putnotif */

const char *lcc_strerror(lcc_connection_t *c);

int lcc_identifier_to_string(lcc_connection_t *c, char *string,
                             size_t string_size, const lcc_identifier_t *ident);
int lcc_string_to_identifier(lcc_connection_t *c, lcc_identifier_t *ident,
                             const char *string);

/* Compares the identifiers "i0" and "i1" and returns less than zero or greater
 * than zero if "i0" is smaller than or greater than "i1", respectively. If
 * "i0" and "i1" are identical, zero is returned. */
int lcc_identifier_compare(const void *i0, const void *i1);
int lcc_sort_identifiers(lcc_connection_t *c, lcc_identifier_t *idents,
                         size_t idents_num);

LCC_END_DECLS

#endif /* LIBCOLLECTD_COLLECTDCLIENT_H */
