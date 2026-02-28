#include "muslisp/env_api.hpp"

#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace muslisp {
namespace {

class env_api_registry {
public:
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        backends_.clear();
        attached_name_.clear();
        attached_backend_.reset();
    }

    void register_backend(const std::string& name, std::shared_ptr<env_backend> backend) {
        if (name.empty()) {
            throw std::invalid_argument("env.register-backend: backend name must not be empty");
        }
        if (!backend) {
            throw std::invalid_argument("env.register-backend: backend pointer must not be null");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        backends_[name] = std::move(backend);
    }

    std::vector<std::string> backend_names() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(backends_.size());
        for (const auto& [name, _] : backends_) {
            names.push_back(name);
        }
        return names;
    }

    bool is_attached() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<bool>(attached_backend_);
    }

    std::string attached_name() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return attached_name_;
    }

    std::shared_ptr<env_backend> attached_backend() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return attached_backend_;
    }

    void attach(const std::string& name) {
        if (name.empty()) {
            throw std::invalid_argument("env.attach: backend name must not be empty");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (attached_backend_) {
            throw std::runtime_error("env.attach: backend already attached: " + attached_name_);
        }
        const auto it = backends_.find(name);
        if (it == backends_.end()) {
            throw std::runtime_error("env.attach: unknown backend: " + name);
        }
        attached_name_ = name;
        attached_backend_ = it->second;
    }

    void detach() {
        std::lock_guard<std::mutex> lock(mutex_);
        attached_name_.clear();
        attached_backend_.reset();
    }

private:
    mutable std::mutex mutex_{};
    std::unordered_map<std::string, std::shared_ptr<env_backend>> backends_{};
    std::string attached_name_{};
    std::shared_ptr<env_backend> attached_backend_{};
};

env_api_registry& registry() {
    static env_api_registry r;
    return r;
}

}  // namespace

void env_api_reset() {
    registry().reset();
}

void env_api_register_backend(const std::string& name, std::shared_ptr<env_backend> backend) {
    registry().register_backend(name, std::move(backend));
}

std::vector<std::string> env_api_registered_backends() {
    return registry().backend_names();
}

bool env_api_is_attached() {
    return registry().is_attached();
}

std::string env_api_attached_backend_name() {
    return registry().attached_name();
}

std::shared_ptr<env_backend> env_api_attached_backend() {
    return registry().attached_backend();
}

void env_api_attach(const std::string& name) {
    registry().attach(name);
}

void env_api_detach() {
    registry().detach();
}

}  // namespace muslisp

