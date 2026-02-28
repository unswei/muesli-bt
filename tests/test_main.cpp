#include <cmath>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "bt/instance.hpp"
#include "bt/logging.hpp"
#include "bt/runtime_host.hpp"
#include "bt/trace.hpp"
#include "muslisp/env.hpp"
#include "muslisp/error.hpp"
#include "muslisp/eval.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"
#include "muslisp/reader.hpp"

namespace {

template <typename T>
concept has_user_member = requires(T t) {
    t.user;
};

static_assert(!has_user_member<bt::services>, "bt::services should be typed and must not expose void* user");

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_close(double actual, double expected, double epsilon, const std::string& message) {
    if (std::fabs(actual - expected) > epsilon) {
        throw std::runtime_error(message + " (expected " + std::to_string(expected) + ", got " + std::to_string(actual) + ")");
    }
}

muslisp::value eval_text(const std::string& source, muslisp::env_ptr env) {
    return muslisp::eval_source(source, env);
}

void reset_bt_runtime_host() {
    bt::runtime_host& host = bt::default_runtime_host();
    host.clear_all();
    bt::install_demo_callbacks(host);
}

std::filesystem::path temp_file_path(const std::string& stem, const std::string& extension = ".lisp") {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("muesli_bt_" + stem + "_" + std::to_string(now) + extension);
}

void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open file for test write: " + path.string());
    }
    out << content;
    if (!out) {
        throw std::runtime_error("failed while writing test file: " + path.string());
    }
}

std::string lisp_string_literal(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size() + 2);
    escaped.push_back('"');
    for (char c : text) {
        switch (c) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\r':
                escaped += "\\r";
                break;
            default:
                escaped.push_back(c);
                break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

void test_reader_basics() {
    using namespace muslisp;

    check(integer_value(read_one("42")) == 42, "integer parse failed");
    check(boolean_value(read_one("#t")), "#t parse failed");
    check(!boolean_value(read_one("#f")), "#f parse failed");

    value float_a = read_one("3.14");
    check(is_float(float_a), "3.14 should parse as float");
    check_close(float_value(float_a), 3.14, 1e-12, "3.14 parse value mismatch");

    value float_b = read_one("1e-3");
    check(is_float(float_b), "1e-3 should parse as float");
    check_close(float_value(float_b), 1e-3, 1e-12, "1e-3 parse value mismatch");

    value float_c = read_one("2.");
    check(is_float(float_c), "2. should parse as float");
    check_close(float_value(float_c), 2.0, 1e-12, "2. parse value mismatch");

    const auto quoted = read_one("'x");
    check(print_value(quoted) == "(quote x)", "quote sugar parse failed");
    check(print_value(read_one("`x")) == "(quasiquote x)", "quasiquote sugar parse failed");
    check(print_value(read_one(",x")) == "(unquote x)", "unquote sugar parse failed");
    check(print_value(read_one(",@xs")) == "(unquote-splicing xs)", "unquote-splicing sugar parse failed");

    const auto list_expr = read_one("(1 2 3)");
    check(print_value(list_expr) == "(1 2 3)", "list parse failed");

    const auto string_expr = read_one("\"hi\\nthere\"");
    check(string_value(string_expr) == "hi\nthere", "string parse failed");

    const auto exprs = read_all("1 ; comment\n2");
    check(exprs.size() == 2, "comment handling failed");

    try {
        (void)read_all("(");
        throw std::runtime_error("expected parse error for incomplete list");
    } catch (const parse_error& e) {
        check(e.incomplete(), "incomplete parse should be marked incomplete");
    }
}

void test_environment_shadowing() {
    using namespace muslisp;

    env_ptr global = make_env();
    define(global, "x", make_integer(1));

    env_ptr child = make_env(global);
    define(child, "y", make_integer(2));
    define(child, "x", make_integer(3));

    check(integer_value(lookup(global, "x")) == 1, "global lookup failed");
    check(integer_value(lookup(child, "x")) == 3, "shadowed lookup failed");
    check(integer_value(lookup(child, "y")) == 2, "child lookup failed");
}

void test_error_hierarchy_basics() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    try {
        (void)eval_text("missing-symbol", env);
        throw std::runtime_error("expected name_error for unbound symbol");
    } catch (const name_error&) {
    }

    try {
        (void)eval_text("(1 2)", env);
        throw std::runtime_error("expected type_error for non-function call");
    } catch (const type_error&) {
    }
}

void test_eval_special_forms_and_arithmetic() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value sum = eval_text("(+ 1 2 3 4)", env);
    check(is_integer(sum), "+ over ints should return int");
    check(integer_value(sum) == 10, "addition failed");

    value sub = eval_text("(- 9 2 3)", env);
    check(is_integer(sub), "- over ints should return int");
    check(integer_value(sub) == 4, "subtraction failed");

    value mul = eval_text("(* 2 3 4)", env);
    check(is_integer(mul), "* over ints should return int");
    check(integer_value(mul) == 24, "multiplication failed");

    value div = eval_text("(/ 20 2 5)", env);
    check(is_float(div), "/ should always return float");
    check_close(float_value(div), 2.0, 1e-12, "division failed");

    const auto value_if = eval_text("(begin (define x 10) (if (> x 5) x 0))", env);
    check(integer_value(value_if) == 10, "if evaluation failed");

    const auto begin_value = eval_text("(begin (define x 2) (define y 7) (+ x y))", env);
    check(integer_value(begin_value) == 9, "begin evaluation failed");
}

