#pragma once

#include <string>

#include "muslisp/value.hpp"

namespace muslisp {

std::string print_value(value v);
std::string write_value(value v);

}  // namespace muslisp
