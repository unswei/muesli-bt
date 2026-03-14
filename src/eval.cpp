#include "muslisp/eval.hpp"

#include <fstream>
#include <list>
#include <sstream>
#include <utility>

#include "bt/compiler.hpp"
#include "bt/runtime_host.hpp"
#include "muslisp/env_api.hpp"
#include "muslisp/env_builtins.hpp"
#include "muslisp/error.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/reader.hpp"

namespace muslisp {
namespace {

class scoped_env_root {
public:
    scoped_env_root(gc& heap, env_ptr env) : heap_(heap) { reset(env); }

    ~scoped_env_root() { reset(nullptr); }

    scoped_env_root(const scoped_env_root&) = delete;
    scoped_env_root& operator=(const scoped_env_root&) = delete;

    void reset(env_ptr env) {
        if (env_ == env) {
            return;
        }
        if (env_) {
            heap_.unregister_root_env(env_);
        }
        env_ = env;
        if (env_) {
            heap_.register_root_env(env_);
        }
    }

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

/*
 * Evaluator organisation notes:
 *
 * - Atomic values and symbols are handled by the atomic evaluator path.
 * - Cons cells flow through one list-form dispatch seam.
 * - Special forms are classified once and then handled by dedicated helpers.
 * - Callable application is separated from list-form dispatch.
 * - Sequence evaluation is centralised so tail-position plumbing is explicit.
 *
 * Future TCO work should only need to change the behaviour behind tail-position
 * entry points. The tail sites tracked here are:
 * - selected branch of if
 * - last expression of begin / sequence
 * - closure body
 * - let body
 * - selected cond clause body
 *
 * All argument evaluation, tests, let initialisers, load path evaluation, and
 * quasiquote nested evaluation remain explicitly non-tail.
 */
enum class eval_position {
    non_tail,
    tail,
};

enum class special_form_kind {
    none,
    quote,
    if_form,
    let_form,
    and_form,
    or_form,
    cond_form,
    define_form,
    bt_form,
    defbt_form,
    quasiquote_form,
    unquote_form,
    unquote_splicing_form,
    lambda_form,
    begin_form,
    load_form,
};

struct eval_outcome {
    value result = nullptr;
    value next_expr = nullptr;
    env_ptr next_scope = nullptr;
    bool has_tail_call = false;
};

eval_outcome make_eval_result(value result) {
    eval_outcome outcome;
    outcome.result = result;
    return outcome;
}

eval_outcome make_tail_call(value expr, env_ptr scope) {
    eval_outcome outcome;
    outcome.next_expr = expr;
    outcome.next_scope = scope;
    outcome.has_tail_call = true;
    return outcome;
}

value eval_in_position(value expr, env_ptr scope, eval_position position);
eval_outcome eval_step_in_position(value expr, env_ptr scope, eval_position position);
value eval_sequence_in_position(const std::vector<value>& exprs, env_ptr scope, eval_position position);
eval_outcome eval_sequence_step(const std::vector<value>& exprs, env_ptr scope, eval_position position);
eval_outcome apply_callable_in_position(value fn_value, const std::vector<value>& args, eval_position call_position);

value eval_non_tail(value expr, env_ptr scope) {
    return eval_in_position(expr, scope, eval_position::non_tail);
}

eval_outcome eval_or_bounce(value expr, env_ptr scope, eval_position position) {
    if (position == eval_position::tail) {
        return make_tail_call(expr, scope);
    }
    return make_eval_result(eval_non_tail(expr, scope));
}

special_form_kind classify_special_form(value head) {
    if (!is_symbol(head)) {
        return special_form_kind::none;
    }

    const std::string& name = symbol_name(head);
    if (name == "quote") {
        return special_form_kind::quote;
    }
    if (name == "if") {
        return special_form_kind::if_form;
    }
    if (name == "let") {
        return special_form_kind::let_form;
    }
    if (name == "and") {
        return special_form_kind::and_form;
    }
    if (name == "or") {
        return special_form_kind::or_form;
    }
    if (name == "cond") {
        return special_form_kind::cond_form;
    }
    if (name == "define") {
        return special_form_kind::define_form;
    }
    if (name == "bt") {
        return special_form_kind::bt_form;
    }
    if (name == "defbt") {
        return special_form_kind::defbt_form;
    }
    if (name == "quasiquote") {
        return special_form_kind::quasiquote_form;
    }
    if (name == "unquote") {
        return special_form_kind::unquote_form;
    }
    if (name == "unquote-splicing") {
        return special_form_kind::unquote_splicing_form;
    }
    if (name == "lambda") {
        return special_form_kind::lambda_form;
    }
    if (name == "begin") {
        return special_form_kind::begin_form;
    }
    if (name == "load") {
        return special_form_kind::load_form;
    }
    return special_form_kind::none;
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
                    value spliced = eval_non_tail(maybe_form[1], scope);
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
            return eval_non_tail(items[1], scope);
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

value eval_define_form(const std::vector<value>& args, env_ptr scope) {
    expect_min("define", args, 2);

    if (is_symbol(args[0])) {
        expect_exact("define", args, 2);
        const std::string name = symbol_name(args[0]);
        value bound = eval_non_tail(args[1], scope);
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

value eval_lambda_form(const std::vector<value>& args, env_ptr scope) {
    expect_min("lambda", args, 2);
    const auto params = parse_params(args[0]);
    std::vector<value> body(args.begin() + 1, args.end());
    return make_closure(params, body, scope);
}

eval_outcome eval_if_form(const std::vector<value>& args, env_ptr scope, eval_position position) {
    if (args.size() != 2 && args.size() != 3) {
        throw lisp_error("if: expected 2 or 3 arguments");
    }
    value test = eval_non_tail(args[0], scope);
    if (is_truthy(test)) {
        return eval_or_bounce(args[1], scope, position);
    }
    if (args.size() == 3) {
        return eval_or_bounce(args[2], scope, position);
    }
    return make_eval_result(make_nil());
}

eval_outcome eval_and_form(const std::vector<value>& args, env_ptr scope, eval_position position) {
    gc_root_scope roots(default_gc());
    value last = make_boolean(true);
    roots.add(&last);
    if (args.empty()) {
        return make_eval_result(last);
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
        const bool is_last = i + 1 == args.size();
        if (is_last && position == eval_position::tail) {
            return make_tail_call(args[i], scope);
        } else {
            last = eval_non_tail(args[i], scope);
        }
        if (!is_truthy(last)) {
            return make_eval_result(last);
        }
    }
    return make_eval_result(last);
}

eval_outcome eval_or_form(const std::vector<value>& args, env_ptr scope, eval_position position) {
    gc_root_scope roots(default_gc());
    value result = make_nil();
    roots.add(&result);
    if (args.empty()) {
        return make_eval_result(result);
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
        const bool is_last = i + 1 == args.size();
        if (is_last && position == eval_position::tail) {
            return make_tail_call(args[i], scope);
        } else {
            result = eval_non_tail(args[i], scope);
        }
        if (is_truthy(result)) {
            return make_eval_result(result);
        }
    }
    return make_eval_result(make_nil());
}

eval_outcome eval_let_form(const std::vector<value>& args, env_ptr scope, eval_position position) {
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

        value init_value = eval_non_tail(binding_items[1], scope);
        define(child_scope, symbol_name(binding_items[0]), init_value);
    }

    std::vector<value> body(args.begin() + 1, args.end());
    return eval_sequence_step(body, child_scope, position);
}

eval_outcome eval_cond_form(const std::vector<value>& args, env_ptr scope, eval_position position) {
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
            test_result = eval_non_tail(clause[0], scope);
            matched = is_truthy(test_result);
        }

        if (!matched) {
            continue;
        }

        if (clause.size() == 1) {
            return make_eval_result(is_else ? make_nil() : test_result);
        }

        std::vector<value> body(clause.begin() + 1, clause.end());
        return eval_sequence_step(body, scope, position);
    }

    return make_eval_result(make_nil());
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

value eval_defbt_form(const std::vector<value>& args, env_ptr scope) {
    expect_exact("defbt", args, 2);
    if (!is_symbol(args[0])) {
        throw lisp_error("defbt: first argument must be a symbol");
    }
    value compiled = eval_bt_form({args[1]});
    define(scope, symbol_name(args[0]), compiled);
    return compiled;
}

eval_outcome eval_begin_form(const std::vector<value>& args, env_ptr scope, eval_position position) {
    return eval_sequence_step(args, scope, position);
}

value eval_load_form(const std::vector<value>& args, env_ptr scope) {
    expect_exact("load", args, 1);
    value path_value = eval_non_tail(args[0], scope);
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
            last = eval_non_tail(expr, scope);
            default_gc().maybe_collect();
        } catch (const lisp_error& e) {
            throw lisp_error("load: " + path + ": " + std::string(e.what()));
        }
    }
    return last;
}

eval_outcome dispatch_special_form(special_form_kind kind,
                                   const std::vector<value>& raw_args,
                                   env_ptr scope,
                                   eval_position position) {
    switch (kind) {
        case special_form_kind::quote:
            expect_exact("quote", raw_args, 1);
            return make_eval_result(raw_args[0]);
        case special_form_kind::if_form:
            return eval_if_form(raw_args, scope, position);
        case special_form_kind::let_form:
            return eval_let_form(raw_args, scope, position);
        case special_form_kind::and_form:
            return eval_and_form(raw_args, scope, position);
        case special_form_kind::or_form:
            return eval_or_form(raw_args, scope, position);
        case special_form_kind::cond_form:
            return eval_cond_form(raw_args, scope, position);
        case special_form_kind::define_form:
            return make_eval_result(eval_define_form(raw_args, scope));
        case special_form_kind::bt_form:
            return make_eval_result(eval_bt_form(raw_args));
        case special_form_kind::defbt_form:
            return make_eval_result(eval_defbt_form(raw_args, scope));
        case special_form_kind::quasiquote_form:
            expect_exact("quasiquote", raw_args, 1);
            return make_eval_result(eval_quasiquote(raw_args[0], scope, 1));
        case special_form_kind::unquote_form:
            throw lisp_error("unquote: only valid inside quasiquote");
        case special_form_kind::unquote_splicing_form:
            throw lisp_error("unquote-splicing: only valid inside quasiquote list context");
        case special_form_kind::lambda_form:
            return make_eval_result(eval_lambda_form(raw_args, scope));
        case special_form_kind::begin_form:
            return eval_begin_form(raw_args, scope, position);
        case special_form_kind::load_form:
            return make_eval_result(eval_load_form(raw_args, scope));
        case special_form_kind::none:
            break;
    }
    throw eval_error("eval: unknown special form");
}

eval_outcome eval_list_form_in_position(value expr, env_ptr scope, eval_position position) {
    gc_root_scope roots(default_gc());
    roots.add(&expr);

    value head = car(expr);
    roots.add(&head);

    std::vector<value> raw_args = vector_from_list(cdr(expr));
    for (value& arg : raw_args) {
        roots.add(&arg);
    }

    const special_form_kind kind = classify_special_form(head);
    if (kind != special_form_kind::none) {
        return dispatch_special_form(kind, raw_args, scope, position);
    }

    value fn = eval_non_tail(head, scope);
    roots.add(&fn);
    std::vector<value> evaluated_args;
    evaluated_args.reserve(raw_args.size());
    for (value raw_arg : raw_args) {
        evaluated_args.push_back(eval_non_tail(raw_arg, scope));
        roots.add(&evaluated_args.back());
    }
    return apply_callable_in_position(fn, evaluated_args, position);
}

eval_outcome apply_callable_in_position(value fn_value, const std::vector<value>& args, eval_position call_position) {
    if (is_primitive(fn_value)) {
        return make_eval_result(primitive_function(fn_value)(args));
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
        if (call_position == eval_position::tail) {
            return eval_sequence_step(closure_body(fn_value), call_scope, eval_position::tail);
        }
        return make_eval_result(eval_sequence_in_position(closure_body(fn_value), call_scope, eval_position::tail));
    }

    throw type_error("attempt to call non-function value");
}

eval_outcome eval_step_in_position(value expr, env_ptr scope, eval_position position) {
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
            return make_eval_result(expr);
        case value_type::symbol:
            return make_eval_result(lookup(scope, symbol_name(expr)));
        case value_type::cons:
            return eval_list_form_in_position(expr, scope, position);
    }

