#include "muslisp/builtins.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

#include "bt/blackboard.hpp"
#include "bt/compiler.hpp"
#include "bt/planner.hpp"
#include "bt/runtime_host.hpp"
#include "bt/serialisation.hpp"
#include "bt/status.hpp"
#include "bt/vla.hpp"
#include "muslisp/error.hpp"
#include "muslisp/env_builtins.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"
#include "muslisp/reader.hpp"

namespace muslisp {
namespace {

void require_arity(const std::string& name, const std::vector<value>& args, std::size_t expected) {
    if (args.size() != expected) {
        throw lisp_error(name + ": expected " + std::to_string(expected) + " arguments, got " + std::to_string(args.size()));
    }
}

void require_min_arity(const std::string& name, const std::vector<value>& args, std::size_t min_expected) {
    if (args.size() < min_expected) {
        throw lisp_error(name + ": expected at least " + std::to_string(min_expected) + " arguments, got " +
                         std::to_string(args.size()));
    }
}

std::int64_t require_int_arg(value v, const std::string& where) {
    if (!is_integer(v)) {
        throw lisp_error(where + ": expected integer");
    }
    return integer_value(v);
}

value require_vec_arg(value v, const std::string& where) {
    if (!is_vec(v)) {
        throw lisp_error(where + ": expected vec");
    }
    return v;
}

value require_map_arg(value v, const std::string& where) {
    if (!is_map(v)) {
        throw lisp_error(where + ": expected map");
    }
    return v;
}

value require_pq_arg(value v, const std::string& where) {
    if (!is_pq(v)) {
        throw lisp_error(where + ": expected pq");
    }
    return v;
}

value require_rng_arg(value v, const std::string& where) {
    if (!is_rng(v) || !v->rng_data) {
        throw lisp_error(where + ": expected rng");
    }
    return v;
}

std::int64_t to_int64_size(std::size_t size, const std::string& where) {
    if (size > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        throw lisp_error(where + ": size exceeds int64 range");
    }
    return static_cast<std::int64_t>(size);
}

std::size_t require_non_negative_index(value v, std::size_t upper_bound, const std::string& where) {
    if (!is_integer(v)) {
        throw lisp_error(where + ": expected integer index");
    }

    const std::int64_t index = integer_value(v);
    if (index < 0 || static_cast<std::size_t>(index) >= upper_bound) {
        throw lisp_error(where + ": index out of range");
    }
    return static_cast<std::size_t>(index);
}

std::size_t require_non_negative_capacity(value v, const std::string& where) {
    if (!is_integer(v)) {
        throw lisp_error(where + ": expected integer capacity");
    }

    const std::int64_t raw = integer_value(v);
    if (raw < 0) {
        throw lisp_error(where + ": expected non-negative capacity");
    }
    return static_cast<std::size_t>(raw);
}

map_key map_key_from_value(value key, const std::string& where) {
    if (is_symbol(key)) {
        map_key out;
        out.type = map_key_type::symbol;
        out.text_data = symbol_name(key);
        return out;
    }
    if (is_string(key)) {
        map_key out;
        out.type = map_key_type::string;
        out.text_data = string_value(key);
        return out;
    }
    if (is_integer(key)) {
        map_key out;
        out.type = map_key_type::integer;
        out.integer_data = integer_value(key);
        return out;
    }
    if (is_float(key)) {
        const double numeric = float_value(key);
        if (std::isnan(numeric)) {
            throw lisp_error(where + ": float keys must not be NaN");
        }
        map_key out;
        out.type = map_key_type::floating;
        out.float_data = (numeric == 0.0) ? 0.0 : numeric;
        return out;
    }

    throw lisp_error(where + ": expected key as symbol, string, integer, or float");
}

value map_key_to_value(const map_key& key) {
    switch (key.type) {
        case map_key_type::symbol:
            return make_symbol(key.text_data);
        case map_key_type::string:
            return make_string(key.text_data);
        case map_key_type::integer:
            return make_integer(key.integer_data);
        case map_key_type::floating:
            return make_float(key.float_data);
    }
    throw lisp_error("map.keys: invalid key type");
}

std::string require_path_arg(value v, const std::string& where) {
    if (!is_string(v)) {
        throw lisp_error(where + ": expected file path string");
    }
    return string_value(v);
}

std::string read_text_file(const std::string& path, const std::string& where) {
    std::ifstream in(path);
    if (!in) {
        throw lisp_error(where + ": failed to open file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_text_file(const std::string& path, const std::string& text, const std::string& where) {
    std::ofstream out(path);
    if (!out) {
        throw lisp_error(where + ": failed to open file for write: " + path);
    }
    out << text;
    if (!out) {
        throw lisp_error(where + ": failed while writing file: " + path);
    }
}

struct numeric {
    bool is_int = false;
    std::int64_t int_value = 0;
    double float_value = 0.0;
};

numeric as_numeric(value v, const std::string& where) {
    if (is_integer(v)) {
        return numeric{true, integer_value(v), static_cast<double>(integer_value(v))};
    }
    if (is_float(v)) {
        return numeric{false, 0, float_value(v)};
    }
    throw lisp_error(where + ": expected number");
}

double number_as_double(const numeric& n) {
    return n.is_int ? static_cast<double>(n.int_value) : n.float_value;
}

bool contains_float(const std::vector<value>& args) {
    for (value arg : args) {
        if (is_float(arg)) {
            return true;
        }
    }
    return false;
}

double require_pq_priority(value v, const std::string& where) {
    if (is_integer(v)) {
        return static_cast<double>(integer_value(v));
    }
    if (is_float(v)) {
        const double out = float_value(v);
        if (!std::isfinite(out)) {
            throw lisp_error(where + ": priority must be finite");
        }
        return out;
    }
    throw lisp_error(where + ": expected numeric priority");
}

bool pq_heap_compare(const pq_entry& lhs, const pq_entry& rhs) {
    if (lhs.priority != rhs.priority) {
        return lhs.priority > rhs.priority;
    }
    return lhs.sequence > rhs.sequence;
}

value pq_entry_to_pair(const pq_entry& entry) {
    gc_root_scope roots(default_gc());
    value priority = make_float(entry.priority);
    value payload = entry.payload;
    roots.add(&priority);
    roots.add(&payload);

    std::vector<value> pair;
    pair.reserve(2);
    pair.push_back(priority);
    pair.push_back(payload);
    roots.add(&pair[0]);
    roots.add(&pair[1]);
    return list_from_vector(pair);
}

std::uint64_t splitmix64_next(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30u)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27u)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31u);
}

double rng_next_unit(value rng_obj) {
    // Use top 53 random bits for deterministic [0,1) doubles.
    constexpr double kScale = 1.0 / 9007199254740992.0;
    const std::uint64_t bits = splitmix64_next(rng_obj->rng_data->state);
    return static_cast<double>(bits >> 11u) * kScale;
}

bool checked_add(std::int64_t lhs, std::int64_t rhs, std::int64_t& out) {
#if defined(__clang__) || defined(__GNUC__)
    return !__builtin_add_overflow(lhs, rhs, &out);
#else
    if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
        (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
        return false;
    }
    out = lhs + rhs;
    return true;
#endif
}

bool checked_sub(std::int64_t lhs, std::int64_t rhs, std::int64_t& out) {
#if defined(__clang__) || defined(__GNUC__)
    return !__builtin_sub_overflow(lhs, rhs, &out);
#else
    if ((rhs < 0 && lhs > std::numeric_limits<std::int64_t>::max() + rhs) ||
        (rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() + rhs)) {
        return false;
    }
    out = lhs - rhs;
    return true;
#endif
}

bool checked_mul(std::int64_t lhs, std::int64_t rhs, std::int64_t& out) {
#if defined(__clang__) || defined(__GNUC__)
    return !__builtin_mul_overflow(lhs, rhs, &out);
#else
    if (lhs == 0 || rhs == 0) {
        out = 0;
        return true;
    }
    if (lhs == -1 && rhs == std::numeric_limits<std::int64_t>::min()) {
        return false;
    }
    if (rhs == -1 && lhs == std::numeric_limits<std::int64_t>::min()) {
        return false;
    }

    const auto result = lhs * rhs;
    if (result / rhs != lhs) {
        return false;
    }
    out = result;
    return true;
#endif
}

void bind_primitive(env_ptr global_env, const std::string& name, primitive_fn fn) {
    define(global_env, name, make_primitive(name, std::move(fn)));
}

value builtin_cons(const std::vector<value>& args) {
    require_arity("cons", args, 2);
    return make_cons(args[0], args[1]);
}

value builtin_car(const std::vector<value>& args) {
    require_arity("car", args, 1);
    if (!is_cons(args[0])) {
        throw lisp_error("car: expected cons");
    }
    return car(args[0]);
}

value builtin_cdr(const std::vector<value>& args) {
    require_arity("cdr", args, 1);
    if (!is_cons(args[0])) {
        throw lisp_error("cdr: expected cons");
    }
    return cdr(args[0]);
}

value builtin_null(const std::vector<value>& args) {
    require_arity("null?", args, 1);
    return make_boolean(is_nil(args[0]));
}

value builtin_eq(const std::vector<value>& args) {
    require_arity("eq?", args, 2);
    return make_boolean(eq_values(args[0], args[1]));
}

value builtin_list(const std::vector<value>& args) {
    return list_from_vector(args);
}

value builtin_add(const std::vector<value>& args) {
    if (args.empty()) {
        return make_integer(0);
    }

    if (contains_float(args)) {
        double sum = 0.0;
        for (value arg : args) {
            sum += number_as_double(as_numeric(arg, "+"));
        }
        return make_float(sum);
    }

    std::int64_t sum = 0;
    for (value arg : args) {
        std::int64_t next = 0;
        if (!checked_add(sum, as_numeric(arg, "+").int_value, next)) {
            throw lisp_error("+: integer overflow");
        }
        sum = next;
    }
    return make_integer(sum);
}

value builtin_sub(const std::vector<value>& args) {
    require_min_arity("-", args, 1);

    const bool float_mode = contains_float(args);
    if (float_mode) {
        double result = number_as_double(as_numeric(args.front(), "-"));
        if (args.size() == 1) {
            return make_float(-result);
        }
        for (std::size_t i = 1; i < args.size(); ++i) {
            result -= number_as_double(as_numeric(args[i], "-"));
        }
        return make_float(result);
    }

    std::int64_t result = as_numeric(args.front(), "-").int_value;
    if (args.size() == 1) {
        if (result == std::numeric_limits<std::int64_t>::min()) {
            throw lisp_error("-: integer overflow");
        }
        return make_integer(-result);
    }

    for (std::size_t i = 1; i < args.size(); ++i) {
        std::int64_t next = 0;
        if (!checked_sub(result, as_numeric(args[i], "-").int_value, next)) {
            throw lisp_error("-: integer overflow");
        }
        result = next;
    }
    return make_integer(result);
}

value builtin_mul(const std::vector<value>& args) {
    if (args.empty()) {
        return make_integer(1);
    }

    if (contains_float(args)) {
        double result = 1.0;
        for (value arg : args) {
            result *= number_as_double(as_numeric(arg, "*"));
        }
        return make_float(result);
    }

    std::int64_t result = 1;
    for (value arg : args) {
        std::int64_t next = 0;
        if (!checked_mul(result, as_numeric(arg, "*").int_value, next)) {
            throw lisp_error("*: integer overflow");
        }
        result = next;
    }
    return make_integer(result);
}

value builtin_div(const std::vector<value>& args) {
    require_min_arity("/", args, 1);

    double result = number_as_double(as_numeric(args.front(), "/"));
    if (args.size() == 1) {
        result = 1.0 / result;
        return make_float(result);
    }

    for (std::size_t i = 1; i < args.size(); ++i) {
        result /= number_as_double(as_numeric(args[i], "/"));
    }
    return make_float(result);
}

value builtin_num_eq(const std::vector<value>& args) {
    require_min_arity("=", args, 2);

    for (std::size_t i = 1; i < args.size(); ++i) {
        const numeric lhs = as_numeric(args[i - 1], "=");
        const numeric rhs = as_numeric(args[i], "=");

        if (lhs.is_int && rhs.is_int) {
            if (lhs.int_value != rhs.int_value) {
                return make_boolean(false);
            }
            continue;
        }

        if (number_as_double(lhs) != number_as_double(rhs)) {
            return make_boolean(false);
        }
    }
    return make_boolean(true);
}

value builtin_less(const std::vector<value>& args) {
    require_min_arity("<", args, 2);

    for (std::size_t i = 1; i < args.size(); ++i) {
        const numeric lhs = as_numeric(args[i - 1], "<");
        const numeric rhs = as_numeric(args[i], "<");

        const bool ok = (lhs.is_int && rhs.is_int) ? (lhs.int_value < rhs.int_value)
                                                   : (number_as_double(lhs) < number_as_double(rhs));
        if (!ok) {
            return make_boolean(false);
        }
    }
    return make_boolean(true);
}

value builtin_greater(const std::vector<value>& args) {
    require_min_arity(">", args, 2);

    for (std::size_t i = 1; i < args.size(); ++i) {
        const numeric lhs = as_numeric(args[i - 1], ">");
        const numeric rhs = as_numeric(args[i], ">");

        const bool ok = (lhs.is_int && rhs.is_int) ? (lhs.int_value > rhs.int_value)
                                                   : (number_as_double(lhs) > number_as_double(rhs));
        if (!ok) {
            return make_boolean(false);
        }
    }
    return make_boolean(true);
}

value builtin_less_equal(const std::vector<value>& args) {
    require_min_arity("<=", args, 2);

    for (std::size_t i = 1; i < args.size(); ++i) {
        const numeric lhs = as_numeric(args[i - 1], "<=");
        const numeric rhs = as_numeric(args[i], "<=");

        const bool ok = (lhs.is_int && rhs.is_int) ? (lhs.int_value <= rhs.int_value)
                                                   : (number_as_double(lhs) <= number_as_double(rhs));
        if (!ok) {
            return make_boolean(false);
        }
    }
    return make_boolean(true);
}

value builtin_greater_equal(const std::vector<value>& args) {
    require_min_arity(">=", args, 2);

    for (std::size_t i = 1; i < args.size(); ++i) {
        const numeric lhs = as_numeric(args[i - 1], ">=");
        const numeric rhs = as_numeric(args[i], ">=");

        const bool ok = (lhs.is_int && rhs.is_int) ? (lhs.int_value >= rhs.int_value)
                                                   : (number_as_double(lhs) >= number_as_double(rhs));
        if (!ok) {
            return make_boolean(false);
        }
    }
    return make_boolean(true);
}

value builtin_number_pred(const std::vector<value>& args) {
    require_arity("number?", args, 1);
    return make_boolean(is_number(args[0]));
}

value builtin_integer_pred(const std::vector<value>& args) {
    require_arity("integer?", args, 1);
    return make_boolean(is_integer(args[0]));
}

value builtin_float_pred(const std::vector<value>& args) {
    require_arity("float?", args, 1);
    return make_boolean(is_float(args[0]));
}

value builtin_zero_pred(const std::vector<value>& args) {
    require_arity("zero?", args, 1);
    if (is_integer(args[0])) {
        return make_boolean(integer_value(args[0]) == 0);
    }
    if (is_float(args[0])) {
        return make_boolean(float_value(args[0]) == 0.0);
    }
    return make_boolean(false);
}

value builtin_sqrt(const std::vector<value>& args) {
    require_arity("sqrt", args, 1);
    const double x = number_as_double(as_numeric(args[0], "sqrt"));
    if (x < 0.0) {
        throw lisp_error("sqrt: expected x >= 0");
    }
    return make_float(std::sqrt(x));
}

value builtin_log(const std::vector<value>& args) {
    require_arity("log", args, 1);
    const double x = number_as_double(as_numeric(args[0], "log"));
    if (x <= 0.0) {
        throw lisp_error("log: expected x > 0");
    }
    return make_float(std::log(x));
}

value builtin_exp(const std::vector<value>& args) {
    require_arity("exp", args, 1);
    return make_float(std::exp(number_as_double(as_numeric(args[0], "exp"))));
}

value builtin_abs(const std::vector<value>& args) {
    require_arity("abs", args, 1);
    if (is_integer(args[0])) {
        const std::int64_t x = integer_value(args[0]);
        if (x == std::numeric_limits<std::int64_t>::min()) {
            throw lisp_error("abs: integer overflow");
        }
        return make_integer(x < 0 ? -x : x);
    }
    if (is_float(args[0])) {
        return make_float(std::fabs(float_value(args[0])));
    }
    throw lisp_error("abs: expected number");
}

value builtin_clamp(const std::vector<value>& args) {
    require_arity("clamp", args, 3);
    const numeric x = as_numeric(args[0], "clamp");
    const numeric lo = as_numeric(args[1], "clamp");
    const numeric hi = as_numeric(args[2], "clamp");

    if (x.is_int && lo.is_int && hi.is_int) {
        if (lo.int_value > hi.int_value) {
            throw lisp_error("clamp: expected lo <= hi");
        }
        const std::int64_t out = std::clamp(x.int_value, lo.int_value, hi.int_value);
        return make_integer(out);
    }

    const double xd = number_as_double(x);
    const double lod = number_as_double(lo);
    const double hid = number_as_double(hi);
    if (lod > hid) {
        throw lisp_error("clamp: expected lo <= hi");
    }
    return make_float(std::clamp(xd, lod, hid));
}

value builtin_time_now_ms(const std::vector<value>& args) {
    require_arity("time.now-ms", args, 0);
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return make_integer(ms);
}

value builtin_rng_make(const std::vector<value>& args) {
    require_arity("rng.make", args, 1);
    const std::int64_t seed = require_int_arg(args[0], "rng.make");
    return make_rng(static_cast<std::uint64_t>(seed));
}

value builtin_rng_uniform(const std::vector<value>& args) {
    require_arity("rng.uniform", args, 3);
    value rng_obj = require_rng_arg(args[0], "rng.uniform");
    const double lo = number_as_double(as_numeric(args[1], "rng.uniform"));
    const double hi = number_as_double(as_numeric(args[2], "rng.uniform"));
    if (lo > hi) {
        throw lisp_error("rng.uniform: expected lo <= hi");
    }
    if (lo == hi) {
        return make_float(lo);
    }

    const double u = rng_next_unit(rng_obj);
    return make_float(lo + (hi - lo) * u);
}

value builtin_rng_int(const std::vector<value>& args) {
    require_arity("rng.int", args, 2);
    value rng_obj = require_rng_arg(args[0], "rng.int");
    const std::int64_t n = require_int_arg(args[1], "rng.int");
    if (n <= 0) {
        throw lisp_error("rng.int: expected n > 0");
    }

    const std::uint64_t bound = static_cast<std::uint64_t>(n);
    const std::uint64_t threshold = static_cast<std::uint64_t>(-bound) % bound;
    while (true) {
        const std::uint64_t r = splitmix64_next(rng_obj->rng_data->state);
        if (r >= threshold) {
            return make_integer(static_cast<std::int64_t>(r % bound));
        }
    }
}

value builtin_rng_normal(const std::vector<value>& args) {
    require_arity("rng.normal", args, 3);
    value rng_obj = require_rng_arg(args[0], "rng.normal");
    const double mu = number_as_double(as_numeric(args[1], "rng.normal"));
    const double sigma = number_as_double(as_numeric(args[2], "rng.normal"));
    if (sigma < 0.0) {
        throw lisp_error("rng.normal: expected sigma >= 0");
    }
    if (sigma == 0.0) {
        return make_float(mu);
    }

    if (rng_obj->rng_data->has_spare_normal) {
        rng_obj->rng_data->has_spare_normal = false;
        return make_float(mu + sigma * rng_obj->rng_data->spare_normal);
    }

    constexpr double kTwoPi = 6.28318530717958647692;
    double u1 = rng_next_unit(rng_obj);
    if (u1 <= 0.0) {
        u1 = std::numeric_limits<double>::min();
    }
    const double u2 = rng_next_unit(rng_obj);

    const double r = std::sqrt(-2.0 * std::log(u1));
    const double theta = kTwoPi * u2;
    const double z0 = r * std::cos(theta);
    const double z1 = r * std::sin(theta);
    rng_obj->rng_data->spare_normal = z1;
    rng_obj->rng_data->has_spare_normal = true;
    return make_float(mu + sigma * z0);
}

value builtin_vec_make(const std::vector<value>& args) {
    if (args.size() > 1) {
        throw lisp_error("vec.make: expected 0 or 1 arguments");
    }
    std::size_t capacity = 4;
    if (args.size() == 1) {
        capacity = require_non_negative_capacity(args[0], "vec.make");
    }
    return make_vec(capacity);
}

value builtin_vec_len(const std::vector<value>& args) {
    require_arity("vec.len", args, 1);
    value vec_obj = require_vec_arg(args[0], "vec.len");
    return make_integer(to_int64_size(vec_obj->vec_data.size(), "vec.len"));
}

value builtin_vec_get(const std::vector<value>& args) {
    require_arity("vec.get", args, 2);
    value vec_obj = require_vec_arg(args[0], "vec.get");
    const std::size_t index = require_non_negative_index(args[1], vec_obj->vec_data.size(), "vec.get");
    return vec_obj->vec_data[index];
}

value builtin_vec_set(const std::vector<value>& args) {
    require_arity("vec.set!", args, 3);
    value vec_obj = require_vec_arg(args[0], "vec.set!");
    const std::size_t index = require_non_negative_index(args[1], vec_obj->vec_data.size(), "vec.set!");
    vec_obj->vec_data[index] = args[2];
    return args[2];
}

value builtin_vec_push(const std::vector<value>& args) {
    require_arity("vec.push!", args, 2);
    value vec_obj = require_vec_arg(args[0], "vec.push!");
    vec_obj->vec_data.push_back(args[1]);
    return make_integer(to_int64_size(vec_obj->vec_data.size() - 1, "vec.push!"));
}

value builtin_vec_pop(const std::vector<value>& args) {
    require_arity("vec.pop!", args, 1);
    value vec_obj = require_vec_arg(args[0], "vec.pop!");
    if (vec_obj->vec_data.empty()) {
        throw lisp_error("vec.pop!: vector is empty");
    }
    value out = vec_obj->vec_data.back();
    vec_obj->vec_data.pop_back();
    return out;
}

value builtin_vec_clear(const std::vector<value>& args) {
    require_arity("vec.clear!", args, 1);
    value vec_obj = require_vec_arg(args[0], "vec.clear!");
    vec_obj->vec_data.clear();
    return make_nil();
}

value builtin_vec_reserve(const std::vector<value>& args) {
    require_arity("vec.reserve!", args, 2);
    value vec_obj = require_vec_arg(args[0], "vec.reserve!");
    const std::size_t capacity = require_non_negative_capacity(args[1], "vec.reserve!");
    vec_obj->vec_data.reserve(capacity);
    return make_nil();
}

value builtin_map_make(const std::vector<value>& args) {
    require_arity("map.make", args, 0);
    return make_map();
}

value builtin_map_get(const std::vector<value>& args) {
    require_arity("map.get", args, 3);
    value map_obj = require_map_arg(args[0], "map.get");
    const map_key key = map_key_from_value(args[1], "map.get");
    const auto it = map_obj->map_data.find(key);
    if (it == map_obj->map_data.end()) {
        return args[2];
    }
    return it->second;
}

value builtin_map_has(const std::vector<value>& args) {
    require_arity("map.has?", args, 2);
    value map_obj = require_map_arg(args[0], "map.has?");
    const map_key key = map_key_from_value(args[1], "map.has?");
    return make_boolean(map_obj->map_data.find(key) != map_obj->map_data.end());
}

value builtin_map_set(const std::vector<value>& args) {
    require_arity("map.set!", args, 3);
    value map_obj = require_map_arg(args[0], "map.set!");
    const map_key key = map_key_from_value(args[1], "map.set!");
    map_obj->map_data[key] = args[2];
    return args[2];
}

value builtin_map_del(const std::vector<value>& args) {
    require_arity("map.del!", args, 2);
    value map_obj = require_map_arg(args[0], "map.del!");
    const map_key key = map_key_from_value(args[1], "map.del!");
    return make_boolean(map_obj->map_data.erase(key) > 0);
}

value builtin_map_keys(const std::vector<value>& args) {
    require_arity("map.keys", args, 1);
    value map_obj = require_map_arg(args[0], "map.keys");
    std::vector<value> keys;
    keys.reserve(map_obj->map_data.size());
    gc_root_scope roots(default_gc());
    for (const auto& [key, _] : map_obj->map_data) {
        keys.push_back(map_key_to_value(key));
        roots.add(&keys.back());
    }
    return list_from_vector(keys);
}

value builtin_pq_make(const std::vector<value>& args) {
    if (args.size() > 1) {
        throw lisp_error("pq.make: expected 0 or 1 arguments");
    }
    std::size_t capacity = 4;
    if (args.size() == 1) {
        capacity = require_non_negative_capacity(args[0], "pq.make");
    }
    return make_pq(capacity);
}

value builtin_pq_len(const std::vector<value>& args) {
    require_arity("pq.len", args, 1);
    value pq_obj = require_pq_arg(args[0], "pq.len");
    return make_integer(to_int64_size(pq_obj->pq_data.size(), "pq.len"));
}

value builtin_pq_empty(const std::vector<value>& args) {
    require_arity("pq.empty?", args, 1);
    value pq_obj = require_pq_arg(args[0], "pq.empty?");
    return make_boolean(pq_obj->pq_data.empty());
}

value builtin_pq_push(const std::vector<value>& args) {
    require_arity("pq.push!", args, 3);
    value pq_obj = require_pq_arg(args[0], "pq.push!");
    const double priority = require_pq_priority(args[1], "pq.push!");
    if (pq_obj->pq_next_sequence == std::numeric_limits<std::uint64_t>::max()) {
        throw lisp_error("pq.push!: insertion sequence overflow");
    }

    pq_entry entry;
    entry.priority = priority;
    entry.sequence = pq_obj->pq_next_sequence++;
    entry.payload = args[2];
    pq_obj->pq_data.push_back(entry);
    std::push_heap(pq_obj->pq_data.begin(), pq_obj->pq_data.end(), pq_heap_compare);
    return make_integer(to_int64_size(pq_obj->pq_data.size(), "pq.push!"));
}

value builtin_pq_peek(const std::vector<value>& args) {
    require_arity("pq.peek", args, 1);
    value pq_obj = require_pq_arg(args[0], "pq.peek");
    if (pq_obj->pq_data.empty()) {
        throw lisp_error("pq.peek: priority queue is empty");
    }
    return pq_entry_to_pair(pq_obj->pq_data.front());
}

value builtin_pq_pop(const std::vector<value>& args) {
    require_arity("pq.pop!", args, 1);
    value pq_obj = require_pq_arg(args[0], "pq.pop!");
    if (pq_obj->pq_data.empty()) {
        throw lisp_error("pq.pop!: priority queue is empty");
    }
    std::pop_heap(pq_obj->pq_data.begin(), pq_obj->pq_data.end(), pq_heap_compare);
    const pq_entry out = pq_obj->pq_data.back();
    pq_obj->pq_data.pop_back();
    return pq_entry_to_pair(out);
}

void print_gc_snapshot(const gc_stats_snapshot& snapshot) {
    std::cout << "total allocated objects: " << snapshot.total_allocated_objects << '\n';
    std::cout << "live objects after last GC: " << snapshot.live_objects_after_last_gc << '\n';
    std::cout << "bytes allocated: " << snapshot.bytes_allocated << '\n';
    std::cout << "next GC threshold: " << snapshot.next_gc_threshold << '\n';
}

value builtin_heap_stats(const std::vector<value>& args) {
    require_arity("heap-stats", args, 0);
    print_gc_snapshot(default_gc().stats());
    return make_nil();
}

value builtin_gc_stats(const std::vector<value>& args) {
    require_arity("gc-stats", args, 0);
    default_gc().collect();
    print_gc_snapshot(default_gc().stats());
    return make_nil();
}

value builtin_print(const std::vector<value>& args) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            std::cout << ' ';
        }
        std::cout << print_value(args[i]);
    }
    std::cout << std::endl;
    return make_nil();
}

