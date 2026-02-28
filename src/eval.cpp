#include "muslisp/eval.hpp"

#include <fstream>
#include <list>
#include <sstream>
#include <utility>

#include "bt/compiler.hpp"
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

std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw lisp_error("failed to open file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
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

value eval_quasiquote(value expr, env_ptr scope, std::size_t depth);

value eval_quasiquote_list(value expr, env_ptr scope, std::size_t depth) {
    gc_root_scope roots(default_gc());
    roots.add(&expr);

    if (!is_proper_list(expr)) {
        throw lisp_error("quasiquote: expected proper list");
    }

    std::vector<value> raw_items = vector_from_list(expr);
    for (value& item : raw_items) {
        roots.add(&item);
    }

    std::list<value> out_items;
    for (value raw_item : raw_items) {
        if (is_cons(raw_item) && is_proper_list(raw_item)) {
            const std::vector<value> maybe_form = vector_from_list(raw_item);
            if (!maybe_form.empty() && is_symbol_named(maybe_form[0], "unquote-splicing")) {
                if (maybe_form.size() != 2) {
                    throw lisp_error("unquote-splicing: expected 1 argument");
                }

                if (depth == 1) {
                    value spliced = eval(maybe_form[1], scope);
                    roots.add(&spliced);
                    if (!is_proper_list(spliced)) {
                        throw lisp_error("unquote-splicing: expected list value");
                    }

                    std::vector<value> splice_items = vector_from_list(spliced);
                    for (value& splice_item : splice_items) {
                        roots.add(&splice_item);
                        out_items.push_back(splice_item);
                        roots.add(&out_items.back());
                    }
                    continue;
                }

                value nested = eval_quasiquote(maybe_form[1], scope, depth - 1);
                roots.add(&nested);
                out_items.push_back(list_from_vector({make_symbol("unquote-splicing"), nested}));
                roots.add(&out_items.back());
                continue;
            }
        }

        value item = eval_quasiquote(raw_item, scope, depth);
        roots.add(&item);
        out_items.push_back(item);
        roots.add(&out_items.back());
    }

    std::vector<value> out;
    out.reserve(out_items.size());
    for (value item : out_items) {
        out.push_back(item);
    }
    return list_from_vector(out);
}

value eval_quasiquote(value expr, env_ptr scope, std::size_t depth) {
    gc_root_scope roots(default_gc());
    roots.add(&expr);

    if (!is_cons(expr)) {
        return expr;
    }

    if (!is_proper_list(expr)) {
        throw lisp_error("quasiquote: expected proper list");
    }

    const std::vector<value> items = vector_from_list(expr);
    if (items.empty()) {
        return expr;
    }

    if (is_symbol_named(items[0], "unquote")) {
        if (items.size() != 2) {
            throw lisp_error("unquote: expected 1 argument");
        }
        if (depth == 1) {
            return eval(items[1], scope);
        }
        value nested = eval_quasiquote(items[1], scope, depth - 1);
        roots.add(&nested);
        return list_from_vector({make_symbol("unquote"), nested});
    }

    if (is_symbol_named(items[0], "unquote-splicing")) {
        if (items.size() != 2) {
            throw lisp_error("unquote-splicing: expected 1 argument");
        }
        if (depth == 1) {
            throw lisp_error("unquote-splicing: only valid in list context");
        }
        value nested = eval_quasiquote(items[1], scope, depth - 1);
        roots.add(&nested);
        return list_from_vector({make_symbol("unquote-splicing"), nested});
    }

    if (is_symbol_named(items[0], "quasiquote")) {
        if (items.size() != 2) {
            throw lisp_error("quasiquote: expected 1 argument");
        }
        value nested = eval_quasiquote(items[1], scope, depth + 1);
        roots.add(&nested);
        return list_from_vector({make_symbol("quasiquote"), nested});
    }

    return eval_quasiquote_list(expr, scope, depth);
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

value eval_let(const std::vector<value>& args, env_ptr scope) {
    expect_min("let", args, 2);
    value bindings_expr = args[0];
    if (!is_proper_list(bindings_expr)) {
        throw lisp_error("let: expected binding list");
    }

    env_ptr child_scope = make_env(scope);
    scoped_env_root child_scope_root(default_gc(), child_scope);

    const std::vector<value> bindings = vector_from_list(bindings_expr);
    for (value binding_expr : bindings) {
        if (!is_proper_list(binding_expr)) {
            throw lisp_error("let: each binding must be a (name expr) pair");
        }
        const std::vector<value> binding_items = vector_from_list(binding_expr);
        if (binding_items.size() != 2) {
            throw lisp_error("let: each binding must contain exactly name and expression");
        }
        if (!is_symbol(binding_items[0])) {
            throw lisp_error("let: binding name must be a symbol");
        }

        value init_value = eval(binding_items[1], scope);
        define(child_scope, symbol_name(binding_items[0]), init_value);
    }

    std::vector<value> body(args.begin() + 1, args.end());
    return eval_sequence(body, child_scope);
}

value eval_cond(const std::vector<value>& args, env_ptr scope) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        value clause_expr = args[i];
        if (!is_proper_list(clause_expr)) {
            throw lisp_error("cond: each clause must be a proper list");
        }
        const std::vector<value> clause = vector_from_list(clause_expr);
        if (clause.empty()) {
            throw lisp_error("cond: clause cannot be empty");
        }

        const bool is_else = is_symbol_named(clause[0], "else");
        if (is_else && i + 1 != args.size()) {
            throw lisp_error("cond: else clause must be last");
        }

        bool matched = false;
        value test_result = make_nil();
        if (is_else) {
            matched = true;
        } else {
            test_result = eval(clause[0], scope);
            matched = is_truthy(test_result);
        }

        if (!matched) {
            continue;
        }

        if (clause.size() == 1) {
            return is_else ? make_nil() : test_result;
        }

        std::vector<value> body(clause.begin() + 1, clause.end());
        return eval_sequence(body, scope);
    }

    return make_nil();
}

