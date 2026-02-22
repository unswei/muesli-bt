#include "bt/compiler.hpp"

#include <string>
#include <vector>

#include "muslisp/error.hpp"

namespace bt {
namespace {

class compiler_state {
public:
    node_id compile_node(muslisp::value expr) {
        if (!muslisp::is_cons(expr)) {
            throw bt_compile_error("BT form must be a list");
        }

        const std::vector<muslisp::value> items = muslisp::vector_from_list(expr);
        if (items.empty()) {
            throw bt_compile_error("BT form list cannot be empty");
        }
        if (!muslisp::is_symbol(items[0])) {
            throw bt_compile_error("BT form head must be a symbol");
        }

        const std::string form_name = muslisp::symbol_name(items[0]);

        if (form_name == "seq" || form_name == "sel") {
            if (items.size() < 2) {
                throw bt_compile_error(form_name + ": expects at least one child");
            }
            node n;
            n.kind = (form_name == "seq") ? node_kind::seq : node_kind::sel;
            for (std::size_t i = 1; i < items.size(); ++i) {
                n.children.push_back(compile_node(items[i]));
            }
            return emit_node(std::move(n));
        }

        if (form_name == "invert") {
            require_arity(form_name, items, 2);
            node n;
            n.kind = node_kind::invert;
            n.children.push_back(compile_node(items[1]));
            return emit_node(std::move(n));
        }

        if (form_name == "repeat" || form_name == "retry") {
            require_arity(form_name, items, 3);
            if (!muslisp::is_integer(items[1])) {
                throw bt_compile_error(form_name + ": repeat/retry count must be integer");
            }

            node n;
            n.kind = (form_name == "repeat") ? node_kind::repeat : node_kind::retry;
            n.int_param = muslisp::integer_value(items[1]);
            if (n.int_param < 0) {
                throw bt_compile_error(form_name + ": count must be non-negative");
            }
            n.children.push_back(compile_node(items[2]));
            return emit_node(std::move(n));
        }

        if (form_name == "cond" || form_name == "act") {
            if (items.size() < 2) {
                throw bt_compile_error(form_name + ": expects at least a leaf name");
            }
            if (!muslisp::is_symbol(items[1]) && !muslisp::is_string(items[1])) {
                throw bt_compile_error(form_name + ": leaf name must be symbol or string");
            }

            node n;
            n.kind = (form_name == "cond") ? node_kind::cond : node_kind::act;
            n.leaf_name = muslisp::is_symbol(items[1]) ? muslisp::symbol_name(items[1]) : muslisp::string_value(items[1]);
            for (std::size_t i = 2; i < items.size(); ++i) {
                n.args.push_back(compile_arg(items[i]));
            }
            return emit_node(std::move(n));
        }

        if (form_name == "succeed") {
            require_arity(form_name, items, 1);
            node n;
            n.kind = node_kind::succeed;
            return emit_node(std::move(n));
        }

        if (form_name == "fail") {
            require_arity(form_name, items, 1);
            node n;
            n.kind = node_kind::fail;
            return emit_node(std::move(n));
        }

        if (form_name == "running") {
            require_arity(form_name, items, 1);
            node n;
            n.kind = node_kind::running;
            return emit_node(std::move(n));
        }

        throw bt_compile_error("unknown BT form: " + form_name);
    }

    definition finish(node_id root) {
        definition def;
        def.nodes = std::move(nodes_);
        def.root = root;
        return def;
    }

private:
    static void require_arity(const std::string& name, const std::vector<muslisp::value>& items, std::size_t expected) {
        if (items.size() != expected) {
            throw bt_compile_error(name + ": expected " + std::to_string(expected - 1) + " arguments, got " +
                                   std::to_string(items.size() - 1));
        }
    }

    static arg_value compile_arg(muslisp::value raw) {
        arg_value arg;

        if (muslisp::is_nil(raw)) {
            arg.kind = arg_kind::nil;
            return arg;
        }

        if (muslisp::is_boolean(raw)) {
            arg.kind = arg_kind::boolean;
            arg.bool_v = muslisp::boolean_value(raw);
            return arg;
        }

        if (muslisp::is_integer(raw)) {
            arg.kind = arg_kind::integer;
            arg.int_v = muslisp::integer_value(raw);
            return arg;
        }

        if (muslisp::is_float(raw)) {
            arg.kind = arg_kind::floating;
            arg.float_v = muslisp::float_value(raw);
            return arg;
        }

        if (muslisp::is_symbol(raw)) {
            arg.kind = arg_kind::symbol;
            arg.text = muslisp::symbol_name(raw);
            return arg;
        }

        if (muslisp::is_string(raw)) {
            arg.kind = arg_kind::string;
            arg.text = muslisp::string_value(raw);
            return arg;
        }

        throw bt_compile_error("leaf args must be literals or symbols");
    }

    node_id emit_node(node n) {
        n.id = static_cast<node_id>(nodes_.size());
        nodes_.push_back(std::move(n));
        return nodes_.back().id;
    }

    std::vector<node> nodes_;
};

}  // namespace

definition compile_definition(muslisp::value form) {
    compiler_state state;
    node_id root = state.compile_node(form);
    return state.finish(root);
}

}  // namespace bt