value builtin_write(const std::vector<value>& args) {
    require_arity("write", args, 1);
    std::cout << write_value(args[0]) << std::endl;
    return args[0];
}

value builtin_write_to_string(const std::vector<value>& args) {
    require_arity("write-to-string", args, 1);
    return make_string(write_value(args[0]));
}

value builtin_save(const std::vector<value>& args) {
    require_arity("save", args, 2);
    const std::string path = require_path_arg(args[0], "save");
    std::string text = write_value(args[1]);
    if (!is_nil(args[1]) && !is_boolean(args[1]) && !is_integer(args[1]) && !is_float(args[1]) && !is_string(args[1])) {
        text = "(quote " + text + ")";
    }
    write_text_file(path, text, "save");
    return make_boolean(true);
}

std::int64_t require_bt_def_handle(value v, const std::string& where) {
    if (!is_bt_def(v)) {
        throw lisp_error(where + ": expected bt_def");
    }
    return bt_handle(v);
}

std::int64_t require_bt_instance_handle(value v, const std::string& where) {
    if (!is_bt_instance(v)) {
        throw lisp_error(where + ": expected bt_instance");
    }
    return bt_handle(v);
}

std::string require_bb_key(value v, const std::string& where) {
    if (is_symbol(v)) {
        return symbol_name(v);
    }
    if (is_string(v)) {
        return string_value(v);
    }
    throw lisp_error(where + ": expected key as symbol or string");
}

