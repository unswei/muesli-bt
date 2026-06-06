#include "ros2/extension.hpp"

#include <memory>

#include "ros2/backend.hpp"
#include "ros2/nav2_capability.hpp"
#include "muslisp/cap_api.hpp"
#include "muslisp/env_api.hpp"

namespace muslisp::integrations::ros2 {
namespace {

class ros2_extension final : public extension {
public:
    [[nodiscard]] std::string name() const override {
        return "integration.ros2";
    }

    void register_lisp(registrar& reg) const override {
        (void)reg;
        env_api_register_backend("ros2", make_backend());
        cap_api_register_backend("cap.navigation.v1", make_nav2_navigation_capability());
    }
};

}  // namespace

std::unique_ptr<extension> make_extension() {
    return std::make_unique<ros2_extension>();
}

}  // namespace muslisp::integrations::ros2
