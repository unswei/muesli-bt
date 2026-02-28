#pragma once

#include <string>

#include "muslisp/env.hpp"
#include "muslisp/value.hpp"

namespace muslisp {

class registrar {
public:
    explicit registrar(env_ptr global_env);

    void register_builtin(const std::string& full_name,
                          primitive_fn fn,
                          const std::string& docstring = {},
                          const std::string& arity_or_signature = {});
    void register_value(const std::string& full_name, value bound_value, const std::string& docstring = {});

private:
    void ensure_registerable_name(const std::string& full_name, const char* where) const;

    env_ptr global_env_;
};

using extension_register_hook_fn = void (*)(registrar* r, void* user);

struct runtime_config {
    extension_register_hook_fn extension_register_hook = nullptr;
    void* extension_register_user = nullptr;
};

}  // namespace muslisp