value numeric_vector_to_lisp_list(const std::vector<double>& values);

bt::bb_value to_bb_value(value v, const std::string& where) {
    if (is_nil(v)) {
        return bt::bb_value{};
    }
    if (is_boolean(v)) {
        return bt::bb_value{boolean_value(v)};
    }
    if (is_integer(v)) {
        return bt::bb_value{integer_value(v)};
    }
    if (is_float(v)) {
        return bt::bb_value{float_value(v)};
    }
    if (is_symbol(v)) {
        return bt::bb_value{symbol_name(v)};
    }
    if (is_string(v)) {
        return bt::bb_value{string_value(v)};
    }
    if (is_image_handle(v)) {
        return bt::bb_value{bt::image_handle_ref{.id = image_handle_id(v)}};
    }
    if (is_blob_handle(v)) {
        return bt::bb_value{bt::blob_handle_ref{.id = blob_handle_id(v)}};
    }
    if (is_proper_list(v)) {
        const std::vector<value> items = vector_from_list(v);
        std::vector<double> out;
        out.reserve(items.size());
        for (value item : items) {
            if (is_integer(item)) {
                out.push_back(static_cast<double>(integer_value(item)));
            } else if (is_float(item)) {
                out.push_back(float_value(item));
            } else {
                throw lisp_error(where + ": list values for blackboard must be numeric");
            }
        }
        return bt::bb_value{std::move(out)};
    }
    throw lisp_error(where + ": unsupported blackboard value type");
}

value bb_value_to_lisp_value(const bt::bb_value& v) {
    if (const auto* b = std::get_if<bool>(&v)) {
        return make_boolean(*b);
    }
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return make_integer(*i);
    }
    if (const auto* f = std::get_if<double>(&v)) {
        return make_float(*f);
    }
    if (const auto* s = std::get_if<std::string>(&v)) {
        return make_string(*s);
    }
    if (const auto* vec = std::get_if<std::vector<double>>(&v)) {
        return numeric_vector_to_lisp_list(*vec);
    }
    if (const auto* image = std::get_if<bt::image_handle_ref>(&v)) {
        return make_image_handle(image->id);
    }
    if (const auto* blob = std::get_if<bt::blob_handle_ref>(&v)) {
        return make_blob_handle(blob->id);
    }
    return make_nil();
}

void apply_tick_blackboard_inputs(bt::instance& inst, value entries) {
    constexpr const char* kWhere = "bt.tick";

    if (!is_proper_list(entries)) {
        throw lisp_error(std::string(kWhere) + ": expected list of key/value pairs");
    }

    for (value item : vector_from_list(entries)) {
        if (!is_proper_list(item)) {
            throw lisp_error(std::string(kWhere) + ": each entry must be a 2-item list");
        }

        const std::vector<value> pair = vector_from_list(item);
        if (pair.size() != 2) {
            throw lisp_error(std::string(kWhere) + ": each entry must contain exactly key and value");
        }

        inst.bb.put(require_bb_key(pair[0], kWhere),
                    to_bb_value(pair[1], kWhere),
                    inst.tick_index + 1,
                    std::chrono::steady_clock::now(),
                    0,
                    "bt.tick");
    }
}

std::string normalize_option_key(std::string key) {
    if (!key.empty() && key.front() == ':') {
        key.erase(key.begin());
    }
    for (char& c : key) {
        if (c == '-') {
            c = '_';
        }
    }
    return key;
}

std::optional<value> map_lookup_option(value map_obj, const std::string& normalized_key) {
    for (const auto& [key, val] : map_obj->map_data) {
        if (key.type != map_key_type::symbol && key.type != map_key_type::string) {
            continue;
        }
        if (normalize_option_key(key.text_data) == normalized_key) {
            return val;
        }
    }
    return std::nullopt;
}

double require_number_value(value v, const std::string& where) {
    if (is_integer(v)) {
        return static_cast<double>(integer_value(v));
    }
    if (is_float(v)) {
        return float_value(v);
    }
    throw lisp_error(where + ": expected numeric value");
}

std::vector<double> lisp_to_numeric_vector(value v, const std::string& where) {
    if (is_integer(v) || is_float(v)) {
        return {require_number_value(v, where)};
    }
    if (!is_proper_list(v)) {
        throw lisp_error(where + ": expected numeric list or number");
    }
    const std::vector<value> items = vector_from_list(v);
    std::vector<double> out;
    out.reserve(items.size());
    for (value item : items) {
        out.push_back(require_number_value(item, where));
    }
    return out;
}

value numeric_vector_to_lisp_list(const std::vector<double>& values) {
    std::vector<value> items;
    items.reserve(values.size());
    gc_root_scope roots(default_gc());
    for (double v : values) {
        items.push_back(make_float(v));
        roots.add(&items.back());
    }
    return list_from_vector(items);
}

map_key symbol_key(const std::string& name) {
    map_key key;
    key.type = map_key_type::symbol;
    key.text_data = name;
    return key;
}

void map_set_symbol(value map_obj, const std::string& key_name, value v) {
    map_obj->map_data[symbol_key(key_name)] = v;
}

std::string require_text_value(value v, const std::string& where) {
    if (is_string(v)) {
        return string_value(v);
    }
    if (is_symbol(v)) {
        return symbol_name(v);
    }
    throw lisp_error(where + ": expected string or symbol");
}

std::int64_t require_non_negative_int(value v, const std::string& where) {
    const std::int64_t out = require_int_arg(v, where);
    if (out < 0) {
        throw lisp_error(where + ": expected non-negative integer");
    }
    return out;
}

std::string map_lookup_text_or(value map_obj, const std::string& key, std::string default_value, const std::string& where) {
    const std::optional<value> found = map_lookup_option(map_obj, key);
    if (!found.has_value()) {
        return default_value;
    }
    return require_text_value(*found, where);
}

std::int64_t map_lookup_int_or(value map_obj, const std::string& key, std::int64_t default_value, const std::string& where) {
    const std::optional<value> found = map_lookup_option(map_obj, key);
    if (!found.has_value()) {
        return default_value;
    }
    return require_int_arg(*found, where);
}

double map_lookup_number_or(value map_obj, const std::string& key, double default_value, const std::string& where) {
    const std::optional<value> found = map_lookup_option(map_obj, key);
    if (!found.has_value()) {
        return default_value;
    }
    return require_number_value(*found, where);
}

std::vector<std::pair<double, double>> lisp_to_bounds(value v, const std::string& where) {
    if (!is_proper_list(v)) {
        throw lisp_error(where + ": expected list of [lo hi] pairs");
    }
    const std::vector<value> rows = vector_from_list(v);
    std::vector<std::pair<double, double>> out;
    out.reserve(rows.size());
    for (value row : rows) {
        if (!is_proper_list(row)) {
            throw lisp_error(where + ": bounds row must be a 2-item list");
        }
        const std::vector<value> pair = vector_from_list(row);
        if (pair.size() != 2) {
            throw lisp_error(where + ": each bounds row must have exactly 2 items");
        }
        const double lo = require_number_value(pair[0], where);
        const double hi = require_number_value(pair[1], where);
        if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi) {
            throw lisp_error(where + ": bounds values must be finite and ordered");
        }
        out.emplace_back(lo, hi);
    }
    return out;
}

std::vector<std::string> lisp_to_text_list(value v, const std::string& where) {
    if (!is_proper_list(v)) {
        throw lisp_error(where + ": expected list");
    }
    const std::vector<value> rows = vector_from_list(v);
    std::vector<std::string> out;
    out.reserve(rows.size());
    for (value row : rows) {
        out.push_back(require_text_value(row, where));
    }
    return out;
}

value keyword_symbol(std::string_view name) {
    return make_symbol(std::string(":") + std::string(name));
}

value vla_job_status_symbol(bt::vla_job_status st) {
    return keyword_symbol(bt::vla_job_status_name(st));
}

value vla_status_symbol(bt::vla_status st) {
    return keyword_symbol(bt::vla_status_name(st));
}

value doubles_map_to_lisp_map(const std::unordered_map<std::string, double>& stats) {
    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);
    for (const auto& [k, v] : stats) {
        value vv = make_float(v);
        roots.add(&vv);
        map_set_symbol(out, k, vv);
    }
    return out;
}

value vla_action_to_lisp(const bt::vla_action& action) {
    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);
    map_set_symbol(out, "type", keyword_symbol(bt::vla_action_type_name(action.type)));
    if (action.type == bt::vla_action_type::continuous) {
        value u = numeric_vector_to_lisp_list(action.u);
        roots.add(&u);
        map_set_symbol(out, "u", u);
    } else if (action.type == bt::vla_action_type::discrete) {
        value id = make_string(action.discrete_id);
        roots.add(&id);
        map_set_symbol(out, "id", id);
    } else {
        std::vector<value> steps;
        steps.reserve(action.steps.size());
        for (const auto& s : action.steps) {
            value step = vla_action_to_lisp(s);
            roots.add(&step);
            steps.push_back(step);
            roots.add(&steps.back());
        }
        value steps_list = list_from_vector(steps);
        roots.add(&steps_list);
        map_set_symbol(out, "steps", steps_list);
    }
    return out;
}

value vla_response_to_lisp(const bt::vla_response& response) {
    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);
    map_set_symbol(out, "status", vla_status_symbol(response.status));

    value action = vla_action_to_lisp(response.action);
    roots.add(&action);
    map_set_symbol(out, "action", action);
    map_set_symbol(out, "confidence", make_float(response.confidence));

    value model = make_map();
    roots.add(&model);
    map_set_symbol(model, "name", make_string(response.model.name));
    map_set_symbol(model, "version", make_string(response.model.version));
    map_set_symbol(out, "model", model);

    if (!response.explanation.empty()) {
        map_set_symbol(out, "explanation", make_string(response.explanation));
    }

    value stats = doubles_map_to_lisp_map(response.stats);
    roots.add(&stats);
    map_set_symbol(out, "stats", stats);
    return out;
}

