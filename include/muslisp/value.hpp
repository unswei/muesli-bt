#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "muslisp/gc.hpp"

namespace muslisp {

using primitive_fn = std::function<value(const std::vector<value>&)>;

enum class value_type {
    nil,
    boolean,
    integer,
    floating,
    symbol,
    string,
    cons,
    primitive_fn,
    closure,
    vec,
    map,
    rng,
    bt_def,
    bt_instance
};

enum class map_key_type {
    symbol,
    string,
    integer,
    floating
};

struct map_key {
    map_key_type type = map_key_type::symbol;
    std::string text_data;
    std::int64_t integer_data = 0;
    double float_data = 0.0;

    [[nodiscard]] bool operator==(const map_key& other) const noexcept;
};

struct map_key_hash {
    [[nodiscard]] std::size_t operator()(const map_key& key) const noexcept;
};

using map_storage = std::unordered_map<map_key, value, map_key_hash>;

struct rng_state {
    std::uint64_t state = 0;
    bool has_spare_normal = false;
    double spare_normal = 0.0;
};

struct object final : gc_node {
    explicit object(value_type value_type_tag);

    value_type type = value_type::nil;
    bool boolean_data = false;
    std::int64_t integer_data = 0;
    double float_data = 0.0;
    std::string text_data;
    value car_data = nullptr;
    value cdr_data = nullptr;
    primitive_fn primitive_data;
    std::vector<std::string> closure_params_data;
    std::vector<value> closure_body_data;
    env_ptr closure_env_data = nullptr;
    std::vector<value> vec_data;
    map_storage map_data;
    std::shared_ptr<rng_state> rng_data;
    std::int64_t bt_handle_data = 0;

    void gc_mark_children(gc& heap) override;
    [[nodiscard]] std::size_t gc_size_bytes() const override;
};

value make_nil();
value make_boolean(bool v);
value make_integer(std::int64_t v);
value make_float(double v);
value make_symbol(const std::string& name);
value make_string(const std::string& text);
value make_cons(value car_value, value cdr_value);
value make_primitive(const std::string& name, primitive_fn fn);
value make_closure(const std::vector<std::string>& params, const std::vector<value>& body, env_ptr captured_env);
value make_vec(std::size_t capacity = 0);
value make_map();
value make_rng(std::uint64_t seed);
value make_bt_def(std::int64_t handle);
value make_bt_instance(std::int64_t handle);

[[nodiscard]] value_type type_of(value v);
[[nodiscard]] std::string_view type_name(value_type t);

[[nodiscard]] bool is_nil(value v);
[[nodiscard]] bool is_boolean(value v);
[[nodiscard]] bool is_integer(value v);
[[nodiscard]] bool is_float(value v);
[[nodiscard]] bool is_number(value v);
[[nodiscard]] bool is_symbol(value v);
[[nodiscard]] bool is_string(value v);
[[nodiscard]] bool is_cons(value v);
[[nodiscard]] bool is_primitive(value v);
[[nodiscard]] bool is_closure(value v);
[[nodiscard]] bool is_vec(value v);
[[nodiscard]] bool is_map(value v);
[[nodiscard]] bool is_rng(value v);
[[nodiscard]] bool is_bt_def(value v);
[[nodiscard]] bool is_bt_instance(value v);
[[nodiscard]] bool is_truthy(value v);

[[nodiscard]] bool boolean_value(value v);
[[nodiscard]] std::int64_t integer_value(value v);
[[nodiscard]] double float_value(value v);
[[nodiscard]] const std::string& symbol_name(value v);
[[nodiscard]] const std::string& string_value(value v);
[[nodiscard]] value car(value v);
[[nodiscard]] value cdr(value v);
[[nodiscard]] const primitive_fn& primitive_function(value v);
[[nodiscard]] const std::string& primitive_name(value v);
[[nodiscard]] const std::vector<std::string>& closure_params(value v);
[[nodiscard]] const std::vector<value>& closure_body(value v);
[[nodiscard]] env_ptr closure_env(value v);
[[nodiscard]] std::int64_t bt_handle(value v);

[[nodiscard]] value list_from_vector(const std::vector<value>& items);
[[nodiscard]] std::vector<value> vector_from_list(value list_value);
[[nodiscard]] bool is_proper_list(value list_value);
[[nodiscard]] bool eq_values(value lhs, value rhs);

void mark_interned_symbols(gc& heap);

}  // namespace muslisp
