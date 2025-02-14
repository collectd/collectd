#include "s3sender_impl.h"

#include <string>
#include <memory>
#include <vector>

#include <ipc/broker.h>
#include <ipc/connection.h>

// using namespace vizio::ipc;
// static std::unique_ptr<Broker> broker = nullptr;
// static std::unique_ptr<Connection> connection = nullptr;

// static CREATE_LOGGER("s3sender", _DEBUG_);

// bool initializeIpc(const std::string& brokerAddress) {
//     if (broker != nullptr) {
//         return true;
//     }

//     broker = std::make_unique<Broker>(); // getDefaultBroker();
//     broker.initialize();
//     connection = broker->connectToServer(brokerAddress);

//     if (!connection) {
//         ERROR(0, "Failed to connect to {}", brokerAddress);
//         return false;
//     }
//     INFO("Connection established successfully");
//     return true;
// }

// static bool sendMetrics([[maybe_unused]]const std::vector<std::string>& names, [[maybe_unused]]const std::vector<double>& values) {
//     if (!connection) {
//         ERROR(0, "Connection is not established");
//         return false;
//     }


//     // std::string metrics;
//     // if (!connection->send(metrics, "analyticstokinesis", false)) {
//     //     ERROR(0, "Failed to send");
//     //     return false;
//     // }

//     return true;
// }


extern "C"
int s3_write(const char *data) {
    vizio::ipc::Message<std::string> message(data);
    return 0;
}