void test_numeric_rules_predicates_and_printing() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value mixed_sum = eval_text("(+ 1 2.5)", env);
    check(is_float(mixed_sum), "mixed + should return float");
    check_close(float_value(mixed_sum), 3.5, 1e-12, "mixed + value mismatch");

    value unary_div = eval_text("(/ 4)", env);
    check(is_float(unary_div), "unary / should return float");
    check_close(float_value(unary_div), 0.25, 1e-12, "unary / value mismatch");

    value zero_arg_plus = eval_text("(+)", env);
    check(is_integer(zero_arg_plus) && integer_value(zero_arg_plus) == 0, "(+ ) identity failed");

    value zero_arg_mul = eval_text("(*)", env);
    check(is_integer(zero_arg_mul) && integer_value(zero_arg_mul) == 1, "(* ) identity failed");

    value unary_sub = eval_text("(- 7)", env);
    check(is_integer(unary_sub) && integer_value(unary_sub) == -7, "unary - failed");

    check(boolean_value(eval_text("(< 3 3.5)", env)), "mixed < should be true");
    check(boolean_value(eval_text("(> 4.0 3)", env)), "mixed > should be true");
    check(boolean_value(eval_text("(<= 3 3.0)", env)), "mixed <= should be true");
    check(boolean_value(eval_text("(>= 4 4.0)", env)), "mixed >= should be true");
    check(boolean_value(eval_text("(= 3 3.0)", env)), "mixed = should be true");

    check(boolean_value(eval_text("(number? 3.0)", env)), "number? failed");
    check(boolean_value(eval_text("(int? 3)", env)), "int? failed");
    check(boolean_value(eval_text("(integer? 3)", env)), "integer? failed");
    check(boolean_value(eval_text("(float? 3.0)", env)), "float? failed");
    check(boolean_value(eval_text("(zero? 0.0)", env)), "zero? float failed");
    check(!boolean_value(eval_text("(zero? 'x)", env)), "zero? non-number should be false");

    value inf_value = eval_text("(/ 1 0)", env);
    check(is_float(inf_value), "(/ 1 0) should produce float");
    check(std::isinf(float_value(inf_value)), "(/ 1 0) should produce infinity");
    check(print_value(inf_value) == "inf", "infinity should print as inf");

    value nan_value = eval_text("(/ 0 0)", env);
    check(is_float(nan_value), "(/ 0 0) should produce float");
    check(std::isnan(float_value(nan_value)), "(/ 0 0) should produce nan");
    check(print_value(nan_value) == "nan", "nan should print as nan");

    check(print_value(make_float(2.0)) == "2.0", "float 2.0 should print with decimal");
}

void test_integer_overflow_checks() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    try {
        (void)eval_text("(* 3037000500 3037000500)", env);
        throw std::runtime_error("expected integer overflow to raise lisp_error");
    } catch (const lisp_error&) {
    }
}

void test_closures_and_function_define_sugar() {
    using namespace muslisp;

    env_ptr env = create_global_env();
    const auto lexical = eval_text(
        "(begin "
        "  (define make-adder (lambda (x) (lambda (y) (+ x y)))) "
        "  (define add5 (make-adder 5)) "
        "  (add5 7))",
        env);
    check(integer_value(lexical) == 12, "closure lexical capture failed");

    const auto sugar = eval_text("(begin (define (inc x) (+ x 1)) (inc 41))", env);
    check(integer_value(sugar) == 42, "define function sugar failed");
}

void test_quasiquote_semantics_and_errors() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value qq = eval_text("(write-to-string `(a ,(+ 1 2) ,@(list 4 5)))", env);
    check(is_string(qq), "quasiquote expansion should return string via write-to-string");
    check(string_value(qq) == "(a 3 4 5)", "quasiquote unquote/splicing expansion mismatch");

    value nested = eval_text("(write-to-string `(outer `(inner ,(+ 1 2))))", env);
    check(is_string(nested), "nested quasiquote should return string via write-to-string");
    check(string_value(nested) == "(outer (quasiquote (inner (unquote (+ 1 2)))))",
          "nested quasiquote depth semantics mismatch");

    try {
        (void)eval_text("(unquote x)", env);
        throw std::runtime_error("expected unquote misuse failure");
    } catch (const lisp_error&) {
    }

    try {
        (void)eval_text("(quasiquote (unquote-splicing (list 1 2)))", env);
        throw std::runtime_error("expected unquote-splicing list-context failure");
    } catch (const lisp_error&) {
    }

    try {
        (void)eval_text("(quasiquote (a (unquote-splicing 1)))", env);
        throw std::runtime_error("expected unquote-splicing non-list failure");
    } catch (const lisp_error&) {
    }
}

void test_let_and_cond_forms() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value let_sum = eval_text("(let ((x 1) (y 2)) (+ x y))", env);
    check(is_integer(let_sum) && integer_value(let_sum) == 3, "let binding sum failed");

    value let_shadow = eval_text("(begin (define x 9) (let ((x 1)) x) x)", env);
    check(is_integer(let_shadow) && integer_value(let_shadow) == 9, "let shadowing should not leak");

    value let_init_scope = eval_text("(begin (define x 10) (let ((x 1) (y x)) y))", env);
    check(is_integer(let_init_scope) && integer_value(let_init_scope) == 10,
          "let initialisers should evaluate in parent scope");

    value cond_else = eval_text("(cond ((< 1 0) 'neg) (else 'pos))", env);
    check(is_symbol(cond_else) && symbol_name(cond_else) == "pos", "cond else clause failed");

    value cond_nil = eval_text("(cond ((< 1 0) 'neg))", env);
    check(is_nil(cond_nil), "cond without matching clause should return nil");

    value cond_multi = eval_text("(cond ((= 1 1) (define z 41) (+ z 1)) (else 0))", env);
    check(is_integer(cond_multi) && integer_value(cond_multi) == 42, "cond multi-expression clause failed");

    try {
        (void)eval_text("(cond (else 1) (#t 2))", env);
        throw std::runtime_error("expected cond else-last validation failure");
    } catch (const lisp_error&) {
    }
}

void test_bt_authoring_sugar() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    value compiled_bt = eval_text("(bt (seq (cond always-true) (act running-then-success)))", env);
    check(is_bt_def(compiled_bt), "bt form should produce bt_def");

    value defined_bt = eval_text("(defbt patrol (seq (cond always-true) (act running-then-success)))", env);
    check(is_bt_def(defined_bt), "defbt should bind bt_def");

    (void)eval_text("(define tree-old (bt.compile '(seq (cond always-true) (act running-then-success))))", env);
    (void)eval_text("(define tree-new (bt (seq (cond always-true) (act running-then-success))))", env);
    (void)eval_text("(define inst-old (bt.new-instance tree-old))", env);
    (void)eval_text("(define inst-new (bt.new-instance tree-new))", env);

    value old_tick1 = eval_text("(bt.tick inst-old)", env);
    value new_tick1 = eval_text("(bt.tick inst-new)", env);
    check(is_symbol(old_tick1) && is_symbol(new_tick1), "bt tick results should be symbols");
    check(symbol_name(old_tick1) == symbol_name(new_tick1), "bt and bt.compile should tick identically (tick1)");

    value old_tick2 = eval_text("(bt.tick inst-old)", env);
    value new_tick2 = eval_text("(bt.tick inst-new)", env);
    check(symbol_name(old_tick2) == symbol_name(new_tick2), "bt and bt.compile should tick identically (tick2)");

    try {
        (void)eval_text("(bt)", env);
        throw std::runtime_error("expected bt arity failure");
    } catch (const lisp_error&) {
    }

    try {
        (void)eval_text("(defbt 42 (succeed))", env);
        throw std::runtime_error("expected defbt name validation failure");
    } catch (const lisp_error&) {
    }
}

void test_load_write_save_and_roundtrip() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    const value original = eval_text("'(1 \"line\\nnext\" #t 2.5 nil foo)", env);
    const value serialised = eval_text("(write-to-string '(1 \"line\\nnext\" #t 2.5 nil foo))", env);
    check(is_string(serialised), "write-to-string should return string");
    const value reparsed = read_one(string_value(serialised));
    check(print_value(reparsed) == print_value(original), "write-to-string should round-trip through reader");

    const auto save_path = temp_file_path("save_value");
    const std::string save_path_lisp = lisp_string_literal(save_path.string());
    value save_ok = eval_text("(save " + save_path_lisp + " '(alpha 1 \"two\"))", env);
    check(is_boolean(save_ok) && boolean_value(save_ok), "save should return #t");

    value loaded_value = eval_text("(load " + save_path_lisp + ")", env);
    check(print_value(loaded_value) == "(alpha 1 \"two\")", "load should evaluate saved readable value");

    const auto script_path = temp_file_path("load_script");
    write_text_file(
        script_path,
        "(define loaded-x 41)\n"
        "(define (loaded-inc x) (+ x 1))\n"
        "(defbt loaded-tree (seq (cond always-true) (act running-then-success)))\n"
        "(define loaded-inst (bt.new-instance loaded-tree))\n"
        "(bt.tick loaded-inst)\n");

    const std::string script_path_lisp = lisp_string_literal(script_path.string());
    value load_result = eval_text("(load " + script_path_lisp + ")", env);
    check(is_symbol(load_result) && symbol_name(load_result) == "running", "load should return last form value");
    check(integer_value(eval_text("loaded-x", env)) == 41, "load should define globals from file");
    check(integer_value(eval_text("(loaded-inc 1)", env)) == 2, "load should define functions from file");
    check(symbol_name(eval_text("(bt.tick loaded-inst)", env)) == "success",
          "loaded BT instance should continue ticking");

    const auto missing_path = temp_file_path("missing_script");
    const std::string missing_path_lisp = lisp_string_literal(missing_path.string());
    try {
        (void)eval_text("(load " + missing_path_lisp + ")", env);
        throw std::runtime_error("expected load missing-file failure");
    } catch (const lisp_error&) {
    }
}

void test_bt_dsl_save_load_roundtrip() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text("(define tree (bt (seq (act bb-put-int foo 42) (cond bb-has foo))))", env);

    value dsl = eval_text("(bt.to-dsl tree)", env);
    check(print_value(dsl) == "(seq (act bb-put-int foo 42) (cond bb-has foo))",
          "bt.to-dsl should return canonical DSL");

    (void)eval_text("(define tree2 (bt.compile (bt.to-dsl tree)))", env);
    (void)eval_text("(define inst1 (bt.new-instance tree))", env);
    (void)eval_text("(define inst2 (bt.new-instance tree2))", env);
    check(symbol_name(eval_text("(bt.tick inst1)", env)) == "success", "source tree tick should succeed");
    check(symbol_name(eval_text("(bt.tick inst2)", env)) == "success", "to-dsl recompiled tree tick should succeed");

    const auto dsl_path = temp_file_path("tree_dsl");
    const std::string dsl_path_lisp = lisp_string_literal(dsl_path.string());
    value save_ok = eval_text("(bt.save-dsl tree " + dsl_path_lisp + ")", env);
    check(is_boolean(save_ok) && boolean_value(save_ok), "bt.save-dsl should return #t");

    (void)eval_text("(define tree3 (bt.load-dsl " + dsl_path_lisp + "))", env);
    (void)eval_text("(define inst3 (bt.new-instance tree3))", env);
    check(symbol_name(eval_text("(bt.tick inst3)", env)) == "success", "bt.load-dsl tree tick should succeed");
}

void test_bt_binary_save_load_roundtrip_and_validation() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text("(define tree (bt (act always-success nil #t 7 3.5 \"txt\" sym)))", env);

    const auto bin_path = temp_file_path("tree_binary", ".mbt");
    const std::string bin_path_lisp = lisp_string_literal(bin_path.string());
    value save_ok = eval_text("(bt.save tree " + bin_path_lisp + ")", env);
    check(is_boolean(save_ok) && boolean_value(save_ok), "bt.save should return #t");

    (void)eval_text("(define tree2 (bt.load " + bin_path_lisp + "))", env);
    (void)eval_text("(define inst1 (bt.new-instance tree))", env);
    (void)eval_text("(define inst2 (bt.new-instance tree2))", env);
    check(symbol_name(eval_text("(bt.tick inst1)", env)) == "success", "original binary source tree tick should succeed");
    check(symbol_name(eval_text("(bt.tick inst2)", env)) == "success", "bt.load binary tree tick should succeed");

    const auto bad_header_path = temp_file_path("bad_header", ".mbt");
    write_text_file(bad_header_path, "NOT_A_VALID_MBT");
    try {
        (void)eval_text("(bt.load " + lisp_string_literal(bad_header_path.string()) + ")", env);
        throw std::runtime_error("expected bt.load invalid-header failure");
    } catch (const lisp_error&) {
    }

    const auto unsupported_arg_path = temp_file_path("unsupported_arg", ".mbt");
    {
        std::ofstream out(unsupported_arg_path, std::ios::binary);
        check(static_cast<bool>(out), "failed to open unsupported_arg test file");

        auto write_u8 = [&](std::uint8_t v) { out.put(static_cast<char>(v)); };
        auto write_u32 = [&](std::uint32_t v) {
            write_u8(static_cast<std::uint8_t>(v & 0xFFu));
            write_u8(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
            write_u8(static_cast<std::uint8_t>((v >> 16u) & 0xFFu));
            write_u8(static_cast<std::uint8_t>((v >> 24u) & 0xFFu));
        };
        auto write_u64 = [&](std::uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                write_u8(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
            }
        };
        auto write_str = [&](const std::string& s) {
            write_u32(static_cast<std::uint32_t>(s.size()));
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
        };

        out.write("MBT1", 4);
        write_u32(1);         // version
        write_u8(1);          // little-endian marker
        write_u8(0);
        write_u8(0);
        write_u8(0);
        write_u32(1);         // node count
        write_u32(0);         // root
        write_u8(6);          // act
        write_u8(0);
        write_u8(0);
        write_u8(0);
        write_u64(0);         // int_param
        write_u32(0);         // children
        write_str("always-success");
        write_u32(1);         // arg count
        write_u8(99);         // unsupported arg kind
    }

    try {
        (void)eval_text("(bt.load " + lisp_string_literal(unsupported_arg_path.string()) + ")", env);
        throw std::runtime_error("expected bt.load unsupported-arg failure");
    } catch (const lisp_error&) {
    }
}

