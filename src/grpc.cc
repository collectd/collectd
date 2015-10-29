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
}

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
	if (s.length())
		sstrncpy(vl->plugin_instance, s.c_str(), sizeof(vl->plugin_instance));

	s = msg.type_instance();
	if (s.length())
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
					grpc::string("unkown value type"));
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
 * request call objects
 */

class Call
{
public:
	Call(collectd::Collectd::AsyncService *service, grpc::ServerCompletionQueue *cq)
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

	collectd::Collectd::AsyncService *service_;
	grpc::ServerCompletionQueue *cq_;
	grpc::ServerContext ctx_;

private:
	enum CallStatus { CREATE, PROCESS, FINISH };
	CallStatus status_;
}; /* class Call */

class DispatchValuesCall : public Call
{
public:
	DispatchValuesCall(collectd::Collectd::AsyncService *service, grpc::ServerCompletionQueue *cq)
		: Call(service, cq), responder_(&ctx_)
	{
		Handle();
	} /* DispatchValuesCall() */

	virtual ~DispatchValuesCall()
	{ }

private:
	void Create()
	{
		service_->RequestDispatchValues(&ctx_, &request_, &responder_, cq_, cq_, this);
	} /* Create() */

	void Process()
	{
		// Add a new request object to the queue.
		new DispatchValuesCall(service_, cq_);

		value_list_t vl = VALUE_LIST_INIT;
		auto status = unmarshal_value_list(request_.values(), &vl);
		if (!status.ok()) {
			responder_.Finish(reply_, status, this);
			return;
		}

		if (plugin_dispatch_values(&vl))
			status = grpc::Status(grpc::StatusCode::INTERNAL,
					grpc::string("failed to enqueue values for writing"));

		responder_.Finish(reply_, status, this);
	} /* Process() */

	void Finish()
	{
		delete this;
	} /* Finish() */

	collectd::DispatchValuesRequest request_;
	collectd::DispatchValuesReply reply_;

	grpc::ServerAsyncResponseWriter<collectd::DispatchValuesReply> responder_;
};

/*
 * gRPC server implementation
 */

class CollectdServer final
{
public:
	void Start()
	{
		// TODO: make configurable
		std::string addr("0.0.0.0:50051");

		// TODO: make configurable
		auto auth = grpc::InsecureServerCredentials();

		grpc::ServerBuilder builder;
		builder.AddListeningPort(addr, auth);
		builder.RegisterAsyncService(&service_);
		cq_ = builder.AddCompletionQueue();
		server_ = builder.BuildAndStart();

		INFO("grpc: Listening on %s", addr.c_str());
	} /* Start() */

	void Shutdown()
	{
		server_->Shutdown();
		cq_->Shutdown();
	} /* Shutdown() */

	void Mainloop()
	{
		// Register request types.
		new DispatchValuesCall(&service_, cq_.get());

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
	collectd::Collectd::AsyncService service_;

	std::unique_ptr<grpc::Server> server_;
	std::unique_ptr<grpc::ServerCompletionQueue> cq_;
}; /* class CollectdServer */

static CollectdServer *server = nullptr;

/*
 * collectd plugin interface
 */

extern "C" {
	static pthread_t *workers;
	static size_t workers_num;

	static void *worker_thread(void *arg)
	{
		CollectdServer *s = (CollectdServer *)arg;
		s->Mainloop();
		return NULL;
	} /* worker_thread() */

	static int c_grpc_init(void)
	{
		server = new CollectdServer();
		size_t i;

		if (! server) {
			ERROR("grpc: Failed to create server");
			return -1;
		}

		workers = (pthread_t *)calloc(5, sizeof(*workers));
		if (! workers) {
			delete server;
			server = nullptr;

			ERROR("grpc: Failed to allocate worker threads");
			return -1;
		}
		workers_num = 5;

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
		plugin_register_init("grpc", c_grpc_init);
		plugin_register_shutdown("grpc", c_grpc_shutdown);
	} /* module_register() */
} /* extern "C" */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */
