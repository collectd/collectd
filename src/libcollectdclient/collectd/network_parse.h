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

#ifndef LIBCOLLECTD_NETWORK_PARSE_H
#define LIBCOLLECTD_NETWORK_PARSE_H 1

#include "collectd/lcc_features.h"

#include "collectd/network.h" /* for lcc_security_level_t */
#include "collectd/types.h"

#include <stdint.h>

LCC_BEGIN_DECLS

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
int lcc_network_parse(void *buffer, size_t buffer_size,
                      lcc_network_parse_options_t opts);

LCC_END_DECLS

#endif /* LIBCOLLECTD_NETWORK_PARSE_H */
