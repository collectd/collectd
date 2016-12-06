/**
 * collectd - src/grpc.cc
 * Copyright (C) 2015-2016 Sebastian Harl
 * Copyright (C) 2016      Florian octo Forster
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

#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

#include <fstream>
#include <iostream>
#include <queue>
#include <vector>

#include "collectd.grpc.pb.h"

extern "C" {
#include <fnmatch.h>
#include <stdbool.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include "daemon/utils_cache.h"
}

using collectd::Collectd;

using collectd::PutValuesRequest;
using collectd::PutValuesResponse;
using collectd::QueryValuesRequest;
using collectd::QueryValuesResponse;

using google::protobuf::util::TimeUtil;

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

static bool ident_matches(const value_list_t *vl, const value_list_t *matcher) {
  if (fnmatch(matcher->host, vl->host, 0))
    return false;

  if (fnmatch(matcher->plugin, vl->plugin, 0))
    return false;
  if (fnmatch(matcher->plugin_instance, vl->plugin_instance, 0))
    return false;

  if (fnmatch(matcher->type, vl->type, 0))
    return false;
  if (fnmatch(matcher->type_instance, vl->type_instance, 0))
    return false;

  return true;
} /* ident_matches */

static grpc::string read_file(const char *filename) {
  std::ifstream f;
  grpc::string s, content;

  f.open(filename);
  if (!f.is_open()) {
    ERROR("grpc: Failed to open '%s'", filename);
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

static void marshal_ident(const value_list_t *vl,
                          collectd::types::Identifier *msg) {
  msg->set_host(vl->host);
  msg->set_plugin(vl->plugin);
  if (vl->plugin_instance[0] != '\0')
    msg->set_plugin_instance(vl->plugin_instance);
  msg->set_type(vl->type);
  if (vl->type_instance[0] != '\0')
    msg->set_type_instance(vl->type_instance);
} /* marshal_ident */

static grpc::Status unmarshal_ident(const collectd::types::Identifier &msg,
                                    value_list_t *vl, bool require_fields) {
  std::string s;

  s = msg.host();
  if (!s.length() && require_fields)
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        grpc::string("missing host name"));
  sstrncpy(vl->host, s.c_str(), sizeof(vl->host));

  s = msg.plugin();
  if (!s.length() && require_fields)
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        grpc::string("missing plugin name"));
  sstrncpy(vl->plugin, s.c_str(), sizeof(vl->plugin));

  s = msg.type();
  if (!s.length() && require_fields)
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        grpc::string("missing type name"));
  sstrncpy(vl->type, s.c_str(), sizeof(vl->type));

  s = msg.plugin_instance();
  sstrncpy(vl->plugin_instance, s.c_str(), sizeof(vl->plugin_instance));

  s = msg.type_instance();
  sstrncpy(vl->type_instance, s.c_str(), sizeof(vl->type_instance));

  return grpc::Status::OK;
} /* unmarshal_ident() */

static grpc::Status marshal_value_list(const value_list_t *vl,
                                       collectd::types::ValueList *msg) {
  auto id = msg->mutable_identifier();
  marshal_ident(vl, id);

  auto ds = plugin_get_ds(vl->type);
  if ((ds == NULL) || (ds->ds_num != vl->values_len)) {
    return grpc::Status(grpc::StatusCode::INTERNAL,
                        grpc::string("failed to retrieve data-set for values"));
  }

  auto t = TimeUtil::NanosecondsToTimestamp(CDTIME_T_TO_NS(vl->time));
  auto d = TimeUtil::NanosecondsToDuration(CDTIME_T_TO_NS(vl->interval));
  msg->set_allocated_time(new google::protobuf::Timestamp(t));
  msg->set_allocated_interval(new google::protobuf::Duration(d));

  for (size_t i = 0; i < vl->values_len; ++i) {
    auto v = msg->add_values();
    switch (ds->ds[i].type) {
    case DS_TYPE_COUNTER:
      v->set_counter(vl->values[i].counter);
      break;
    case DS_TYPE_GAUGE:
      v->set_gauge(vl->values[i].gauge);
      break;
    case DS_TYPE_DERIVE:
      v->set_derive(vl->values[i].derive);
      break;
    case DS_TYPE_ABSOLUTE:
      v->set_absolute(vl->values[i].absolute);
      break;
    default:
      return grpc::Status(grpc::StatusCode::INTERNAL,
                          grpc::string("unknown value type"));
    }

    auto name = msg->add_ds_names();
    name->assign(ds->ds[i].name);
  }

  return grpc::Status::OK;
} /* marshal_value_list */

