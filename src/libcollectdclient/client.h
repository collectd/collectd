/**
 * libcollectdclient - src/libcollectdclient/client.h
 * Copyright (C) 2008  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
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
  time_t   time;
  int      interval;
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

int lcc_sort_identifiers (lcc_connection_t *c,
    lcc_identifier_t *idents, size_t idents_num);

LCC_END_DECLS

/* vim: set sw=2 sts=2 et : */
#endif /* LIBCOLLECTD_COLLECTDCLIENT_H */
