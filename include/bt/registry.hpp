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

class registry {
public:
    void register_condition(std::string name, condition_fn fn);
    void register_action(std::string name, action_fn fn);

    const condition_fn* find_condition(std::string_view name) const;
    const action_fn* find_action(std::string_view name) const;

    void clear();

private:
    std::unordered_map<std::string, condition_fn> conditions_;
    std::unordered_map<std::string, action_fn> actions_;
};

}  // namespace bt
