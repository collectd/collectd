/**
 * collectd - src/utils_format_json.h
 * Copyright (C) 2009       Florian octo Forster
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

#ifndef JSON_GAUGE_FORMAT
# define JSON_GAUGE_FORMAT GAUGE_FORMAT
#endif

int format_json_initialize (char *buffer,
    size_t *ret_buffer_fill, size_t *ret_buffer_free);
int format_json_value_list (char *buffer,
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    const data_set_t *ds, const value_list_t *vl, int store_rates);
int format_json_finalize (char *buffer,
    size_t *ret_buffer_fill, size_t *ret_buffer_free);

#endif /* UTILS_FORMAT_JSON_H */