void test_list_and_predicate_builtins() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    const auto second = eval_text("(car (cdr (list 1 2 3)))", env);
    check(integer_value(second) == 2, "car/cdr/list failed");

    const auto null_true = eval_text("(null? (cdr (list 1)))", env);
    check(boolean_value(null_true), "null? expected true");

    const auto eq_symbol = eval_text("(eq? 'a 'a)", env);
    check(boolean_value(eq_symbol), "eq? symbol interning behavior failed");

    const auto eq_int = eval_text("(eq? 4 4)", env);
    check(boolean_value(eq_int), "eq? integer equality failed");

    const auto eq_list = eval_text("(eq? (list 1) (list 1))", env);
    check(!boolean_value(eq_list), "eq? list pointer equality failed");
}

void test_gc_and_stats_builtins() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    for (int i = 0; i < 2000; ++i) {
        (void)eval_text("(list 1 2 3 4 5 6 7 8 9 10)", env);
    }

    default_gc().collect();
    const gc_stats_snapshot stats = default_gc().stats();

    check(stats.total_allocated_objects > 0, "gc stats should report allocated objects");
    check(stats.live_objects_after_last_gc > 0, "gc stats should report live objects");
    check(stats.total_allocated_objects >= stats.live_objects_after_last_gc,
          "total allocated should be >= live after last gc");
    check(stats.next_gc_threshold >= stats.live_objects_after_last_gc,
          "next gc threshold should be >= live after last gc");

    const auto heap_stats_result = eval_text("(heap-stats)", env);
    check(is_nil(heap_stats_result), "heap-stats should return nil");

    const auto gc_stats_result = eval_text("(gc-stats)", env);
    check(is_nil(gc_stats_result), "gc-stats should return nil");
}

void test_gc_during_argument_evaluation() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    // Calling gc-stats while evaluating later arguments must not invalidate earlier values.
    value out = eval_text("(begin (define x (list 1 2 3)) (list x (gc-stats) x))", env);
    const auto items = vector_from_list(out);
    check(items.size() == 3, "list size mismatch");
    check(print_value(items[0]) == "(1 2 3)", "first retained value mismatch");
    check(is_nil(items[1]), "gc-stats return value mismatch");
    check(print_value(items[2]) == "(1 2 3)", "second retained value mismatch");
}

void test_math_time_and_domain_errors() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value sqrt_value = eval_text("(sqrt 4)", env);
    check(is_float(sqrt_value), "sqrt should return float");
    check_close(float_value(sqrt_value), 2.0, 1e-12, "sqrt(4) mismatch");

    value log_value = eval_text("(log 1)", env);
    check(is_float(log_value), "log should return float");
    check_close(float_value(log_value), 0.0, 1e-12, "log(1) mismatch");

    value exp_value = eval_text("(exp 0)", env);
    check(is_float(exp_value), "exp should return float");
    check_close(float_value(exp_value), 1.0, 1e-12, "exp(0) mismatch");

    value abs_i = eval_text("(abs -7)", env);
    check(is_integer(abs_i) && integer_value(abs_i) == 7, "abs over integer mismatch");
    value abs_f = eval_text("(abs -2.5)", env);
    check(is_float(abs_f), "abs over float should return float");
    check_close(float_value(abs_f), 2.5, 1e-12, "abs over float mismatch");

    value clamp_i = eval_text("(clamp 9 0 5)", env);
    check(is_integer(clamp_i) && integer_value(clamp_i) == 5, "clamp int mismatch");
    value clamp_f = eval_text("(clamp 0.2 0.3 0.9)", env);
    check(is_float(clamp_f), "clamp float should return float");
    check_close(float_value(clamp_f), 0.3, 1e-12, "clamp float mismatch");

    value t1 = eval_text("(time.now-ms)", env);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    value t2 = eval_text("(time.now-ms)", env);
    check(is_integer(t1) && is_integer(t2), "time.now-ms should return integer");
    check(integer_value(t2) >= integer_value(t1), "time.now-ms should be monotonic");

    try {
        (void)eval_text("(sqrt -1)", env);
        throw std::runtime_error("expected sqrt domain error");
    } catch (const lisp_error&) {
    }
    try {
        (void)eval_text("(log 0)", env);
        throw std::runtime_error("expected log domain error");
    } catch (const lisp_error&) {
    }
}