value eval_bt_form(const std::vector<value>& args) {
    expect_exact("bt", args, 1);
    try {
        bt::definition def = bt::compile_definition(args[0]);
        const std::int64_t handle = bt::default_runtime_host().store_definition(std::move(def));
        return make_bt_def(handle);
    } catch (const bt::bt_compile_error& e) {
        throw lisp_error(std::string("bt: ") + e.what());
    }
}

value eval_defbt(const std::vector<value>& args, env_ptr scope) {
    expect_exact("defbt", args, 2);
    if (!is_symbol(args[0])) {
        throw lisp_error("defbt: first argument must be a symbol");
    }
    value compiled = eval_bt_form({args[1]});
    define(scope, symbol_name(args[0]), compiled);
    return compiled;
}

value eval_load(const std::vector<value>& args, env_ptr scope) {
    expect_exact("load", args, 1);
    value path_value = eval(args[0], scope);
    if (!is_string(path_value)) {
        throw lisp_error("load: expected file path string");
    }
    const std::string path = string_value(path_value);

    std::string source;
    try {
        source = read_text_file(path);
    } catch (const lisp_error& e) {
        throw lisp_error("load: " + path + ": " + e.what());
    }

    std::vector<value> exprs;
    try {
        exprs = read_all(source);
    } catch (const parse_error& e) {
        throw lisp_error("load: " + path + ": " + std::string(e.what()));
    }

    gc_root_scope roots(default_gc());
    for (value& expr : exprs) {
        roots.add(&expr);
    }

    value last = make_nil();
    roots.add(&last);
    for (value expr : exprs) {
        try {
            last = eval(expr, scope);
            default_gc().maybe_collect();
        } catch (const lisp_error& e) {
            throw lisp_error("load: " + path + ": " + std::string(e.what()));
        }
    }
    return last;
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

    if (is_symbol_named(head, "let")) {
        return eval_let(raw_args, scope);
    }

    if (is_symbol_named(head, "cond")) {
        return eval_cond(raw_args, scope);
    }

    if (is_symbol_named(head, "define")) {
        return eval_define(raw_args, scope);
    }

    if (is_symbol_named(head, "bt")) {
        return eval_bt_form(raw_args);
    }

    if (is_symbol_named(head, "defbt")) {
        return eval_defbt(raw_args, scope);
    }

    if (is_symbol_named(head, "quasiquote")) {
        expect_exact("quasiquote", raw_args, 1);
        return eval_quasiquote(raw_args[0], scope, 1);
    }

    if (is_symbol_named(head, "unquote")) {
        throw lisp_error("unquote: only valid inside quasiquote");
    }

    if (is_symbol_named(head, "unquote-splicing")) {
        throw lisp_error("unquote-splicing: only valid inside quasiquote list context");
    }

    if (is_symbol_named(head, "lambda")) {
        return eval_lambda(raw_args, scope);
    }

    if (is_symbol_named(head, "begin")) {
        return eval_sequence(raw_args, scope);
    }

    if (is_symbol_named(head, "load")) {
        return eval_load(raw_args, scope);
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
        case value_type::vec:
        case value_type::map:
        case value_type::pq:
        case value_type::rng:
        case value_type::bt_def:
        case value_type::bt_instance:
        case value_type::image_handle:
        case value_type::blob_handle:
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
