/**
 * libcollectdclient - src/libcollectdclient/collectd/types.h
 * Copyright (C) 2008-2017  Florian octo Forster
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

#ifndef LIBCOLLECTD_COLLECTD_TYPES_H
#define LIBCOLLECTD_COLLECTD_TYPES_H 1

#include "collectd/lcc_features.h"

#include <stdint.h>    /* for uint64_t */
#include <sys/types.h> /* for size_t */

/*
 * Defines
 */
#define LCC_NAME_LEN 64
#define LCC_DEFAULT_PORT "25826"

/*
 * Types
 */
#define LCC_TYPE_COUNTER 0
#define LCC_TYPE_GAUGE 1
#define LCC_TYPE_DERIVE 2
#define LCC_TYPE_ABSOLUTE 3

LCC_BEGIN_DECLS

typedef uint64_t counter_t;
typedef double gauge_t;
typedef uint64_t derive_t;
typedef uint64_t absolute_t;

union value_u {
  counter_t counter;
  gauge_t gauge;
  derive_t derive;
  absolute_t absolute;
};
typedef union value_u value_t;

struct lcc_identifier_s {
  char host[LCC_NAME_LEN];
  char plugin[LCC_NAME_LEN];
  char plugin_instance[LCC_NAME_LEN];
  char type[LCC_NAME_LEN];
  char type_instance[LCC_NAME_LEN];
};
typedef struct lcc_identifier_s lcc_identifier_t;
#define LCC_IDENTIFIER_INIT                                                    \
  { "localhost", "", "", "", "" }

struct lcc_value_list_s {
  value_t *values;
  int *values_types;
  size_t values_len;
  double time;
  double interval;
  lcc_identifier_t identifier;
};
typedef struct lcc_value_list_s lcc_value_list_t;
#define LCC_VALUE_LIST_INIT                                                    \
  { NULL, NULL, 0, 0, 0, LCC_IDENTIFIER_INIT }

/* lcc_value_list_writer_t is a write callback to which value lists are
 * dispatched. */
typedef int (*lcc_value_list_writer_t)(lcc_value_list_t const *);

/* lcc_password_lookup_t is a callback for looking up the password for a given
 * user. Must return NULL if the user is not known. */
typedef char const *(*lcc_password_lookup_t)(char const *);

LCC_END_DECLS

#endif /* LIBCOLLECTD_COLLECTD_TYPES_H */
