/**
 * collectd - src/grpc.cc
 * Copyright (C) 2015 Sebastian Harl
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

#include "collectd.grpc.pb.h"

extern "C" {
#include <stdbool.h>
#include <pthread.h>

#include "collectd.h"
#include "common.h"
#include "configfile.h"
#include "plugin.h"

#include "daemon/utils_cache.h"

	typedef struct {
		char *addr;
		char *port;
	} listener_t;

	static listener_t *listeners;
	static size_t listeners_num;
}

using collectd::Collectd;

using collectd::DispatchValuesRequest;
using collectd::DispatchValuesReply;
using collectd::ListValuesRequest;
using collectd::ListValuesReply;

using google::protobuf::util::TimeUtil;

/*
 * proto conversion
 */

static grpc::Status unmarshal_value_list(const collectd::types::ValueList &msg, value_list_t *vl)
{
	vl->time = NS_TO_CDTIME_T(TimeUtil::TimestampToNanoseconds(msg.time()));
	vl->interval = NS_TO_CDTIME_T(TimeUtil::DurationToNanoseconds(msg.interval()));

	std::string s;

	s = msg.host();
	if (!s.length())
		return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
				grpc::string("missing host name"));
	sstrncpy(vl->host, s.c_str(), sizeof(vl->host));

	s = msg.plugin();
	if (!s.length())
		return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
				grpc::string("missing plugin name"));
	sstrncpy(vl->plugin, s.c_str(), sizeof(vl->plugin));

	s = msg.type();
	if (!s.length())
		return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
				grpc::string("missing type name"));
	sstrncpy(vl->type, s.c_str(), sizeof(vl->type));

	s = msg.plugin_instance();
	sstrncpy(vl->plugin_instance, s.c_str(), sizeof(vl->plugin_instance));

	s = msg.type_instance();
	sstrncpy(vl->type_instance, s.c_str(), sizeof(vl->type_instance));

	value_t *values = NULL;
	size_t values_len = 0;
	auto status = grpc::Status::OK;

	for (auto v : msg.value()) {
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

static grpc::Status Process(grpc::ServerContext *ctx,
		DispatchValuesRequest request, DispatchValuesReply *reply)
{
	value_list_t vl = VALUE_LIST_INIT;
	auto status = unmarshal_value_list(request.values(), &vl);
	if (!status.ok())
		return status;

	if (plugin_dispatch_values(&vl))
		status = grpc::Status(grpc::StatusCode::INTERNAL,
				grpc::string("failed to enqueue values for writing"));
	return status;
} /* Process(): DispatchValues */

static grpc::Status Process(grpc::ServerContext *ctx,
		ListValuesRequest request, ListValuesReply *reply)
{
	char **names = NULL;
	cdtime_t *times = NULL;
	size_t i, n = 0;

	if (uc_get_names(&names, &times, &n))
		return grpc::Status(grpc::StatusCode::INTERNAL,
				grpc::string("failed to retrieve values"));

	for (i = 0; i < n; i++) {
		auto v = reply->add_value();
		auto t = TimeUtil::NanosecondsToTimestamp(CDTIME_T_TO_NS(times[i]));
		v->set_name(names[i]);
		v->set_allocated_time(new google::protobuf::Timestamp(t));
		sfree(names[i]);
	}
	sfree(names);
	sfree(times);

	return grpc::Status::OK;
} /* Process(): ListValues */

class Call
{
public:
	Call(Collectd::AsyncService *service, grpc::ServerCompletionQueue *cq)
		: service_(service), cq_(cq), status_(CREATE)
	{ }

	virtual ~Call()
	{ }

	void Handle()
	{
		if (status_ == CREATE) {
			Create();
			status_ = PROCESS;
		}
		else if (status_ == PROCESS) {
			Process();
			status_ = FINISH;
		}
		else {
			GPR_ASSERT(status_ == FINISH);
			Finish();
		}
	} /* Handle() */

protected:
	virtual void Create() = 0;
	virtual void Process() = 0;
	virtual void Finish() = 0;

	Collectd::AsyncService *service_;
	grpc::ServerCompletionQueue *cq_;
	grpc::ServerContext ctx_;

private:
	enum CallStatus { CREATE, PROCESS, FINISH };
	CallStatus status_;
}; /* class Call */

template<typename RequestT, typename ReplyT>
class RpcCall final : public Call
{
	typedef void (Collectd::AsyncService::*CreatorT)(grpc::ServerContext *,
			RequestT *, grpc::ServerAsyncResponseWriter<ReplyT> *,
			grpc::CompletionQueue *, grpc::ServerCompletionQueue *, void *);

public:
	RpcCall(Collectd::AsyncService *service,
			CreatorT creator, grpc::ServerCompletionQueue *cq)
		: Call(service, cq), creator_(creator), responder_(&ctx_)
	{
		Handle();
	} /* RpcCall() */

	virtual ~RpcCall()
	{ }

private:
	void Create()
	{
		(service_->*creator_)(&ctx_, &request_, &responder_, cq_, cq_, this);
	} /* Create() */

	void Process()
	{
		// Add a new request object to the queue.
		new RpcCall<RequestT, ReplyT>(service_, creator_, cq_);
		grpc::Status status = ::Process(&ctx_, request_, &reply_);
		responder_.Finish(reply_, status, this);
	} /* Process() */

	void Finish()
	{
		delete this;
	} /* Finish() */

	CreatorT creator_;

	RequestT request_;
	ReplyT reply_;

	grpc::ServerAsyncResponseWriter<ReplyT> responder_;
}; /* class RpcCall */

/*
 * gRPC server implementation
 */

class CollectdServer final
{
public:
	void Start()
	{
		// TODO: make configurable
		auto auth = grpc::InsecureServerCredentials();

		grpc::ServerBuilder builder;

		if (!listeners_num) {
			std::string default_addr("0.0.0.0:50051");
			builder.AddListeningPort(default_addr, auth);
			INFO("grpc: Listening on %s", default_addr.c_str());
		}
		else {
			size_t i;
			for (i = 0; i < listeners_num; i++) {
				auto l = listeners[i];
				std::string addr(l.addr);
				addr += std::string(":") + std::string(l.port);
				builder.AddListeningPort(addr, auth);
				INFO("grpc: Listening on %s", addr.c_str());
			}
		}

		builder.RegisterService(&service_);
		cq_ = builder.AddCompletionQueue();
		server_ = builder.BuildAndStart();
	} /* Start() */

	void Shutdown()
	{
		server_->Shutdown();
		cq_->Shutdown();
	} /* Shutdown() */

	void Mainloop()
	{
		// Register request types.
		new RpcCall<DispatchValuesRequest, DispatchValuesReply>(&service_,
				&Collectd::AsyncService::RequestDispatchValues, cq_.get());
		new RpcCall<ListValuesRequest, ListValuesReply>(&service_,
				&Collectd::AsyncService::RequestListValues, cq_.get());

		while (true) {
			void *req = NULL;
			bool ok = false;

			if (!cq_->Next(&req, &ok))
				break; // Queue shut down.
			if (!ok) {
				ERROR("grpc: Failed to read from queue");
				break;
			}

			static_cast<Call *>(req)->Handle();
		}
	} /* Mainloop() */

private:
	Collectd::AsyncService service_;

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
		listener_t *listener;
		int i;

		if ((ci->values_num != 2)
				|| (ci->values[0].type != OCONFIG_TYPE_STRING)
				|| (ci->values[1].type != OCONFIG_TYPE_STRING)) {
			ERROR("grpc: The `%s` config option needs exactly "
					"two string argument (address and port).", ci->key);
			return -1;
		}

		listener = (listener_t *)realloc(listeners,
				(listeners_num + 1) * sizeof(*listeners));
		if (!listener) {
			ERROR("grpc: Failed to allocate listeners");
			return -1;
		}
		listeners = listener;
		listener = listeners + listeners_num;
		listeners_num++;

		listener->addr = strdup(ci->values[0].value.string);
		listener->port = strdup(ci->values[1].value.string);

		for (i = 0; i < ci->children_num; i++) {
			oconfig_item_t *child = ci->children + i;
			WARNING("grpc: Option `%s` not allowed in <%s> block.",
					child->key, ci->key);
		}

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
			pthread_create(&workers[i], /* attr = */ NULL,
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
