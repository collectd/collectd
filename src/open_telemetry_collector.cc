/**
 * collectd - src/grpc.cc
 * Copyright (C) 2015-2016 Sebastian Harl
 * Copyright (C) 2016-2024 Florian octo Forster
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
 *   Sebastian Harl <sh at tokkee.org>
 *   Florian octo Forster <octo at collectd.org>
 **/

extern "C" {
#include "daemon/collectd.h"
#include "daemon/metric.h"
#include "daemon/utils_cache.h"
#include "utils/common/common.h"
}

#include <fstream>
#include <vector>

#include <grpc++/grpc++.h>

#include "opentelemetry/proto/collector/metrics/v1/metrics_service.grpc.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

using opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using opentelemetry::proto::collector::metrics::v1::
    ExportMetricsServiceResponse;
using opentelemetry::proto::collector::metrics::v1::MetricsService;
using opentelemetry::proto::common::v1::AnyValue;
using opentelemetry::proto::common::v1::KeyValue;
using opentelemetry::proto::metrics::v1::Gauge;
using opentelemetry::proto::metrics::v1::Metric;
using opentelemetry::proto::metrics::v1::NumberDataPoint;
using opentelemetry::proto::metrics::v1::ResourceMetrics;
using opentelemetry::proto::metrics::v1::Sum;
using opentelemetry::proto::resource::v1::Resource;

/*
 * private types
 */

struct Listener {
  grpc::string addr;
  grpc::string port;

  grpc::SslServerCredentialsOptions *ssl;
};
static std::vector<Listener> listeners;
static grpc::string default_addr("0.0.0.0:50051");

/*
 * helper functions
 */
static grpc::string read_file(const char *filename) {
  std::ifstream f;
  grpc::string s, content;

  f.open(filename);
  if (!f.is_open()) {
    ERROR("open_telemetry_collector: Failed to open '%s'", filename);
    return "";
  }

  while (std::getline(f, s)) {
    content += s;
    content.push_back('\n');
  }
  f.close();
  return content;
} /* read_file */

/*
 * proto conversion
 */
static grpc::Status wrap_error(int err) {
  if (!err) {
    return grpc::Status::OK;
  }
  return grpc::Status(grpc::StatusCode::INTERNAL, "wrapped internal error");
}

static grpc::Status unmarshal_label_pair(KeyValue kv, label_set_t *labels) {
  switch (kv.value().value_case()) {
  case AnyValue::kStringValue:
    return wrap_error(label_set_add(labels, kv.key().c_str(),
                                    kv.value().string_value().c_str()));
  case AnyValue::kBoolValue:
    return wrap_error(label_set_add(
        labels, kv.key().c_str(), kv.value().bool_value() ? "true" : "false"));
  case AnyValue::kIntValue: {
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "%" PRId64, kv.value().int_value());
    return wrap_error(label_set_add(labels, kv.key().c_str(), buf));
  }
  case AnyValue::kDoubleValue: {
    char buf[64] = {0};
    snprintf(buf, sizeof(buf), GAUGE_FORMAT, kv.value().double_value());
    return wrap_error(label_set_add(labels, kv.key().c_str(), buf));
  }
  case AnyValue::kArrayValue:
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                        "array labels are not supported");
  case AnyValue::kKvlistValue:
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                        "key/value list labels are not supported");
  case AnyValue::kBytesValue:
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                        "byte labels are not supported");
  default:
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "unexpected label value type");
  }
}

static grpc::Status unmarshal_data_point(NumberDataPoint dp,
                                         metric_family_t *fam,
                                         bool is_cumulative) {
  metric_t m = {
      .time = NS_TO_CDTIME_T(dp.time_unix_nano()),
  };

  switch (dp.value_case()) {
  case NumberDataPoint::kAsDouble:
// TODO(octo): enable once floating point counters have been merged (#4266)
#if 0
    if (is_cumulative) {
      fam->type = METRIC_TYPE_FPCOUNTER;
      m.value.fpcounter = dp.as_double();
      break;
    }
#endif
    m.value.gauge = dp.as_double();
    break;
  case NumberDataPoint::kAsInt:
    if (is_cumulative) {
      fam->type = METRIC_TYPE_COUNTER;
      m.value.counter = (counter_t)dp.as_int();
      break;
    }
    m.value.gauge = (gauge_t)dp.as_int();
    break;
  default:
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "unexpected data point value type");
  }

  for (auto kv : dp.attributes()) {
    grpc::Status s = unmarshal_label_pair(kv, &m.label);
    if (!s.ok()) {
      metric_reset(&m);
      return s;
    }
  }

  // TODO(octo): get the first metric time from the cache and detect counter
  // resets.
  // TODO(octo): when aggregation temporality == AGGREGATION_TEMPORALITY_DELTA,
  // get the previous value from the cache and add to it.

  int err = metric_family_metric_append(fam, m);

  metric_reset(&m);
  return wrap_error(err);
}

