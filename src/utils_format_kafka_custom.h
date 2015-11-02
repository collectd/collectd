/**
 * collectd - src/utils_format_kafka_custom.h
 * Copyright (C) 2015       Yves Mettier
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
 *   Yves Mettier <ymettier at free dot fr>
 **/

#ifndef UTILS_FORMAT_KAFKA_CUSTOM_H
#define UTILS_FORMAT_KAFKA_CUSTOM_H 1

#include "collectd.h"
#include "plugin.h"

/*   DOCUMENTATION
 * --=============--
 *
 * OK, you want to implement your own custom format to send
 * data to kafka. You can do that in utils_format_kafka_custom.c
 *
 * 1/ Do not edit utils_format_kafka_custom.h
 * 2/ Do not edit write_kafka.c
 *
 * How do you write your custom code ?
 * -----------------------------------
 *
 * 1/ Define IMPLEMENT_KAFKA_FORMAT_CUSTOM_1 at compilation time :
 *
 * ./configure CPPFLAGS=-DIMPLEMENT_KAFKA_FORMAT_CUSTOM_1
 * make
 * [...]
 *
 *
 * 2/ Implement the 3 fonctions defined below.
 * Reading utils_format_json.c can help you a lot.
 *
 * 3/ Rebuild Collectd (with IMPLEMENT_KAFKA_FORMAT_CUSTOM_1 defined)
 *
 * 4/ Edit your configuration file and set :
 *   <Topic "your-topic-name">
 *       Format CUSTOM1
 *   </Topic>
 *
 * 5/ Start Collectd, enjoy...
 *
 *
 * Functions description
 * ---------------------
 * When a value is received from write_kafka, it will
 * 1/ initialize a buffer
 * 2/ call format_kafka_custom_initialize()
 * 3/ call format_kafka_custom_value_list() one time (and maybe multiple times in the future)
 * 4/ call format_kafka_custom_finalize()
 * 5/ send the buffer to Kafka
 *
 * Note that last argument (named format) is set to KAFKA_FORMAT_CUSTOM_1 for now.
 * If one day write_kafka.c allows more than one custom format, it should be easy to
 * use the same callbacks and dispatch to other functions according to the format.
 */

/*
#define IMPLEMENT_KAFKA_FORMAT_CUSTOM_1
*/


#ifdef IMPLEMENT_KAFKA_FORMAT_CUSTOM_1

#ifndef KAFKA_FORMAT_CUSTOM_1
#define KAFKA_FORMAT_CUSTOM_1    3


int format_kafka_custom_initialize (char *buffer,
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    uint8_t format);
int format_kafka_custom_value_list (char *buffer,
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    const data_set_t *ds, const value_list_t *vl, int store_rates,
    uint8_t format);
int format_kafka_custom_finalize (char *buffer,
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    uint8_t format);
#endif
#endif

#endif /* UTILS_FORMAT_KAFKA_CUSTOM_H */


