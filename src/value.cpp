#include "muslisp/value.hpp"

#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "muslisp/env.hpp"
#include "muslisp/error.hpp"

namespace muslisp {
namespace {

value make_object(value_type type) {
    return default_gc().allocate<object>(type);
}

void require_type(value v, value_type expected, const std::string& where) {
    if (!v || v->type != expected) {
        throw lisp_error(where + ": expected " + std::string(type_name(expected)));
    }
}

std::unordered_map<std::string, value>& symbol_table() {
    static std::unordered_map<std::string, value> table;
    return table;
}

std::mutex& symbol_table_mutex() {
    static std::mutex mutex;
    return mutex;
}

double number_to_double(value v) {
    if (is_integer(v)) {
        return static_cast<double>(integer_value(v));
    }
    if (is_float(v)) {
        return float_value(v);
    }
    throw lisp_error("expected numeric value");
}

}  // namespace

bool map_key::operator==(const map_key& other) const noexcept {
    if (type != other.type) {
        return false;
    }

    switch (type) {
        case map_key_type::symbol:
        case map_key_type::string:
            return text_data == other.text_data;
        case map_key_type::integer:
            return integer_data == other.integer_data;
        case map_key_type::floating:
            return float_data == other.float_data;
    }
    return false;
}

std::size_t map_key_hash::operator()(const map_key& key) const noexcept {
    auto combine = [](std::size_t lhs, std::size_t rhs) {
        constexpr std::size_t mix = 0x9e3779b97f4a7c15ull;
        return lhs ^ (rhs + mix + (lhs << 6u) + (lhs >> 2u));
    };

    std::size_t seed = std::hash<int>{}(static_cast<int>(key.type));
    switch (key.type) {
        case map_key_type::symbol:
        case map_key_type::string:
            seed = combine(seed, std::hash<std::string>{}(key.text_data));
            break;
        case map_key_type::integer:
            seed = combine(seed, std::hash<std::int64_t>{}(key.integer_data));
            break;
        case map_key_type::floating:
            seed = combine(seed, std::hash<double>{}(key.float_data));
            break;
    }
    return seed;
}

object::object(value_type value_type_tag) : type(value_type_tag) {}

void object::gc_mark_children(gc& heap) {
    switch (type) {
        case value_type::cons:
            heap.mark_value(car_data);
            heap.mark_value(cdr_data);
            break;
        case value_type::closure:
            for (value expr : closure_body_data) {
                heap.mark_value(expr);
            }
            heap.mark_env(closure_env_data);
            break;
        case value_type::vec:
            for (value elem : vec_data) {
                heap.mark_value(elem);
            }
            break;
        case value_type::map:
            for (const auto& [_, mapped] : map_data) {
                heap.mark_value(mapped);
            }
            break;
        case value_type::pq:
            for (const pq_entry& entry : pq_data) {
                heap.mark_value(entry.payload);
            }
            break;
        default:
            break;
    }
}

std::size_t object::gc_size_bytes() const {
    return sizeof(object);
}

value make_nil() {
    static value nil_value = [] {
        return make_object(value_type::nil);
    }();
    static bool rooted = [] {
        default_gc().add_root_slot(&nil_value);
        return true;
    }();
    (void)rooted;
    return nil_value;
}

value make_boolean(bool v) {
    static value true_value = [] {
        auto out = make_object(value_type::boolean);
        out->boolean_data = true;
        return out;
    }();
    static bool true_rooted = [] {
        default_gc().add_root_slot(&true_value);
        return true;
    }();
    (void)true_rooted;

    static value false_value = [] {
        auto out = make_object(value_type::boolean);
        out->boolean_data = false;
        return out;
    }();
    static bool false_rooted = [] {
        default_gc().add_root_slot(&false_value);
        return true;
    }();
    (void)false_rooted;

    return v ? true_value : false_value;
}

value make_integer(std::int64_t v) {
    auto out = make_object(value_type::integer);
    out->integer_data = v;
    return out;
}

value make_float(double v) {
    auto out = make_object(value_type::floating);
    out->float_data = v;
    return out;
}

value make_symbol(const std::string& name) {
    std::lock_guard<std::mutex> lock(symbol_table_mutex());
    auto& table = symbol_table();

    const auto found = table.find(name);
    if (found != table.end()) {
        return found->second;
    }

    auto sym = make_object(value_type::symbol);
    sym->text_data = name;
    table.emplace(name, sym);
    return sym;
}

value make_string(const std::string& text) {
    auto out = make_object(value_type::string);
    out->text_data = text;
    return out;
}

value make_cons(value car_value, value cdr_value) {
    auto out = make_object(value_type::cons);
    out->car_data = car_value;
    out->cdr_data = cdr_value;
    return out;
}

value make_primitive(const std::string& name, primitive_fn fn) {
    auto out = make_object(value_type::primitive_fn);
    out->text_data = name;
    out->primitive_data = std::move(fn);
    return out;
}

value make_closure(const std::vector<std::string>& params, const std::vector<value>& body, env_ptr captured_env) {
    auto out = make_object(value_type::closure);
    out->closure_params_data = params;
    out->closure_body_data = body;
    out->closure_env_data = captured_env;
    return out;
}

value make_vec(std::size_t capacity) {
    auto out = make_object(value_type::vec);
    out->vec_data.reserve(capacity);
    return out;
}

value make_map() {
    return make_object(value_type::map);
}

value make_pq(std::size_t capacity) {
    auto out = make_object(value_type::pq);
    out->pq_data.reserve(capacity);
    out->pq_next_sequence = 0;
    return out;
}

value make_rng(std::uint64_t seed) {
    auto out = make_object(value_type::rng);
    out->rng_data = std::make_shared<rng_state>();
    out->rng_data->state = seed;
    out->rng_data->has_spare_normal = false;
    out->rng_data->spare_normal = 0.0;
    return out;
}

value make_bt_def(std::int64_t handle) {
    auto out = make_object(value_type::bt_def);
    out->bt_handle_data = handle;
    return out;
}

value make_bt_instance(std::int64_t handle) {
    auto out = make_object(value_type::bt_instance);
    out->bt_handle_data = handle;
    return out;
}

value make_image_handle(std::int64_t handle) {
    auto out = make_object(value_type::image_handle);
    out->image_handle_data = handle;
    return out;
}

value make_blob_handle(std::int64_t handle) {
    auto out = make_object(value_type::blob_handle);
    out->blob_handle_data = handle;
    return out;
}

value_type type_of(value v) {
    if (!v) {
        throw lisp_error("null value");
    }
    return v->type;
}

std::string_view type_name(value_type t) {
    switch (t) {
        case value_type::nil:
            return "nil";
        case value_type::boolean:
            return "boolean";
        case value_type::integer:
            return "integer";
        case value_type::floating:
            return "float";
        case value_type::symbol:
            return "symbol";
        case value_type::string:
            return "string";
        case value_type::cons:
            return "cons";
        case value_type::primitive_fn:
            return "primitive_fn";
        case value_type::closure:
            return "closure";
        case value_type::vec:
            return "vec";
        case value_type::map:
            return "map";
        case value_type::pq:
            return "pq";
        case value_type::rng:
            return "rng";
        case value_type::bt_def:
            return "bt_def";
        case value_type::bt_instance:
            return "bt_instance";
        case value_type::image_handle:
            return "image_handle";
        case value_type::blob_handle:
            return "blob_handle";
    }
    return "unknown";
}

bool is_nil(value v) {
    return v && v->type == value_type::nil;
}

bool is_boolean(value v) {
    return v && v->type == value_type::boolean;
}

bool is_integer(value v) {
    return v && v->type == value_type::integer;
}

bool is_float(value v) {
    return v && v->type == value_type::floating;
}

bool is_number(value v) {
    return is_integer(v) || is_float(v);
}

bool is_symbol(value v) {
    return v && v->type == value_type::symbol;
}

bool is_string(value v) {
    return v && v->type == value_type::string;
}

bool is_cons(value v) {
    return v && v->type == value_type::cons;
}

bool is_primitive(value v) {
    return v && v->type == value_type::primitive_fn;
}

bool is_closure(value v) {
    return v && v->type == value_type::closure;
}

bool is_vec(value v) {
    return v && v->type == value_type::vec;
}

bool is_map(value v) {
    return v && v->type == value_type::map;
}

bool is_pq(value v) {
    return v && v->type == value_type::pq;
}

bool is_rng(value v) {
    return v && v->type == value_type::rng;
}

bool is_bt_def(value v) {
    return v && v->type == value_type::bt_def;
}

bool is_bt_instance(value v) {
    return v && v->type == value_type::bt_instance;
}

bool is_image_handle(value v) {
    return v && v->type == value_type::image_handle;
}

bool is_blob_handle(value v) {
    return v && v->type == value_type::blob_handle;
}

bool is_truthy(value v) {
    if (is_nil(v)) {
        return false;
    }
    if (is_boolean(v) && !boolean_value(v)) {
        return false;
    }
    return true;
}

bool boolean_value(value v) {
    require_type(v, value_type::boolean, "boolean_value");
    return v->boolean_data;
}

std::int64_t integer_value(value v) {
    require_type(v, value_type::integer, "integer_value");
    return v->integer_data;
}

double float_value(value v) {
    require_type(v, value_type::floating, "float_value");
    return v->float_data;
}

const std::string& symbol_name(value v) {
    require_type(v, value_type::symbol, "symbol_name");
    return v->text_data;
}

const std::string& string_value(value v) {
    require_type(v, value_type::string, "string_value");
    return v->text_data;
}

value car(value v) {
    require_type(v, value_type::cons, "car");
    return v->car_data;
}

value cdr(value v) {
    require_type(v, value_type::cons, "cdr");
    return v->cdr_data;
}

const primitive_fn& primitive_function(value v) {
    require_type(v, value_type::primitive_fn, "primitive_function");
    return v->primitive_data;
}

const std::string& primitive_name(value v) {
    require_type(v, value_type::primitive_fn, "primitive_name");
    return v->text_data;
}

const std::vector<std::string>& closure_params(value v) {
    require_type(v, value_type::closure, "closure_params");
    return v->closure_params_data;
}

const std::vector<value>& closure_body(value v) {
    require_type(v, value_type::closure, "closure_body");
    return v->closure_body_data;
}

env_ptr closure_env(value v) {
    require_type(v, value_type::closure, "closure_env");
    return v->closure_env_data;
}

std::int64_t bt_handle(value v) {
    if (!is_bt_def(v) && !is_bt_instance(v)) {
        throw lisp_error("bt_handle: expected bt_def or bt_instance");
    }
    return v->bt_handle_data;
}

std::int64_t image_handle_id(value v) {
    require_type(v, value_type::image_handle, "image_handle_id");
    return v->image_handle_data;
}

std::int64_t blob_handle_id(value v) {
    require_type(v, value_type::blob_handle, "blob_handle_id");
    return v->blob_handle_data;
}

value list_from_vector(const std::vector<value>& items) {
    value out = make_nil();
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        out = make_cons(*it, out);
    }
    return out;
}

