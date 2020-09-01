/**
 * collectd - src/utils_format_kairosdb.h
 * Copyright (C) 2016       Aurelien Rougemont
 * Copyright (C) 2020       Florian Forster
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
 *   Aurelien beorn Rougemont <beorn at gandi dot net>
 *   Florian Forster <octo at collectd.org>
 **/

#ifndef UTILS_FORMAT_KAIROSDB_H
#define UTILS_FORMAT_KAIROSDB_H 1

#include "collectd.h"

#include "plugin.h"

typedef struct {
  bool store_rates;
  int ttl_secs;
  char const *metrics_prefix;
} format_kairosdb_opts_t;

/* format_kairosdb_metric_family adds the metric family "fam" to the buffer
 * "buf". Calling this function repeatedly with the same buffer will append
 * additional metric families to the buffer. If the buffer has fixed size and
 * the serialized metric family exceeds the buffer length, the buffer is
 * unmodified and ENOBUFS is returned. */
int format_kairosdb_metric_family(strbuf_t *buf, metric_family_t const *fam,
                                  format_kairosdb_opts_t const *opts);

#endif
