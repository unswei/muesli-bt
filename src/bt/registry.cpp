#include "bt/registry.hpp"

namespace bt {

void registry::register_condition(std::string name, condition_fn fn) {
    conditions_[std::move(name)] = std::move(fn);
}

void registry::register_action(std::string name, action_fn fn, action_halt_fn halt_fn) {
    if (halt_fn) {
        action_halts_[name] = std::move(halt_fn);
    } else {
        action_halts_.erase(name);
    }
    actions_[std::move(name)] = std::move(fn);
}

const condition_fn* registry::find_condition(std::string_view name) const {
    const auto it = conditions_.find(std::string(name));
    return it == conditions_.end() ? nullptr : &it->second;
}

const action_fn* registry::find_action(std::string_view name) const {
    const auto it = actions_.find(std::string(name));
    return it == actions_.end() ? nullptr : &it->second;
}

const action_halt_fn* registry::find_action_halt(std::string_view name) const {
    const auto it = action_halts_.find(std::string(name));
    return it == action_halts_.end() ? nullptr : &it->second;
}

void registry::clear() {
    conditions_.clear();
    actions_.clear();
    action_halts_.clear();
}

}  // namespace bt
