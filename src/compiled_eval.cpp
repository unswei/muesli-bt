#include "compiled_eval.hpp"

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "muslisp/env.hpp"
#include "muslisp/error.hpp"
#include "muslisp/eval.hpp"

namespace muslisp {
namespace {

bool is_symbol_named(value v, const std::string& name) {
    return is_symbol(v) && symbol_name(v) == name;
}

struct compile_scope {
    std::unordered_map<std::string, std::size_t> locals;
    std::size_t next_slot = 0;
};

class rooted_value_stack {
public:
    void push(value v) {
        values_.push_back(v);
        default_gc().add_root_slot(&values_.back());
    }

    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

    value pop() {
        if (values_.empty()) {
            throw eval_error("compiled closure: stack underflow");
        }
        value out = values_.back();
        default_gc().remove_root_slot(&values_.back());
        values_.pop_back();
        return out;
    }

    void discard() {
        if (values_.empty()) {
            throw eval_error("compiled closure: stack underflow");
        }
        default_gc().remove_root_slot(&values_.back());
        values_.pop_back();
    }

    value& top() {
        if (values_.empty()) {
            throw eval_error("compiled closure: stack underflow");
        }
        return values_.back();
    }

private:
    std::deque<value> values_;
};

void emit(compiled_closure& out,
          compiled_opcode opcode,
          std::size_t index = 0,
          value literal = nullptr,
          std::string text = {}) {
    compiled_instruction instr;
    instr.opcode = opcode;
    instr.index = index;
    instr.literal = literal;
    instr.text = std::move(text);
    out.code.push_back(std::move(instr));
}

bool compile_expr(value expr, const compile_scope& scope, compiled_closure& out, bool tail_position);

bool compile_sequence(const std::vector<value>& exprs,
                      const compile_scope& scope,
                      compiled_closure& out,
                      bool tail_position) {
    if (exprs.empty()) {
        emit(out, compiled_opcode::push_const, 0, make_nil());
        return true;
    }

    for (std::size_t i = 0; i < exprs.size(); ++i) {
        const bool is_last = i + 1 == exprs.size();
        if (!compile_expr(exprs[i], scope, out, tail_position && is_last)) {
            return false;
        }
        if (!is_last) {
            emit(out, compiled_opcode::pop);
        }
    }
    return true;
}

bool compile_if_form(const std::vector<value>& args,
                     const compile_scope& scope,
                     compiled_closure& out,
                     bool tail_position) {
    if (args.size() != 2 && args.size() != 3) {
        return false;
    }
    if (!compile_expr(args[0], scope, out, false)) {
        return false;
    }

    const std::size_t false_jump = out.code.size();
    emit(out, compiled_opcode::jump_if_false, 0);
    if (!compile_expr(args[1], scope, out, tail_position)) {
        return false;
    }

    const std::size_t end_jump = out.code.size();
    emit(out, compiled_opcode::jump, 0);
    out.code[false_jump].index = out.code.size();

    if (args.size() == 3) {
        if (!compile_expr(args[2], scope, out, tail_position)) {
            return false;
        }
    } else {
        emit(out, compiled_opcode::push_const, 0, make_nil());
    }

    out.code[end_jump].index = out.code.size();
    return true;
}

bool compile_and_form(const std::vector<value>& args,
                      const compile_scope& scope,
                      compiled_closure& out,
                      bool tail_position) {
    if (args.empty()) {
        emit(out, compiled_opcode::push_const, 0, make_boolean(true));
        return true;
    }

    std::vector<std::size_t> exit_jumps;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const bool is_last = i + 1 == args.size();
        if (!compile_expr(args[i], scope, out, tail_position && is_last)) {
            return false;
        }
        if (!is_last) {
            exit_jumps.push_back(out.code.size());
            emit(out, compiled_opcode::jump_if_false_keep, 0);
            emit(out, compiled_opcode::pop);
        }
    }

    const std::size_t exit_target = out.code.size();
    for (std::size_t jump_index : exit_jumps) {
        out.code[jump_index].index = exit_target;
    }
    return true;
}

bool compile_or_form(const std::vector<value>& args,
                     const compile_scope& scope,
                     compiled_closure& out,
                     bool tail_position) {
    if (args.empty()) {
        emit(out, compiled_opcode::push_const, 0, make_nil());
        return true;
    }

    std::vector<std::size_t> exit_jumps;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const bool is_last = i + 1 == args.size();
        if (!compile_expr(args[i], scope, out, tail_position && is_last)) {
            return false;
        }
        if (!is_last) {
            exit_jumps.push_back(out.code.size());
            emit(out, compiled_opcode::jump_if_truthy_keep, 0);
            emit(out, compiled_opcode::pop);
        }
    }

