#pragma once

#include <stdexcept>

#include "bt/ast.hpp"
#include "muslisp/value.hpp"

namespace bt {

class bt_compile_error : public std::runtime_error {
public:
    explicit bt_compile_error(const std::string& message) : std::runtime_error(message) {}
};

definition compile_definition(muslisp::value form);

}  // namespace bt
