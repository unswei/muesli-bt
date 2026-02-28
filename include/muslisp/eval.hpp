#pragma once

#include <string_view>
#include <vector>

#include "muslisp/builtins.hpp"
#include "muslisp/env.hpp"
#include "muslisp/extensions.hpp"

namespace muslisp {

value eval(value expr, env_ptr scope);
value eval_sequence(const std::vector<value>& exprs, env_ptr scope);
value eval_source(std::string_view source, env_ptr scope);
env_ptr create_global_env();
env_ptr create_global_env(const runtime_config& config);

}  // namespace muslisp
