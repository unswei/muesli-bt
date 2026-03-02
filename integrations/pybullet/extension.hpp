#pragma once

#include <memory>

#include "muslisp/extensions.hpp"

namespace muslisp::integrations::pybullet {

std::unique_ptr<extension> make_extension();

}  // namespace muslisp::integrations::pybullet
