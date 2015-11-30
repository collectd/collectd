/**
 * collectd - src/utils_format_kafka_custom.c
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
 *   Yves Mettier <ymettier at free dot fr>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#include "utils_cache.h"
#include "utils_format_kafka_custom.h"

/*
 * IMPORTANT : see utils_format_kafka_custom.h for more
 * information on how to implement custom format.
 */

/*
 * Do not set KAFKA_FORMAT_CUSTOM_1 here.
 */
#ifdef KAFKA_FORMAT_CUSTOM_1

int format_kafka_custom_initialize (char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    uint8_t format)
{
/*
 * Implement your own format_kafka_custom_initialize() function here.
 */

  return (0);
} /* }}} int format_kafka_custom_initialize */

int format_kafka_custom_finalize (char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    uint8_t format)
{
/*
 * Implement your own format_kafka_custom_finalize() function here.
 */

  return (0);
} /* }}} int format_kafka_custom_finalize */

int format_kafka_custom_value_list (char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    const data_set_t *ds, const value_list_t *vl,
    int store_rates, uint8_t format)
{

  return (0);
} /* }}} int format_kafka_custom_value_list */

#endif

/* vim: set sw=2 sts=2 et fdm=marker : */