std::string json_escape(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string map_key_to_json_object_key(const map_key& key) {
    switch (key.type) {
        case map_key_type::symbol:
        case map_key_type::string:
            return key.text_data;
        case map_key_type::integer:
            return std::to_string(key.integer_data);
        case map_key_type::floating: {
            std::ostringstream out;
            out << key.float_data;
            return out.str();
        }
    }
    return "";
}

std::string value_to_json(value v);

std::string list_to_json(value list_v) {
    if (!is_proper_list(list_v)) {
        throw lisp_error("json.encode: expected proper list");
    }
    const std::vector<value> items = vector_from_list(list_v);
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << value_to_json(items[i]);
    }
    out << ']';
    return out.str();
}

std::string value_to_json(value v) {
    if (is_nil(v)) {
        return "null";
    }
    if (is_boolean(v)) {
        return boolean_value(v) ? "true" : "false";
    }
    if (is_integer(v)) {
        return std::to_string(integer_value(v));
    }
    if (is_float(v)) {
        const double d = float_value(v);
        if (!std::isfinite(d)) {
            throw lisp_error("json.encode: non-finite floats are not supported");
        }
        std::ostringstream out;
        out << d;
        return out.str();
    }
    if (is_string(v)) {
        return "\"" + json_escape(string_value(v)) + "\"";
    }
    if (is_symbol(v)) {
        return "\"" + json_escape(symbol_name(v)) + "\"";
    }
    if (is_cons(v)) {
        return list_to_json(v);
    }
    if (is_vec(v)) {
        std::ostringstream out;
        out << '[';
        for (std::size_t i = 0; i < v->vec_data.size(); ++i) {
            if (i != 0) {
                out << ',';
            }
            out << value_to_json(v->vec_data[i]);
        }
        out << ']';
        return out.str();
    }
    if (is_map(v)) {
        std::ostringstream out;
        out << '{';
        bool first = true;
        for (const auto& [k, mapped] : v->map_data) {
            if (!first) {
                out << ',';
            }
            first = false;
            out << "\"" << json_escape(map_key_to_json_object_key(k)) << "\":" << value_to_json(mapped);
        }
        out << '}';
        return out.str();
    }
    if (is_image_handle(v)) {
        return "{\"type\":\"image_handle\",\"id\":" + std::to_string(image_handle_id(v)) + "}";
    }
    if (is_blob_handle(v)) {
        return "{\"type\":\"blob_handle\",\"id\":" + std::to_string(blob_handle_id(v)) + "}";
    }
    throw lisp_error("json.encode: unsupported value type: " + std::string(type_name(type_of(v))));
}

class json_parser {
public:
    explicit json_parser(std::string_view input) : input_(input) {}

    value parse_document() {
        value out = parse_value();
        skip_ws();
        if (!eof()) {
            throw lisp_error("json.decode: trailing characters");
        }
        return out;
    }

private:
    [[nodiscard]] bool eof() const noexcept { return pos_ >= input_.size(); }
    [[nodiscard]] char peek() const noexcept { return eof() ? '\0' : input_[pos_]; }
    char get() noexcept { return eof() ? '\0' : input_[pos_++]; }

    void skip_ws() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) {
            ++pos_;
        }
    }

    void expect(char c, const char* what) {
        if (get() != c) {
            throw lisp_error(std::string("json.decode: expected ") + what);
        }
    }

    bool consume_if(char c) {
        skip_ws();
        if (peek() == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool consume_literal(std::string_view literal) {
        if (input_.substr(pos_, literal.size()) == literal) {
            pos_ += literal.size();
            return true;
        }
        return false;
    }

    value parse_value() {
        skip_ws();
        if (eof()) {
            throw lisp_error("json.decode: unexpected end of input");
        }
        const char c = peek();
        if (c == 'n') {
            if (!consume_literal("null")) {
                throw lisp_error("json.decode: invalid literal");
            }
            return make_nil();
        }
        if (c == 't') {
            if (!consume_literal("true")) {
                throw lisp_error("json.decode: invalid literal");
            }
            return make_boolean(true);
        }
        if (c == 'f') {
            if (!consume_literal("false")) {
                throw lisp_error("json.decode: invalid literal");
            }
            return make_boolean(false);
        }
        if (c == '"') {
            return make_string(parse_string());
        }
        if (c == '[') {
            return parse_array();
        }
        if (c == '{') {
            return parse_object();
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parse_number();
        }
        throw lisp_error("json.decode: unexpected token");
    }

    std::string parse_string() {
        expect('"', "\"");
        std::string out;
        while (!eof()) {
            const char c = get();
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (eof()) {
                    throw lisp_error("json.decode: invalid escape");
                }
                const char esc = get();
                switch (esc) {
                    case '"':
                    case '\\':
                    case '/':
                        out.push_back(esc);
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    default:
                        throw lisp_error("json.decode: unsupported escape sequence");
                }
            } else {
                out.push_back(c);
            }
        }
        throw lisp_error("json.decode: unterminated string");
    }

    value parse_number() {
        const std::size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            throw lisp_error("json.decode: invalid number");
        }
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            ++pos_;
        }
        bool is_float_num = false;
        if (peek() == '.') {
            is_float_num = true;
            ++pos_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                ++pos_;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            is_float_num = true;
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                throw lisp_error("json.decode: invalid exponent");
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                ++pos_;
            }
        }

        const std::string token(input_.substr(start, pos_ - start));
        if (!is_float_num) {
            try {
                return make_integer(std::stoll(token));
            } catch (...) {
                // Fall through to float parse.
            }
        }
        const double parsed = std::strtod(token.c_str(), nullptr);
        if (!std::isfinite(parsed)) {
            throw lisp_error("json.decode: non-finite number");
        }
        return make_float(parsed);
    }

    value parse_array() {
        expect('[', "[");
        skip_ws();
        std::vector<value> items;
        gc_root_scope roots(default_gc());
        if (consume_if(']')) {
            return make_nil();
        }
        while (true) {
            value item = parse_value();
            roots.add(&item);
            items.push_back(item);
            roots.add(&items.back());
            skip_ws();
            if (consume_if(']')) {
                return list_from_vector(items);
            }
            expect(',', ",");
        }
    }

    value parse_object() {
        expect('{', "{");
        value out = make_map();
        gc_root_scope roots(default_gc());
        roots.add(&out);
        skip_ws();
        if (consume_if('}')) {
            return out;
        }
        while (true) {
            skip_ws();
            if (peek() != '"') {
                throw lisp_error("json.decode: expected string key");
            }
            const std::string key = parse_string();
            skip_ws();
            expect(':', ":");
            value mapped = parse_value();
            roots.add(&mapped);
            map_key map_k;
            map_k.type = map_key_type::string;
            map_k.text_data = key;
            out->map_data[map_k] = mapped;
            skip_ws();
            if (consume_if('}')) {
                return out;
            }
            expect(',', ",");
        }
    }

    std::string_view input_;
    std::size_t pos_ = 0;
};

std::optional<std::uint64_t> maybe_seed_from_value(value v, const std::string& where) {
    if (is_integer(v)) {
        const std::int64_t raw = integer_value(v);
        if (raw < 0) {
            throw lisp_error(where + ": seed must be non-negative");
        }
        return static_cast<std::uint64_t>(raw);
    }
    if (is_float(v)) {
        const double raw = float_value(v);
        if (!std::isfinite(raw) || raw < 0.0) {
            throw lisp_error(where + ": seed must be non-negative finite");
        }
        return static_cast<std::uint64_t>(raw);
    }
    if (is_string(v)) {
        return bt::vla_service::hash64(string_value(v));
    }
    if (is_symbol(v)) {
        return bt::vla_service::hash64(symbol_name(v));
    }
    return std::nullopt;
}

bt::vla_request parse_vla_request_map(value request_map) {
    if (!is_map(request_map)) {
        throw lisp_error("vla.submit: expected request map");
    }

    bt::vla_request req;
    req.run_id = "lisp";
    req.node_name = "vla.submit";

    req.capability = map_lookup_text_or(request_map, "capability", req.capability, "vla.submit capability");
    req.task_id = map_lookup_text_or(request_map, "task_id", req.task_id, "vla.submit task_id");
    req.instruction = map_lookup_text_or(request_map, "instruction", req.instruction, "vla.submit instruction");
    req.deadline_ms = map_lookup_int_or(request_map, "deadline_ms", req.deadline_ms, "vla.submit deadline_ms");
    if (req.deadline_ms <= 0) {
        throw lisp_error("vla.submit: deadline_ms must be > 0");
    }
    req.run_id = map_lookup_text_or(request_map, "run_id", req.run_id, "vla.submit run_id");
    req.node_name = map_lookup_text_or(request_map, "node_name", req.node_name, "vla.submit node_name");
    req.tick_index = static_cast<std::uint64_t>(
        map_lookup_int_or(request_map, "tick_index", static_cast<std::int64_t>(req.tick_index), "vla.submit tick_index"));

    if (const std::optional<value> seed_v = map_lookup_option(request_map, "seed"); seed_v.has_value()) {
        req.seed = maybe_seed_from_value(*seed_v, "vla.submit seed");
        if (!req.seed.has_value()) {
            throw lisp_error("vla.submit: unsupported seed type");
        }
    }

    {
        const std::optional<value> model_v = map_lookup_option(request_map, "model");
        if (model_v.has_value()) {
            const value model_map = require_map_arg(*model_v, "vla.submit model");
            req.model.name = map_lookup_text_or(model_map, "name", req.model.name, "vla.submit model.name");
            req.model.version = map_lookup_text_or(model_map, "version", req.model.version, "vla.submit model.version");
        }
    }

    {
        const std::optional<value> obs_v = map_lookup_option(request_map, "observation");
        if (obs_v.has_value()) {
            const value obs_map = require_map_arg(*obs_v, "vla.submit observation");
            if (const std::optional<value> image_v = map_lookup_option(obs_map, "image"); image_v.has_value()) {
                if (!is_image_handle(*image_v)) {
                    throw lisp_error("vla.submit observation.image: expected image_handle");
                }
                req.observation.image = bt::image_handle_ref{.id = image_handle_id(*image_v)};
            }
            if (const std::optional<value> blob_v = map_lookup_option(obs_map, "blob"); blob_v.has_value()) {
                if (!is_blob_handle(*blob_v)) {
                    throw lisp_error("vla.submit observation.blob: expected blob_handle");
                }
                req.observation.blob = bt::blob_handle_ref{.id = blob_handle_id(*blob_v)};
            }
            if (const std::optional<value> state_v = map_lookup_option(obs_map, "state"); state_v.has_value()) {
                req.observation.state = lisp_to_numeric_vector(*state_v, "vla.submit observation.state");
            }
            req.observation.timestamp_ms = map_lookup_int_or(
                obs_map, "timestamp_ms", req.observation.timestamp_ms, "vla.submit observation.timestamp_ms");
            req.observation.frame_id =
                map_lookup_text_or(obs_map, "frame_id", req.observation.frame_id, "vla.submit observation.frame_id");
        }
    }

    {
        const std::optional<value> space_v = map_lookup_option(request_map, "action_space");
        if (!space_v.has_value()) {
            throw lisp_error("vla.submit: missing action_space");
        }
        const value space_map = require_map_arg(*space_v, "vla.submit action_space");
        req.action_space.type = map_lookup_text_or(space_map, "type", req.action_space.type, "vla.submit action_space.type");
        req.action_space.dims = map_lookup_int_or(space_map, "dims", req.action_space.dims, "vla.submit action_space.dims");
        if (req.action_space.dims <= 0) {
            throw lisp_error("vla.submit: action_space.dims must be > 0");
        }
        if (const std::optional<value> bounds_v = map_lookup_option(space_map, "bounds"); bounds_v.has_value()) {
            req.action_space.bounds = lisp_to_bounds(*bounds_v, "vla.submit action_space.bounds");
        }
        if (req.action_space.bounds.empty()) {
            req.action_space.bounds.assign(static_cast<std::size_t>(req.action_space.dims), {-1.0, 1.0});
        }
        if (req.action_space.bounds.size() != static_cast<std::size_t>(req.action_space.dims)) {
            throw lisp_error("vla.submit: action_space.bounds length must match dims");
        }
        if (const std::optional<value> units_v = map_lookup_option(space_map, "units"); units_v.has_value()) {
            req.action_space.units = lisp_to_text_list(*units_v, "vla.submit action_space.units");
        }
        if (const std::optional<value> sem_v = map_lookup_option(space_map, "semantic"); sem_v.has_value()) {
            req.action_space.semantic = lisp_to_text_list(*sem_v, "vla.submit action_space.semantic");
        }
    }

    {
        const std::optional<value> constraints_v = map_lookup_option(request_map, "constraints");
        if (constraints_v.has_value()) {
            const value constraints_map = require_map_arg(*constraints_v, "vla.submit constraints");
            req.constraints.max_abs_value = map_lookup_number_or(
                constraints_map, "max_abs_value", req.constraints.max_abs_value, "vla.submit constraints.max_abs_value");
            req.constraints.max_delta =
                map_lookup_number_or(constraints_map, "max_delta", req.constraints.max_delta, "vla.submit constraints.max_delta");
            if (const std::optional<value> forb_v = map_lookup_option(constraints_map, "forbidden_ranges"); forb_v.has_value()) {
                req.constraints.forbidden_ranges = lisp_to_bounds(*forb_v, "vla.submit constraints.forbidden_ranges");
            }
        }
    }

    return req;
}