void test_rng_determinism_and_ranges() {
    using namespace muslisp;

    env_ptr env = create_global_env();
    value seq = eval_text(
        "(begin "
        "  (define r (rng.make 42)) "
        "  (list "
        "    (rng.int r 1000) "
        "    (rng.int r 1000) "
        "    (rng.int r 1000) "
        "    (rng.int r 1000) "
        "    (rng.int r 1000) "
        "    (rng.int r 1000) "
        "    (rng.int r 1000) "
        "    (rng.int r 1000)))",
        env);
    const std::vector<value> items = vector_from_list(seq);
    const std::vector<std::int64_t> expected = {413, 291, 858, 764, 250, 62, 925, 908};
    check(items.size() == expected.size(), "rng.int deterministic sequence size mismatch");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        check(is_integer(items[i]), "rng.int should return integer");
        check(integer_value(items[i]) == expected[i], "rng.int deterministic sequence mismatch");
    }

    value uniform_ok = eval_text(
        "(begin "
        "  (define r (rng.make 7)) "
        "  (define (check i) "
        "    (if (= i 0) #t "
        "      (let ((x (rng.uniform r -1.25 3.5))) "
        "        (if (>= x -1.25) (if (<= x 3.5) (check (- i 1)) #f) #f)))) "
        "  (check 200))",
        env);
    check(is_boolean(uniform_ok) && boolean_value(uniform_ok), "rng.uniform range check failed");

    value int_ok = eval_text(
        "(begin "
        "  (define r (rng.make 9)) "
        "  (define (check i) "
        "    (if (= i 0) #t "
        "      (let ((x (rng.int r 17))) "
        "        (if (>= x 0) (if (< x 17) (check (- i 1)) #f) #f)))) "
        "  (check 200))",
        env);
    check(is_boolean(int_ok) && boolean_value(int_ok), "rng.int range check failed");

    value normal_repeat = eval_text(
        "(begin "
        "  (define r1 (rng.make 123)) "
        "  (define r2 (rng.make 123)) "
        "  (define a1 (rng.normal r1 0.0 1.0)) "
        "  (define a2 (rng.normal r1 0.0 1.0)) "
        "  (define b1 (rng.normal r2 0.0 1.0)) "
        "  (define b2 (rng.normal r2 0.0 1.0)) "
        "  (if (= a1 b1) (= a2 b2) #f))",
        env);
    check(is_boolean(normal_repeat) && boolean_value(normal_repeat), "rng.normal should be deterministic for fixed seed");

    try {
        (void)eval_text("(begin (define r (rng.make 1)) (rng.int r 0))", env);
        throw std::runtime_error("expected rng.int n>0 validation error");
    } catch (const lisp_error&) {
    }
    try {
        (void)eval_text("(begin (define r (rng.make 1)) (rng.normal r 0.0 -1.0))", env);
        throw std::runtime_error("expected rng.normal sigma>=0 validation error");
    } catch (const lisp_error&) {
    }
}

void test_vec_gc_growth_and_fuzz() {
    using namespace muslisp;

    env_ptr env = create_global_env();
    (void)eval_text("(define v (vec.make 1))", env);

    for (int i = 0; i < 256; ++i) {
        (void)eval_text("(vec.push! v (list 'item " + std::to_string(i) + "))", env);
        if ((i % 23) == 0) {
            default_gc().collect();
        }
    }
    default_gc().collect();

    value len = eval_text("(vec.len v)", env);
    check(is_integer(len) && integer_value(len) == 256, "vec.len mismatch after growth");
    for (int i = 0; i < 256; ++i) {
        value got = eval_text("(vec.get v " + std::to_string(i) + ")", env);
        check(print_value(got) == "(item " + std::to_string(i) + ")", "vec retained value mismatch after GC");
    }

    (void)eval_text("(vec.set! v 7 (list 'replaced 7))", env);
    default_gc().collect();
    check(print_value(eval_text("(vec.get v 7)", env)) == "(replaced 7)", "vec.set! overwrite mismatch");

    std::vector<std::int64_t> model;
    std::uint64_t state = 0x123456789abcdef0ull;
    auto next = [&]() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return state;
    };
    (void)eval_text("(define vf (vec.make 2))", env);
    for (int step = 0; step < 400; ++step) {
        const std::uint64_t r = next();
        if (model.empty() || (r % 3u == 0u)) {
            const std::int64_t value_i = static_cast<std::int64_t>((next() % 4000u)) - 2000;
            (void)eval_text("(vec.push! vf " + std::to_string(value_i) + ")", env);
            model.push_back(value_i);
        } else if (r % 3u == 1u) {
            const std::size_t idx = static_cast<std::size_t>(next() % model.size());
            const std::int64_t value_i = static_cast<std::int64_t>((next() % 4000u)) - 2000;
            (void)eval_text("(vec.set! vf " + std::to_string(idx) + " " + std::to_string(value_i) + ")", env);
            model[idx] = value_i;
        } else {
            const std::size_t idx = static_cast<std::size_t>(next() % model.size());
            value got = eval_text("(vec.get vf " + std::to_string(idx) + ")", env);
            check(is_integer(got), "vec.get in fuzz should return integer");
            check(integer_value(got) == model[idx], "vec fuzz model mismatch");
        }

        if ((step % 37) == 0) {
            default_gc().collect();
        }
    }
    check(integer_value(eval_text("(vec.len vf)", env)) == static_cast<std::int64_t>(model.size()), "vec fuzz len mismatch");
    for (std::size_t i = 0; i < model.size(); ++i) {
        value got = eval_text("(vec.get vf " + std::to_string(i) + ")", env);
        check(integer_value(got) == model[i], "vec fuzz final state mismatch");
    }
}

void test_map_gc_rehash_and_ops() {
    using namespace muslisp;

    env_ptr env = create_global_env();
    (void)eval_text("(define m (map.make))", env);

    for (int i = 0; i < 400; ++i) {
        const std::string key = "\"k" + std::to_string(i) + "\"";
        (void)eval_text("(map.set! m " + key + " (list 'v " + std::to_string(i) + "))", env);
        if ((i % 41) == 0) {
            default_gc().collect();
        }
    }
    default_gc().collect();

    for (int i = 0; i < 400; ++i) {
        const std::string key = "\"k" + std::to_string(i) + "\"";
        value got = eval_text("(map.get m " + key + " nil)", env);
        check(print_value(got) == "(v " + std::to_string(i) + ")", "map retrieval mismatch after GC/rehash");
    }

    (void)eval_text("(map.set! m \"k12\" (list 'new 12))", env);
    check(print_value(eval_text("(map.get m \"k12\" nil)", env)) == "(new 12)", "map overwrite mismatch");
    check(boolean_value(eval_text("(map.has? m \"k12\")", env)), "map.has? should be true");
    check(boolean_value(eval_text("(map.del! m \"k12\")", env)), "map.del! should return true on existing key");
    check(!boolean_value(eval_text("(map.has? m \"k12\")", env)), "map.has? should be false after delete");
    check(boolean_value(eval_text("(null? (map.get m \"k12\" nil))", env)), "map.get should return default for missing key");

    value keys = eval_text("(map.keys m)", env);
    check(is_proper_list(keys), "map.keys should return a proper list");
    check(integer_value(eval_text("(vec.len (vec.make 0))", env)) == 0, "sanity check for vec.make default path");

    try {
        (void)eval_text("(map.set! m '(bad key) 1)", env);
        throw std::runtime_error("expected map key type validation error");
    } catch (const lisp_error&) {
    }
}

