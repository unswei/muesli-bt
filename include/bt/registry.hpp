#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "bt/ast.hpp"
#include "bt/status.hpp"
#include "muslisp/value.hpp"

namespace bt {

struct tick_context;
struct node_memory;

using condition_fn = std::function<bool(tick_context&, std::span<const muslisp::value> args)>;
using action_fn = std::function<status(tick_context&, node_id, node_memory&, std::span<const muslisp::value> args)>;
using action_halt_fn = std::function<void(tick_context&, node_id, node_memory&)>;

struct transparent_string_hash {
    using is_transparent = void;

    std::size_t operator()(std::string_view value) const noexcept {
        return std::hash<std::string_view>{}(value);
    }
};

struct transparent_string_equal {
    using is_transparent = void;

    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return lhs == rhs;
    }
};

class registry {
public:
    void register_condition(std::string name, condition_fn fn);
    void register_action(std::string name, action_fn fn, action_halt_fn halt_fn = {});

    const condition_fn* find_condition(std::string_view name) const;
    const action_fn* find_action(std::string_view name) const;
    const action_halt_fn* find_action_halt(std::string_view name) const;

    void clear();

private:
    std::unordered_map<std::string, condition_fn, transparent_string_hash, transparent_string_equal> conditions_;
    std::unordered_map<std::string, action_fn, transparent_string_hash, transparent_string_equal> actions_;
    std::unordered_map<std::string, action_halt_fn, transparent_string_hash, transparent_string_equal> action_halts_;
};

}  // namespace bt
