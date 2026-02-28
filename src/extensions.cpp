#include "muslisp/extensions.hpp"

#include <utility>

#include "muslisp/error.hpp"

namespace muslisp {

registrar::registrar(env_ptr global_env) : global_env_(global_env) {
    if (!global_env_) {
        throw lisp_error("registrar: null global environment");
    }
}

void registrar::register_builtin(const std::string& full_name,
                                 primitive_fn fn,
                                 const std::string& docstring,
                                 const std::string& arity_or_signature) {
    (void)docstring;
    (void)arity_or_signature;
    ensure_registerable_name(full_name, "registrar.register_builtin");
    define(global_env_, full_name, make_primitive(full_name, std::move(fn)));
}

void registrar::register_value(const std::string& full_name, value bound_value, const std::string& docstring) {
    (void)docstring;
    ensure_registerable_name(full_name, "registrar.register_value");
    if (!bound_value) {
        throw lisp_error("registrar.register_value: bound value must not be null");
    }
    define(global_env_, full_name, bound_value);
}

void registrar::ensure_registerable_name(const std::string& full_name, const char* where) const {
    if (full_name.empty()) {
        throw lisp_error(std::string(where) + ": name must not be empty");
    }
    if (global_env_->bindings.find(full_name) != global_env_->bindings.end()) {
        throw lisp_error(std::string(where) + ": symbol already defined: " + full_name);
    }
}

}  // namespace muslisp
