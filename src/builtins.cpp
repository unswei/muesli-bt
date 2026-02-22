#include "muslisp/builtins.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

#include "muslisp/error.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"

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

value builtin_bt_compile(const std::vector<value>&) {
    throw lisp_error("bt.compile is not available in phase 2");
}

value builtin_bt_new_instance(const std::vector<value>&) {
    throw lisp_error("bt.new-instance is not available in phase 2");
}

value builtin_bt_tick(const std::vector<value>&) {
    throw lisp_error("bt.tick is not available in phase 2");
}

value builtin_bt_reset(const std::vector<value>&) {
    throw lisp_error("bt.reset is not available in phase 2");
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

    bind_primitive(global_env, "number?", builtin_number_pred);
    bind_primitive(global_env, "int?", builtin_integer_pred);
    bind_primitive(global_env, "integer?", builtin_integer_pred);
    bind_primitive(global_env, "float?", builtin_float_pred);
    bind_primitive(global_env, "zero?", builtin_zero_pred);

    bind_primitive(global_env, "heap-stats", builtin_heap_stats);
    bind_primitive(global_env, "gc-stats", builtin_gc_stats);

    bind_primitive(global_env, "print", builtin_print);

    bind_primitive(global_env, "bt.compile", builtin_bt_compile);
    bind_primitive(global_env, "bt.new-instance", builtin_bt_new_instance);
    bind_primitive(global_env, "bt.tick", builtin_bt_tick);
    bind_primitive(global_env, "bt.reset", builtin_bt_reset);
}

}  // namespace muslisp