value vla_poll_to_lisp(const bt::vla_poll& poll) {
    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);
    map_set_symbol(out, "status", vla_job_status_symbol(poll.status));
    if (poll.partial.has_value()) {
        value partial = make_map();
        roots.add(&partial);
        map_set_symbol(partial, "sequence", make_integer(static_cast<std::int64_t>(poll.partial->sequence)));
        map_set_symbol(partial, "confidence", make_float(poll.partial->confidence));
        if (!poll.partial->text_chunk.empty()) {
            map_set_symbol(partial, "text_chunk", make_string(poll.partial->text_chunk));
        }
        if (poll.partial->action_candidate.has_value()) {
            value action_cand = vla_action_to_lisp(*poll.partial->action_candidate);
            roots.add(&action_cand);
            map_set_symbol(partial, "action_candidate", action_cand);
        }
        map_set_symbol(out, "partial", partial);
    }
    if (poll.final.has_value()) {
        value final = vla_response_to_lisp(*poll.final);
        roots.add(&final);
        map_set_symbol(out, "final", final);
    }
    value stats = doubles_map_to_lisp_map(poll.stats);
    roots.add(&stats);
    map_set_symbol(out, "stats", stats);
    return out;
}

value planner_status_symbol(bt::planner_status status) {
    return make_symbol(std::string(":") + bt::planner_status_name(status));
}

value status_to_symbol(bt::status st) {
    return make_symbol(bt::status_name(st));
}

value bt_arg_to_lisp_value(const bt::arg_value& arg) {
    switch (arg.kind) {
        case bt::arg_kind::nil:
            return make_nil();
        case bt::arg_kind::boolean:
            return make_boolean(arg.bool_v);
        case bt::arg_kind::integer:
            return make_integer(arg.int_v);
        case bt::arg_kind::floating:
            return make_float(arg.float_v);
        case bt::arg_kind::symbol:
            return make_symbol(arg.text);
        case bt::arg_kind::string:
            return make_string(arg.text);
    }
    throw lisp_error("bt.to-dsl: unsupported arg kind");
}

value bt_node_to_dsl(const bt::definition& def, bt::node_id id) {
    if (id >= def.nodes.size()) {
        throw lisp_error("bt.to-dsl: invalid node id");
    }
    const bt::node& n = def.nodes[id];

    std::vector<value> form;
    switch (n.kind) {
        case bt::node_kind::seq:
            form.push_back(make_symbol("seq"));
            for (bt::node_id child : n.children) {
                form.push_back(bt_node_to_dsl(def, child));
            }
            break;
        case bt::node_kind::sel:
            form.push_back(make_symbol("sel"));
            for (bt::node_id child : n.children) {
                form.push_back(bt_node_to_dsl(def, child));
            }
            break;
        case bt::node_kind::invert:
            form.push_back(make_symbol("invert"));
            if (n.children.size() != 1) {
                throw lisp_error("bt.to-dsl: invert node requires one child");
            }
            form.push_back(bt_node_to_dsl(def, n.children[0]));
            break;
        case bt::node_kind::repeat:
            form.push_back(make_symbol("repeat"));
            form.push_back(make_integer(n.int_param));
            if (n.children.size() != 1) {
                throw lisp_error("bt.to-dsl: repeat node requires one child");
            }
            form.push_back(bt_node_to_dsl(def, n.children[0]));
            break;
        case bt::node_kind::retry:
            form.push_back(make_symbol("retry"));
            form.push_back(make_integer(n.int_param));
            if (n.children.size() != 1) {
                throw lisp_error("bt.to-dsl: retry node requires one child");
            }
            form.push_back(bt_node_to_dsl(def, n.children[0]));
            break;
        case bt::node_kind::cond:
            form.push_back(make_symbol("cond"));
            form.push_back(make_symbol(n.leaf_name));
            for (const bt::arg_value& arg : n.args) {
                form.push_back(bt_arg_to_lisp_value(arg));
            }
            break;
        case bt::node_kind::act:
            form.push_back(make_symbol("act"));
            form.push_back(make_symbol(n.leaf_name));
            for (const bt::arg_value& arg : n.args) {
                form.push_back(bt_arg_to_lisp_value(arg));
            }
            break;
        case bt::node_kind::succeed:
            form.push_back(make_symbol("succeed"));
            break;
        case bt::node_kind::fail:
            form.push_back(make_symbol("fail"));
            break;
        case bt::node_kind::running:
            form.push_back(make_symbol("running"));
            break;
        case bt::node_kind::plan_action:
            form.push_back(make_symbol("plan-action"));
            for (const bt::arg_value& arg : n.args) {
                form.push_back(bt_arg_to_lisp_value(arg));
            }
            break;
        case bt::node_kind::vla_request:
            form.push_back(make_symbol("vla-request"));
            for (const bt::arg_value& arg : n.args) {
                form.push_back(bt_arg_to_lisp_value(arg));
            }
            break;
        case bt::node_kind::vla_wait:
            form.push_back(make_symbol("vla-wait"));
            for (const bt::arg_value& arg : n.args) {
                form.push_back(bt_arg_to_lisp_value(arg));
            }
            break;
        case bt::node_kind::vla_cancel:
            form.push_back(make_symbol("vla-cancel"));
            for (const bt::arg_value& arg : n.args) {
                form.push_back(bt_arg_to_lisp_value(arg));
            }
            break;
    }

    return list_from_vector(form);
}

value bt_definition_to_dsl(const bt::definition& def) {
    if (def.nodes.empty()) {
        throw lisp_error("bt.to-dsl: definition has no nodes");
    }
    if (def.root >= def.nodes.size()) {
        throw lisp_error("bt.to-dsl: root node out of range");
    }
    return bt_node_to_dsl(def, def.root);
}

value builtin_bt_compile(const std::vector<value>& args) {
    require_arity("bt.compile", args, 1);
    try {
        bt::definition def = bt::compile_definition(args[0]);
        const std::int64_t handle = bt::default_runtime_host().store_definition(std::move(def));
        return make_bt_def(handle);
    } catch (const bt::bt_compile_error& e) {
        throw lisp_error(std::string("bt.compile: ") + e.what());
    }
}

value builtin_bt_to_dsl(const std::vector<value>& args) {
    require_arity("bt.to-dsl", args, 1);
    const std::int64_t def_handle = require_bt_def_handle(args[0], "bt.to-dsl");
    const bt::definition* def = bt::default_runtime_host().find_definition(def_handle);
    if (!def) {
        throw lisp_error("bt.to-dsl: unknown definition");
    }
    return bt_definition_to_dsl(*def);
}

value builtin_bt_save_dsl(const std::vector<value>& args) {
    require_arity("bt.save-dsl", args, 2);
    const std::int64_t def_handle = require_bt_def_handle(args[0], "bt.save-dsl");
    const std::string path = require_path_arg(args[1], "bt.save-dsl");
    const bt::definition* def = bt::default_runtime_host().find_definition(def_handle);
    if (!def) {
        throw lisp_error("bt.save-dsl: unknown definition");
    }
    const value dsl = bt_definition_to_dsl(*def);
    write_text_file(path, write_value(dsl), "bt.save-dsl");
    return make_boolean(true);
}

value builtin_bt_export_dot(const std::vector<value>& args) {
    require_arity("bt.export-dot", args, 2);
    const std::int64_t def_handle = require_bt_def_handle(args[0], "bt.export-dot");
    const std::string path = require_path_arg(args[1], "bt.export-dot");
    const bt::definition* def = bt::default_runtime_host().find_definition(def_handle);
    if (!def) {
        throw lisp_error("bt.export-dot: unknown definition");
    }
    try {
        bt::export_definition_dot(*def, path);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("bt.export-dot: ") + e.what());
    }
    return make_boolean(true);
}

value builtin_bt_load_dsl(const std::vector<value>& args) {
    require_arity("bt.load-dsl", args, 1);
    const std::string path = require_path_arg(args[0], "bt.load-dsl");
    try {
        const std::string source = read_text_file(path, "bt.load-dsl");
        value form = read_one(source);
        bt::definition def = bt::compile_definition(form);
        const std::int64_t handle = bt::default_runtime_host().store_definition(std::move(def));
        return make_bt_def(handle);
    } catch (const parse_error& e) {
        throw lisp_error("bt.load-dsl: " + path + ": " + std::string(e.what()));
    } catch (const bt::bt_compile_error& e) {
        throw lisp_error("bt.load-dsl: " + path + ": " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw lisp_error("bt.load-dsl: " + path + ": " + std::string(e.what()));
    }
}

value builtin_bt_save_binary(const std::vector<value>& args) {
    require_arity("bt.save", args, 2);
    const std::int64_t def_handle = require_bt_def_handle(args[0], "bt.save");
    const std::string path = require_path_arg(args[1], "bt.save");
    const bt::definition* def = bt::default_runtime_host().find_definition(def_handle);
    if (!def) {
        throw lisp_error("bt.save: unknown definition");
    }
    try {
        bt::save_definition_binary(*def, path);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("bt.save: ") + e.what());
    }
    return make_boolean(true);
}

value builtin_bt_load_binary(const std::vector<value>& args) {
    require_arity("bt.load", args, 1);
    const std::string path = require_path_arg(args[0], "bt.load");
    try {
        bt::definition def = bt::load_definition_binary(path);
        const std::int64_t handle = bt::default_runtime_host().store_definition(std::move(def));
        return make_bt_def(handle);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("bt.load: ") + e.what());
    }
}

value builtin_bt_new_instance(const std::vector<value>& args) {
    require_arity("bt.new-instance", args, 1);
    const std::int64_t def_handle = require_bt_def_handle(args[0], "bt.new-instance");
    try {
        const std::int64_t inst_handle = bt::default_runtime_host().create_instance(def_handle);
        return make_bt_instance(inst_handle);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("bt.new-instance: ") + e.what());
    }
}

value builtin_bt_tick(const std::vector<value>& args) {
    if (args.size() != 1 && args.size() != 2) {
        throw lisp_error("bt.tick: expected 1 or 2 arguments");
    }
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.tick");
    try {
        bt::runtime_host& host = bt::default_runtime_host();
        if (args.size() == 2) {
            bt::instance* inst = host.find_instance(inst_handle);
            if (!inst) {
                throw lisp_error("bt.tick: unknown instance");
            }
            apply_tick_blackboard_inputs(*inst, args[1]);
        }
        const bt::status st = host.tick_instance(inst_handle);
        return status_to_symbol(st);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("bt.tick: ") + e.what());
    }
}

value builtin_bt_reset(const std::vector<value>& args) {
    require_arity("bt.reset", args, 1);
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.reset");
    try {
        bt::default_runtime_host().reset_instance(inst_handle);
        return make_nil();
    } catch (const std::exception& e) {
        throw lisp_error(std::string("bt.reset: ") + e.what());
    }
}

value builtin_bt_status_to_symbol(const std::vector<value>& args) {
    require_arity("bt.status->symbol", args, 1);
    if (!is_symbol(args[0])) {
        throw lisp_error("bt.status->symbol: expected symbol");
    }
    const std::string name = symbol_name(args[0]);
    if (name != "success" && name != "failure" && name != "running") {
        throw lisp_error("bt.status->symbol: expected success/failure/running");
    }
    return args[0];
}

value builtin_bt_stats(const std::vector<value>& args) {
    require_arity("bt.stats", args, 1);
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.stats");
    return make_string(bt::default_runtime_host().dump_instance_stats(inst_handle));
}

value builtin_bt_trace_dump(const std::vector<value>& args) {
    require_arity("bt.trace.dump", args, 1);
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.trace.dump");
    return make_string(bt::default_runtime_host().dump_instance_trace(inst_handle));
}

value builtin_bt_trace_snapshot(const std::vector<value>& args) {
    require_arity("bt.trace.snapshot", args, 1);
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.trace.snapshot");
    return make_string(bt::default_runtime_host().dump_instance_trace(inst_handle));
}

value builtin_bt_blackboard_dump(const std::vector<value>& args) {
    require_arity("bt.blackboard.dump", args, 1);
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.blackboard.dump");
    return make_string(bt::default_runtime_host().dump_instance_blackboard(inst_handle));
}

value builtin_bt_blackboard_get(const std::vector<value>& args) {
    if (args.size() != 2 && args.size() != 3) {
        throw lisp_error("bt.blackboard.get: expected 2 or 3 arguments");
    }
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.blackboard.get");
    const std::string key = require_bb_key(args[1], "bt.blackboard.get");

    const bt::instance* inst = bt::default_runtime_host().find_instance(inst_handle);
    if (!inst) {
        throw lisp_error("bt.blackboard.get: unknown instance");
    }

    const bt::bb_entry* entry = inst->bb.get(key);
    if (!entry) {
        return args.size() == 3 ? args[2] : make_nil();
    }
    return bb_value_to_lisp_value(entry->value);
}

value builtin_bt_logs_dump(const std::vector<value>& args) {
    require_arity("bt.logs.dump", args, 0);
    return make_string(bt::default_runtime_host().dump_logs());
}

value builtin_bt_logs_snapshot(const std::vector<value>& args) {
    require_arity("bt.logs.snapshot", args, 0);
    return make_string(bt::default_runtime_host().dump_logs());
}

