#pragma once

#include <string>

#include "bt/ast.hpp"

namespace bt {

void save_definition_binary(const definition& def, const std::string& path);
definition load_definition_binary(const std::string& path);
void export_definition_dot(const definition& def, const std::string& path);

}  // namespace bt