    throw eval_error("eval: unknown value type");
}

value eval_in_position(value expr, env_ptr scope, eval_position position) {
    gc_root_scope roots(default_gc());
    roots.add(&expr);
    scoped_env_root scope_root(default_gc(), scope);
    std::size_t tail_bounce_count = 0;

    // Tail calls now bounce through this loop instead of recursing through the
    // host C++ stack. Env roots must therefore behave like a stack as scopes
    // come and go, and GC polling needs to stay regular without charging every
    // single tail hop in deep recursion.
    while (true) {
        eval_outcome outcome = eval_step_in_position(expr, scope, position);
        if (!outcome.has_tail_call) {
            return outcome.result;
        }

        expr = outcome.next_expr;
        scope = outcome.next_scope;
        scope_root.reset(scope);
        position = eval_position::tail;
        ++tail_bounce_count;
        if ((tail_bounce_count & 63u) == 0u) {
            default_gc().maybe_collect();
        }
    }
}

eval_outcome eval_sequence_step(const std::vector<value>& exprs, env_ptr scope, eval_position position) {
    value last = make_nil();
    for (std::size_t i = 0; i < exprs.size(); ++i) {
        const bool is_last = i + 1 == exprs.size();
        if (is_last && position == eval_position::tail) {
            return make_tail_call(exprs[i], scope);
        } else {
            last = eval_non_tail(exprs[i], scope);
        }
    }
    return make_eval_result(last);
}

value eval_sequence_in_position(const std::vector<value>& exprs, env_ptr scope, eval_position position) {
    eval_outcome outcome = eval_sequence_step(exprs, scope, position);
    if (outcome.has_tail_call) {
        return eval_in_position(outcome.next_expr, outcome.next_scope, eval_position::tail);
    }
    return outcome.result;
}

}  // namespace

