#include "muslisp/cap_api.hpp"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace muslisp {
namespace {

class cap_api_registry {
public:
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        backends_.clear();
    }

    void register_backend(const std::string& capability, std::shared_ptr<cap_backend> backend) {
        if (capability.empty()) {
            throw std::invalid_argument("cap.register-backend: capability must not be empty");
        }
        if (!backend) {
            throw std::invalid_argument("cap.register-backend: backend pointer must not be null");
        }
        const bt::capability_descriptor descriptor = backend->describe();
        if (descriptor.name != capability) {
            throw std::invalid_argument("cap.register-backend: descriptor name must match capability");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        backends_[capability] = std::move(backend);
    }

    std::vector<std::string> capability_names() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(backends_.size());
        for (const auto& [name, _] : backends_) {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    std::optional<bt::capability_descriptor> describe(const std::string& capability) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = backends_.find(capability);
        if (it == backends_.end()) {
            return std::nullopt;
        }
        return it->second->describe();
    }

    bool has_backend(const std::string& capability) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return backends_.find(capability) != backends_.end();
    }

    value call(const std::string& capability, value request_map) const {
        std::shared_ptr<cap_backend> backend;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = backends_.find(capability);
            if (it == backends_.end()) {
                throw std::runtime_error("cap.call: no registered backend for capability: " + capability);
            }
            backend = it->second;
        }
        return backend->call(request_map);
    }

private:
    mutable std::mutex mutex_{};
    std::unordered_map<std::string, std::shared_ptr<cap_backend>> backends_{};
};

cap_api_registry& registry() {
    static cap_api_registry r;
    return r;
}

}  // namespace

void cap_api_reset() {
    registry().reset();
}

void cap_api_register_backend(const std::string& capability, std::shared_ptr<cap_backend> backend) {
    registry().register_backend(capability, std::move(backend));
}

std::vector<std::string> cap_api_registered_capabilities() {
    return registry().capability_names();
}

std::optional<bt::capability_descriptor> cap_api_describe(const std::string& capability) {
    return registry().describe(capability);
}

bool cap_api_has_backend(const std::string& capability) {
    return registry().has_backend(capability);
}

value cap_api_call(const std::string& capability, value request_map) {
    return registry().call(capability, request_map);
}

}  // namespace muslisp
