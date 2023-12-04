/**
 * collectd - src/utils/format_open_telemetry/format_open_telemetry.h
 * Copyright (C) 2023       Florian octo Forster
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

#ifndef UTILS_FORMAT_OPEN_TELEMETRY_H
#define UTILS_FORMAT_OPEN_TELEMETRY_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include "collectd.h"
#include "metric.h"

int format_open_telemetry_resource_metrics_serialized(
    strbuf_t *sb, metric_family_t const **fam, size_t fam_num);

#ifdef __cplusplus
}

#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"

opentelemetry::proto::metrics::v1::ResourceMetrics *
format_open_telemetry_resource_metrics_serialized(metric_family_t const **fam,
                                                  size_t fam_num);

opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest *
format_open_telemetry_export_metrics_service_request(
    metric_family_t const **fam, size_t fam_num);
#endif

#endif /* UTILS_FORMAT_OPEN_TELEMETRY_H */