    const std::size_t exit_target = out.code.size();
    for (std::size_t jump_index : exit_jumps) {
        out.code[jump_index].index = exit_target;
    }
    return true;
}

bool compile_let_form(const std::vector<value>& args,
                      const compile_scope& scope,
                      compiled_closure& out,
                      bool tail_position) {
    if (args.size() < 2) {
        return false;
    }
    value bindings_expr = args[0];
    if (!is_proper_list(bindings_expr)) {
        return false;
    }

    const std::vector<value> bindings = vector_from_list(bindings_expr);
    compile_scope body_scope = scope;
    std::vector<std::pair<value, std::size_t>> compiled_bindings;
    compiled_bindings.reserve(bindings.size());

    for (value binding_expr : bindings) {
        if (!is_proper_list(binding_expr)) {
            return false;
        }
        const std::vector<value> binding_items = vector_from_list(binding_expr);
        if (binding_items.size() != 2 || !is_symbol(binding_items[0])) {
            return false;
        }
        const std::size_t slot = body_scope.next_slot++;
        body_scope.locals[symbol_name(binding_items[0])] = slot;
        compiled_bindings.emplace_back(binding_items[1], slot);
    }
    if (body_scope.next_slot > out.local_count) {
        out.local_count = body_scope.next_slot;
    }

    for (const auto& [init_expr, slot] : compiled_bindings) {
        if (!compile_expr(init_expr, scope, out, false)) {
            return false;
        }
        emit(out, compiled_opcode::store_local, slot);
    }

    std::vector<value> body(args.begin() + 1, args.end());
    return compile_sequence(body, body_scope, out, tail_position);
}

bool compile_call_form(const std::vector<value>& items,
                       const compile_scope& scope,
                       compiled_closure& out,
                       bool tail_position) {
    if (items.empty()) {
        return false;
    }

    if (!compile_expr(items[0], scope, out, false)) {
        return false;
    }
    for (std::size_t i = 1; i < items.size(); ++i) {
        if (!compile_expr(items[i], scope, out, false)) {
            return false;
        }
    }

    emit(out, tail_position ? compiled_opcode::tail_call : compiled_opcode::call, items.size() - 1);
    return true;
}

bool compile_list_form(value expr, const compile_scope& scope, compiled_closure& out, bool tail_position) {
    if (!is_proper_list(expr)) {
        return false;
    }

    const std::vector<value> items = vector_from_list(expr);
    if (items.empty()) {
        return false;
    }

    value head = items[0];
    std::vector<value> args(items.begin() + 1, items.end());

    if (is_symbol_named(head, "quote")) {
        if (args.size() != 1) {
            return false;
        }
        emit(out, compiled_opcode::push_const, 0, args[0]);
        return true;
    }
    if (is_symbol_named(head, "if")) {
        return compile_if_form(args, scope, out, tail_position);
    }
    if (is_symbol_named(head, "begin")) {
        return compile_sequence(args, scope, out, tail_position);
    }
    if (is_symbol_named(head, "let")) {
        return compile_let_form(args, scope, out, tail_position);
    }
    if (is_symbol_named(head, "and")) {
        return compile_and_form(args, scope, out, tail_position);
    }
    if (is_symbol_named(head, "or")) {
        return compile_or_form(args, scope, out, tail_position);
    }

    if (is_symbol(head)) {
        const std::string& special = symbol_name(head);
        if (special == "cond" || special == "define" || special == "lambda" || special == "quasiquote" ||
            special == "load" || special == "bt" || special == "defbt" || special == "unquote" ||
            special == "unquote-splicing") {
            return false;
        }
    }

    return compile_call_form(items, scope, out, tail_position);
}

bool compile_expr(value expr, const compile_scope& scope, compiled_closure& out, bool tail_position) {
    if (!expr) {
        return false;
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
            emit(out, compiled_opcode::push_const, 0, expr);
            return true;
        case value_type::symbol: {
            const auto found = scope.locals.find(symbol_name(expr));
            if (found != scope.locals.end()) {
                emit(out, compiled_opcode::load_local, found->second);
            } else {
                emit(out, compiled_opcode::load_name, 0, nullptr, symbol_name(expr));
            }
            return true;
        }
        case value_type::cons:
            return compile_list_form(expr, scope, out, tail_position);
    }

    return false;
}

std::vector<value> pop_call_args(rooted_value_stack& stack, std::size_t argc) {
    std::vector<value> args(argc);
    for (std::size_t i = argc; i > 0; --i) {
        args[i - 1] = stack.pop();
    }
    return args;
}

}  // namespace

