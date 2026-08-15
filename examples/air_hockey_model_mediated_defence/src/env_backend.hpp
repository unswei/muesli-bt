#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <memory>
#include <string>

#include "host_client.hpp"
#include "muslisp/env_api.hpp"

namespace air_hockey_demo {

class air_hockey_env_backend final : public muslisp::env_backend {
public:
    explicit air_hockey_env_backend(std::filesystem::path socket_path);

    [[nodiscard]] std::string backend_version() const override;
    [[nodiscard]] muslisp::env_backend_supports supports() const override;
    [[nodiscard]] std::string notes() const override;
    [[nodiscard]] muslisp::value info() const override;

    void configure(muslisp::value options) override;
    [[nodiscard]] muslisp::value reset(std::optional<std::int64_t> seed) override;
    [[nodiscard]] muslisp::value observe() override;
    void act(muslisp::value action) override;
    [[nodiscard]] bool step() override;

    void configure_host(const host_configuration& configuration);
    [[nodiscard]] public_state reset_host(std::optional<std::uint32_t> seed);
    [[nodiscard]] public_state observe_host();
    void act_target(const std::array<double, kActionDimension>& action);
    [[nodiscard]] host_step_result step_host();
    [[nodiscard]] const public_state& last_state() const;

private:
    [[nodiscard]] muslisp::value state_to_lisp(const public_state& state) const;

    mutable host_client client_;
    host_configuration configuration_;
    std::optional<public_state> last_state_;
};

void register_air_hockey_env_backend(const std::string& name,
                                     const std::shared_ptr<air_hockey_env_backend>& backend);

}  // namespace air_hockey_demo