static grpc::Status unmarshal_value_list(const collectd::types::ValueList &msg,
                                         value_list_t *vl) {
  vl->time = NS_TO_CDTIME_T(TimeUtil::TimestampToNanoseconds(msg.time()));
  vl->interval =
      NS_TO_CDTIME_T(TimeUtil::DurationToNanoseconds(msg.interval()));

  auto status = unmarshal_ident(msg.identifier(), vl, true);
  if (!status.ok())
    return status;

  value_t *values = NULL;
  size_t values_len = 0;

  status = grpc::Status::OK;
  for (auto v : msg.values()) {
    value_t *val =
        (value_t *)realloc(values, (values_len + 1) * sizeof(*values));
    if (!val) {
      status = grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                            grpc::string("failed to allocate values array"));
      break;
    }

    values = val;
    val = values + values_len;
    values_len++;

    switch (v.value_case()) {
    case collectd::types::Value::ValueCase::kCounter:
      val->counter = counter_t(v.counter());
      break;
    case collectd::types::Value::ValueCase::kGauge:
      val->gauge = gauge_t(v.gauge());
      break;
    case collectd::types::Value::ValueCase::kDerive:
      val->derive = derive_t(v.derive());
      break;
    case collectd::types::Value::ValueCase::kAbsolute:
      val->absolute = absolute_t(v.absolute());
      break;
    default:
      status = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            grpc::string("unknown value type"));
      break;
    }

    if (!status.ok())
      break;
  }
  if (status.ok()) {
    vl->values = values;
    vl->values_len = values_len;
  } else if (values) {
    free(values);
  }

  return status;
} /* unmarshal_value_list() */

/*
 * Collectd service
 */
class CollectdImpl : public collectd::Collectd::Service {
public:
  grpc::Status
  QueryValues(grpc::ServerContext *ctx, QueryValuesRequest const *req,
              grpc::ServerWriter<QueryValuesResponse> *writer) override {
    value_list_t match;
    auto status = unmarshal_ident(req->identifier(), &match, false);
    if (!status.ok()) {
      return status;
    }

    std::queue<value_list_t> value_lists;
    status = this->queryValuesRead(&match, &value_lists);
    if (status.ok()) {
      status = this->queryValuesWrite(ctx, writer, &value_lists);
    }

    while (!value_lists.empty()) {
      auto vl = value_lists.front();
      value_lists.pop();
      sfree(vl.values);
    }

    return status;
  }

  grpc::Status PutValues(grpc::ServerContext *ctx,
                         grpc::ServerReader<PutValuesRequest> *reader,
                         PutValuesResponse *res) override {
    PutValuesRequest req;

    while (reader->Read(&req)) {
      value_list_t vl = {0};
      auto status = unmarshal_value_list(req.value_list(), &vl);
      if (!status.ok())
        return status;

      if (plugin_dispatch_values(&vl))
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            grpc::string("failed to enqueue values for writing"));
    }

    res->Clear();
    return grpc::Status::OK;
  }

