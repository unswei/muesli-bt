#pragma once

#include "muslisp/env.hpp"

namespace muslisp {

void install_env_capability_builtins(env_ptr global_env);
void reset_env_capability_runtime_state();

}  // namespace muslisp

