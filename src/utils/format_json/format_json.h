/**
 * collectd - src/utils_format_json.h
 * Copyright (C) 2009-2020  Florian octo Forster
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

#ifndef UTILS_FORMAT_JSON_H
#define UTILS_FORMAT_JSON_H 1

#include "collectd.h"

#include "plugin.h"
#include "utils/strbuf/strbuf.h"

#ifndef JSON_GAUGE_FORMAT
#define JSON_GAUGE_FORMAT GAUGE_FORMAT
#endif

/* format_json_metric_family adds the metric family "fam" to the buffer "buf"
 * in JSON format. The format produced is compatible to the
 * "prometheus/prom2json" project. Calling this function repeatedly with the
 * same buffer will append additional metric families to the buffer. If the
 * buffer has fixed size and the serialized metric family exceeds the buffer
 * length, the buffer is unmodified and ENOBUFS is returned. */
int format_json_metric_family(strbuf_t *buf, metric_family_t const *fam,
                              bool store_rates);

int format_json_notification(char *buffer, size_t buffer_size,
                             notification_t const *n);

#endif /* UTILS_FORMAT_JSON_H */
