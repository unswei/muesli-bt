#pragma once

#include <memory>

#include "muslisp/cap_api.hpp"

namespace muslisp::integrations::ros2 {

std::shared_ptr<cap_backend> make_nav2_navigation_capability();

}  // namespace muslisp::integrations::ros2