value eval(value expr, env_ptr scope) {
    return eval_non_tail(expr, scope);
}

value eval_sequence(const std::vector<value>& exprs, env_ptr scope) {
    return eval_sequence_in_position(exprs, scope, eval_position::non_tail);
}

value invoke_callable(value fn_value, const std::vector<value>& args) {
    eval_outcome outcome = apply_callable_in_position(fn_value, args, eval_position::non_tail);
    if (outcome.has_tail_call) {
        return eval_in_position(outcome.next_expr, outcome.next_scope, eval_position::tail);
    }
    return outcome.result;
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
        last = eval_non_tail(expr, scope);
        default_gc().maybe_collect();
    }

    return last;
}

env_ptr create_global_env(runtime_config config) {
    env_api_reset();
    reset_env_capability_runtime_state();
    env_ptr global = make_env();
    install_core_builtins(global);
    registrar reg(global);
    bt::runtime_host& host = bt::default_runtime_host();
    for (const auto& ext : config.extensions()) {
        if (!ext) {
            throw lisp_error("create_global_env: null extension entry");
        }
        ext->register_lisp(reg);
        ext->register_bt(host);
    }
    default_gc().register_root_env(global);
    return global;
}

env_ptr create_global_env() {
    runtime_config config;
    return create_global_env(std::move(config));
}

}  // namespace muslisp
