#pragma once

#include <string>
#include <unordered_map>

#include "muslisp/gc.hpp"

namespace muslisp {

struct env final : gc_node {
    explicit env(env_ptr parent_env = nullptr);

    env_ptr parent = nullptr;
    std::unordered_map<std::string, value> bindings;

    void gc_mark_children(gc& heap) override;
    [[nodiscard]] std::size_t gc_size_bytes() const override;
};

env_ptr make_env(env_ptr parent = nullptr);
void define(env_ptr scope, const std::string& name, value bound_value);
value lookup(env_ptr scope, const std::string& name);

}  // namespace muslisp
