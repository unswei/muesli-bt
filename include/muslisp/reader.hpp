#pragma once

#include <string_view>
#include <vector>

#include "muslisp/value.hpp"

namespace muslisp {

value read_one(std::string_view source);
std::vector<value> read_all(std::string_view source);

}  // namespace muslisp
