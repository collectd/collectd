/**
 * collectd - src/utils_format_gcm.h
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

#ifndef UTILS_FORMAT_GCM_H
#define UTILS_FORMAT_GCM_H 1

#include "collectd.h"
#include "plugin.h"

/* gcm_output_t is a buffer to which value_list_t* can be added and from which
 * an appropriately formatted char* can be read. */
struct gcm_output_s;
typedef struct gcm_output_s gcm_output_t;

/* gcm_resource_t represents a MonitoredResource. */
struct gcm_resource_s;
typedef struct gcm_resource_s gcm_resource_t;

gcm_output_t *gcm_output_create(gcm_resource_t *res);

/* gcm_output_destroy frees all memory used by out, including the
 * gcm_resource_t* passed to gcm_output_create. */
void gcm_output_destroy(gcm_output_t *out);

/* gcm_output_add adds a value_list_t* to "out".
 *
 * Return values:
 *   - 0        Success
 *   - ENOBUFS  Success, but the buffer should be flushed soon.
 *   - EEXIST   The value list is already encoded in the buffer.
 *              Flush the buffer, then call gcm_output_add again.
 *   - ENOENT   First time we encounter this metric. Create a metric descriptor
 *              using the GCM API and then call gcm_output_register_metric.
 */
int gcm_output_add(gcm_output_t *out, data_set_t const *ds,
                   value_list_t const *vl);

/* gcm_output_register_metric adds the metric descriptor which vl maps to, to
 * the list of known metric descriptors. */
int gcm_output_register_metric(gcm_output_t *out, data_set_t const *ds,
                               value_list_t const *vl);

/* gcm_output_reset resets the output and returns the previous content of the
 * buffer. It is the caller's responsibility to call free() with the returned
 * pointer. */
char *gcm_output_reset(gcm_output_t *out);

gcm_resource_t *gcm_resource_create(char const *type);
void gcm_resource_destroy(gcm_resource_t *res);
int gcm_resource_add_label(gcm_resource_t *res, char const *key,
                           char const *value);

/* gcm_format_metric_descriptor creates the payload for a
 * projects.metricDescriptors.create() request. */
int gcm_format_metric_descriptor(char *buffer, size_t buffer_size,
                                 data_set_t const *ds, value_list_t const *vl,
                                 int ds_index);

#endif /* UTILS_FORMAT_GCM_H */
