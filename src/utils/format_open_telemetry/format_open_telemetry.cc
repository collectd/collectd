/**
 * collectd - src/utils_format_open_telemetry.c
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

extern "C" {
#include "collectd.h"
#include "metric.h"

#include "utils/format_open_telemetry/format_open_telemetry.h"
}

#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

using opentelemetry::proto::common::v1::AnyValue;
using opentelemetry::proto::common::v1::InstrumentationScope;
using opentelemetry::proto::common::v1::KeyValue;
using opentelemetry::proto::metrics::v1::AGGREGATION_TEMPORALITY_CUMULATIVE;
using opentelemetry::proto::metrics::v1::Gauge;
using opentelemetry::proto::metrics::v1::Metric;
using opentelemetry::proto::metrics::v1::NumberDataPoint;
using opentelemetry::proto::metrics::v1::ResourceMetrics;
using opentelemetry::proto::metrics::v1::ScopeMetrics;
using opentelemetry::proto::metrics::v1::Sum;

static void metric_to_number_data_point(NumberDataPoint *dp, metric_t const *m) {
  for (size_t i = 0; i < m->label.num; i++) {
    label_pair_t *l = m->label.ptr + i;

    KeyValue *kv = dp->add_attributes();
    kv->set_key(l->name);
    AnyValue *v = kv->mutable_value();
    v->set_string_value(l->value);
  }

  dp->set_time_unix_nano(CDTIME_T_TO_NS(m->time));
  // TODO(): also set "start time". We may need to use the cache to determine
  // when we've seen a metric for the first time.

  switch (m->family->type) {
    case METRIC_TYPE_COUNTER:
      dp->set_as_int(m->value.derive);
    case METRIC_TYPE_GAUGE:
      dp->set_as_double(m->value.gauge);
    case METRIC_TYPE_UNTYPED:
      // TODO
      assert(0);
  }
}

static void set_sum(Metric *m, metric_family_t const *fam) {
  Sum *s = m->mutable_sum();
  for (size_t i = 0; i < fam->metric.num; i++) {
    NumberDataPoint *dp = s->add_data_points();
    metric_to_number_data_point(dp, fam->metric.ptr + i);
  }

  s->set_aggregation_temporality(AGGREGATION_TEMPORALITY_CUMULATIVE);
  s->set_is_monotonic(true);
}

static void set_gauge(Metric *m, metric_family_t const *fam) {
  Gauge *g = m->mutable_gauge();
  for (size_t i = 0; i < fam->metric.num; i++) {
    NumberDataPoint *dp = g->add_data_points();
    metric_to_number_data_point(dp, fam->metric.ptr + i);
  }
}

static void add_metric(ScopeMetrics *sm, metric_family_t const *fam) {
  Metric *m = sm->add_metrics();

  m->set_name(fam->name);
  if (fam->help != NULL) {
    m->set_description(fam->help);
  }

  switch (fam->type) {
    case METRIC_TYPE_COUNTER:
      set_sum(m, fam);
      return;
    case METRIC_TYPE_GAUGE:
      set_gauge(m, fam);
      return;
    case METRIC_TYPE_UNTYPED:
      // TODO
      assert(0);
  }
}

static void set_instrumentation_scope(ScopeMetrics *sm) {
  InstrumentationScope *is = sm->mutable_scope();
  is->set_name(PACKAGE_NAME);
  is->set_version(PACKAGE_VERSION);
}

static void set_scope_metrics(ResourceMetrics *rm, metric_family_t const *fam) {
  ScopeMetrics *sm = rm->add_scope_metrics();

  set_instrumentation_scope(sm);

  add_metric(sm, fam);
}

int format_open_telemetry(strbuf_t *sb, metric_family_t const *fam) {
  ResourceMetrics rm;

  set_scope_metrics(&rm, fam);

  std::string serialization;
  bool ok = rm.SerializeToString(&serialization);
  if (!ok) {
    return -1;
  }

  strbuf_print(sb, serialization.c_str());
  return 0;
}

