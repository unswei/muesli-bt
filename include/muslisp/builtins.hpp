#pragma once

#include "muslisp/env.hpp"

namespace muslisp {

void install_core_builtins(env_ptr global_env);
void install_racecar_demo_builtins(env_ptr env);

}  // namespace muslisp