static grpc::Status unmarshal_gauge_metric(Gauge g, metric_family_t *fam) {
  for (auto db : g.data_points()) {
    grpc::Status s = unmarshal_data_point(db, fam, false);
    if (!s.ok()) {
      return s;
    }
  }

  return grpc::Status::OK;
}

static grpc::Status unmarshal_sum_metric(Sum s, metric_family_t *fam) {
  // TODO(octo): check is_monotonic and aggregation temporality
  for (auto db : s.data_points()) {
    grpc::Status s = unmarshal_data_point(db, fam, true);
    if (!s.ok()) {
      return s;
    }
  }

  return grpc::Status::OK;
}

static grpc::Status dispatch_metric(Metric mpb, label_set_t resource) {
  metric_family_t fam = {
      .name = (char *)mpb.name().c_str(),
      .help = (char *)mpb.description().c_str(),
      .unit = (char *)mpb.unit().c_str(),
      .resource = resource,
  };

  switch (mpb.data_case()) {
  case Metric::kGauge: {
    fam.type = METRIC_TYPE_GAUGE;
    grpc::Status s = unmarshal_gauge_metric(mpb.gauge(), &fam);
    if (!s.ok()) {
      metric_family_metric_reset(&fam);
      return s;
    }
    break;
  }
  case Metric::kSum: {
    grpc::Status s = unmarshal_sum_metric(mpb.sum(), &fam);
    if (!s.ok()) {
      metric_family_metric_reset(&fam);
      return s;
    }
    break;
  }
  case Metric::kHistogram:
  case Metric::kExponentialHistogram:
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                        "histogram metrics are not supported");
  case Metric::kSummary:
    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                        "summary metrics are not supported");
  default:
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "unexpected data type");
  }

  int err = plugin_dispatch_metric_family(&fam);

  metric_family_metric_reset(&fam);
  return wrap_error(err);
}

static grpc::Status unmarshal_resource(Resource rpb, label_set_t *resource) {
  for (auto kv : rpb.attributes()) {
    grpc::Status s = unmarshal_label_pair(kv, resource);
    if (!s.ok()) {
      return s;
    }
  }

  return grpc::Status::OK;
}

static grpc::Status dispatch_resource_metrics(ResourceMetrics rm) {
  label_set_t resource = {0};

  grpc::Status s = unmarshal_resource(rm.resource(), &resource);
  if (!s.ok()) {
    return s;
  }

  for (auto sm : rm.scope_metrics()) {
    for (auto m : sm.metrics()) {
      grpc::Status s = dispatch_metric(m, resource);
      if (!s.ok()) {
        return s;
      }
    }
  }

  label_set_reset(&resource);
  return grpc::Status::OK;
}

/*
 * OpenTelemetry MetricsService
 */
class OTMetricsService : public MetricsService::Service {
public:
  grpc::Status Export(grpc::ClientContext *context,
                      const ExportMetricsServiceRequest &req,
                      ExportMetricsServiceResponse *resp) {
    for (auto rm : req.resource_metrics()) {
      grpc::Status s = dispatch_resource_metrics(rm);
      if (!s.ok()) {
        return s;
      }
    }

    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                        "Export is not implemented yet");
  }
};

/*
 * gRPC server implementation
 */