value builtin_bt_scheduler_stats(const std::vector<value>& args) {
    require_arity("bt.scheduler.stats", args, 0);
    return make_string(bt::default_runtime_host().dump_scheduler_stats());
}

value builtin_bt_set_tick_budget_ms(const std::vector<value>& args) {
    require_arity("bt.set-tick-budget-ms", args, 2);
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.set-tick-budget-ms");
    if (!is_integer(args[1])) {
        throw lisp_error("bt.set-tick-budget-ms: expected integer milliseconds");
    }

    bt::instance* inst = bt::default_runtime_host().find_instance(inst_handle);
    if (!inst) {
        throw lisp_error("bt.set-tick-budget-ms: unknown instance");
    }
    bt::set_tick_budget_ms(*inst, integer_value(args[1]));
    return make_nil();
}

value builtin_bt_set_trace_enabled(const std::vector<value>& args) {
    require_arity("bt.set-trace-enabled", args, 2);
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.set-trace-enabled");
    if (!is_boolean(args[1])) {
        throw lisp_error("bt.set-trace-enabled: expected boolean");
    }
    bt::instance* inst = bt::default_runtime_host().find_instance(inst_handle);
    if (!inst) {
        throw lisp_error("bt.set-trace-enabled: unknown instance");
    }
    inst->trace_enabled = boolean_value(args[1]);
    return make_nil();
}

value builtin_bt_set_read_trace_enabled(const std::vector<value>& args) {
    require_arity("bt.set-read-trace-enabled", args, 2);
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.set-read-trace-enabled");
    if (!is_boolean(args[1])) {
        throw lisp_error("bt.set-read-trace-enabled: expected boolean");
    }
    bt::instance* inst = bt::default_runtime_host().find_instance(inst_handle);
    if (!inst) {
        throw lisp_error("bt.set-read-trace-enabled: unknown instance");
    }
    inst->read_trace_enabled = boolean_value(args[1]);
    return make_nil();
}

value builtin_bt_clear_trace(const std::vector<value>& args) {
    require_arity("bt.clear-trace", args, 1);
    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "bt.clear-trace");
    bt::instance* inst = bt::default_runtime_host().find_instance(inst_handle);
    if (!inst) {
        throw lisp_error("bt.clear-trace: unknown instance");
    }
    inst->trace.clear();
    return make_nil();
}

value builtin_bt_clear_logs(const std::vector<value>& args) {
    require_arity("bt.clear-logs", args, 0);
    bt::default_runtime_host().clear_logs();
    return make_nil();
}

value builtin_hash64(const std::vector<value>& args) {
    require_arity("hash64", args, 1);
    std::string text;
    if (is_string(args[0])) {
        text = string_value(args[0]);
    } else if (is_symbol(args[0])) {
        text = symbol_name(args[0]);
    } else {
        throw lisp_error("hash64: expected string or symbol");
    }
    const std::uint64_t h = bt::planner_service::hash64(text);
    return make_integer(static_cast<std::int64_t>(h & 0x7fffffffffffffffull));
}

value builtin_json_encode(const std::vector<value>& args) {
    require_arity("json.encode", args, 1);
    return make_string(value_to_json(args[0]));
}

value builtin_json_decode(const std::vector<value>& args) {
    require_arity("json.decode", args, 1);
    if (!is_string(args[0])) {
        throw lisp_error("json.decode: expected string");
    }
    json_parser parser(string_value(args[0]));
    return parser.parse_document();
}

value require_image_handle_arg(value v, const std::string& where) {
    if (!is_image_handle(v)) {
        throw lisp_error(where + ": expected image_handle");
    }
    return v;
}

value require_blob_handle_arg(value v, const std::string& where) {
    if (!is_blob_handle(v)) {
        throw lisp_error(where + ": expected blob_handle");
    }
    return v;
}

value builtin_image_make(const std::vector<value>& args) {
    require_arity("image.make", args, 6);
    const std::int64_t width = require_int_arg(args[0], "image.make width");
    const std::int64_t height = require_int_arg(args[1], "image.make height");
    const std::int64_t channels = require_int_arg(args[2], "image.make channels");
    const std::string encoding = require_text_value(args[3], "image.make encoding");
    const std::int64_t timestamp_ms = require_int_arg(args[4], "image.make timestamp_ms");
    const std::string frame_id = require_text_value(args[5], "image.make frame_id");

    const bt::image_handle_ref handle = bt::default_runtime_host().vla_ref().create_image(
        width, height, channels, encoding, timestamp_ms, frame_id);
    return make_image_handle(handle.id);
}

value builtin_blob_make(const std::vector<value>& args) {
    require_arity("blob.make", args, 4);
    const std::int64_t size_bytes = require_int_arg(args[0], "blob.make size_bytes");
    const std::string mime_type = require_text_value(args[1], "blob.make mime_type");
    const std::int64_t timestamp_ms = require_int_arg(args[2], "blob.make timestamp_ms");
    const std::string tag = require_text_value(args[3], "blob.make tag");

    const bt::blob_handle_ref handle =
        bt::default_runtime_host().vla_ref().create_blob(size_bytes, mime_type, timestamp_ms, tag);
    return make_blob_handle(handle.id);
}

value builtin_image_info(const std::vector<value>& args) {
    require_arity("image.info", args, 1);
    const value image = require_image_handle_arg(args[0], "image.info");
    const auto info = bt::default_runtime_host().vla_ref().get_image_info(bt::image_handle_ref{.id = image_handle_id(image)});
    if (!info.has_value()) {
        throw lisp_error("image.info: unknown handle");
    }

    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);
    map_set_symbol(out, "id", make_integer(info->id));
    map_set_symbol(out, "w", make_integer(info->width));
    map_set_symbol(out, "h", make_integer(info->height));
    map_set_symbol(out, "channels", make_integer(info->channels));
    map_set_symbol(out, "encoding", make_string(info->encoding));
    map_set_symbol(out, "timestamp_ms", make_integer(info->timestamp_ms));
    map_set_symbol(out, "frame_id", make_string(info->frame_id));
    return out;
}

value builtin_blob_info(const std::vector<value>& args) {
    require_arity("blob.info", args, 1);
    const value blob = require_blob_handle_arg(args[0], "blob.info");
    const auto info = bt::default_runtime_host().vla_ref().get_blob_info(bt::blob_handle_ref{.id = blob_handle_id(blob)});
    if (!info.has_value()) {
        throw lisp_error("blob.info: unknown handle");
    }

    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);
    map_set_symbol(out, "id", make_integer(info->id));
    map_set_symbol(out, "size_bytes", make_integer(info->size_bytes));
    map_set_symbol(out, "mime_type", make_string(info->mime_type));
    map_set_symbol(out, "timestamp_ms", make_integer(info->timestamp_ms));
    map_set_symbol(out, "tag", make_string(info->tag));
    return out;
}

value builtin_cap_list(const std::vector<value>& args) {
    require_arity("cap.list", args, 0);
    const std::vector<std::string> names = bt::default_runtime_host().vla_ref().capabilities().list();
    std::vector<value> out;
    out.reserve(names.size());
    gc_root_scope roots(default_gc());
    for (const std::string& name : names) {
        out.push_back(make_string(name));
        roots.add(&out.back());
    }
    return list_from_vector(out);
}

value builtin_cap_describe(const std::vector<value>& args) {
    require_arity("cap.describe", args, 1);
    const std::string name = require_text_value(args[0], "cap.describe");
    const auto cap = bt::default_runtime_host().vla_ref().capabilities().describe(name);
    if (!cap.has_value()) {
        throw lisp_error("cap.describe: unknown capability");
    }

    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);
    map_set_symbol(out, "name", make_string(cap->name));
    map_set_symbol(out, "safety_class", make_string(cap->safety_class));
    map_set_symbol(out, "cost_category", make_string(cap->cost_category));

    auto schema_to_list = [&](const std::vector<bt::capability_field>& fields) -> value {
        std::vector<value> rows;
        rows.reserve(fields.size());
        for (const auto& field : fields) {
            value row = make_map();
            roots.add(&row);
            map_set_symbol(row, "name", make_string(field.name));
            map_set_symbol(row, "type", make_string(field.type));
            map_set_symbol(row, "required", make_boolean(field.required));
            rows.push_back(row);
            roots.add(&rows.back());
        }
        return list_from_vector(rows);
    };

    value req_schema = schema_to_list(cap->request_schema);
    roots.add(&req_schema);
    value res_schema = schema_to_list(cap->response_schema);
    roots.add(&res_schema);
    map_set_symbol(out, "request_schema", req_schema);
    map_set_symbol(out, "response_schema", res_schema);
    return out;
}

value builtin_vla_submit(const std::vector<value>& args) {
    require_arity("vla.submit", args, 1);
    bt::vla_request req = parse_vla_request_map(args[0]);
    const bt::vla_service::vla_job_id id = bt::default_runtime_host().vla_ref().submit(req);
    return make_integer(static_cast<std::int64_t>(id));
}

value builtin_vla_poll(const std::vector<value>& args) {
    require_arity("vla.poll", args, 1);
    const std::int64_t raw_id = require_non_negative_int(args[0], "vla.poll");
    if (raw_id <= 0) {
        throw lisp_error("vla.poll: job id must be > 0");
    }
    const bt::vla_poll poll = bt::default_runtime_host().vla_ref().poll(static_cast<bt::vla_service::vla_job_id>(raw_id));
    return vla_poll_to_lisp(poll);
}

value builtin_vla_cancel(const std::vector<value>& args) {
    require_arity("vla.cancel", args, 1);
    const std::int64_t raw_id = require_non_negative_int(args[0], "vla.cancel");
    if (raw_id <= 0) {
        throw lisp_error("vla.cancel: job id must be > 0");
    }
    const bool cancelled =
        bt::default_runtime_host().vla_ref().cancel(static_cast<bt::vla_service::vla_job_id>(raw_id));
    return make_boolean(cancelled);
}

value builtin_vla_logs_dump(const std::vector<value>& args) {
    if (args.size() > 1) {
        throw lisp_error("vla.logs.dump: expected 0 or 1 arguments");
    }
    std::size_t max_count = 200;
    if (!args.empty()) {
        const std::int64_t raw = require_non_negative_int(args[0], "vla.logs.dump");
        max_count = static_cast<std::size_t>(raw);
    }
    return make_string(bt::default_runtime_host().dump_vla_records(max_count));
}

value builtin_vla_set_log_path(const std::vector<value>& args) {
    require_arity("vla.set-log-path", args, 1);
    if (!is_string(args[0])) {
        throw lisp_error("vla.set-log-path: expected string path");
    }
    bt::default_runtime_host().vla_ref().set_log_path(string_value(args[0]));
    return make_nil();
}

value builtin_vla_set_log_enabled(const std::vector<value>& args) {
    require_arity("vla.set-log-enabled", args, 1);
    if (!is_boolean(args[0])) {
        throw lisp_error("vla.set-log-enabled: expected boolean");
    }
    bt::default_runtime_host().vla_ref().set_log_enabled(boolean_value(args[0]));
    return make_nil();
}

value builtin_vla_clear_logs(const std::vector<value>& args) {
    require_arity("vla.clear-logs", args, 0);
    bt::default_runtime_host().vla_ref().clear_records();
    return make_nil();
}

std::uint64_t planner_seed_from_value(value seed_v, const std::string& where) {
    if (is_integer(seed_v)) {
        const std::int64_t raw = integer_value(seed_v);
        if (raw < 0) {
            throw lisp_error(where + ": seed must be non-negative");
        }
        return static_cast<std::uint64_t>(raw);
    }
    if (is_float(seed_v)) {
        const double raw = float_value(seed_v);
        if (!std::isfinite(raw) || raw < 0.0) {
            throw lisp_error(where + ": seed must be non-negative finite");
        }
        return static_cast<std::uint64_t>(raw);
    }
    if (is_string(seed_v) || is_symbol(seed_v)) {
        const std::string text = is_string(seed_v) ? string_value(seed_v) : symbol_name(seed_v);
        return bt::planner_service::hash64(text);
    }
    throw lisp_error(where + ": unsupported seed type");
}

bt::planner_action planner_action_from_lisp(value action_v, const std::string& where) {
    const value action_map = require_map_arg(action_v, where);
    const std::optional<value> schema_v = map_lookup_option(action_map, "action_schema");
    if (!schema_v.has_value()) {
        throw lisp_error(where + ": missing action_schema");
    }
    const std::optional<value> u_v = map_lookup_option(action_map, "u");
    if (!u_v.has_value()) {
        throw lisp_error(where + ": missing u");
    }
    bt::planner_action action;
    action.action_schema = require_text_value(*schema_v, where + " action_schema");
    action.u = lisp_to_numeric_vector(*u_v, where + " u");
    return action;
}

value planner_action_to_lisp(const bt::planner_action& action) {
    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);

    value schema = make_string(action.action_schema);
    roots.add(&schema);
    map_set_symbol(out, "action_schema", schema);

    value u = numeric_vector_to_lisp_list(action.u);
    roots.add(&u);
    map_set_symbol(out, "u", u);
    return out;
}

