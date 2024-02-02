/**
 * collectd - src/open_telemetry_exporter.cc
 * Copyright (C) 2023-2024  Florian octo Forster
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

#include "daemon/plugin.h"
#include "utils/common/common.h"

#include "utils/resource_metrics/resource_metrics.h"
#include "utils/strbuf/strbuf.h"
#include "utils_complain.h"

#include <netdb.h>
}

#include <grpc++/grpc++.h>

#include "opentelemetry/proto/collector/metrics/v1/metrics_service.grpc.pb.h"
#include "utils/format_open_telemetry/format_open_telemetry.h"

#ifndef OT_DEFAULT_PORT
#define OT_DEFAULT_PORT "4317"
#endif

using opentelemetry::proto::collector::metrics::v1::
    ExportMetricsServiceResponse;
using opentelemetry::proto::collector::metrics::v1::MetricsService;

/*
 * Private variables
 */
typedef struct {
  int reference_count;

  char *host;
  char *port;

  resource_metrics_set_t resource_metrics;
  cdtime_t staged_time;

  std::unique_ptr<MetricsService::Stub> stub;

  pthread_mutex_t mu;
} ot_callback_t;

static int export_metrics(ot_callback_t *cb) {
  if (cb->stub == NULL) {
    strbuf_t buf = STRBUF_CREATE;
    strbuf_printf(&buf, "%s:%s", cb->host, cb->port);

    auto chan =
        grpc::CreateChannel(buf.ptr, grpc::InsecureChannelCredentials());
    cb->stub = MetricsService::NewStub(chan);

    STRBUF_DESTROY(buf);
  }

  auto req = format_open_telemetry_export_metrics_service_request(
      cb->resource_metrics);

  grpc::ClientContext context;
  ExportMetricsServiceResponse resp;
  grpc::Status status = cb->stub->Export(&context, *req, &resp);

  if (!status.ok()) {
    ERROR("open_telemetry plugin: Exporting metrics failed: %s",
          status.error_message().c_str());
    return -1;
  }

  if (resp.has_partial_success() &&
      resp.partial_success().rejected_data_points() > 0) {
    auto ps = resp.partial_success();
    NOTICE("open_telemetry plugin: %" PRId64 " data points were rejected: %s",
           ps.rejected_data_points(), ps.error_message().c_str());
  }

  delete req;
  return 0;
}

/* NOTE: You must hold cb->send_lock when calling this function! */
static int ot_flush_nolock(cdtime_t timeout, ot_callback_t *cb) {
  if (cb->resource_metrics.num == 0) {
    cb->staged_time = cdtime();
    return 0;
  }

  /* timeout == 0  => flush unconditionally */
  if (timeout > 0) {
    cdtime_t now = cdtime();
    if (now < (cb->staged_time + timeout))
      return 0;
  }

  int status = export_metrics(cb);
  resource_metrics_reset(&cb->resource_metrics);

  return status;
}

static void ot_callback_decref(void *data) {
  ot_callback_t *cb = (ot_callback_t *)data;
  if (cb == NULL)
    return;

  pthread_mutex_lock(&cb->mu);
  cb->reference_count--;
  if (cb->reference_count > 0) {
    pthread_mutex_unlock(&cb->mu);
    return;
  }

  ot_flush_nolock(/* timeout = */ 0, cb);

  cb->stub.reset();

  sfree(cb->host);
  sfree(cb->port);

  pthread_mutex_unlock(&cb->mu);
  pthread_mutex_destroy(&cb->mu);

  sfree(cb);
}

static int ot_flush(cdtime_t timeout,
                    const char *identifier __attribute__((unused)),
                    user_data_t *user_data) {
  if (user_data == NULL)
    return -EINVAL;

  ot_callback_t *cb = (ot_callback_t *)user_data->data;

  pthread_mutex_lock(&cb->mu);
  int status = ot_flush_nolock(timeout, cb);
  pthread_mutex_unlock(&cb->mu);

  return status;
}

static int ot_write(metric_family_t const *fam, user_data_t *user_data) {
  ot_callback_t *cb = (ot_callback_t *)user_data->data;
  if ((fam == NULL) || (cb == NULL)) {
    return EINVAL;
  }
  pthread_mutex_lock(&cb->mu);
  int status = resource_metrics_add(&cb->resource_metrics, fam);
  pthread_mutex_unlock(&cb->mu);

  if (status < 0) {
    return status;
  }
  return 0;
}

int exporter_config(oconfig_item_t *ci) {
  if (ci->values_num < 1 || ci->values_num > 2 ||
      ci->values[0].type != OCONFIG_TYPE_STRING ||
      (ci->values_num > 1 && ci->values[1].type != OCONFIG_TYPE_STRING)) {
    ERROR("open_telemetry plugin: The \"%s\" config option needs "
          "one or two string arguments (address and port).",
          ci->key);
    return EINVAL;
  }

  ot_callback_t *cb = (ot_callback_t *)calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("open_telemetry plugin: calloc failed.");
    return -1;
  }

  *cb = (ot_callback_t){
      .reference_count = 1,
      .host = strdup(ci->values[0].value.string),
  };
  if (ci->values_num >= 2) {
    cb->port = strdup(ci->values[1].value.string);
  } else {
    cb->port = strdup(OT_DEFAULT_PORT);
  }
  pthread_mutex_init(&cb->mu, /* attr = */ NULL);

  strbuf_t callback_name = STRBUF_CREATE;
  strbuf_printf(&callback_name, "open_telemetry/[%s]:%s", cb->host, cb->port);

  user_data_t user_data = {
      .data = cb,
      .free_func = ot_callback_decref,
  };

  /* Call ot_flush periodically. */
  plugin_ctx_t ctx = plugin_get_ctx();
  ctx.flush_interval = plugin_get_interval();
  plugin_set_ctx(ctx);

  cb->reference_count++;
  plugin_register_write(callback_name.ptr, ot_write, &user_data);

  cb->reference_count++;
  plugin_register_flush(callback_name.ptr, ot_flush, &user_data);

  ot_callback_decref(cb);
  STRBUF_DESTROY(callback_name);
  return 0;
}