class CollectorServer final {
public:
  void Start() {
    auto auth = grpc::InsecureServerCredentials();

    grpc::ServerBuilder builder;

    if (listeners.empty()) {
      builder.AddListeningPort(default_addr, auth);
      INFO("open_telemetry_collector: Listening on %s", default_addr.c_str());
    } else {
      for (auto l : listeners) {
        grpc::string addr = l.addr + ":" + l.port;

        auto use_ssl = grpc::string("");
        auto a = auth;
        if (l.ssl != nullptr) {
          use_ssl = grpc::string(" (SSL enabled)");
          a = grpc::SslServerCredentials(*l.ssl);
        }

        builder.AddListeningPort(addr, a);
        INFO("open_telemetry_collector: Listening on %s%s", addr.c_str(),
             use_ssl.c_str());
      }
    }

    builder.RegisterService(&metrics_service);

    server = builder.BuildAndStart();
  }

  void Shutdown() { server->Shutdown(); }

private:
  OTMetricsService metrics_service;

  std::unique_ptr<grpc::Server> server;
};

static CollectorServer *server = nullptr;

/*
 * collectd plugin interface
 */
extern "C" {
static int otelcol_config_listen(oconfig_item_t *ci) {
  if ((ci->values_num != 2) || (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING)) {
    ERROR("open_telemetry_collector: The `%s` config option needs exactly "
          "two string argument (address and port).",
          ci->key);
    return -1;
  }

  auto listener = Listener();
  listener.addr = grpc::string(ci->values[0].value.string);
  listener.port = grpc::string(ci->values[1].value.string);
  listener.ssl = nullptr;

  auto ssl_opts = new grpc::SslServerCredentialsOptions(
      GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
  grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp = {};
  bool use_ssl = false;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (!strcasecmp("EnableSSL", child->key)) {
      if (cf_util_get_boolean(child, &use_ssl)) {
        ERROR("open_telemetry_collector: Option `%s` expects a boolean value",
              child->key);
        return -1;
      }
    } else if (!strcasecmp("SSLCACertificateFile", child->key)) {
      char *certs = NULL;
      if (cf_util_get_string(child, &certs)) {
        ERROR("open_telemetry_collector: Option `%s` expects a string value",
              child->key);
        return -1;
      }
      ssl_opts->pem_root_certs = read_file(certs);
    } else if (!strcasecmp("SSLCertificateKeyFile", child->key)) {
      char *key = NULL;
      if (cf_util_get_string(child, &key)) {
        ERROR("open_telemetry_collector: Option `%s` expects a string value",
              child->key);
        return -1;
      }
      pkcp.private_key = read_file(key);
    } else if (!strcasecmp("SSLCertificateFile", child->key)) {
      char *cert = NULL;
      if (cf_util_get_string(child, &cert)) {
        ERROR("open_telemetry_collector: Option `%s` expects a string value",
              child->key);
        return -1;
      }
      pkcp.cert_chain = read_file(cert);
    } else if (!strcasecmp("VerifyPeer", child->key)) {
      bool verify = false;
      if (cf_util_get_boolean(child, &verify)) {
        return -1;
      }
      ssl_opts->client_certificate_request =
          verify ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
                 : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
    } else {
      WARNING(
          "open_telemetry_collector: Option `%s` not allowed in <%s> block.",
          child->key, ci->key);
    }
  }

  ssl_opts->pem_key_cert_pairs.push_back(pkcp);
  if (use_ssl)
    listener.ssl = ssl_opts;
  else
    delete (ssl_opts);

  listeners.push_back(listener);
  return 0;
} /* otelcol_config_listen() */

static int otelcol_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (!strcasecmp("Listen", child->key)) {
      if (otelcol_config_listen(child)) {
        return -1;
      }
    }

    else {
      WARNING("open_telemetry_collector: Option `%s` not allowed here.",
              child->key);
    }
  }

  return 0;
} /* otelcol_config() */

static int otelcol_init(void) {
  if (server) {
    return 0;
  }

  server = new CollectorServer();
  if (!server) {
    ERROR("open_telemetry_collector: Failed to create server");
    return -1;
  }

  server->Start();
  return 0;
} /* otelcol_init() */

static int otelcol_shutdown(void) {
  if (!server)
    return 0;

  server->Shutdown();

  delete server;
  server = nullptr;

  return 0;
} /* otelcol_shutdown() */

void module_register(void) {
  plugin_register_complex_config("open_telemetry_collector", otelcol_config);
  plugin_register_init("open_telemetry_collector", otelcol_init);
  plugin_register_shutdown("open_telemetry_collector", otelcol_shutdown);
} /* module_register() */
} /* extern "C" */