void test_continuous_mcts_smoke_deterministic() {
    using namespace muslisp;

    env_ptr env = create_global_env();
    const std::string program =
        "(begin "
        "  (define goal 1.0) "
        "  (define (step x a) "
        "    (let ((a2 (clamp a -1.0 1.0))) "
        "      (let ((x2 (+ x (* 0.25 a2)))) "
        "        (- 0.0 (abs (- goal x2)))))) "
        "  (define (pw-allow? n-visits n-children k alpha) "
        "    (< n-children (* k (exp (* alpha (log (if (< n-visits 1) 1 n-visits))))))) "
        "  (define (ucb q n parent-n c) "
        "    (if (= n 0) 1.0e30 (+ q (* c (sqrt (/ (log (if (< parent-n 1) 1 parent-n)) n)))))) "
        "  (define (child.new a) "
        "    (let ((m (map.make))) "
        "      (begin (map.set! m 'a a) (map.set! m 'n 0) (map.set! m 'w 0.0) m))) "
        "  (define (child.q ch) "
        "    (let ((n (map.get ch 'n 0)) (w (map.get ch 'w 0.0))) "
        "      (if (= n 0) 0.0 (/ w n)))) "
        "  (define (node.new) "
        "    (let ((m (map.make))) "
        "      (begin (map.set! m 'n 0) (map.set! m 'w 0.0) (map.set! m 'children (vec.make 4)) m))) "
        "  (define (select-child children i nch parent-n c best best-score) "
        "    (if (>= i nch) "
        "        best "
        "        (let ((ch (vec.get children i))) "
        "          (let ((score (ucb (child.q ch) (map.get ch 'n 0) parent-n c))) "
        "            (if (> score best-score) "
        "                (select-child children (+ i 1) nch parent-n c ch score) "
        "                (select-child children (+ i 1) nch parent-n c best best-score)))))) "
        "  (define (simulate root x rng c k alpha) "
        "    (let ((n (map.get root 'n 0)) (children (map.get root 'children nil))) "
        "      (let ((nch (vec.len children))) "
        "        (if (pw-allow? n nch k alpha) "
        "            (let ((a (rng.uniform rng -1.0 1.0))) "
        "              (let ((ch (child.new a)) (v (step x a))) "
        "                (begin "
        "                  (vec.push! children ch) "
        "                  (map.set! ch 'n (+ (map.get ch 'n 0) 1)) "
        "                  (map.set! ch 'w (+ (map.get ch 'w 0.0) v)) "
        "                  (map.set! root 'n (+ n 1)) "
        "                  (map.set! root 'w (+ (map.get root 'w 0.0) v)) "
        "                  v))) "
        "            (let ((ch (select-child children 0 nch n c nil -1.0e30))) "
        "              (let ((v (step x (map.get ch 'a 0.0)))) "
        "                (begin "
        "                  (map.set! ch 'n (+ (map.get ch 'n 0) 1)) "
        "                  (map.set! ch 'w (+ (map.get ch 'w 0.0) v)) "
        "                  (map.set! root 'n (+ n 1)) "
        "                  (map.set! root 'w (+ (map.get root 'w 0.0) v)) "
        "                  v))))))) "
        "  (define (search-loop root x rng i iters c k alpha) "
        "    (if (>= i iters) "
        "        root "
        "        (begin "
        "          (simulate root x rng c k alpha) "
        "          (search-loop root x rng (+ i 1) iters c k alpha)))) "
        "  (define (best-child children i nch best best-n) "
        "    (if (>= i nch) "
        "        best "
        "        (let ((ch (vec.get children i)) (cn (map.get (vec.get children i) 'n 0))) "
        "          (if (> cn best-n) "
        "              (best-child children (+ i 1) nch ch cn) "
        "              (best-child children (+ i 1) nch best best-n))))) "
        "  (define (mcts.search x0 seed iters) "
        "    (let ((rng (rng.make seed)) (root (node.new)) (c 1.2) (k 2.0) (alpha 0.5)) "
        "      (begin "
        "        (search-loop root x0 rng 0 iters c k alpha) "
        "        (let ((children (map.get root 'children nil))) "
        "          (let ((best (best-child children 0 (vec.len children) nil -1))) "
        "            (map.get best 'a 0.0)))))) "
        "  (let ((a1 (mcts.search 0.0 42 350)) (a2 (mcts.search 0.0 42 350))) "
        "    (list a1 a2)))";

    value out = eval_text(program, env);
    const std::vector<value> pair = vector_from_list(out);
    check(pair.size() == 2, "mcts smoke output shape mismatch");
    check(is_float(pair[0]) && is_float(pair[1]), "mcts smoke should return float actions");
    const double a1 = float_value(pair[0]);
    const double a2 = float_value(pair[1]);
    check(a1 > 0.0, "mcts smoke action should move toward positive goal");
    check_close(a1, a2, 1e-12, "mcts smoke should be deterministic for fixed seed");
}

void test_bt_compile_checks() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    try {
        (void)eval_text("(bt.compile '(seq))", env);
        throw std::runtime_error("expected bt.compile arity check failure");
    } catch (const lisp_error&) {
    }

    try {
        (void)eval_text("(bt.compile '(unknown foo))", env);
        throw std::runtime_error("expected bt.compile unknown-form failure");
    } catch (const lisp_error&) {
    }

    try {
        (void)eval_text("(bt.compile '(invert (succeed) (fail)))", env);
        throw std::runtime_error("expected bt.compile invert arity failure");
    } catch (const lisp_error&) {
    }

    try {
        (void)eval_text("(bt.compile '(repeat -1 (succeed)))", env);
        throw std::runtime_error("expected bt.compile negative repeat count failure");
    } catch (const lisp_error&) {
    }
}

void test_bt_seq_and_running_semantics() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text("(define tree (bt.compile '(seq (cond always-true) (act running-then-success))))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);

    value first = eval_text("(bt.tick inst)", env);
    check(is_symbol(first) && symbol_name(first) == "running", "first tick should be running");

    value second = eval_text("(bt.tick inst)", env);
    check(is_symbol(second) && symbol_name(second) == "success", "second tick should be success");
}