std::shared_ptr<compiled_closure> try_compile_closure(const std::vector<std::string>& params,
                                                      const std::vector<value>& body) {
    compile_scope scope;
    scope.next_slot = params.size();
    for (std::size_t i = 0; i < params.size(); ++i) {
        scope.locals[params[i]] = i;
    }

    auto compiled = std::make_shared<compiled_closure>();
    compiled->local_count = params.size();
    if (!compile_sequence(body, scope, *compiled, true)) {
        return nullptr;
    }
    emit(*compiled, compiled_opcode::return_value);
    return compiled;
}

void compiled_closure_mark_children(const std::shared_ptr<compiled_closure>& compiled, gc& heap) {
    if (!compiled) {
        return;
    }
    for (const compiled_instruction& instr : compiled->code) {
        if (instr.literal) {
            heap.mark_value(instr.literal);
        }
    }
}

value execute_compiled_closure(value fn_value, const std::vector<value>& args) {
    value active_fn = fn_value;
    std::vector<value> active_args = args;
    std::size_t tail_bounce_count = 0;

    while (true) {
        gc_root_scope input_roots(default_gc());
        input_roots.add(&active_fn);
        for (value& arg : active_args) {
            input_roots.add(&arg);
        }
        if (tail_bounce_count != 0 && (tail_bounce_count & 63u) == 0u) {
            default_gc().maybe_collect();
        }

        const auto& compiled = closure_compiled(active_fn);
        if (!compiled) {
            throw eval_error("execute_compiled_closure: missing compiled body");
        }
        if (closure_params(active_fn).size() != active_args.size()) {
            throw lisp_error("closure call: expected " + std::to_string(closure_params(active_fn).size()) +
                             " arguments, got " + std::to_string(active_args.size()));
        }

        bool reuse_frame = false;
        value next_fn = nullptr;
        std::vector<value> next_args;

        {
            std::vector<value> locals(compiled->local_count, make_nil());
            gc_root_scope local_roots(default_gc());
            for (value& local : locals) {
                local_roots.add(&local);
            }
            for (std::size_t i = 0; i < active_args.size(); ++i) {
                locals[i] = active_args[i];
            }

            rooted_value_stack stack;
            const env_ptr lookup_env = closure_env(active_fn);
            std::size_t ip = 0;
            while (ip < compiled->code.size()) {
                const compiled_instruction& instr = compiled->code[ip];
                switch (instr.opcode) {
                    case compiled_opcode::push_const:
                        stack.push(instr.literal);
                        ++ip;
                        break;
                    case compiled_opcode::load_local:
                        if (instr.index >= locals.size()) {
                            throw eval_error("compiled closure: invalid local slot");
                        }
                        stack.push(locals[instr.index]);
                        ++ip;
                        break;
                    case compiled_opcode::load_name:
                        stack.push(lookup(lookup_env, instr.text));
                        ++ip;
                        break;
                    case compiled_opcode::store_local: {
                        if (instr.index >= locals.size()) {
                            throw eval_error("compiled closure: invalid local slot");
                        }
                        locals[instr.index] = stack.pop();
                        ++ip;
                        break;
                    }
                    case compiled_opcode::pop:
                        stack.discard();
                        ++ip;
                        break;
                    case compiled_opcode::jump:
                        ip = instr.index;
                        break;
                    case compiled_opcode::jump_if_false: {
                        value cond = stack.pop();
                        ip = is_truthy(cond) ? ip + 1 : instr.index;
                        break;
                    }
                    case compiled_opcode::jump_if_false_keep:
                        ip = is_truthy(stack.top()) ? ip + 1 : instr.index;
                        break;
                    case compiled_opcode::jump_if_truthy_keep:
                        ip = is_truthy(stack.top()) ? instr.index : ip + 1;
                        break;
                    case compiled_opcode::call: {
                        std::vector<value> call_args = pop_call_args(stack, instr.index);
                        value callee = stack.pop();
                        stack.push(invoke_callable(callee, call_args));
                        ++ip;
                        break;
                    }
                    case compiled_opcode::tail_call: {
                        std::vector<value> call_args = pop_call_args(stack, instr.index);
                        value callee = stack.pop();
                        if (is_closure(callee) && closure_compiled(callee)) {
                            next_fn = callee;
                            next_args = std::move(call_args);
                            reuse_frame = true;
                            ip = compiled->code.size();
                            break;
                        }
                        return invoke_callable(callee, call_args);
                    }
                    case compiled_opcode::return_value:
                        return stack.empty() ? make_nil() : stack.pop();
                }
            }
        }

        if (!reuse_frame) {
            throw eval_error("compiled closure: fell off end of bytecode");
        }

        active_fn = next_fn;
        active_args = std::move(next_args);
        ++tail_bounce_count;
    }
}

}  // namespace muslisp
