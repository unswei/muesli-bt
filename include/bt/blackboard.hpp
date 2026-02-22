#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "bt/ast.hpp"

namespace bt {

using bb_value = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

struct bb_entry {
    bb_value value;
    std::uint64_t last_write_tick = 0;
    std::chrono::steady_clock::time_point last_write_ts{};
    node_id last_writer_node_id = 0;
    std::string last_writer_name;
};

class blackboard {
public:
    bool has(std::string_view key) const;
    const bb_entry* get(std::string_view key) const;
    bb_entry* get_mut(std::string_view key);

    void put(std::string key,
             bb_value value,
             std::uint64_t tick,
             std::chrono::steady_clock::time_point ts,
             node_id writer_node,
             std::string writer_name);

    std::vector<std::pair<std::string, bb_entry>> snapshot() const;
    void clear();

private:
    std::unordered_map<std::string, bb_entry> map_;
};

std::string bb_value_repr(const bb_value& value);
const char* bb_value_type_name(const bb_value& value) noexcept;

}  // namespace bt
