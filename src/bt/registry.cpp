#include "bt/registry.hpp"

namespace bt {

void registry::register_condition(std::string name, condition_fn fn) {
    conditions_[std::move(name)] = std::move(fn);
}

void registry::register_action(std::string name, action_fn fn) {
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

void registry::clear() {
    conditions_.clear();
    actions_.clear();
}

}  // namespace bt
