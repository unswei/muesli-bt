#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "muslisp/value.hpp"

namespace muslisp {

struct env_backend_supports {
    bool reset = false;
    bool debug_draw = false;
    bool headless = false;
    bool realtime_pacing = false;
    bool deterministic_seed = false;
};

class env_backend {
public:
    virtual ~env_backend() = default;

    [[nodiscard]] virtual std::string backend_version() const { return {}; }
    [[nodiscard]] virtual env_backend_supports supports() const = 0;
    [[nodiscard]] virtual std::string notes() const { return {}; }

    virtual void configure(value opts) = 0;
    [[nodiscard]] virtual value reset(std::optional<std::int64_t> seed) = 0;
    [[nodiscard]] virtual value observe() = 0;
    virtual void act(value action) = 0;
    [[nodiscard]] virtual bool step() = 0;
    virtual void debug_draw(value payload) {
        (void)payload;
    }
};

void env_api_reset();
void env_api_register_backend(const std::string& name, std::shared_ptr<env_backend> backend);
std::vector<std::string> env_api_registered_backends();
[[nodiscard]] bool env_api_is_attached();
[[nodiscard]] std::string env_api_attached_backend_name();
[[nodiscard]] std::shared_ptr<env_backend> env_api_attached_backend();
void env_api_attach(const std::string& name);
void env_api_detach();

}  // namespace muslisp
