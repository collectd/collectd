/**
 * collectd - src/utils_format_stackdriver.h
 * ISC license
 *
 * Copyright (C) 2017  Florian Forster
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
 **/

#ifndef UTILS_FORMAT_STACKDRIVER_H
#define UTILS_FORMAT_STACKDRIVER_H 1

#include "collectd.h"
#include "plugin.h"

/* sd_output_t is a buffer to which value_list_t* can be added and from which
 * an appropriately formatted char* can be read. */
struct sd_output_s;
typedef struct sd_output_s sd_output_t;

/* sd_resource_t represents a MonitoredResource. */
struct sd_resource_s;
typedef struct sd_resource_s sd_resource_t;

sd_output_t *sd_output_create(sd_resource_t *res);

/* sd_output_destroy frees all memory used by out, including the
 * sd_resource_t* passed to sd_output_create. */
void sd_output_destroy(sd_output_t *out);

/* sd_output_add adds a value_list_t* to "out".
 *
 * Return values:
 *   - 0        Success
 *   - ENOBUFS  Success, but the buffer should be flushed soon.
 *   - EEXIST   The value list is already encoded in the buffer.
 *              Flush the buffer, then call sd_output_add again.
 *   - ENOENT   First time we encounter this metric. Create a metric descriptor
 *              using the Stackdriver API and then call
 *              sd_output_register_metric.
 */
int sd_output_add(sd_output_t *out, data_set_t const *ds,
                  value_list_t const *vl);

/* sd_output_register_metric adds the metric descriptor which vl maps to, to
 * the list of known metric descriptors. */
int sd_output_register_metric(sd_output_t *out, data_set_t const *ds,
                              value_list_t const *vl);

/* sd_output_reset resets the output and returns the previous content of the
 * buffer. It is the caller's responsibility to call free() with the returned
 * pointer. */
char *sd_output_reset(sd_output_t *out);

sd_resource_t *sd_resource_create(char const *type);
void sd_resource_destroy(sd_resource_t *res);
int sd_resource_add_label(sd_resource_t *res, char const *key,
                          char const *value);

/* sd_format_metric_descriptor creates the payload for a
 * projects.metricDescriptors.create() request. */
int sd_format_metric_descriptor(char *buffer, size_t buffer_size,
                                data_set_t const *ds, value_list_t const *vl,
                                int ds_index);

#endif /* UTILS_FORMAT_STACKDRIVER_H */
