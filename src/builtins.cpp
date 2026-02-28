#include "muslisp/builtins.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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
#include "muslisp/error.hpp"
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

value map_lookup_option_or(value map_obj, const std::string& normalized_key, value default_value) {
    const std::optional<value> found = map_lookup_option(map_obj, normalized_key);
    return found.has_value() ? *found : default_value;
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

value builtin_planner_mcts(const std::vector<value>& args) {
    require_arity("planner.mcts", args, 1);
    const value request_map = require_map_arg(args[0], "planner.mcts");

    bt::planner_request request;
    request.run_id = "lisp";
    request.node_name = "planner.mcts";

    {
        const value model_v = map_lookup_option_or(request_map, "model_service", make_symbol("toy-1d"));
        if (is_symbol(model_v)) {
            request.model_service = symbol_name(model_v);
        } else if (is_string(model_v)) {
            request.model_service = string_value(model_v);
        } else {
            throw lisp_error("planner.mcts: model_service must be symbol or string");
        }
    }

    {
        const std::optional<value> state_v = map_lookup_option(request_map, "state");
        if (!state_v.has_value()) {
            throw lisp_error("planner.mcts: missing required state");
        }
        request.state = lisp_to_numeric_vector(*state_v, "planner.mcts state");
    }

    if (const std::optional<value> budget_v = map_lookup_option(request_map, "budget_ms"); budget_v.has_value()) {
        request.config.budget_ms = require_int_arg(*budget_v, "planner.mcts budget_ms");
    }
    if (const std::optional<value> iters_v = map_lookup_option(request_map, "iters_max"); iters_v.has_value()) {
        request.config.iters_max = require_int_arg(*iters_v, "planner.mcts iters_max");
    }
    if (const std::optional<value> gamma_v = map_lookup_option(request_map, "gamma"); gamma_v.has_value()) {
        request.config.gamma = require_number_value(*gamma_v, "planner.mcts gamma");
    }
    if (const std::optional<value> depth_v = map_lookup_option(request_map, "max_depth"); depth_v.has_value()) {
        request.config.max_depth = require_int_arg(*depth_v, "planner.mcts max_depth");
    }
    if (const std::optional<value> c_v = map_lookup_option(request_map, "c_ucb"); c_v.has_value()) {
        request.config.c_ucb = require_number_value(*c_v, "planner.mcts c_ucb");
    }
    if (const std::optional<value> k_v = map_lookup_option(request_map, "pw_k"); k_v.has_value()) {
        request.config.pw_k = require_number_value(*k_v, "planner.mcts pw_k");
    }
    if (const std::optional<value> alpha_v = map_lookup_option(request_map, "pw_alpha"); alpha_v.has_value()) {
        request.config.pw_alpha = require_number_value(*alpha_v, "planner.mcts pw_alpha");
    }
    if (const std::optional<value> rollout_v = map_lookup_option(request_map, "rollout_policy"); rollout_v.has_value()) {
        if (is_symbol(*rollout_v)) {
            request.config.rollout_policy = symbol_name(*rollout_v);
        } else if (is_string(*rollout_v)) {
            request.config.rollout_policy = string_value(*rollout_v);
        } else {
            throw lisp_error("planner.mcts: rollout_policy must be symbol or string");
        }
    }
    if (const std::optional<value> sampler_v = map_lookup_option(request_map, "action_sampler"); sampler_v.has_value()) {
        if (is_symbol(*sampler_v)) {
            request.config.action_sampler = symbol_name(*sampler_v);
        } else if (is_string(*sampler_v)) {
            request.config.action_sampler = string_value(*sampler_v);
        } else {
            throw lisp_error("planner.mcts: action_sampler must be symbol or string");
        }
    }
    if (const std::optional<value> fallback_v = map_lookup_option(request_map, "fallback_action"); fallback_v.has_value()) {
        request.config.fallback_action = lisp_to_numeric_vector(*fallback_v, "planner.mcts fallback_action");
    }

    if (const std::optional<value> run_id_v = map_lookup_option(request_map, "run_id"); run_id_v.has_value()) {
        if (is_symbol(*run_id_v)) {
            request.run_id = symbol_name(*run_id_v);
        } else if (is_string(*run_id_v)) {
            request.run_id = string_value(*run_id_v);
        } else {
            throw lisp_error("planner.mcts: run_id must be symbol or string");
        }
    }
    if (const std::optional<value> node_name_v = map_lookup_option(request_map, "node_name"); node_name_v.has_value()) {
        if (is_symbol(*node_name_v)) {
            request.node_name = symbol_name(*node_name_v);
        } else if (is_string(*node_name_v)) {
            request.node_name = string_value(*node_name_v);
        } else {
            throw lisp_error("planner.mcts: node_name must be symbol or string");
        }
    }
    if (const std::optional<value> tick_idx_v = map_lookup_option(request_map, "tick_index"); tick_idx_v.has_value()) {
        const std::int64_t tick_i = require_int_arg(*tick_idx_v, "planner.mcts tick_index");
        if (tick_i < 0) {
            throw lisp_error("planner.mcts: tick_index must be non-negative");
        }
        request.tick_index = static_cast<std::uint64_t>(tick_i);
    }
    if (const std::optional<value> state_key_v = map_lookup_option(request_map, "state_key"); state_key_v.has_value()) {
        if (is_symbol(*state_key_v)) {
            request.state_key = symbol_name(*state_key_v);
        } else if (is_string(*state_key_v)) {
            request.state_key = string_value(*state_key_v);
        } else {
            throw lisp_error("planner.mcts: state_key must be symbol or string");
        }
    }

    if (const std::optional<value> seed_v = map_lookup_option(request_map, "seed"); seed_v.has_value()) {
        if (is_integer(*seed_v)) {
            const std::int64_t raw = integer_value(*seed_v);
            if (raw < 0) {
                throw lisp_error("planner.mcts: seed must be non-negative");
            }
            request.seed = static_cast<std::uint64_t>(raw);
        } else if (is_float(*seed_v)) {
            const double raw = float_value(*seed_v);
            if (!std::isfinite(raw) || raw < 0.0) {
                throw lisp_error("planner.mcts: seed must be non-negative finite");
            }
            request.seed = static_cast<std::uint64_t>(raw);
        } else if (is_string(*seed_v) || is_symbol(*seed_v)) {
            const std::string text = is_string(*seed_v) ? string_value(*seed_v) : symbol_name(*seed_v);
            request.seed = bt::planner_service::hash64(text);
        } else {
            throw lisp_error("planner.mcts: unsupported seed type");
        }
    } else {
        request.seed = bt::planner_service::hash64(write_value(args[0]));
    }

    bt::planner_result result = bt::default_runtime_host().planner_ref().plan(request);

    gc_root_scope roots(default_gc());
    value result_map = make_map();
    roots.add(&result_map);

    value action = numeric_vector_to_lisp_list(result.action);
    roots.add(&action);
    map_set_symbol(result_map, "action", action);
    map_set_symbol(result_map, "status", planner_status_symbol(result.status));
    map_set_symbol(result_map, "confidence", make_float(result.confidence));

    value stats = make_map();
    roots.add(&stats);
    map_set_symbol(stats, "iters", make_integer(result.stats.iters));
    map_set_symbol(stats, "root_visits", make_integer(result.stats.root_visits));
    map_set_symbol(stats, "root_children", make_integer(result.stats.root_children));
    map_set_symbol(stats, "widen_added", make_integer(result.stats.widen_added));
    map_set_symbol(stats, "depth_max", make_integer(result.stats.depth_max));
    map_set_symbol(stats, "depth_mean", make_float(result.stats.depth_mean));
    map_set_symbol(stats, "time_used_ms", make_float(result.stats.time_used_ms));
    map_set_symbol(stats, "value_est", make_float(result.stats.value_est));
    map_set_symbol(stats, "seed", make_integer(static_cast<std::int64_t>(result.stats.seed & 0x7fffffffffffffffull)));

    std::vector<value> top_entries;
    top_entries.reserve(result.stats.top_k.size());
    for (const bt::planner_top_choice& top : result.stats.top_k) {
        value entry = make_map();
        roots.add(&entry);
        value top_action = numeric_vector_to_lisp_list(top.action);
        roots.add(&top_action);
        map_set_symbol(entry, "action", top_action);
        map_set_symbol(entry, "visits", make_integer(top.visits));
        map_set_symbol(entry, "q", make_float(top.q));
        top_entries.push_back(entry);
        roots.add(&top_entries.back());
    }
    value top_list = list_from_vector(top_entries);
    roots.add(&top_list);
    map_set_symbol(stats, "top_k", top_list);

    map_set_symbol(result_map, "stats", stats);
    if (!result.error.empty()) {
        map_set_symbol(result_map, "error", make_string(result.error));
    }

    return result_map;
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

    bind_primitive(global_env, "rng.make", builtin_rng_make);
    bind_primitive(global_env, "rng.uniform", builtin_rng_uniform);
    bind_primitive(global_env, "rng.normal", builtin_rng_normal);
    bind_primitive(global_env, "rng.int", builtin_rng_int);
    bind_primitive(global_env, "planner.mcts", builtin_planner_mcts);
    bind_primitive(global_env, "planner.logs.dump", builtin_planner_logs_dump);
    bind_primitive(global_env, "planner.set-log-path", builtin_planner_set_log_path);
    bind_primitive(global_env, "planner.set-log-enabled", builtin_planner_set_log_enabled);
    bind_primitive(global_env, "planner.set-base-seed", builtin_planner_set_base_seed);
    bind_primitive(global_env, "planner.get-base-seed", builtin_planner_get_base_seed);

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

    bind_primitive(global_env, "heap-stats", builtin_heap_stats);
    bind_primitive(global_env, "gc-stats", builtin_gc_stats);

    bind_primitive(global_env, "print", builtin_print);
    bind_primitive(global_env, "write", builtin_write);
    bind_primitive(global_env, "write-to-string", builtin_write_to_string);
    bind_primitive(global_env, "save", builtin_save);

    bind_primitive(global_env, "bt.compile", builtin_bt_compile);
    bind_primitive(global_env, "bt.to-dsl", builtin_bt_to_dsl);
    bind_primitive(global_env, "bt.save-dsl", builtin_bt_save_dsl);
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
