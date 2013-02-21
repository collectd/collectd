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

#include "lcc_features.h"

/*
 * Includes (for data types)
 */
#if HAVE_STDINT_H
# include <stdint.h>
#endif
#include <inttypes.h>
#include <time.h>

/*
 * Defines
 */
#define LCC_NAME_LEN 64
#define LCC_DEFAULT_PORT "25826"

/*
 * Types
 */
#define LCC_TYPE_COUNTER 0
#define LCC_TYPE_GAUGE   1
#define LCC_TYPE_DERIVE   2
#define LCC_TYPE_ABSOLUTE   3

LCC_BEGIN_DECLS

typedef uint64_t counter_t;
typedef double gauge_t;
typedef uint64_t derive_t;
typedef uint64_t absolute_t;

union value_u
{
  counter_t counter;
  gauge_t   gauge;
  derive_t  derive;
  absolute_t absolute;
};
typedef union value_u value_t;

struct lcc_identifier_s
{
  char host[LCC_NAME_LEN];
  char plugin[LCC_NAME_LEN];
  char plugin_instance[LCC_NAME_LEN];
  char type[LCC_NAME_LEN];
  char type_instance[LCC_NAME_LEN];
};
typedef struct lcc_identifier_s lcc_identifier_t;
#define LCC_IDENTIFIER_INIT { "localhost", "", "", "", "" }

struct lcc_value_list_s
{
  value_t *values;
  int     *values_types;
  size_t   values_len;
  double   time;
  double   interval;
  lcc_identifier_t identifier;
};
typedef struct lcc_value_list_s lcc_value_list_t;
#define LCC_VALUE_LIST_INIT { NULL, NULL, 0, 0, 0, LCC_IDENTIFIER_INIT }

struct lcc_connection_s;
typedef struct lcc_connection_s lcc_connection_t;

/*
 * Functions
 */
int lcc_connect (const char *address, lcc_connection_t **ret_con);
int lcc_disconnect (lcc_connection_t *c);
#define LCC_DESTROY(c) do { lcc_disconnect (c); (c) = NULL; } while (0)

int lcc_getval (lcc_connection_t *c, lcc_identifier_t *ident,
    size_t *ret_values_num, gauge_t **ret_values, char ***ret_values_names);

int lcc_putval (lcc_connection_t *c, const lcc_value_list_t *vl);

int lcc_flush (lcc_connection_t *c, const char *plugin,
    lcc_identifier_t *ident, int timeout);

int lcc_listval (lcc_connection_t *c,
    lcc_identifier_t **ret_ident, size_t *ret_ident_num);

/* TODO: putnotif */

const char *lcc_strerror (lcc_connection_t *c);

int lcc_identifier_to_string (lcc_connection_t *c,
    char *string, size_t string_size, const lcc_identifier_t *ident);
int lcc_string_to_identifier (lcc_connection_t *c,
    lcc_identifier_t *ident, const char *string);

/* Compares the identifiers "i0" and "i1" and returns less than zero or greater
 * than zero if "i0" is smaller than or greater than "i1", respectively. If
 * "i0" and "i1" are identical, zero is returned. */
int lcc_identifier_compare (const lcc_identifier_t *i0,
    const lcc_identifier_t *i1);
int lcc_sort_identifiers (lcc_connection_t *c,
    lcc_identifier_t *idents, size_t idents_num);

LCC_END_DECLS

/* vim: set sw=2 sts=2 et : */
#endif /* LIBCOLLECTD_COLLECTDCLIENT_H */