bt::planner_backend planner_backend_from_value(value planner_v, const std::string& where) {
    const std::string text = require_text_value(planner_v, where);
    bt::planner_backend backend = bt::planner_backend::mcts;
    if (!bt::planner_backend_from_string(text, backend)) {
        throw lisp_error(where + ": expected :mcts, :mppi, or :ilqr");
    }
    return backend;
}

bt::planner_ilqr_derivatives_mode planner_derivatives_mode_from_value(value mode_v, const std::string& where) {
    const std::string text = require_text_value(mode_v, where);
    bt::planner_ilqr_derivatives_mode mode = bt::planner_ilqr_derivatives_mode::analytic;
    if (!bt::planner_ilqr_derivatives_mode_from_string(text, mode)) {
        throw lisp_error(where + ": expected :analytic or :finite_diff");
    }
    return mode;
}

bt::planner_request parse_planner_request_map(value request_map) {
    bt::planner_request request;
    request.run_id = "lisp";
    request.node_name = "planner.plan";

    const std::optional<value> schema_v = map_lookup_option(request_map, "schema_version");
    if (!schema_v.has_value()) {
        throw lisp_error("planner.plan: missing required schema_version");
    }
    request.schema_version = require_text_value(*schema_v, "planner.plan schema_version");
    if (request.schema_version != "planner.request.v1") {
        throw lisp_error("planner.plan: schema_version must be \"planner.request.v1\"");
    }

    const std::optional<value> planner_v = map_lookup_option(request_map, "planner");
    if (!planner_v.has_value()) {
        throw lisp_error("planner.plan: missing required planner");
    }
    request.planner = planner_backend_from_value(*planner_v, "planner.plan planner");

    const std::optional<value> model_v = map_lookup_option(request_map, "model_service");
    if (!model_v.has_value()) {
        throw lisp_error("planner.plan: missing required model_service");
    }
    request.model_service = require_text_value(*model_v, "planner.plan model_service");

    const std::optional<value> state_v = map_lookup_option(request_map, "state");
    if (!state_v.has_value()) {
        throw lisp_error("planner.plan: missing required state");
    }
    request.state = lisp_to_numeric_vector(*state_v, "planner.plan state");

    const std::optional<value> budget_v = map_lookup_option(request_map, "budget_ms");
    if (!budget_v.has_value()) {
        throw lisp_error("planner.plan: missing required budget_ms");
    }
    request.budget_ms = require_int_arg(*budget_v, "planner.plan budget_ms");

    if (const std::optional<value> seed_v = map_lookup_option(request_map, "seed"); seed_v.has_value()) {
        request.seed = planner_seed_from_value(*seed_v, "planner.plan");
    } else {
        std::ostringstream seed_material;
        seed_material << request.model_service << '|' << bt::planner_backend_name(request.planner) << '|'
                      << request.budget_ms;
        for (double s : request.state) {
            seed_material << '|' << s;
        }
        request.seed = bt::planner_service::hash64(seed_material.str());
    }

    if (const std::optional<value> safe_v = map_lookup_option(request_map, "safe_action"); safe_v.has_value()) {
        request.safe_action = planner_action_from_lisp(*safe_v, "planner.plan safe_action");
    }
    if (const std::optional<value> action_schema_v = map_lookup_option(request_map, "action_schema");
        action_schema_v.has_value()) {
        request.action_schema = require_text_value(*action_schema_v, "planner.plan action_schema");
    }

    if (const std::optional<value> work_v = map_lookup_option(request_map, "work_max"); work_v.has_value()) {
        request.work_max = require_int_arg(*work_v, "planner.plan work_max");
    }
    if (const std::optional<value> horizon_v = map_lookup_option(request_map, "horizon"); horizon_v.has_value()) {
        request.horizon = require_int_arg(*horizon_v, "planner.plan horizon");
    }
    if (const std::optional<value> dt_v = map_lookup_option(request_map, "dt_ms"); dt_v.has_value()) {
        request.dt_ms = require_int_arg(*dt_v, "planner.plan dt_ms");
    }
    if (const std::optional<value> top_k_v = map_lookup_option(request_map, "top_k"); top_k_v.has_value()) {
        request.top_k = require_int_arg(*top_k_v, "planner.plan top_k");
    }

    if (const std::optional<value> bounds_v = map_lookup_option(request_map, "bounds"); bounds_v.has_value()) {
        const std::vector<std::pair<double, double>> rows = lisp_to_bounds(*bounds_v, "planner.plan bounds");
        request.bounds.reserve(rows.size());
        for (const auto& [lo, hi] : rows) {
            bt::planner_bound b;
            b.lo = lo;
            b.hi = hi;
            request.bounds.push_back(b);
        }
    }

    if (const std::optional<value> constraints_v = map_lookup_option(request_map, "constraints");
        constraints_v.has_value()) {
        const value constraints_map = require_map_arg(*constraints_v, "planner.plan constraints");
        if (const std::optional<value> max_du_v = map_lookup_option(constraints_map, "max_du"); max_du_v.has_value()) {
            request.constraints.max_du = lisp_to_numeric_vector(*max_du_v, "planner.plan constraints.max_du");
        }
        if (const std::optional<value> smooth_v = map_lookup_option(constraints_map, "smoothness_weight");
            smooth_v.has_value()) {
            request.constraints.smoothness_weight =
                require_number_value(*smooth_v, "planner.plan constraints.smoothness_weight");
            request.constraints.has_smoothness_weight = true;
        }
        if (const std::optional<value> collision_v = map_lookup_option(constraints_map, "collision_weight");
            collision_v.has_value()) {
            request.constraints.collision_weight =
                require_number_value(*collision_v, "planner.plan constraints.collision_weight");
            request.constraints.has_collision_weight = true;
        }
        if (const std::optional<value> goal_tol_v = map_lookup_option(constraints_map, "goal_tolerance");
            goal_tol_v.has_value()) {
            request.constraints.goal_tolerance =
                require_number_value(*goal_tol_v, "planner.plan constraints.goal_tolerance");
            request.constraints.has_goal_tolerance = true;
        }
    }

    if (const std::optional<value> run_id_v = map_lookup_option(request_map, "run_id"); run_id_v.has_value()) {
        request.run_id = require_text_value(*run_id_v, "planner.plan run_id");
    }
    if (const std::optional<value> node_name_v = map_lookup_option(request_map, "node_name"); node_name_v.has_value()) {
        request.node_name = require_text_value(*node_name_v, "planner.plan node_name");
    }
    if (const std::optional<value> tick_v = map_lookup_option(request_map, "tick_index"); tick_v.has_value()) {
        const std::int64_t tick_i = require_int_arg(*tick_v, "planner.plan tick_index");
        if (tick_i < 0) {
            throw lisp_error("planner.plan tick_index: expected non-negative integer");
        }
        request.tick_index = static_cast<std::uint64_t>(tick_i);
    }
    if (const std::optional<value> state_key_v = map_lookup_option(request_map, "state_key"); state_key_v.has_value()) {
        request.state_key = require_text_value(*state_key_v, "planner.plan state_key");
    }

    if (const std::optional<value> mcts_v = map_lookup_option(request_map, "mcts"); mcts_v.has_value()) {
        const value cfg_map = require_map_arg(*mcts_v, "planner.plan mcts");
        request.mcts.c_ucb = map_lookup_number_or(cfg_map, "c_ucb", request.mcts.c_ucb, "planner.plan mcts c_ucb");
        request.mcts.pw_k = map_lookup_number_or(cfg_map, "pw_k", request.mcts.pw_k, "planner.plan mcts pw_k");
        request.mcts.pw_alpha =
            map_lookup_number_or(cfg_map, "pw_alpha", request.mcts.pw_alpha, "planner.plan mcts pw_alpha");
        request.mcts.max_depth =
            map_lookup_int_or(cfg_map, "max_depth", request.mcts.max_depth, "planner.plan mcts max_depth");
        request.mcts.gamma = map_lookup_number_or(cfg_map, "gamma", request.mcts.gamma, "planner.plan mcts gamma");
        request.mcts.rollout_policy =
            map_lookup_text_or(cfg_map, "rollout_policy", request.mcts.rollout_policy, "planner.plan mcts rollout_policy");
        request.mcts.action_sampler =
            map_lookup_text_or(cfg_map, "action_sampler", request.mcts.action_sampler, "planner.plan mcts action_sampler");
    }

    if (const std::optional<value> mppi_v = map_lookup_option(request_map, "mppi"); mppi_v.has_value()) {
        const value cfg_map = require_map_arg(*mppi_v, "planner.plan mppi");
        request.mppi.lambda = map_lookup_number_or(cfg_map, "lambda", request.mppi.lambda, "planner.plan mppi lambda");
        request.mppi.n_samples =
            map_lookup_int_or(cfg_map, "n_samples", request.mppi.n_samples, "planner.plan mppi n_samples");
        request.mppi.n_elite = map_lookup_int_or(cfg_map, "n_elite", request.mppi.n_elite, "planner.plan mppi n_elite");
        if (const std::optional<value> sigma_v = map_lookup_option(cfg_map, "sigma"); sigma_v.has_value()) {
            request.mppi.sigma = lisp_to_numeric_vector(*sigma_v, "planner.plan mppi sigma");
        }
        if (const std::optional<value> u_init_v = map_lookup_option(cfg_map, "u_init"); u_init_v.has_value()) {
            request.mppi.u_init = lisp_to_numeric_vector(*u_init_v, "planner.plan mppi u_init");
        }
        if (const std::optional<value> u_nominal_v = map_lookup_option(cfg_map, "u_nominal");
            u_nominal_v.has_value()) {
            request.mppi.u_nominal = lisp_to_numeric_vector(*u_nominal_v, "planner.plan mppi u_nominal");
        }
    }

    if (const std::optional<value> ilqr_v = map_lookup_option(request_map, "ilqr"); ilqr_v.has_value()) {
        const value cfg_map = require_map_arg(*ilqr_v, "planner.plan ilqr");
        request.ilqr.max_iters =
            map_lookup_int_or(cfg_map, "max_iters", request.ilqr.max_iters, "planner.plan ilqr max_iters");
        request.ilqr.reg_init =
            map_lookup_number_or(cfg_map, "reg_init", request.ilqr.reg_init, "planner.plan ilqr reg_init");
        request.ilqr.reg_factor =
            map_lookup_number_or(cfg_map, "reg_factor", request.ilqr.reg_factor, "planner.plan ilqr reg_factor");
        request.ilqr.tol_cost =
            map_lookup_number_or(cfg_map, "tol_cost", request.ilqr.tol_cost, "planner.plan ilqr tol_cost");
        request.ilqr.tol_grad =
            map_lookup_number_or(cfg_map, "tol_grad", request.ilqr.tol_grad, "planner.plan ilqr tol_grad");
        request.ilqr.fd_eps = map_lookup_number_or(cfg_map, "fd_eps", request.ilqr.fd_eps, "planner.plan ilqr fd_eps");
        if (const std::optional<value> mode_v = map_lookup_option(cfg_map, "derivatives"); mode_v.has_value()) {
            request.ilqr.derivatives = planner_derivatives_mode_from_value(*mode_v, "planner.plan ilqr derivatives");
        }
        if (const std::optional<value> u_init_v = map_lookup_option(cfg_map, "u_init"); u_init_v.has_value()) {
            request.ilqr.u_init = lisp_to_numeric_vector(*u_init_v, "planner.plan ilqr u_init");
        }
    }

    return request;
}