std::vector<value> vector_from_list(value list_value) {
    std::vector<value> out;
    value cursor = list_value;
    while (!is_nil(cursor)) {
        if (!is_cons(cursor)) {
            throw lisp_error("expected proper list");
        }
        out.push_back(car(cursor));
        cursor = cdr(cursor);
    }
    return out;
}

bool is_proper_list(value list_value) {
    value cursor = list_value;
    while (!is_nil(cursor)) {
        if (!is_cons(cursor)) {
            return false;
        }
        cursor = cdr(cursor);
    }
    return true;
}

bool eq_values(value lhs, value rhs) {
    if (lhs == rhs) {
        return true;
    }
    if (!lhs || !rhs) {
        return false;
    }

    if (is_boolean(lhs) && is_boolean(rhs)) {
        return boolean_value(lhs) == boolean_value(rhs);
    }
    if (is_number(lhs) && is_number(rhs)) {
        if (is_integer(lhs) && is_integer(rhs)) {
            return integer_value(lhs) == integer_value(rhs);
        }
        return number_to_double(lhs) == number_to_double(rhs);
    }

    return false;
}

void mark_interned_symbols(gc& heap) {
    std::lock_guard<std::mutex> lock(symbol_table_mutex());
    for (const auto& [_, sym] : symbol_table()) {
        heap.mark_value(sym);
    }
}

}  // namespace muslisp
