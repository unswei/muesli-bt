#pragma once

#include <memory>

#include "muslisp/env_api.hpp"

namespace muslisp::integrations::ros2 {

std::shared_ptr<env_backend> make_backend();

}  // namespace muslisp::integrations::ros2
