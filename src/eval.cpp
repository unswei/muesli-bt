#include "muslisp/eval.hpp"

#include <utility>

#include "bt/runtime_host.hpp"
#include "muslisp/error.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/reader.hpp"

namespace muslisp {
namespace {

class scoped_env_root {
public:
    scoped_env_root(gc& heap, env_ptr env) : heap_(heap), env_(env) {
        if (env_) {
            heap_.register_root_env(env_);
        }
    }

    ~scoped_env_root() {
        if (env_) {
            heap_.unregister_root_env(env_);
        }
    }

    scoped_env_root(const scoped_env_root&) = delete;
    scoped_env_root& operator=(const scoped_env_root&) = delete;

private:
    gc& heap_;
    env_ptr env_ = nullptr;
};

void expect_exact(const std::string& name, const std::vector<value>& args, std::size_t expected) {
    if (args.size() != expected) {
        throw lisp_error(name + ": expected " + std::to_string(expected) + " arguments, got " + std::to_string(args.size()));
    }
}

void expect_min(const std::string& name, const std::vector<value>& args, std::size_t minimum) {
    if (args.size() < minimum) {
        throw lisp_error(name + ": expected at least " + std::to_string(minimum) + " arguments, got " +
                         std::to_string(args.size()));
    }
}

bool is_symbol_named(value v, const std::string& name) {
    return is_symbol(v) && symbol_name(v) == name;
}

std::vector<std::string> parse_params(value params_expr) {
    if (!is_proper_list(params_expr)) {
        throw lisp_error("lambda: parameter list must be a proper list");
    }

    std::vector<std::string> params;
    for (value entry : vector_from_list(params_expr)) {
        if (!is_symbol(entry)) {
            throw lisp_error("lambda: parameters must be symbols");
        }
        params.push_back(symbol_name(entry));
    }
    return params;
}

value apply_callable(value fn_value, const std::vector<value>& args) {
    if (is_primitive(fn_value)) {
        return primitive_function(fn_value)(args);
    }

    if (is_closure(fn_value)) {
        const auto& params = closure_params(fn_value);
        if (params.size() != args.size()) {
            throw lisp_error("closure call: expected " + std::to_string(params.size()) + " arguments, got " +
                             std::to_string(args.size()));
        }

        env_ptr call_scope = make_env(closure_env(fn_value));
        scoped_env_root call_scope_root(default_gc(), call_scope);
        for (std::size_t i = 0; i < params.size(); ++i) {
            define(call_scope, params[i], args[i]);
        }
        return eval_sequence(closure_body(fn_value), call_scope);
    }

    throw type_error("attempt to call non-function value");
}

value eval_define(const std::vector<value>& args, env_ptr scope) {
    expect_min("define", args, 2);

    if (is_symbol(args[0])) {
        expect_exact("define", args, 2);
        const std::string name = symbol_name(args[0]);
        value bound = eval(args[1], scope);
        define(scope, name, bound);
        return bound;
    }

    if (is_cons(args[0])) {
        const auto signature = vector_from_list(args[0]);
        if (signature.empty()) {
            throw lisp_error("define: invalid function signature");
        }
        if (!is_symbol(signature[0])) {
            throw lisp_error("define: function name must be a symbol");
        }

        std::vector<value> param_values;
        param_values.reserve(signature.size() - 1);
        for (std::size_t i = 1; i < signature.size(); ++i) {
            param_values.push_back(signature[i]);
        }

        const auto params = parse_params(list_from_vector(param_values));
        std::vector<value> body(args.begin() + 1, args.end());
        const std::string function_name = symbol_name(signature[0]);
        value fn = make_closure(params, body, scope);
        define(scope, function_name, fn);
        return fn;
    }

    throw type_error("define: first argument must be symbol or function signature");
}

value eval_lambda(const std::vector<value>& args, env_ptr scope) {
    expect_min("lambda", args, 2);
    const auto params = parse_params(args[0]);
    std::vector<value> body(args.begin() + 1, args.end());
    return make_closure(params, body, scope);
}

value eval_if(const std::vector<value>& args, env_ptr scope) {
    if (args.size() != 2 && args.size() != 3) {
        throw lisp_error("if: expected 2 or 3 arguments");
    }
    value test = eval(args[0], scope);
    if (is_truthy(test)) {
        return eval(args[1], scope);
    }
    if (args.size() == 3) {
        return eval(args[2], scope);
    }
    return make_nil();
}

value eval_list_form(value expr, env_ptr scope) {
    gc_root_scope roots(default_gc());
    roots.add(&expr);

    value head = car(expr);
    roots.add(&head);

    std::vector<value> raw_args = vector_from_list(cdr(expr));
    for (value& arg : raw_args) {
        roots.add(&arg);
    }

    if (is_symbol_named(head, "quote")) {
        expect_exact("quote", raw_args, 1);
        return raw_args[0];
    }

    if (is_symbol_named(head, "if")) {
        return eval_if(raw_args, scope);
    }

    if (is_symbol_named(head, "define")) {
        return eval_define(raw_args, scope);
    }

    if (is_symbol_named(head, "lambda")) {
        return eval_lambda(raw_args, scope);
    }

    if (is_symbol_named(head, "begin")) {
        return eval_sequence(raw_args, scope);
    }

    value fn = eval(head, scope);
    roots.add(&fn);
    std::vector<value> evaluated_args;
    evaluated_args.reserve(raw_args.size());
    for (value raw_arg : raw_args) {
        evaluated_args.push_back(eval(raw_arg, scope));
        roots.add(&evaluated_args.back());
    }
    return apply_callable(fn, evaluated_args);
}

}  // namespace

value eval(value expr, env_ptr scope) {
    gc_root_scope roots(default_gc());
    roots.add(&expr);

    if (!expr) {
        throw eval_error("eval: null expression");
    }
    if (!scope) {
        throw eval_error("eval: null environment");
    }

    switch (type_of(expr)) {
        case value_type::nil:
        case value_type::boolean:
        case value_type::integer:
        case value_type::floating:
        case value_type::string:
        case value_type::primitive_fn:
        case value_type::closure:
        case value_type::bt_def:
        case value_type::bt_instance:
            return expr;
        case value_type::symbol:
            return lookup(scope, symbol_name(expr));
        case value_type::cons:
            return eval_list_form(expr, scope);
    }

    throw eval_error("eval: unknown value type");
}

value eval_sequence(const std::vector<value>& exprs, env_ptr scope) {
    value last = make_nil();
    for (value expr : exprs) {
        last = eval(expr, scope);
    }
    return last;
}

value eval_source(std::string_view source, env_ptr scope) {
    std::vector<value> exprs = read_all(source);

    gc_root_scope roots(default_gc());
    for (value& expr : exprs) {
        roots.add(&expr);
    }

    value last = make_nil();
    roots.add(&last);

    for (value expr : exprs) {
        last = eval(expr, scope);
        default_gc().maybe_collect();
    }

    return last;
}

env_ptr create_global_env() {
    env_ptr global = make_env();
    install_core_builtins(global);
    bt::install_demo_callbacks(bt::default_runtime_host());
    default_gc().register_root_env(global);
    return global;
}

}  // namespace muslisp
