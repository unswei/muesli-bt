#include "muslisp/env.hpp"

#include "muslisp/error.hpp"

namespace muslisp {

env::env(env_ptr parent_env) : parent(parent_env) {}

void env::gc_mark_children(gc& heap) {
    heap.mark_env(parent);
    for (const auto& [_, bound] : bindings) {
        heap.mark_value(bound);
    }
}

std::size_t env::gc_size_bytes() const {
    return sizeof(env);
}

env_ptr make_env(env_ptr parent) {
    return default_gc().allocate<env>(parent);
}

void define(env_ptr scope, const std::string& name, value bound_value) {
    if (!scope) {
        throw lisp_error("define: null environment");
    }
    scope->bindings[name] = bound_value;
}

value lookup(env_ptr scope, const std::string& name) {
    for (env_ptr cursor = scope; cursor; cursor = cursor->parent) {
        const auto it = cursor->bindings.find(name);
        if (it != cursor->bindings.end()) {
            return it->second;
        }
    }
    throw lisp_error("unbound symbol: " + name);
}

}  // namespace muslisp