void test_bt_decorator_semantics() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text("(define invert-tree (bt.compile '(invert (cond always-false))))", env);
    (void)eval_text("(define invert-inst (bt.new-instance invert-tree))", env);
    check(symbol_name(eval_text("(bt.tick invert-inst)", env)) == "success", "invert should flip failure to success");

    (void)eval_text("(define invert-running-tree (bt.compile '(invert (running))))", env);
    (void)eval_text("(define invert-running-inst (bt.new-instance invert-running-tree))", env);
    check(symbol_name(eval_text("(bt.tick invert-running-inst)", env)) == "running",
          "invert should leave running unchanged");

    (void)eval_text("(define rtree (bt.compile '(repeat 3 (act always-success))))", env);
    (void)eval_text("(define rinst (bt.new-instance rtree))", env);
    check(symbol_name(eval_text("(bt.tick rinst)", env)) == "running", "repeat tick1 should be running");
    check(symbol_name(eval_text("(bt.tick rinst)", env)) == "running", "repeat tick2 should be running");
    check(symbol_name(eval_text("(bt.tick rinst)", env)) == "success", "repeat tick3 should be success");

    (void)eval_text("(define retry-tree (bt.compile '(retry 2 (act always-fail))))", env);
    (void)eval_text("(define retry-inst (bt.new-instance retry-tree))", env);
    check(symbol_name(eval_text("(bt.tick retry-inst)", env)) == "running", "retry tick1 should be running");
    check(symbol_name(eval_text("(bt.tick retry-inst)", env)) == "running", "retry tick2 should be running");
    check(symbol_name(eval_text("(bt.tick retry-inst)", env)) == "failure", "retry tick3 should be failure");
}

void test_bt_reset_clears_phase4_state() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text("(define rtree (bt.compile '(repeat 2 (act always-success))))", env);
    (void)eval_text("(define rinst (bt.new-instance rtree))", env);
    check(symbol_name(eval_text("(bt.tick rinst)", env)) == "running", "repeat pre-reset tick should be running");
    check(is_nil(eval_text("(bt.reset rinst)", env)), "bt.reset should return nil");
    check(symbol_name(eval_text("(bt.tick rinst)", env)) == "running", "repeat should restart after reset");
    check(symbol_name(eval_text("(bt.tick rinst)", env)) == "success", "repeat should complete after restart");

    (void)eval_text("(define btree (bt.compile '(cond bb-has foo)))", env);
    (void)eval_text("(define binst (bt.new-instance btree))", env);
    check(symbol_name(eval_text("(bt.tick binst '((foo 1)))", env)) == "success",
          "tick input should make bb-has succeed");
    (void)eval_text("(bt.reset binst)", env);
    check(symbol_name(eval_text("(bt.tick binst)", env)) == "failure", "reset should clear blackboard entries");
}

void test_bt_blackboard_trace_and_stats_builtins() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text("(define tree (bt.compile '(seq (act bb-put-int foo 42) (cond bb-has foo))))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success", "bb tree should tick to success");

    value bb_dump = eval_text("(bt.blackboard.dump inst)", env);
    check(is_string(bb_dump), "bt.blackboard.dump should return string");
    check(string_value(bb_dump).find("foo=42") != std::string::npos, "blackboard dump missing foo=42");
    check(string_value(bb_dump).find("type=int64") != std::string::npos, "blackboard dump missing type metadata");
    check(string_value(bb_dump).find("ts_ns=") != std::string::npos, "blackboard dump missing timestamp metadata");
    check(string_value(bb_dump).find("writer_name=bb-put-int") != std::string::npos,
          "blackboard dump missing writer metadata");

    value trace_dump = eval_text("(bt.trace.snapshot inst)", env);
    check(is_string(trace_dump), "bt.trace.snapshot should return string");
    check(string_value(trace_dump).find("kind=tick_begin") != std::string::npos, "trace should include tick_begin");
    check(string_value(trace_dump).find("kind=node_enter") != std::string::npos, "trace should include node_enter");
    check(string_value(trace_dump).find("kind=node_exit") != std::string::npos, "trace should include node_exit");
    check(string_value(trace_dump).find("bb_write") != std::string::npos, "trace should include bb_write");
    check(string_value(trace_dump).find("duration_ns=") != std::string::npos, "trace should include duration metadata");
    check(string_value(trace_dump).find("ts_ns=") != std::string::npos, "trace should include timestamp metadata");

    value stats_dump = eval_text("(bt.stats inst)", env);
    check(is_string(stats_dump), "bt.stats should return string");
    check(string_value(stats_dump).find("tick_count=1") != std::string::npos, "stats should include tick_count=1");

    (void)eval_text("(bt.set-tick-budget-ms inst 1)", env);
    (void)eval_text("(bt.set-trace-enabled inst #t)", env);
    (void)eval_text("(bt.clear-trace inst)", env);
    (void)eval_text("(bt.set-read-trace-enabled inst #t)", env);
    (void)eval_text("(bt.tick inst)", env);
    trace_dump = eval_text("(bt.trace.snapshot inst)", env);
    check(string_value(trace_dump).find("bb_read") != std::string::npos, "trace should include bb_read");
    (void)eval_text("(bt.clear-logs)", env);

    value log_dump_alias = eval_text("(bt.log.dump)", env);
    check(is_string(log_dump_alias), "bt.log.dump alias should return string");
    value log_snapshot_alias = eval_text("(bt.log.snapshot)", env);
    check(is_string(log_snapshot_alias), "bt.log.snapshot alias should return string");
}

void test_bt_scheduler_backed_action() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text("(define tree (bt.compile '(act async-sleep-ms 10)))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);

    value st = eval_text("(bt.tick inst)", env);
    check(is_symbol(st) && symbol_name(st) == "running", "first async tick should be running");

    bool done = false;
    for (int i = 0; i < 40; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        st = eval_text("(bt.tick inst)", env);
        if (is_symbol(st) && symbol_name(st) == "success") {
            done = true;
            break;
        }
    }
    check(done, "async action should eventually succeed");

    value trace_dump = eval_text("(bt.trace.snapshot inst)", env);
    check(is_string(trace_dump), "bt.trace.snapshot should return string");
    check(string_value(trace_dump).find("scheduler_submit") != std::string::npos,
          "trace should include scheduler_submit");
    check(string_value(trace_dump).find("scheduler_finish") != std::string::npos,
          "trace should include scheduler_finish");

    value sched_stats = eval_text("(bt.scheduler.stats)", env);
    check(is_string(sched_stats), "bt.scheduler.stats should return string");
    check(string_value(sched_stats).find("submitted=") != std::string::npos, "scheduler stats missing submitted");
}

void test_bt_tick_with_blackboard_input() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text("(define tree (bt.compile '(cond bb-has foo)))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "failure", "bb key should not exist before input");
    check(symbol_name(eval_text("(bt.tick inst '((foo 1)))", env)) == "success", "tick input should seed blackboard");
}