private:
  grpc::Status queryValuesRead(value_list_t const *match,
                               std::queue<value_list_t> *value_lists) {
    uc_iter_t *iter;
    if ((iter = uc_get_iterator()) == NULL) {
      return grpc::Status(
          grpc::StatusCode::INTERNAL,
          grpc::string("failed to query values: cannot create iterator"));
    }

    grpc::Status status = grpc::Status::OK;
    char *name = NULL;
    while (uc_iterator_next(iter, &name) == 0) {
      value_list_t vl;
      if (parse_identifier_vl(name, &vl) != 0) {
        status = grpc::Status(grpc::StatusCode::INTERNAL,
                              grpc::string("failed to parse identifier"));
        break;
      }

      if (!ident_matches(&vl, match))
        continue;

      if (uc_iterator_get_time(iter, &vl.time) < 0) {
        status =
            grpc::Status(grpc::StatusCode::INTERNAL,
                         grpc::string("failed to retrieve value timestamp"));
        break;
      }
      if (uc_iterator_get_interval(iter, &vl.interval) < 0) {
        status =
            grpc::Status(grpc::StatusCode::INTERNAL,
                         grpc::string("failed to retrieve value interval"));
        break;
      }
      if (uc_iterator_get_values(iter, &vl.values, &vl.values_len) < 0) {
        status = grpc::Status(grpc::StatusCode::INTERNAL,
                              grpc::string("failed to retrieve values"));
        break;
      }

      value_lists->push(vl);
    } // while (uc_iterator_next(iter, &name) == 0)

    uc_iterator_destroy(iter);
    return status;
  }

  grpc::Status queryValuesWrite(grpc::ServerContext *ctx,
                                grpc::ServerWriter<QueryValuesResponse> *writer,
                                std::queue<value_list_t> *value_lists) {
    while (!value_lists->empty()) {
      auto vl = value_lists->front();
      QueryValuesResponse res;
      res.Clear();

      auto status = marshal_value_list(&vl, res.mutable_value_list());
      if (!status.ok()) {
        return status;
      }

      if (!writer->Write(res)) {
        return grpc::Status::CANCELLED;
      }

      value_lists->pop();
      sfree(vl.values);
    }

    return grpc::Status::OK;
  }
};

/*
 * gRPC server implementation
 */
class CollectdServer final {
public:
  void Start() {
    auto auth = grpc::InsecureServerCredentials();

    grpc::ServerBuilder builder;

    if (listeners.empty()) {
      builder.AddListeningPort(default_addr, auth);
      INFO("grpc: Listening on %s", default_addr.c_str());
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
        INFO("grpc: Listening on %s%s", addr.c_str(), use_ssl.c_str());
      }
    }

    builder.RegisterService(&collectd_service_);

    server_ = builder.BuildAndStart();
  } /* Start() */

  void Shutdown() { server_->Shutdown(); } /* Shutdown() */

private:
  CollectdImpl collectd_service_;

  std::unique_ptr<grpc::Server> server_;
}; /* class CollectdServer */

class CollectdClient final {
public:
  CollectdClient(std::shared_ptr<grpc::ChannelInterface> channel)
      : stub_(Collectd::NewStub(channel)) {}

  int PutValues(value_list_t const *vl) {
    grpc::ClientContext ctx;

    PutValuesRequest req;
    auto status = marshal_value_list(vl, req.mutable_value_list());
    if (!status.ok()) {
      ERROR("grpc: Marshalling value_list_t failed.");
      return -1;
    }

    PutValuesResponse res;
    auto stream = stub_->PutValues(&ctx, &res);
    if (!stream->Write(req)) {
      NOTICE("grpc: Broken stream.");
      /* intentionally not returning. */
    }

    stream->WritesDone();
    status = stream->Finish();
    if (!status.ok()) {
      ERROR("grpc: Error while closing stream.");
      return -1;
    }

    return 0;
  } /* int PutValues */

private:
  std::unique_ptr<Collectd::Stub> stub_;
};

static CollectdServer *server = nullptr;

/*
 * collectd plugin interface
 */
extern "C" {
static void c_grpc_destroy_write_callback(void *ptr) {
  delete (CollectdClient *)ptr;
}

static int c_grpc_write(__attribute__((unused)) data_set_t const *ds,
                        value_list_t const *vl, user_data_t *ud) {
  CollectdClient *c = (CollectdClient *)ud->data;
  return c->PutValues(vl);
}

static int c_grpc_config_listen(oconfig_item_t *ci) {
  if ((ci->values_num != 2) || (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING)) {
    ERROR("grpc: The `%s` config option needs exactly "
          "two string argument (address and port).",
          ci->key);
    return -1;
  }

  auto listener = Listener();
  listener.addr = grpc::string(ci->values[0].value.string);
  listener.port = grpc::string(ci->values[1].value.string);
  listener.ssl = nullptr;

  auto ssl_opts = new (grpc::SslServerCredentialsOptions);
  grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp = {};
  bool use_ssl = false;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (!strcasecmp("EnableSSL", child->key)) {
      if (cf_util_get_boolean(child, &use_ssl)) {
        ERROR("grpc: Option `%s` expects a boolean value", child->key);
        return -1;
      }
    } else if (!strcasecmp("SSLCACertificateFile", child->key)) {
      char *certs = NULL;
      if (cf_util_get_string(child, &certs)) {
        ERROR("grpc: Option `%s` expects a string value", child->key);
        return -1;
      }
      ssl_opts->pem_root_certs = read_file(certs);
    } else if (!strcasecmp("SSLCertificateKeyFile", child->key)) {
      char *key = NULL;
      if (cf_util_get_string(child, &key)) {
        ERROR("grpc: Option `%s` expects a string value", child->key);
        return -1;
      }
      pkcp.private_key = read_file(key);
    } else if (!strcasecmp("SSLCertificateFile", child->key)) {
      char *cert = NULL;
      if (cf_util_get_string(child, &cert)) {
        ERROR("grpc: Option `%s` expects a string value", child->key);
        return -1;
      }
      pkcp.cert_chain = read_file(cert);
    } else {
      WARNING("grpc: Option `%s` not allowed in <%s> block.", child->key,
              ci->key);
    }
  }

  ssl_opts->pem_key_cert_pairs.push_back(pkcp);
  if (use_ssl)
    listener.ssl = ssl_opts;
  else
    delete (ssl_opts);

  listeners.push_back(listener);
  return 0;
} /* c_grpc_config_listen() */

