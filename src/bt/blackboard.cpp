#include "bt/blackboard.hpp"

#include <sstream>

namespace bt {

bool blackboard::has(std::string_view key) const {
    return map_.find(std::string(key)) != map_.end();
}

const bb_entry* blackboard::get(std::string_view key) const {
    const auto it = map_.find(std::string(key));
    return it == map_.end() ? nullptr : &it->second;
}

bb_entry* blackboard::get_mut(std::string_view key) {
    const auto it = map_.find(std::string(key));
    return it == map_.end() ? nullptr : &it->second;
}

void blackboard::put(std::string key,
                     bb_value value,
                     std::uint64_t tick,
                     std::chrono::steady_clock::time_point ts,
                     node_id writer_node,
                     std::string writer_name) {
    bb_entry& entry = map_[std::move(key)];
    entry.value = std::move(value);
    entry.last_write_tick = tick;
    entry.last_write_ts = ts;
    entry.last_writer_node_id = writer_node;
    entry.last_writer_name = std::move(writer_name);
}

std::vector<std::pair<std::string, bb_entry>> blackboard::snapshot() const {
    std::vector<std::pair<std::string, bb_entry>> out;
    out.reserve(map_.size());
    for (const auto& [key, entry] : map_) {
        out.emplace_back(key, entry);
    }
    return out;
}

void blackboard::clear() {
    map_.clear();
}

std::string bb_value_repr(const bb_value& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "nil";
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "#t" : "#f";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream out;
                out << v;
                return out.str();
            } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                std::ostringstream out;
                out << '[';
                for (std::size_t i = 0; i < v.size(); ++i) {
                    if (i != 0) {
                        out << ',';
                    }
                    out << v[i];
                }
                out << ']';
                return out.str();
            } else if constexpr (std::is_same_v<T, image_handle_ref>) {
                return std::string("image_handle(") + std::to_string(v.id) + ")";
            } else if constexpr (std::is_same_v<T, blob_handle_ref>) {
                return std::string("blob_handle(") + std::to_string(v.id) + ")";
            } else {
                return v;
            }
        },
        value);
}

const char* bb_value_type_name(const bb_value& value) noexcept {
    switch (value.index()) {
        case 0:
            return "nil";
        case 1:
            return "bool";
        case 2:
            return "int64";
        case 3:
            return "float64";
        case 4:
            return "string";
        case 5:
            return "float64[]";
        case 6:
            return "image_handle";
        case 7:
            return "blob_handle";
        default:
            return "unknown";
    }
}

}  // namespace bt