void test_phase5_ring_buffer_bounds() {
    bt::trace_buffer trace(3);

    bt::trace_event a{};
    a.kind = bt::trace_event_kind::tick_begin;
    trace.push(a);
    bt::trace_event b{};
    b.kind = bt::trace_event_kind::node_enter;
    trace.push(b);
    bt::trace_event c{};
    c.kind = bt::trace_event_kind::node_exit;
    trace.push(c);
    bt::trace_event d{};
    d.kind = bt::trace_event_kind::tick_end;
    trace.push(d);

    const auto trace_events = trace.snapshot();
    check(trace_events.size() == 3, "trace ring should cap at configured capacity");
    check(trace_events.front().sequence == 2, "trace ring should evict oldest event first");
    check(trace_events.back().sequence == 4, "trace ring should keep newest event");

    bt::memory_log_sink logs(2);
    bt::log_record r1{};
    r1.message = "one";
    logs.write(r1);
    bt::log_record r2{};
    r2.message = "two";
    logs.write(r2);
    bt::log_record r3{};
    r3.message = "three";
    logs.write(r3);

    const auto log_records = logs.snapshot();
    check(log_records.size() == 2, "log ring should cap at configured capacity");
    check(log_records.front().sequence == 2, "log ring should evict oldest record first");
    check(log_records.back().sequence == 3, "log ring should keep newest record");
}

void test_phase6_sample_wrappers_tree() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text(
        "(define tree "
        "  (bt.compile "
        "    '(sel "
        "       (seq "
        "         (cond battery-ok) "
        "         (cond target-visible) "
        "         (act approach-target) "
        "         (act grasp)) "
        "       (act search-target))))",
        env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);

    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success",
          "tick1 should use search-target fallback and succeed");
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "running",
          "tick2 should run approach-target once target is visible");
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success",
          "tick3 should complete approach-target and grasp");
}

class test_robot_service final : public bt::robot_interface {
public:
    bool battery_ok(bt::tick_context&) override {
        ++battery_checks;
        return true;
    }

    bool target_visible(bt::tick_context&) override {
        ++visibility_checks;
        return visible;
    }

    bt::status approach_target(bt::tick_context&, bt::node_memory&) override {
        ++approach_calls;
        return bt::status::success;
    }

    bt::status grasp(bt::tick_context&, bt::node_memory&) override {
        ++grasp_calls;
        return bt::status::success;
    }

    bt::status search_target(bt::tick_context&, bt::node_memory&) override {
        ++search_calls;
        visible = true;
        return bt::status::success;
    }

    bool visible = false;
    int battery_checks = 0;
    int visibility_checks = 0;
    int approach_calls = 0;
    int grasp_calls = 0;
    int search_calls = 0;
};

void test_phase6_custom_robot_interface() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    test_robot_service robot;
    host.set_robot_interface(&robot);

    env_ptr env = create_global_env();
    (void)eval_text(
        "(define tree "
        "  (bt.compile "
        "    '(sel "
        "       (seq "
        "         (cond battery-ok) "
        "         (cond target-visible) "
        "         (act approach-target) "
        "         (act grasp)) "
        "       (act search-target))))",
        env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);

    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success", "custom robot tick1 should search and succeed");
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success", "custom robot tick2 should approach/grasp and succeed");

    check(robot.search_calls == 1, "custom robot search-target should be called once");
    check(robot.approach_calls == 1, "custom robot approach-target should be called once");
    check(robot.grasp_calls == 1, "custom robot grasp should be called once");
    check(robot.battery_checks >= 2, "custom robot battery-ok should be checked each tick");
    check(robot.visibility_checks >= 2, "custom robot target-visible should be checked each tick");

    host.set_robot_interface(nullptr);
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"reader basics", test_reader_basics},
        {"environment shadowing", test_environment_shadowing},
        {"error hierarchy basics", test_error_hierarchy_basics},
        {"eval special forms and arithmetic", test_eval_special_forms_and_arithmetic},
        {"numeric rules, predicates, and printing", test_numeric_rules_predicates_and_printing},
        {"integer overflow checks", test_integer_overflow_checks},
        {"closures and function define sugar", test_closures_and_function_define_sugar},
        {"quasiquote semantics and errors", test_quasiquote_semantics_and_errors},
        {"let and cond forms", test_let_and_cond_forms},
        {"bt authoring sugar", test_bt_authoring_sugar},
        {"load/write/save and roundtrip", test_load_write_save_and_roundtrip},
        {"bt dsl save/load roundtrip", test_bt_dsl_save_load_roundtrip},
        {"bt binary save/load roundtrip and validation", test_bt_binary_save_load_roundtrip_and_validation},
        {"list and predicate builtins", test_list_and_predicate_builtins},
        {"gc and stats builtins", test_gc_and_stats_builtins},
        {"gc during argument evaluation", test_gc_during_argument_evaluation},
        {"math/time builtins and domain errors", test_math_time_and_domain_errors},
        {"rng determinism and ranges", test_rng_determinism_and_ranges},
        {"vec gc/growth/fuzz", test_vec_gc_growth_and_fuzz},
        {"map gc/rehash/ops", test_map_gc_rehash_and_ops},
        {"continuous mcts smoke deterministic", test_continuous_mcts_smoke_deterministic},
        {"bt compile checks", test_bt_compile_checks},
        {"bt seq/running semantics", test_bt_seq_and_running_semantics},
        {"bt decorator semantics", test_bt_decorator_semantics},
        {"bt reset clears phase4 state", test_bt_reset_clears_phase4_state},
        {"bt blackboard/trace/stats builtins", test_bt_blackboard_trace_and_stats_builtins},
        {"bt scheduler-backed action", test_bt_scheduler_backed_action},
        {"bt tick with blackboard input", test_bt_tick_with_blackboard_input},
        {"phase5 ring buffer bounds", test_phase5_ring_buffer_bounds},
        {"phase6 sample wrappers tree", test_phase6_sample_wrappers_tree},
        {"phase6 custom robot interface", test_phase6_custom_robot_interface},
    };

    std::size_t passed = 0;
    for (const auto& [name, test_fn] : tests) {
        try {
            test_fn();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& e) {
            std::cerr << "[FAIL] " << name << ": " << e.what() << '\n';
            return 1;
        }
    }

    std::cout << "All tests passed (" << passed << "/" << tests.size() << ").\n";
    return 0;
}