value builtin_planner_plan(const std::vector<value>& args) {
    require_arity("planner.plan", args, 1);
    const value request_map = require_map_arg(args[0], "planner.plan");
    bt::planner_request request = parse_planner_request_map(request_map);

    bt::planner_result result = bt::default_runtime_host().planner_ref().plan(request);

    gc_root_scope roots(default_gc());
    value out = make_map();
    roots.add(&out);
    map_set_symbol(out, "schema_version", make_string(result.schema_version));
    map_set_symbol(out, "planner", make_string(bt::planner_backend_name(result.planner)));
    map_set_symbol(out, "status", planner_status_symbol(result.status));
    map_set_symbol(out, "confidence", make_float(result.confidence));

    value action = planner_action_to_lisp(result.action);
    roots.add(&action);
    map_set_symbol(out, "action", action);

    value stats = make_map();
    roots.add(&stats);
    map_set_symbol(stats, "budget_ms", make_integer(result.stats.budget_ms));
    map_set_symbol(stats, "time_used_ms", make_integer(result.stats.time_used_ms));
    map_set_symbol(stats, "work_done", make_integer(result.stats.work_done));
    map_set_symbol(stats, "seed", make_integer(static_cast<std::int64_t>(result.stats.seed & 0x7fffffffffffffffull)));
    if (result.stats.overrun) {
        map_set_symbol(stats, "overrun", make_boolean(true));
    }
    if (!result.stats.note.empty()) {
        map_set_symbol(stats, "note", make_string(result.stats.note));
    }
    map_set_symbol(out, "stats", stats);

    value trace = make_map();
    roots.add(&trace);
    bool has_trace = false;
    if (result.planner == bt::planner_backend::mcts && result.trace.mcts.available) {
        has_trace = true;
        map_set_symbol(trace, "root_visits", make_integer(result.trace.mcts.root_visits));
        map_set_symbol(trace, "root_children", make_integer(result.trace.mcts.root_children));
        map_set_symbol(trace, "widen_added", make_integer(result.trace.mcts.widen_added));
        std::vector<value> entries;
        entries.reserve(result.trace.mcts.top_k.size());
        for (const bt::planner_top_choice_mcts& top : result.trace.mcts.top_k) {
            value entry = make_map();
            roots.add(&entry);
            value top_action = planner_action_to_lisp(top.action);
            roots.add(&top_action);
            map_set_symbol(entry, "action", top_action);
            map_set_symbol(entry, "visits", make_integer(top.visits));
            map_set_symbol(entry, "q", make_float(top.q));
            entries.push_back(entry);
            roots.add(&entries.back());
        }
        value list = list_from_vector(entries);
        roots.add(&list);
        map_set_symbol(trace, "top_k", list);
    } else if (result.planner == bt::planner_backend::mppi && result.trace.mppi.available) {
        has_trace = true;
        map_set_symbol(trace, "n_samples", make_integer(result.trace.mppi.n_samples));
        map_set_symbol(trace, "horizon", make_integer(result.trace.mppi.horizon));
        std::vector<value> entries;
        entries.reserve(result.trace.mppi.top_k.size());
        for (const bt::planner_top_choice_mppi& top : result.trace.mppi.top_k) {
            value entry = make_map();
            roots.add(&entry);
            value top_action = planner_action_to_lisp(top.action);
            roots.add(&top_action);
            map_set_symbol(entry, "action", top_action);
            map_set_symbol(entry, "weight", make_float(top.weight));
            map_set_symbol(entry, "cost", make_float(top.cost));
            entries.push_back(entry);
            roots.add(&entries.back());
        }
        value list = list_from_vector(entries);
        roots.add(&list);
        map_set_symbol(trace, "top_k", list);
    } else if (result.planner == bt::planner_backend::ilqr && result.trace.ilqr.available) {
        has_trace = true;
        map_set_symbol(trace, "iters", make_integer(result.trace.ilqr.iters));
        map_set_symbol(trace, "cost_init", make_float(result.trace.ilqr.cost_init));
        map_set_symbol(trace, "cost_final", make_float(result.trace.ilqr.cost_final));
        map_set_symbol(trace, "reg_final", make_float(result.trace.ilqr.reg_final));
        if (!result.trace.ilqr.top_k.empty()) {
            std::vector<value> entries;
            entries.reserve(result.trace.ilqr.top_k.size());
            for (const bt::planner_top_choice_mppi& top : result.trace.ilqr.top_k) {
                value entry = make_map();
                roots.add(&entry);
                value top_action = planner_action_to_lisp(top.action);
                roots.add(&top_action);
                map_set_symbol(entry, "action", top_action);
                map_set_symbol(entry, "weight", make_float(top.weight));
                map_set_symbol(entry, "cost", make_float(top.cost));
                entries.push_back(entry);
                roots.add(&entries.back());
            }
            value list = list_from_vector(entries);
            roots.add(&list);
            map_set_symbol(trace, "top_k", list);
        }
    }
    if (has_trace) {
        map_set_symbol(out, "trace", trace);
    }

    if (!result.error.empty()) {
        map_set_symbol(out, "error", make_string(result.error));
    }
    return out;
}

value builtin_planner_logs_dump(const std::vector<value>& args) {
    if (args.size() > 1) {
        throw lisp_error("planner.logs.dump: expected 0 or 1 arguments");
    }
    std::size_t max_count = 200;
    if (!args.empty()) {
        const std::int64_t raw = require_int_arg(args[0], "planner.logs.dump");
        if (raw < 0) {
            throw lisp_error("planner.logs.dump: expected non-negative max_count");
        }
        max_count = static_cast<std::size_t>(raw);
    }
    return make_string(bt::default_runtime_host().dump_planner_records(max_count));
}

value builtin_planner_set_log_path(const std::vector<value>& args) {
    require_arity("planner.set-log-path", args, 1);
    if (!is_string(args[0])) {
        throw lisp_error("planner.set-log-path: expected string path");
    }
    bt::default_runtime_host().planner_ref().set_jsonl_path(string_value(args[0]));
    return make_nil();
}

value builtin_planner_set_log_enabled(const std::vector<value>& args) {
    require_arity("planner.set-log-enabled", args, 1);
    if (!is_boolean(args[0])) {
        throw lisp_error("planner.set-log-enabled: expected boolean");
    }
    bt::default_runtime_host().planner_ref().set_jsonl_enabled(boolean_value(args[0]));
    return make_nil();
}

value builtin_planner_set_base_seed(const std::vector<value>& args) {
    require_arity("planner.set-base-seed", args, 1);
    const std::int64_t raw = require_int_arg(args[0], "planner.set-base-seed");
    if (raw < 0) {
        throw lisp_error("planner.set-base-seed: seed must be non-negative");
    }
    bt::default_runtime_host().planner_ref().set_base_seed(static_cast<std::uint64_t>(raw));
    return make_nil();
}

value builtin_planner_get_base_seed(const std::vector<value>& args) {
    require_arity("planner.get-base-seed", args, 0);
    const std::uint64_t seed = bt::default_runtime_host().planner_ref().base_seed();
    return make_integer(static_cast<std::int64_t>(seed & 0x7fffffffffffffffull));
}

}  // namespace

void install_core_builtins(env_ptr global_env) {
    if (!global_env) {
        throw lisp_error("install_core_builtins: null environment");
    }

    define(global_env, "nil", make_nil());
    define(global_env, "true", make_boolean(true));
    define(global_env, "false", make_boolean(false));

    bind_primitive(global_env, "cons", builtin_cons);
    bind_primitive(global_env, "car", builtin_car);
    bind_primitive(global_env, "cdr", builtin_cdr);
    bind_primitive(global_env, "null?", builtin_null);
    bind_primitive(global_env, "eq?", builtin_eq);
    bind_primitive(global_env, "list", builtin_list);

    bind_primitive(global_env, "+", builtin_add);
    bind_primitive(global_env, "-", builtin_sub);
    bind_primitive(global_env, "*", builtin_mul);
    bind_primitive(global_env, "/", builtin_div);
    bind_primitive(global_env, "=", builtin_num_eq);
    bind_primitive(global_env, "<", builtin_less);
    bind_primitive(global_env, ">", builtin_greater);
    bind_primitive(global_env, "<=", builtin_less_equal);
    bind_primitive(global_env, ">=", builtin_greater_equal);
    bind_primitive(global_env, "sqrt", builtin_sqrt);
    bind_primitive(global_env, "log", builtin_log);
    bind_primitive(global_env, "exp", builtin_exp);
    bind_primitive(global_env, "abs", builtin_abs);
    bind_primitive(global_env, "clamp", builtin_clamp);

    bind_primitive(global_env, "number?", builtin_number_pred);
    bind_primitive(global_env, "int?", builtin_integer_pred);
    bind_primitive(global_env, "integer?", builtin_integer_pred);
    bind_primitive(global_env, "float?", builtin_float_pred);
    bind_primitive(global_env, "zero?", builtin_zero_pred);
    bind_primitive(global_env, "time.now-ms", builtin_time_now_ms);
    bind_primitive(global_env, "hash64", builtin_hash64);
    bind_primitive(global_env, "json.encode", builtin_json_encode);
    bind_primitive(global_env, "json.decode", builtin_json_decode);
    bind_primitive(global_env, "image.make", builtin_image_make);
    bind_primitive(global_env, "image.info", builtin_image_info);
    bind_primitive(global_env, "blob.make", builtin_blob_make);
    bind_primitive(global_env, "blob.info", builtin_blob_info);

    bind_primitive(global_env, "rng.make", builtin_rng_make);
    bind_primitive(global_env, "rng.uniform", builtin_rng_uniform);
    bind_primitive(global_env, "rng.normal", builtin_rng_normal);
    bind_primitive(global_env, "rng.int", builtin_rng_int);
    bind_primitive(global_env, "cap.list", builtin_cap_list);
    bind_primitive(global_env, "cap.describe", builtin_cap_describe);
    bind_primitive(global_env, "vla.submit", builtin_vla_submit);
    bind_primitive(global_env, "vla.poll", builtin_vla_poll);
    bind_primitive(global_env, "vla.cancel", builtin_vla_cancel);
    bind_primitive(global_env, "vla.logs.dump", builtin_vla_logs_dump);
    bind_primitive(global_env, "vla.set-log-path", builtin_vla_set_log_path);
    bind_primitive(global_env, "vla.set-log-enabled", builtin_vla_set_log_enabled);
    bind_primitive(global_env, "vla.clear-logs", builtin_vla_clear_logs);
    bind_primitive(global_env, "planner.plan", builtin_planner_plan);
    bind_primitive(global_env, "planner.logs.dump", builtin_planner_logs_dump);
    bind_primitive(global_env, "planner.set-log-path", builtin_planner_set_log_path);
    bind_primitive(global_env, "planner.set-log-enabled", builtin_planner_set_log_enabled);
    bind_primitive(global_env, "planner.set-base-seed", builtin_planner_set_base_seed);
    bind_primitive(global_env, "planner.get-base-seed", builtin_planner_get_base_seed);
    install_env_capability_builtins(global_env);

    bind_primitive(global_env, "vec.make", builtin_vec_make);
    bind_primitive(global_env, "vec.len", builtin_vec_len);
    bind_primitive(global_env, "vec.get", builtin_vec_get);
    bind_primitive(global_env, "vec.set!", builtin_vec_set);
    bind_primitive(global_env, "vec.push!", builtin_vec_push);
    bind_primitive(global_env, "vec.pop!", builtin_vec_pop);
    bind_primitive(global_env, "vec.clear!", builtin_vec_clear);
    bind_primitive(global_env, "vec.reserve!", builtin_vec_reserve);

    bind_primitive(global_env, "map.make", builtin_map_make);
    bind_primitive(global_env, "map.get", builtin_map_get);
    bind_primitive(global_env, "map.has?", builtin_map_has);
    bind_primitive(global_env, "map.set!", builtin_map_set);
    bind_primitive(global_env, "map.del!", builtin_map_del);
    bind_primitive(global_env, "map.keys", builtin_map_keys);

    bind_primitive(global_env, "pq.make", builtin_pq_make);
    bind_primitive(global_env, "pq.len", builtin_pq_len);
    bind_primitive(global_env, "pq.empty?", builtin_pq_empty);
    bind_primitive(global_env, "pq.push!", builtin_pq_push);
    bind_primitive(global_env, "pq.peek", builtin_pq_peek);
    bind_primitive(global_env, "pq.pop!", builtin_pq_pop);

    bind_primitive(global_env, "heap-stats", builtin_heap_stats);
    bind_primitive(global_env, "gc-stats", builtin_gc_stats);

    bind_primitive(global_env, "print", builtin_print);
    bind_primitive(global_env, "write", builtin_write);
    bind_primitive(global_env, "write-to-string", builtin_write_to_string);
    bind_primitive(global_env, "save", builtin_save);

    bind_primitive(global_env, "bt.compile", builtin_bt_compile);
    bind_primitive(global_env, "bt.to-dsl", builtin_bt_to_dsl);
    bind_primitive(global_env, "bt.save-dsl", builtin_bt_save_dsl);
    bind_primitive(global_env, "bt.export-dot", builtin_bt_export_dot);
    bind_primitive(global_env, "bt.load-dsl", builtin_bt_load_dsl);
    bind_primitive(global_env, "bt.save", builtin_bt_save_binary);
    bind_primitive(global_env, "bt.load", builtin_bt_load_binary);
    bind_primitive(global_env, "bt.new-instance", builtin_bt_new_instance);
    bind_primitive(global_env, "bt.tick", builtin_bt_tick);
    bind_primitive(global_env, "bt.reset", builtin_bt_reset);
    bind_primitive(global_env, "bt.status->symbol", builtin_bt_status_to_symbol);

    bind_primitive(global_env, "bt.stats", builtin_bt_stats);
    bind_primitive(global_env, "bt.trace.dump", builtin_bt_trace_dump);
    bind_primitive(global_env, "bt.trace.snapshot", builtin_bt_trace_snapshot);
    bind_primitive(global_env, "bt.blackboard.dump", builtin_bt_blackboard_dump);
    bind_primitive(global_env, "bt.blackboard.get", builtin_bt_blackboard_get);
    bind_primitive(global_env, "bt.logs.dump", builtin_bt_logs_dump);
    bind_primitive(global_env, "bt.logs.snapshot", builtin_bt_logs_snapshot);
    bind_primitive(global_env, "bt.log.dump", builtin_bt_logs_dump);
    bind_primitive(global_env, "bt.log.snapshot", builtin_bt_logs_snapshot);
    bind_primitive(global_env, "bt.scheduler.stats", builtin_bt_scheduler_stats);

    bind_primitive(global_env, "bt.set-tick-budget-ms", builtin_bt_set_tick_budget_ms);
    bind_primitive(global_env, "bt.set-trace-enabled", builtin_bt_set_trace_enabled);
    bind_primitive(global_env, "bt.set-read-trace-enabled", builtin_bt_set_read_trace_enabled);
    bind_primitive(global_env, "bt.clear-trace", builtin_bt_clear_trace);
    bind_primitive(global_env, "bt.clear-logs", builtin_bt_clear_logs);
}

}  // namespace muslisp
