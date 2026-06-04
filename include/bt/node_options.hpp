#pragma once

#include <span>
#include <string_view>

namespace bt {

enum class option_value_kind {
    text,
    integer,
    number,
    boolean
};

struct node_option_spec {
    std::string_view name;
    option_value_kind kind;
    std::string_view default_value;
    bool required = false;
    std::span<const std::string_view> aliases{};
    std::span<const std::string_view> enum_values{};
};

struct node_option_schema {
    std::string_view node_name;
    std::span<const node_option_spec> options;
};

const node_option_schema* find_node_option_schema(std::string_view node_name) noexcept;
const node_option_spec* find_node_option_spec(const node_option_schema& schema, std::string_view option_name) noexcept;
std::string_view canonical_node_option_name(const node_option_schema& schema, std::string_view option_name) noexcept;

}  // namespace bt
