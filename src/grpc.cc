/**
 * collectd - src/grpc.cc
 * Copyright (C) 2015-2016 Sebastian Harl
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
 **/

#include <grpc++/grpc++.h>
#include <google/protobuf/util/time_util.h>

#include <fstream>
#include <iostream>
#include <vector>

#include "collectd.grpc.pb.h"

extern "C" {
#include <fnmatch.h>
#include <stdbool.h>

#include "collectd.h"
#include "common.h"
#include "configfile.h"
#include "plugin.h"

#include "daemon/utils_cache.h"
}

using collectd::Collectd;
using collectd::Dispatch;

using collectd::DispatchValuesRequest;
using collectd::DispatchValuesResponse;
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

static bool ident_matches(const value_list_t *vl, const value_list_t *matcher)
{
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

static grpc::string read_file(const char *filename)
{
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

static void marshal_ident(const value_list_t *vl, collectd::types::Identifier *msg)
{
	msg->set_host(vl->host);
	msg->set_plugin(vl->plugin);
	if (vl->plugin_instance[0] != '\0')
		msg->set_plugin_instance(vl->plugin_instance);
	msg->set_type(vl->type);
	if (vl->type_instance[0] != '\0')
		msg->set_type_instance(vl->type_instance);
} /* marshal_ident */

static grpc::Status unmarshal_ident(const collectd::types::Identifier &msg, value_list_t *vl,
		bool require_fields)
{
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

static grpc::Status marshal_value_list(const value_list_t *vl, collectd::types::ValueList *msg)
{
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

static grpc::Status unmarshal_value_list(const collectd::types::ValueList &msg, value_list_t *vl)
{
	vl->time = NS_TO_CDTIME_T(TimeUtil::TimestampToNanoseconds(msg.time()));
	vl->interval = NS_TO_CDTIME_T(TimeUtil::DurationToNanoseconds(msg.interval()));

	auto status = unmarshal_ident(msg.identifier(), vl, true);
	if (!status.ok())
		return status;

	value_t *values = NULL;
	size_t values_len = 0;

	status = grpc::Status::OK;
	for (auto v : msg.values()) {
		value_t *val = (value_t *)realloc(values, (values_len + 1) * sizeof(*values));
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
	}
	else if (values) {
		free(values);
	}

	return status;
} /* unmarshal_value_list() */

/*
 * request call-backs and call objects
 */
static grpc::Status DispatchValue(grpc::ServerContext *ctx, DispatchValuesRequest request, DispatchValuesResponse *reply)
{
	value_list_t vl = VALUE_LIST_INIT;
	auto status = unmarshal_value_list(request.value_list(), &vl);
	if (!status.ok())
		return status;

	if (plugin_dispatch_values(&vl))
		status = grpc::Status(grpc::StatusCode::INTERNAL,
				grpc::string("failed to enqueue values for writing"));

	reply->Clear();
	return status;
} /* grpc::Status DispatchValue */

static grpc::Status QueryValues(grpc::ServerContext *ctx, QueryValuesRequest req, QueryValuesResponse *res)
{
	uc_iter_t *iter;
	char *name = NULL;

	value_list_t matcher;
	auto status = unmarshal_ident(req.identifier(), &matcher, false);
	if (!status.ok())
		return status;

	if ((iter = uc_get_iterator()) == NULL) {
		return grpc::Status(grpc::StatusCode::INTERNAL,
				grpc::string("failed to query values: cannot create iterator"));
	}

	status = grpc::Status::OK;
	while (uc_iterator_next(iter, &name) == 0) {
		value_list_t vl;
		if (parse_identifier_vl(name, &vl) != 0) {
			status = grpc::Status(grpc::StatusCode::INTERNAL,
					grpc::string("failed to parse identifier"));
			break;
		}

		if (!ident_matches(&vl, &matcher))
			continue;

		if (uc_iterator_get_time(iter, &vl.time) < 0) {
			status = grpc::Status(grpc::StatusCode::INTERNAL,
					grpc::string("failed to retrieve value timestamp"));
			break;
		}
		if (uc_iterator_get_interval(iter, &vl.interval) < 0) {
			status = grpc::Status(grpc::StatusCode::INTERNAL,
					grpc::string("failed to retrieve value interval"));
			break;
		}
		if (uc_iterator_get_values(iter, &vl.values, &vl.values_len) < 0) {
			status = grpc::Status(grpc::StatusCode::INTERNAL,
					grpc::string("failed to retrieve values"));
			break;
		}

		auto pb_vl = res->add_value_lists();
		status = marshal_value_list(&vl, pb_vl);
		free(vl.values);
		if (!status.ok())
			break;
	}

	uc_iterator_destroy(iter);

	return status;
} /* grpc::Status QueryValues */

// CallData is the abstract base class for asynchronous calls.
class CallData {
public:
  virtual ~CallData() {}
  virtual void process(bool ok) = 0;

protected:
  CallData() {}

private:
  CallData(const CallData&) = delete;
  CallData& operator=(const CallData&) = delete;
};

/*
 * Collectd service
 */
// QueryValuesCallData holds the state and implements the logic for QueryValues calls.
class QueryValuesCallData : public CallData {
public:
	QueryValuesCallData(Collectd::AsyncService* service, grpc::ServerCompletionQueue* cq)
			: cq_(cq), service_(service), writer_(&context_) {
		// As part of the initialization, we *request* that the system start
		// processing QueryValues requests. In this request, "this" acts as
		// the tag uniquely identifying the request (so that different
		// QueryValuesCallData instances can serve different requests
		// concurrently), in this case the memory address of this
		// QueryValuesCallData instance.
		service->RequestQueryValues(&context_, &request_, &writer_, cq_, cq_, this);
	}

	void process(bool ok) final {
		if (done_) {
			delete this;
		} else {
			// Spawn a new QueryValuesCallData instance to serve new clients
			// while we process the one for this QueryValuesCallData. The
			// instance will deallocate itself as part of its FINISH state.
			new QueryValuesCallData(service_, cq_);

			auto status = QueryValues(&context_, request_, &response_);
			if (!status.ok()) {
				writer_.FinishWithError(status, this);
			} else {
				writer_.Finish(response_, grpc::Status::OK, this);
			}

			done_ = true;
		}
	}

private:
	bool done_ = false;
	grpc::ServerContext context_;
	grpc::ServerCompletionQueue* cq_;
	Collectd::AsyncService* service_;
	QueryValuesRequest request_;
	QueryValuesResponse response_;
	grpc::ServerAsyncResponseWriter<QueryValuesResponse> writer_;
};

/*
 * Dispatch service
 */
// DispatchValuesCallData holds the state and implements the logic for DispatchValues calls.
class DispatchValuesCallData : public CallData {
public:
	DispatchValuesCallData(Dispatch::AsyncService* service, grpc::ServerCompletionQueue* cq)
			: cq_(cq), service_(service), reader_(&context_) {
		process(true);
	}

	void process(bool ok) final {
		if (status == Status::INIT) {
			service_->RequestDispatchValues(&context_, &reader_, cq_, cq_, this);
			status = Status::CALL;
		} else if (status == Status::CALL) {
			reader_.Read(&request_, this);
			status = Status::READ;
		} else if (status == Status::READ && ok) {
			(void) DispatchValue(&context_, request_, &response_);

			reader_.Read(&request_, this);
		} else if (status == Status::READ) {
			response_.Clear();

			status = Status::DONE;
		} else if (status == Status::DONE) {
			new DispatchValuesCallData(service_, cq_);
			delete this;
		} else {
			ERROR("grpc: DispatchValuesCallData: invalid state");
		}
	}

private:
	enum class Status {
		INIT,
		CALL,
		READ,
		DONE,
	};
	Status status = Status::INIT;

	grpc::ServerContext          context_;
	grpc::ServerCompletionQueue* cq_;
	Dispatch::AsyncService*      service_;

	DispatchValuesRequest request_;
	DispatchValuesResponse response_;
	grpc::ServerAsyncReader<DispatchValuesResponse, DispatchValuesRequest> reader_;
};

/*
 * gRPC server implementation
 */
class CollectdServer final
{
public:
	void Start()
	{
		auto auth = grpc::InsecureServerCredentials();

		grpc::ServerBuilder builder;

		if (listeners.empty()) {
			builder.AddListeningPort(default_addr, auth);
			INFO("grpc: Listening on %s", default_addr.c_str());
		}
		else {
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

		cq_ = builder.AddCompletionQueue();

		builder.RegisterService(&collectd_service_);
		builder.RegisterService(&dispatch_service_);

		server_ = builder.BuildAndStart();
		new QueryValuesCallData(&collectd_service_, cq_.get());
		new DispatchValuesCallData(&dispatch_service_, cq_.get());
	} /* Start() */

	void Shutdown()
	{
		server_->Shutdown();
		cq_->Shutdown();
	} /* Shutdown() */

	void Mainloop()
	{
		while (true) {
			void *tag = NULL;
			bool ok = false;

			// Block waiting to read the next event from the completion queue.
			// The event is uniquely identified by its tag, which in this case
			// is the memory address of a CallData instance.
			if (!cq_->Next(&tag, &ok))
				break; // Queue shut down.

			static_cast<CallData*>(tag)->process(ok);
		}
	} /* Mainloop() */

private:
	Collectd::AsyncService collectd_service_;
	Dispatch::AsyncService dispatch_service_;

	std::unique_ptr<grpc::Server> server_;
	std::unique_ptr<grpc::ServerCompletionQueue> cq_;
}; /* class CollectdServer */

static CollectdServer *server = nullptr;

/*
 * collectd plugin interface
 */
extern "C" {
	static pthread_t *workers;
	static size_t workers_num = 5;

	static void *worker_thread(void *arg)
	{
		CollectdServer *s = (CollectdServer *)arg;
		s->Mainloop();
		return NULL;
	} /* worker_thread() */

	static int c_grpc_config_listen(oconfig_item_t *ci)
	{
		if ((ci->values_num != 2)
				|| (ci->values[0].type != OCONFIG_TYPE_STRING)
				|| (ci->values[1].type != OCONFIG_TYPE_STRING)) {
			ERROR("grpc: The `%s` config option needs exactly "
					"two string argument (address and port).", ci->key);
			return -1;
		}

		auto listener = Listener();
		listener.addr = grpc::string(ci->values[0].value.string);
		listener.port = grpc::string(ci->values[1].value.string);
		listener.ssl = nullptr;

		auto ssl_opts = new(grpc::SslServerCredentialsOptions);
		grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp = {};
		bool use_ssl = false;

		for (int i = 0; i < ci->children_num; i++) {
			oconfig_item_t *child = ci->children + i;

			if (!strcasecmp("EnableSSL", child->key)) {
				if (cf_util_get_boolean(child, &use_ssl)) {
					ERROR("grpc: Option `%s` expects a boolean value",
							child->key);
					return -1;
				}
			}
			else if (!strcasecmp("SSLRootCerts", child->key)) {
				char *certs = NULL;
				if (cf_util_get_string(child, &certs)) {
					ERROR("grpc: Option `%s` expects a string value",
							child->key);
					return -1;
				}
				ssl_opts->pem_root_certs = read_file(certs);
			}
			else if (!strcasecmp("SSLServerKey", child->key)) {
				char *key = NULL;
				if (cf_util_get_string(child, &key)) {
					ERROR("grpc: Option `%s` expects a string value",
							child->key);
					return -1;
				}
				pkcp.private_key = read_file(key);
			}
			else if (!strcasecmp("SSLServerCert", child->key)) {
				char *cert = NULL;
				if (cf_util_get_string(child, &cert)) {
					ERROR("grpc: Option `%s` expects a string value",
							child->key);
					return -1;
				}
				pkcp.cert_chain = read_file(cert);
			}
			else {
				WARNING("grpc: Option `%s` not allowed in <%s> block.",
						child->key, ci->key);
			}
		}

		ssl_opts->pem_key_cert_pairs.push_back(pkcp);
		if (use_ssl)
			listener.ssl = ssl_opts;
		else
			delete(ssl_opts);

		listeners.push_back(listener);
		return 0;
	} /* c_grpc_config_listen() */

	static int c_grpc_config(oconfig_item_t *ci)
	{
		int i;

		for (i = 0; i < ci->children_num; i++) {
			oconfig_item_t *child = ci->children + i;

			if (!strcasecmp("Listen", child->key)) {
				if (c_grpc_config_listen(child))
					return -1;
			}
			else if (!strcasecmp("WorkerThreads", child->key)) {
				int n;
				if (cf_util_get_int(child, &n))
					return -1;
				workers_num = (size_t)n;
			}
			else {
				WARNING("grpc: Option `%s` not allowed here.", child->key);
			}
		}

		return 0;
	} /* c_grpc_config() */

	static int c_grpc_init(void)
	{
		server = new CollectdServer();
		size_t i;

		if (! server) {
			ERROR("grpc: Failed to create server");
			return -1;
		}

		workers = (pthread_t *)calloc(workers_num, sizeof(*workers));
		if (! workers) {
			delete server;
			server = nullptr;

			ERROR("grpc: Failed to allocate worker threads");
			return -1;
		}

		server->Start();
		for (i = 0; i < workers_num; i++) {
			plugin_thread_create(&workers[i], /* attr = */ NULL,
					worker_thread, server);
		}
		INFO("grpc: Started %zu workers", workers_num);
		return 0;
	} /* c_grpc_init() */

	static int c_grpc_shutdown(void)
	{
		size_t i;

		if (!server)
			return -1;

		server->Shutdown();

		INFO("grpc: Waiting for %zu workers to terminate", workers_num);
		for (i = 0; i < workers_num; i++)
			pthread_join(workers[i], NULL);
		free(workers);
		workers = NULL;
		workers_num = 0;

		delete server;
		server = nullptr;

		return 0;
	} /* c_grpc_shutdown() */

	void module_register(void)
	{
		plugin_register_complex_config("grpc", c_grpc_config);
		plugin_register_init("grpc", c_grpc_init);
		plugin_register_shutdown("grpc", c_grpc_shutdown);
	} /* module_register() */
} /* extern "C" */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */
