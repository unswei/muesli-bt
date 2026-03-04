#pragma once

#include <memory>

#include "muslisp/extensions.hpp"

namespace muslisp::integrations::ros2 {

std::unique_ptr<extension> make_extension();

}  // namespace muslisp::integrations::ros2