static int c_grpc_config_server(oconfig_item_t *ci) {
  if ((ci->values_num != 2) || (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING)) {
    ERROR("grpc: The `%s` config option needs exactly "
          "two string argument (address and port).",
          ci->key);
    return -1;
  }

  grpc::SslCredentialsOptions ssl_opts;
  bool use_ssl = false;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (!strcasecmp("EnableSSL", child->key)) {
      if (cf_util_get_boolean(child, &use_ssl)) {
        return -1;
      }
    } else if (!strcasecmp("SSLCACertificateFile", child->key)) {
      char *certs = NULL;
      if (cf_util_get_string(child, &certs)) {
        return -1;
      }
      ssl_opts.pem_root_certs = read_file(certs);
    } else if (!strcasecmp("SSLCertificateKeyFile", child->key)) {
      char *key = NULL;
      if (cf_util_get_string(child, &key)) {
        return -1;
      }
      ssl_opts.pem_private_key = read_file(key);
    } else if (!strcasecmp("SSLCertificateFile", child->key)) {
      char *cert = NULL;
      if (cf_util_get_string(child, &cert)) {
        return -1;
      }
      ssl_opts.pem_cert_chain = read_file(cert);
    } else {
      WARNING("grpc: Option `%s` not allowed in <%s> block.", child->key,
              ci->key);
    }
  }

  auto node = grpc::string(ci->values[0].value.string);
  auto service = grpc::string(ci->values[1].value.string);
  auto addr = node + ":" + service;

  CollectdClient *client;
  if (use_ssl) {
    auto channel_creds = grpc::SslCredentials(ssl_opts);
    auto channel = grpc::CreateChannel(addr, channel_creds);
    client = new CollectdClient(channel);
  } else {
    auto channel =
        grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
    client = new CollectdClient(channel);
  }

  auto callback_name = grpc::string("grpc/") + addr;
  user_data_t ud = {
      .data = client, .free_func = c_grpc_destroy_write_callback,
  };

  plugin_register_write(callback_name.c_str(), c_grpc_write, &ud);
  return 0;
} /* c_grpc_config_server() */

static int c_grpc_config(oconfig_item_t *ci) {
  int i;

  for (i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (!strcasecmp("Listen", child->key)) {
      if (c_grpc_config_listen(child))
        return -1;
    } else if (!strcasecmp("Server", child->key)) {
      if (c_grpc_config_server(child))
        return -1;
    }

    else {
      WARNING("grpc: Option `%s` not allowed here.", child->key);
    }
  }

  return 0;
} /* c_grpc_config() */

static int c_grpc_init(void) {
  server = new CollectdServer();
  if (!server) {
    ERROR("grpc: Failed to create server");
    return -1;
  }

  server->Start();
  return 0;
} /* c_grpc_init() */

static int c_grpc_shutdown(void) {
  if (!server)
    return 0;

  server->Shutdown();

  delete server;
  server = nullptr;

  return 0;
} /* c_grpc_shutdown() */

void module_register(void) {
  plugin_register_complex_config("grpc", c_grpc_config);
  plugin_register_init("grpc", c_grpc_init);
  plugin_register_shutdown("grpc", c_grpc_shutdown);
} /* module_register() */
} /* extern "C" */
