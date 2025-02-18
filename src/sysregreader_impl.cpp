#include "sysregreader_impl.h"
#include "plugin.h"
#include "utils/common/common.h"
#include <string>
#include <memory>
#include <stdexcept>
#include <vector>

class SysRegReader {
public:
    SysRegReader() 
        : interval_(10) {}
    
    ~SysRegReader() = default;

    int configure(const oconfig_item_t *ci) {
        try {
            for (int i = 0; i < ci->children_num; i++) {
                oconfig_item_t *child = ci->children + i;

                INFO("sysregreader plugin: Processing config key: %s", child->key);

                if (strcasecmp("Keys", child->key) == 0) {
                    registry_keys_.clear();
                    for (int j = 0; j < child->values_num; j++) {
                        if (child->values[j].type != OCONFIG_TYPE_STRING) {
                            WARNING("sysregreader plugin: Keys value must be a string");
                            continue;
                        }
                        std::string key = child->values[j].value.string;
                        registry_keys_.push_back(key);
                        INFO("sysregreader plugin: Added registry key: '%s'", key.c_str());
                    }
                }
                else if (strcasecmp("Interval", child->key) == 0) {
                    if (child->values_num != 1 || child->values[0].type != OCONFIG_TYPE_NUMBER) {
                        WARNING("sysregreader plugin: Interval requires a numeric argument");
                        continue;
                    }
                    interval_ = child->values[0].value.number;
                    INFO("sysregreader plugin: Set interval to %d seconds", interval_);
                }
                else {
                    WARNING("sysregreader plugin: Unknown config option '%s'", child->key);
                }
            }
            return 0;
        } catch (const std::exception& e) {
            ERROR("sysregreader plugin: Configuration error: %s", e.what());
            return -1;
        }
    }

    bool init() {
        try {
            if (registry_keys_.empty()) {
                WARNING("sysregreader plugin: No registry keys configured");
                return false;
            }
            INFO("sysregreader plugin: Initialized with %zu keys, interval %d seconds", 
                 registry_keys_.size(), interval_);
            return true;
        } catch (const std::exception& e) {
            ERROR("sysregreader plugin: Init failed - %s", e.what());
            return false;
        }
    }

    bool read() {
        try {
            INFO("sysregreader plugin: Reading values for %zu keys", registry_keys_.size());
            for (const auto& key : registry_keys_) {
                INFO("sysregreader plugin: Configured key: '%s'", key.c_str());
            }
            return true;
        } catch (const std::exception& e) {
            ERROR("sysregreader plugin: Read failed - %s", e.what());
            return false;
        }
    }

    bool shutdown() {
        try {
            registry_keys_.clear();
            INFO("sysregreader plugin: Shutdown complete");
            return true;
        } catch (const std::exception& e) {
            ERROR("sysregreader plugin: Shutdown failed - %s", e.what());
            return false;
        }
    }

private:
    std::vector<std::string> registry_keys_;
    int interval_;
};

extern "C" {

void* sr_create_implementation() {
    try {
        return new SysRegReader();
    } catch (const std::exception& e) {
        ERROR("sysregreader plugin: Failed to create implementation - %s", e.what());
        return nullptr;
    }
}

void sr_destroy_implementation(void *impl) {
    delete static_cast<SysRegReader*>(impl);
}

int sr_impl_configure(void *impl, const oconfig_item_t *ci) {
    try {
        auto reader = static_cast<SysRegReader*>(impl);
        return reader->configure(ci);
    } catch (const std::exception& e) {
        ERROR("sysregreader plugin: Configure failed - %s", e.what());
        return -1;
    }
}

int sr_impl_init(void *impl) {
    try {
        auto reader = static_cast<SysRegReader*>(impl);
        return reader->init() ? 0 : -1;
    } catch (const std::exception& e) {
        ERROR("sysregreader plugin: Init failed - %s", e.what());
        return -1;
    }
}

int sr_impl_read(void *impl) {
    try {
        auto reader = static_cast<SysRegReader*>(impl);
        return reader->read() ? 0 : -1;
    } catch (const std::exception& e) {
        ERROR("sysregreader plugin: Read failed - %s", e.what());
        return -1;
    }
}

int sr_impl_shutdown(void *impl) {
    try {
        auto reader = static_cast<SysRegReader*>(impl);
        return reader->shutdown() ? 0 : -1;
    } catch (const std::exception& e) {
        ERROR("sysregreader plugin: Shutdown failed - %s", e.what());
        return -1;
    }
}

}