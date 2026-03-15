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
#include <utility>
#include <vector>

#include "bt/instance.hpp"
#include "bt/logging.hpp"
#include "bt/runtime_host.hpp"
#include "bt/trace.hpp"
#include "../src/compiled_eval.hpp"
#include "../src/repl_support.hpp"
#if MUESLI_BT_WITH_PYBULLET_INTEGRATION
#include "pybullet/extension.hpp"
#include "pybullet/racecar_demo.hpp"
#endif
#if MUESLI_BT_WITH_ROS2_INTEGRATION
#include "ros2/extension.hpp"
#include "ros2_test_harness.hpp"
#endif
#include "muslisp/env.hpp"
#include "muslisp/env_api.hpp"
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

void expect_lisp_error_message(const std::string& source,
                               muslisp::env_ptr env,
                               const std::string& expected,
                               const std::string& context) {
    try {
        (void)eval_text(source, env);
        throw std::runtime_error("expected lisp_error for " + context);
    } catch (const muslisp::lisp_error& e) {
        check(std::string(e.what()) == expected, context + " error mismatch");
    }
}

void reset_bt_runtime_host() {
    bt::runtime_host& host = bt::default_runtime_host();
    host.clear_all();
    bt::install_demo_callbacks(host);
}

muslisp::env_ptr create_env_with_pybullet_extension() {
#if MUESLI_BT_WITH_PYBULLET_INTEGRATION
    muslisp::runtime_config config;
    config.register_extension(muslisp::integrations::pybullet::make_extension());
    return muslisp::create_global_env(std::move(config));
#else
    throw std::runtime_error("pybullet integration tests are disabled");
#endif
}

muslisp::env_ptr create_env_with_ros2_extension() {
#if MUESLI_BT_WITH_ROS2_INTEGRATION
    muslisp::runtime_config config;
    config.register_extension(muslisp::integrations::ros2::make_extension());
    return muslisp::create_global_env(std::move(config));
#else
    throw std::runtime_error("ros2 integration tests are disabled");
#endif
}

std::filesystem::path temp_file_path(const std::string& stem, const std::string& extension = ".lisp") {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("muesli_bt_" + stem + "_" + std::to_string(now) + extension);
}

std::filesystem::path find_repo_root() {
    auto current = std::filesystem::current_path();
    while (true) {
        if (std::filesystem::exists(current / "CMakeLists.txt") && std::filesystem::exists(current / "README.md")) {
            return current;
        }
        const std::filesystem::path parent = current.parent_path();
        if (parent.empty() || parent == current) {
            break;
        }
        current = parent;
    }
    throw std::runtime_error("failed to locate repository root from current working directory");
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

void test_repl_support_commands_and_history_path() {
    using namespace muslisp::repl_support;

    check(should_exit_repl(":q", true), "repl should exit on :q when buffer is empty");
    check(should_exit_repl(":quit", true), "repl should exit on :quit when buffer is empty");
    check(should_exit_repl(":exit", true), "repl should exit on :exit when buffer is empty");
    check(!should_exit_repl(":q", false), "repl should not exit on :q with a pending buffer");
    check(!should_exit_repl(":clear", true), "repl should not treat :clear as an exit command");
    check(is_clear_command(":clear"), "repl should recognise :clear");
    check(!is_clear_command(":q"), "repl should not treat :q as :clear");

    const auto history_path = history_path_from_home("/tmp/muesli-home");
    check(history_path.has_value(), "repl history path should exist for a non-empty HOME");
    check(history_path.value() == std::filesystem::path("/tmp/muesli-home/.muesli_bt_history"),
          "repl history path should append the fixed history filename");
    check(!history_path_from_home("").has_value(), "repl history path should be absent for an empty HOME");
}

void test_repl_support_history_entry_normalisation() {
    using namespace muslisp::repl_support;

    check(normalise_history_entry("") == "", "empty repl history entry should stay empty");
    check(normalise_history_entry("(+ 1 2)\n") == "(+ 1 2)", "single-line repl history entry should lose the final newline");
    check(normalise_history_entry("(begin\n  1\n  2)\n") == "(begin\n  1\n  2)",
          "multi-line repl history entry should keep internal newlines");
    check(normalise_history_entry("\n") == "", "blank repl submissions should not be persisted in history");
}

#if MUESLI_BT_WITH_ROS2_INTEGRATION
std::string ros2_configure_script(const std::string& topic_ns, const std::string& extra_lines = {}) {
    return "(begin "
           "  (define cfg (map.make)) "
           "  (map.set! cfg 'tick_hz 1000) "
           "  (map.set! cfg 'steps_per_tick 1) "
           "  (map.set! cfg 'control_hz 50) "
           "  (map.set! cfg 'observe_timeout_ms 50) "
           "  (map.set! cfg 'step_timeout_ms 50) "
           "  (map.set! cfg 'use_sim_time #f) "
           "  (map.set! cfg 'require_fresh_obs #f) "
           "  (map.set! cfg 'action_clamp \"clamp\") "
           "  (map.set! cfg 'topic_ns " +
           lisp_string_literal(topic_ns) +
           ") "
           "  (map.set! cfg 'obs_source \"odom\") "
           "  (map.set! cfg 'action_sink \"cmd_vel\") " +
           extra_lines +
           "  (env.configure cfg))";
}
#endif

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

void test_and_or_forms() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value and_empty = eval_text("(and)", env);
    check(is_boolean(and_empty) && boolean_value(and_empty), "and with zero args should be true");

    value and_value = eval_text("(and #t 7)", env);
    check(is_integer(and_value) && integer_value(and_value) == 7, "and should return last truthy value");

    value or_empty = eval_text("(or)", env);
    check(is_nil(or_empty), "or with zero args should be nil");

    value or_value = eval_text("(or #f nil 42)", env);
    check(is_integer(or_value) && integer_value(or_value) == 42, "or should return first truthy value");

    value and_short = eval_text("(begin (define x 0) (and #f (define x 1)) x)", env);
    check(is_integer(and_short) && integer_value(and_short) == 0, "and should short-circuit");

    value or_short = eval_text("(begin (define y 0) (or 1 (define y 1)) y)", env);
    check(is_integer(or_short) && integer_value(or_short) == 0, "or should short-circuit");
}

void test_evaluator_tail_position_readiness() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value begin_effects = eval_text(
        "(begin "
        "  (define v (vec.make)) "
        "  (begin "
        "    (vec.push! v 1) "
        "    (vec.push! v 2) "
        "    99) "
        "  (write-to-string (list (vec.get v 0) (vec.get v 1) (vec.len v))))",
        env);
    check(is_string(begin_effects) && string_value(begin_effects) == "(1 2 2)",
          "begin should preserve side-effect order and return last value");

    value self_if = eval_text(
        "(begin "
        "  (define (countdown-if n) "
        "    (if (= n 0) "
        "        0 "
        "        (countdown-if (- n 1)))) "
        "  (countdown-if 48))",
        env);
    check(is_integer(self_if) && integer_value(self_if) == 0, "bounded self recursion through if failed");

    value self_begin = eval_text(
        "(begin "
        "  (define (countdown-begin n) "
        "    (begin "
        "      (if (= n 0) "
        "          0 "
        "          (countdown-begin (- n 1))))) "
        "  (countdown-begin 48))",
        env);
    check(is_integer(self_begin) && integer_value(self_begin) == 0, "bounded self recursion through begin failed");

    value self_let = eval_text(
        "(begin "
        "  (define (countdown-let n) "
        "    (let ((m n)) "
        "      (if (= m 0) "
        "          0 "
        "          (countdown-let (- m 1))))) "
        "  (countdown-let 48))",
        env);
    check(is_integer(self_let) && integer_value(self_let) == 0, "bounded self recursion through let failed");

    value self_cond = eval_text(
        "(begin "
        "  (define (countdown-cond n) "
        "    (cond "
        "      ((= n 0) 0) "
        "      (else (countdown-cond (- n 1))))) "
        "  (countdown-cond 48))",
        env);
    check(is_integer(self_cond) && integer_value(self_cond) == 0, "bounded self recursion through cond failed");

    value mutual = eval_text(
        "(begin "
        "  (define (evenish n) "
        "    (if (= n 0) "
        "        #t "
        "        (oddish (- n 1)))) "
        "  (define (oddish n) "
        "    (if (= n 0) "
        "        #f "
        "        (evenish (- n 1)))) "
        "  (evenish 48))",
        env);
    check(is_boolean(mutual) && boolean_value(mutual), "bounded mutual recursion through closure calls failed");
}

void test_tail_call_optimisation_smoke() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value self_tail = eval_text(
        "(begin "
        "  (define (countdown-tail n) "
        "    (if (= n 0) "
        "        0 "
        "        (countdown-tail (- n 1)))) "
        "  (countdown-tail 4096))",
        env);
    check(is_integer(self_tail) && integer_value(self_tail) == 0, "tail self recursion should survive a deeper stack");

    value mutual_tail = eval_text(
        "(begin "
        "  (define (even-tail n) "
        "    (if (= n 0) "
        "        #t "
        "        (odd-tail (- n 1)))) "
        "  (define (odd-tail n) "
        "    (if (= n 0) "
        "        #f "
        "        (even-tail (- n 1)))) "
        "  (even-tail 4096))",
        env);
    check(is_boolean(mutual_tail) && boolean_value(mutual_tail),
          "tail mutual recursion should survive a deeper stack");
}

void test_tail_call_optimisation_deep_recursion() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value deep_self = eval_text(
        "(begin "
        "  (define (countdown-deep n) "
        "    (if (= n 0) "
        "        0 "
        "        (countdown-deep (- n 1)))) "
        "  (countdown-deep 20000))",
        env);
    check(is_integer(deep_self) && integer_value(deep_self) == 0, "deep tail self recursion should complete");

    value deep_mutual = eval_text(
        "(begin "
        "  (define (even-deep n) "
        "    (if (= n 0) "
        "        #t "
        "        (odd-deep (- n 1)))) "
        "  (define (odd-deep n) "
        "    (if (= n 0) "
        "        #f "
        "        (even-deep (- n 1)))) "
        "  (even-deep 20000))",
        env);
    check(is_boolean(deep_mutual) && boolean_value(deep_mutual), "deep tail mutual recursion should complete");

    value alloc_tail = eval_text(
        "(begin "
        "  (define (countdown-alloc n) "
        "    (let ((tmp (list n n n n))) "
        "      (if (= n 0) "
        "          0 "
        "          (countdown-alloc (- n 1))))) "
        "  (countdown-alloc 6000))",
        env);
    check(is_integer(alloc_tail) && integer_value(alloc_tail) == 0,
          "tail recursion under allocation pressure should complete");
}

void test_compiled_closure_path() {
    using namespace muslisp;

    env_ptr env = create_global_env();
    (void)eval_text("(define offset 3)", env);

    value supported = eval_text("(lambda (x) (let ((y (+ x 1))) (if (> y 0) (+ y offset) 0)))", env);
    check(is_closure(supported), "supported compiled closure test should produce a closure");
    check(static_cast<bool>(closure_compiled(supported)), "supported closure should compile");

    value supported_result = invoke_callable(supported, {make_integer(4)});
    check(is_integer(supported_result) && integer_value(supported_result) == 8,
          "compiled closure should preserve let/local/global semantics");

    value recursive = eval_text(
        "(begin "
        "  (define (countdown-vm n) "
        "    (if (= n 0) "
        "        0 "
        "        (countdown-vm (- n 1)))) "
        "  countdown-vm)",
        env);
    check(is_closure(recursive) && static_cast<bool>(closure_compiled(recursive)),
          "simple recursive closure should compile");
    value recursive_result = invoke_callable(recursive, {make_integer(5000)});
    check(is_integer(recursive_result) && integer_value(recursive_result) == 0,
          "compiled recursive closure should run correctly");

    value unsupported_cond = eval_text("(lambda (x) (cond ((> x 0) x) (else 0)))", env);
    check(is_closure(unsupported_cond), "unsupported cond test should produce a closure");
    check(!closure_compiled(unsupported_cond), "unsupported closure should fall back to the evaluator");
    value unsupported_result = invoke_callable(unsupported_cond, {make_integer(2)});
    check(is_integer(unsupported_result) && integer_value(unsupported_result) == 2,
          "unsupported closure fallback should preserve semantics");

    value unsupported_nested = eval_text("(lambda (x) (lambda (y) (+ x y)))", env);
    check(is_closure(unsupported_nested), "unsupported nested-lambda test should produce a closure");
    check(!closure_compiled(unsupported_nested), "nested lambda should currently fall back to the evaluator");
}

void test_tail_call_optimisation_and_or() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value deep_and = eval_text(
        "(begin "
        "  (define (countdown-and n) "
        "    (and #t "
        "         (if (= n 0) "
        "             0 "
        "             (countdown-and (- n 1))))) "
        "  (countdown-and 20000))",
        env);
    check(is_integer(deep_and) && integer_value(deep_and) == 0,
          "tail recursion through and should complete");

    value deep_or = eval_text(
        "(begin "
        "  (define (countdown-or n) "
        "    (or #f "
        "        (if (= n 0) "
        "            0 "
        "            (countdown-or (- n 1))))) "
        "  (countdown-or 20000))",
        env);
    check(is_integer(deep_or) && integer_value(deep_or) == 0,
          "tail recursion through or should complete");
}

void test_gc_env_root_stack_regression() {
    using namespace muslisp;

    env_ptr env_a = create_global_env();
    try {
        (void)eval_text("missing-symbol", env_a);
        throw std::runtime_error("expected unbound symbol during env root regression setup");
    } catch (const lisp_error&) {
    }
    try {
        (void)eval_text("(1 2)", env_a);
        throw std::runtime_error("expected non-function call during env root regression setup");
    } catch (const lisp_error&) {
    }

    env_ptr env_b = create_global_env();
    check(is_integer(eval_text("(+ 1 2 3 4)", env_b)), "env root regression setup should retain +");
    default_gc().collect();
    check(is_integer(eval_text("(* 2 3 4)", env_b)), "env root regression should retain * after collection");
    default_gc().collect();

    value branch = eval_text("(begin (define x 10) (if (> x 5) x 0))", env_b);
    check(is_integer(branch) && integer_value(branch) == 10,
          "global env root should survive nested begin/if evaluation");
    default_gc().collect();

    value begin_value = eval_text("(begin (define x 2) (define y 7) (+ x y))", env_b);
    check(is_integer(begin_value) && integer_value(begin_value) == 9,
          "global env root should survive repeated begin evaluation after collection");
}

void test_gc_duplicate_env_roots_are_stack_like() {
    using namespace muslisp;

    default_gc().collect();
    const std::size_t baseline = default_gc().stats().live_objects_after_last_gc;

    env_ptr env = make_env();
    default_gc().register_root_env(env);
    default_gc().register_root_env(env);
    default_gc().collect();
    check(default_gc().stats().live_objects_after_last_gc == baseline + 1,
          "duplicate env roots should keep one env object live");

    default_gc().unregister_root_env(env);
    default_gc().collect();
    check(default_gc().stats().live_objects_after_last_gc == baseline + 1,
          "unregistering one duplicate env root should keep the env live");

    default_gc().unregister_root_env(env);
    default_gc().collect();
    check(default_gc().stats().live_objects_after_last_gc == baseline,
          "removing the final env root should release the env");
}

void test_evaluator_error_messages_stable() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    expect_lisp_error_message("(begin (define (inc x) (+ x 1)) (inc))",
                              env,
                              "closure call: expected 1 arguments, got 0",
                              "closure arity");
    expect_lisp_error_message("(if #t)", env, "if: expected 2 or 3 arguments", "if arity");
    expect_lisp_error_message("(let 1 2)", env, "let: expected binding list", "let binding list");
    expect_lisp_error_message("(cond (else 1) (#t 2))", env, "cond: else clause must be last", "cond else-last");
    expect_lisp_error_message("(unquote x)", env, "unquote: only valid inside quasiquote", "unquote misuse");
    expect_lisp_error_message("(quasiquote (unquote-splicing (list 1 2)))",
                              env,
                              "unquote-splicing: only valid in list context",
                              "unquote-splicing list context");
    expect_lisp_error_message("(quasiquote (a (unquote-splicing 1)))",
                              env,
                              "unquote-splicing: expected list value",
                              "unquote-splicing list value");
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

    gc_root_scope roots(default_gc());
    value original = eval_text("'(1 \"line\\nnext\" #t 2.5 nil foo)", env);
    roots.add(&original);
    value serialised = eval_text("(write-to-string '(1 \"line\\nnext\" #t 2.5 nil foo))", env);
    roots.add(&serialised);
    check(is_string(serialised), "write-to-string should return string");
    value reparsed = read_one(string_value(serialised));
    roots.add(&reparsed);
    const std::string reparsed_text = print_value(reparsed);
    const std::string original_text = print_value(original);
    check(reparsed_text == original_text,
          "write-to-string should round-trip through reader (expected " + original_text + ", got " + reparsed_text + ")");

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

void test_load_resolves_nested_relative_paths_from_loaded_file() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    const std::filesystem::path fixture_root = temp_file_path("load_fixture_root", "");
    const std::filesystem::path scripts_dir = fixture_root / "scripts";
    const std::filesystem::path lib_dir = fixture_root / "lib";
    std::filesystem::create_directories(scripts_dir);
    std::filesystem::create_directories(lib_dir);

    const std::filesystem::path child_path = lib_dir / "child.lisp";
    const std::filesystem::path main_path = scripts_dir / "main.lisp";

    write_text_file(child_path, "(define nested-load-value 42)\n'ok\n");
    write_text_file(main_path, "(load \"../lib/child.lisp\")\nnested-load-value\n");

    value loaded_value = eval_text("(load " + lisp_string_literal(main_path.string()) + ")", env);
    check(integer_value(loaded_value) == 42, "nested load should resolve relative to the loaded file");
    check(integer_value(eval_text("nested-load-value", env)) == 42, "nested load should define values in the current environment");
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

void test_bt_export_dot_builtin() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text(
        "(define tree "
        "  (bt (seq (cond always-true) (act bb-put-int foo 42) (retry 2 (act always-fail)))))",
        env);

    const auto dot_path = temp_file_path("tree_graph", ".dot");
    const std::string dot_path_lisp = lisp_string_literal(dot_path.string());
    value export_ok = eval_text("(bt.export-dot tree " + dot_path_lisp + ")", env);
    check(is_boolean(export_ok) && boolean_value(export_ok), "bt.export-dot should return #t");

    std::ifstream in(dot_path);
    check(static_cast<bool>(in), "bt.export-dot should write the .dot file");
    std::ostringstream text;
    text << in.rdbuf();
    const std::string dot = text.str();

    check(dot.find("digraph bt") != std::string::npos, "dot output should include graph header");
    check(dot.find("always-true") != std::string::npos, "dot output should include condition leaf label");
    check(dot.find("bb-put-int") != std::string::npos, "dot output should include action leaf label");
    check(dot.find("[retry 2]") != std::string::npos, "dot output should include retry node metadata");
    check(dot.find("->") != std::string::npos, "dot output should include edges");

    std::filesystem::remove(dot_path);
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

void test_pq_builtins_gc_and_errors() {
    using namespace muslisp;

    env_ptr env = create_global_env();

    value pq_value = eval_text("(pq.make)", env);
    check(is_pq(pq_value), "pq.make should return pq handle");

    value empty_meta = eval_text("(begin (define q0 (pq.make)) (list (pq.len q0) (pq.empty? q0)))", env);
    {
        const auto fields = vector_from_list(empty_meta);
        check(fields.size() == 2, "pq empty metadata shape mismatch");
        check(is_integer(fields[0]) && integer_value(fields[0]) == 0, "pq.len should start at zero");
        check(is_boolean(fields[1]) && boolean_value(fields[1]), "pq.empty? should be true for new queue");
    }

    auto check_pair = [&](value pair_value, double expected_priority, const std::string& expected_symbol, const std::string& where) {
        const auto pair = vector_from_list(pair_value);
        check(pair.size() == 2, where + ": pair shape mismatch");
        check(is_float(pair[0]), where + ": priority should be float");
        check_close(float_value(pair[0]), expected_priority, 1e-12, where + ": priority mismatch");
        check(is_symbol(pair[1]), where + ": payload should be symbol");
        check(symbol_name(pair[1]) == expected_symbol, where + ": payload symbol mismatch");
    };

    value ordered = eval_text(
        "(begin "
        "  (define q1 (pq.make)) "
        "  (list "
        "    (pq.push! q1 3 'c) "
        "    (pq.push! q1 1.5 'b) "
        "    (pq.push! q1 1 'a) "
        "    (pq.pop! q1) "
        "    (pq.pop! q1) "
        "    (pq.pop! q1) "
        "    (pq.empty? q1)))",
        env);
    {
        const auto fields = vector_from_list(ordered);
        check(fields.size() == 7, "pq ordering result shape mismatch");
        check(is_integer(fields[0]) && integer_value(fields[0]) == 1, "pq.push! should return size=1");
        check(is_integer(fields[1]) && integer_value(fields[1]) == 2, "pq.push! should return size=2");
        check(is_integer(fields[2]) && integer_value(fields[2]) == 3, "pq.push! should return size=3");
        check_pair(fields[3], 1.0, "a", "pq.pop order[0]");
        check_pair(fields[4], 1.5, "b", "pq.pop order[1]");
        check_pair(fields[5], 3.0, "c", "pq.pop order[2]");
        check(is_boolean(fields[6]) && boolean_value(fields[6]), "pq.empty? should be true after draining");
    }

    value tie_order = eval_text(
        "(begin "
        "  (define q2 (pq.make)) "
        "  (pq.push! q2 2 'first) "
        "  (pq.push! q2 2.0 'second) "
        "  (list (pq.pop! q2) (pq.pop! q2)))",
        env);
    {
        const auto fields = vector_from_list(tie_order);
        check(fields.size() == 2, "pq tie-order result shape mismatch");
        check_pair(fields[0], 2.0, "first", "pq tie-order[0]");
        check_pair(fields[1], 2.0, "second", "pq tie-order[1]");
    }

    value peek_meta = eval_text(
        "(begin "
        "  (define q3 (pq.make)) "
        "  (pq.push! q3 4 'x) "
        "  (list (pq.peek q3) (pq.len q3) (pq.pop! q3) (pq.empty? q3)))",
        env);
    {
        const auto fields = vector_from_list(peek_meta);
        check(fields.size() == 4, "pq.peek metadata shape mismatch");
        check_pair(fields[0], 4.0, "x", "pq.peek pair");
        check(is_integer(fields[1]) && integer_value(fields[1]) == 1, "pq.peek should not mutate queue");
        check_pair(fields[2], 4.0, "x", "pq.pop after peek");
        check(is_boolean(fields[3]) && boolean_value(fields[3]), "queue should be empty after pop");
    }

    eval_text(
        "(begin "
        "  (define qgc (pq.make)) "
        "  (define (fill i) "
        "    (if (= i 64) "
        "        nil "
        "        (begin "
        "          (pq.push! qgc i (list 'node i (list i (+ i 1)))) "
        "          (fill (+ i 1))))) "
        "  (fill 0))",
        env);
    default_gc().collect();
    for (int i = 0; i < 64; ++i) {
        value entry = eval_text("(pq.pop! qgc)", env);
        const auto pair = vector_from_list(entry);
        check(pair.size() == 2, "pq gc payload pair shape mismatch");
        check(is_float(pair[0]), "pq gc payload priority should be float");
        check_close(float_value(pair[0]), static_cast<double>(i), 1e-12, "pq gc payload priority mismatch");
        const std::string expected = "(node " + std::to_string(i) + " (" + std::to_string(i) + " " + std::to_string(i + 1) + "))";
        check(print_value(pair[1]) == expected, "pq gc payload value mismatch");
        if ((i % 9) == 0) {
            default_gc().collect();
        }
    }
    check(boolean_value(eval_text("(pq.empty? qgc)", env)), "pq should be empty after gc-drain loop");

    auto expect_lisp_error = [&](const std::string& expr, const std::string& label) {
        try {
            (void)eval_text(expr, env);
            throw std::runtime_error("expected lisp_error: " + label);
        } catch (const lisp_error&) {
        }
    };

    expect_lisp_error("(begin (define qe (pq.make)) (pq.pop! qe))", "pq.pop! on empty");
    expect_lisp_error("(begin (define qe (pq.make)) (pq.peek qe))", "pq.peek on empty");
    expect_lisp_error("(begin (define qe (pq.make)) (pq.push! qe 'bad 1))", "pq.push! non-numeric priority");
    expect_lisp_error("(begin (define qe (pq.make)) (pq.push! qe (/ 1 0) 1))", "pq.push! infinite priority");
    expect_lisp_error("(begin (define qe (pq.make)) (pq.push! qe (/ 0 0) 1))", "pq.push! nan priority");
    expect_lisp_error("(write-to-string (pq.make))", "write-to-string should reject pq");
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

void test_planner_plan_builtin_determinism_bounds_budget_and_sanity() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    try {
        (void)eval_text("(planner.plan (map.make))", env);
        throw std::runtime_error("expected planner.plan request validation failure");
    } catch (const lisp_error&) {
    }

    value out = eval_text(
        "(begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"planner.request.v1\") "
        "  (map.set! req 'planner \"mcts\") "
        "  (map.set! req 'model_service \"toy-1d\") "
        "  (map.set! req 'state 0.0) "
        "  (map.set! req 'seed 42) "
        "  (map.set! req 'budget_ms 16) "
        "  (map.set! req 'work_max 400) "
        "  (map.set! req 'bounds (list (list -0.4 0.4))) "
        "  (define mcts (map.make)) "
        "  (map.set! mcts 'max_depth 16) "
        "  (map.set! req 'mcts mcts) "
        "  (define r1 (planner.plan req)) "
        "  (define r2 (planner.plan req)) "
        "  (list (car (map.get (map.get r1 'action nil) 'u (list 0.0))) "
        "        (car (map.get (map.get r2 'action nil) 'u (list 0.0))) "
        "        (map.get r1 'status 'none) "
        "        (map.get (map.get r1 'stats nil) 'time_used_ms 0) "
        "        (map.get (map.get r1 'stats nil) 'work_done 0)))",
        env);

    const std::vector<value> fields = vector_from_list(out);
    check(fields.size() == 5, "planner.plan mcts deterministic output shape mismatch");
    check(is_float(fields[0]) && is_float(fields[1]), "planner.plan mcts action should be float");
    check_close(float_value(fields[0]), float_value(fields[1]), 1e-12, "planner.plan mcts should be deterministic");
    check(float_value(fields[0]) >= -0.4 && float_value(fields[0]) <= 0.4, "planner.plan should clamp by bounds");
    check(is_symbol(fields[2]) && (symbol_name(fields[2]) == ":ok" || symbol_name(fields[2]) == ":timeout"),
          "planner.plan mcts status should be :ok or :timeout");
    check(is_integer(fields[3]) && integer_value(fields[3]) >= 0, "planner.plan time_used_ms should be non-negative int");
    check(is_integer(fields[4]) && integer_value(fields[4]) > 0, "planner.plan work_done should be positive");

    value budget_out = eval_text(
        "(begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"planner.request.v1\") "
        "  (map.set! req 'planner \"mcts\") "
        "  (map.set! req 'model_service \"toy-1d\") "
        "  (map.set! req 'state 0.0) "
        "  (map.set! req 'seed 99) "
        "  (map.set! req 'budget_ms 1) "
        "  (map.set! req 'work_max 100000) "
        "  (define r (planner.plan req)) "
        "  (list (map.get r 'status 'none) "
        "        (map.get (map.get r 'stats nil) 'time_used_ms 0) "
        "        (map.get (map.get r 'stats nil) 'work_done 0)))",
        env);

    const std::vector<value> budget_fields = vector_from_list(budget_out);
    check(budget_fields.size() == 3, "planner.plan budget output shape mismatch");
    check(is_symbol(budget_fields[0]), "planner.plan budget status should be symbol");
    check(is_integer(budget_fields[1]), "planner.plan budget time should be integer");
    check(integer_value(budget_fields[1]) <= 40, "planner.plan should honor bounded-time budget");
    check(is_integer(budget_fields[2]) && integer_value(budget_fields[2]) > 0,
          "planner.plan budget run should still perform work");

    value mppi_det = eval_text(
        "(begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"planner.request.v1\") "
        "  (map.set! req 'planner \"mppi\") "
        "  (map.set! req 'model_service \"toy-1d\") "
        "  (map.set! req 'state 0.0) "
        "  (map.set! req 'seed 123) "
        "  (map.set! req 'budget_ms 16) "
        "  (map.set! req 'work_max 128) "
        "  (map.set! req 'horizon 12) "
        "  (define cfg (map.make)) "
        "  (map.set! cfg 'lambda 0.8) "
        "  (map.set! cfg 'sigma (list 0.35)) "
        "  (map.set! cfg 'n_samples 128) "
        "  (map.set! req 'mppi cfg) "
        "  (define r1 (planner.plan req)) "
        "  (define r2 (planner.plan req)) "
        "  (list (car (map.get (map.get r1 'action nil) 'u (list 0.0))) "
        "        (car (map.get (map.get r2 'action nil) 'u (list 0.0))) "
        "        (map.get r1 'status ':none)))",
        env);
    const std::vector<value> mppi_fields = vector_from_list(mppi_det);
    check(mppi_fields.size() == 3, "planner.plan mppi deterministic shape mismatch");
    check(is_float(mppi_fields[0]) && is_float(mppi_fields[1]), "planner.plan mppi actions should be float");
    check_close(float_value(mppi_fields[0]), float_value(mppi_fields[1]), 1e-12,
                "planner.plan mppi should be deterministic for fixed seed");
    check(is_symbol(mppi_fields[2]) && (symbol_name(mppi_fields[2]) == ":ok" || symbol_name(mppi_fields[2]) == ":timeout"),
          "planner.plan mppi status should be :ok or :timeout");

    value ilqr_ok = eval_text(
        "(begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"planner.request.v1\") "
        "  (map.set! req 'planner \"ilqr\") "
        "  (map.set! req 'model_service \"toy-1d\") "
        "  (map.set! req 'state -1.0) "
        "  (map.set! req 'seed 9) "
        "  (map.set! req 'budget_ms 20) "
        "  (map.set! req 'work_max 20) "
        "  (map.set! req 'horizon 12) "
        "  (define cfg (map.make)) "
        "  (map.set! cfg 'max_iters 20) "
        "  (map.set! cfg 'derivatives \"analytic\") "
        "  (map.set! req 'ilqr cfg) "
        "  (define r (planner.plan req)) "
        "  (list (map.get r 'status ':none) "
        "        (car (map.get (map.get r 'action nil) 'u (list 0.0)))))",
        env);
    const std::vector<value> ilqr_ok_fields = vector_from_list(ilqr_ok);
    check(ilqr_ok_fields.size() == 2, "planner.plan ilqr output shape mismatch");
    check(is_symbol(ilqr_ok_fields[0]) && (symbol_name(ilqr_ok_fields[0]) == ":ok" || symbol_name(ilqr_ok_fields[0]) == ":timeout"),
          "planner.plan ilqr status should be :ok or :timeout");
    check(is_float(ilqr_ok_fields[1]) && float_value(ilqr_ok_fields[1]) > 0.0,
          "planner.plan ilqr should move positive from x=-1.0");

    value ilqr_missing_deriv = eval_text(
        "(begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"planner.request.v1\") "
        "  (map.set! req 'planner \"ilqr\") "
        "  (map.set! req 'model_service \"ptz-track\") "
        "  (map.set! req 'state (list 0.0 0.0 0.0 0.0)) "
        "  (map.set! req 'budget_ms 8) "
        "  (map.set! req 'work_max 4) "
        "  (map.set! req 'horizon 4) "
        "  (define cfg (map.make)) "
        "  (map.set! cfg 'derivatives \"analytic\") "
        "  (map.set! req 'ilqr cfg) "
        "  (planner.plan req))",
        env);
    check(is_map(ilqr_missing_deriv), "planner.plan ilqr missing-derivatives should return map");
    check(symbol_name(eval_text("(map.get (planner.plan req) 'status ':none)", env)) == ":error",
          "planner.plan ilqr analytic without derivatives should return :error");

    value sanity = eval_text(
        "(begin "
        "  (define (step x u) (+ x (* 0.25 (clamp u -1.0 1.0)))) "
        "  (define (run req x n) "
        "    (if (= n 0) "
        "        x "
        "        (begin "
        "          (map.set! req 'state x) "
        "          (define r (planner.plan req)) "
        "          (define u (car (map.get (map.get r 'action nil) 'u (list 0.0)))) "
        "          (run req (step x u) (- n 1))))) "
        "  (define mreq (map.make)) "
        "  (map.set! mreq 'schema_version \"planner.request.v1\") "
        "  (map.set! mreq 'planner \"mppi\") "
        "  (map.set! mreq 'model_service \"toy-1d\") "
        "  (map.set! mreq 'budget_ms 12) "
        "  (map.set! mreq 'work_max 96) "
        "  (map.set! mreq 'horizon 10) "
        "  (define mcfg (map.make)) "
        "  (map.set! mcfg 'lambda 1.0) "
        "  (map.set! mcfg 'sigma (list 0.3)) "
        "  (map.set! mcfg 'n_samples 96) "
        "  (map.set! mreq 'mppi mcfg) "
        "  (define ireq (map.make)) "
        "  (map.set! ireq 'schema_version \"planner.request.v1\") "
        "  (map.set! ireq 'planner \"ilqr\") "
        "  (map.set! ireq 'model_service \"toy-1d\") "
        "  (map.set! ireq 'budget_ms 20) "
        "  (map.set! ireq 'work_max 16) "
        "  (map.set! ireq 'horizon 10) "
        "  (define icfg (map.make)) "
        "  (map.set! icfg 'max_iters 16) "
        "  (map.set! icfg 'derivatives \"analytic\") "
        "  (map.set! ireq 'ilqr icfg) "
        "  (list (run mreq -1.0 8) (run ireq -1.0 8)))",
        env);
    const std::vector<value> sanity_fields = vector_from_list(sanity);
    check(sanity_fields.size() == 2, "planner sanity output shape mismatch");
    check(is_float(sanity_fields[0]) && float_value(sanity_fields[0]) > -0.95, "mppi should improve 1D integrator");
    check(is_float(sanity_fields[1]) && float_value(sanity_fields[1]) > -0.95, "ilqr should improve 1D integrator");

    value uni = eval_text(
        "(begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"planner.request.v1\") "
        "  (map.set! req 'planner \"mppi\") "
        "  (map.set! req 'model_service \"toy-unicycle\") "
        "  (map.set! req 'state (list 0.0 0.0 0.0 1.0 0.0)) "
        "  (map.set! req 'budget_ms 12) "
        "  (map.set! req 'work_max 96) "
        "  (map.set! req 'horizon 12) "
        "  (define cfg (map.make)) "
        "  (map.set! cfg 'lambda 1.0) "
        "  (map.set! cfg 'sigma (list 0.25 0.35)) "
        "  (map.set! cfg 'n_samples 96) "
        "  (map.set! req 'mppi cfg) "
        "  (define r (planner.plan req)) "
        "  (list (map.get r 'status ':none) "
        "        (car (map.get (map.get r 'action nil) 'u (list 0.0)))))",
        env);
    const std::vector<value> uni_fields = vector_from_list(uni);
    check(uni_fields.size() == 2, "toy-unicycle planner output shape mismatch");
    check(is_symbol(uni_fields[0]) && (symbol_name(uni_fields[0]) == ":ok" || symbol_name(uni_fields[0]) == ":timeout"),
          "toy-unicycle mppi status should be :ok or :timeout");
    check(is_float(uni_fields[1]) && float_value(uni_fields[1]) > 0.0, "toy-unicycle mppi should command forward velocity");
}

void test_plan_action_node_blackboard_meta_and_logs() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    check(is_nil(eval_text("(planner.set-base-seed 4242)", env)), "planner.set-base-seed should return nil");
    check(is_integer(eval_text("(planner.get-base-seed)", env)) &&
              integer_value(eval_text("(planner.get-base-seed)", env)) == 4242,
          "planner.get-base-seed should return configured value");

    (void)eval_text(
        "(define tree "
        "  (bt.compile "
        "    '(seq "
        "       (plan-action :name \"toy-plan\" :planner :mcts :budget_ms 20 :work_max 300 "
        "                    :model_service \"toy-1d\" :state_key state :action_key action :meta_key plan-meta) "
        "       (act apply-planned-1d state action state))))",
        env);
    check(string_value(eval_text("(write-to-string (bt.to-dsl tree))", env)).find("(plan-action") != std::string::npos,
          "bt.to-dsl should include plan-action node");
    (void)eval_text("(define inst-a (bt.new-instance tree))", env);
    (void)eval_text("(define inst-b (bt.new-instance tree))", env);

    check(symbol_name(eval_text("(bt.tick inst-a '((state 0.0)))", env)) == "success",
          "plan-action tree tick should succeed");
    check(symbol_name(eval_text("(bt.tick inst-b '((state 0.0)))", env)) == "success",
          "plan-action tree tick on second instance should succeed");

    bt::runtime_host& host = bt::default_runtime_host();
    const std::int64_t inst_a_handle = bt_handle(eval_text("inst-a", env));
    const std::int64_t inst_b_handle = bt_handle(eval_text("inst-b", env));
    bt::instance* inst_a = host.find_instance(inst_a_handle);
    bt::instance* inst_b = host.find_instance(inst_b_handle);
    check(inst_a && inst_b, "plan-action test instances should exist");

    const bt::bb_entry* action_a = inst_a->bb.get("action");
    const bt::bb_entry* action_b = inst_b->bb.get("action");
    check(action_a && action_b, "plan-action should write action key");

    double action_a_value = 0.0;
    double action_b_value = 0.0;
    if (const double* f = std::get_if<double>(&action_a->value)) {
        action_a_value = *f;
    } else if (const std::vector<double>* vec = std::get_if<std::vector<double>>(&action_a->value)) {
        check(!vec->empty(), "plan-action vector action should not be empty");
        action_a_value = (*vec)[0];
    } else {
        throw std::runtime_error("plan-action action value should be numeric");
    }
    if (const double* f = std::get_if<double>(&action_b->value)) {
        action_b_value = *f;
    } else if (const std::vector<double>* vec = std::get_if<std::vector<double>>(&action_b->value)) {
        check(!vec->empty(), "plan-action vector action should not be empty");
        action_b_value = (*vec)[0];
    } else {
        throw std::runtime_error("plan-action action value should be numeric");
    }
    check_close(action_a_value, action_b_value, 1e-12,
                "plan-action should be deterministic for same base seed/node/tick/state");

    const bt::bb_entry* state_a = inst_a->bb.get("state");
    check(state_a, "apply-planned-1d should update state");
    check(std::holds_alternative<double>(state_a->value), "updated state should be float");
    check(std::get<double>(state_a->value) > 0.0, "planned action should move state toward goal");

    const bt::bb_entry* meta_a = inst_a->bb.get("plan-meta");
    check(meta_a && std::holds_alternative<std::string>(meta_a->value), "plan-action should write meta key");
    check(std::get<std::string>(meta_a->value).find("\"status\"") != std::string::npos,
          "plan-action meta should include status");

    value planner_events = eval_text("(events.dump 200)", env);
    check(is_proper_list(planner_events), "events.dump should return list");
    const auto planner_rows = vector_from_list(planner_events);
    bool saw_planner_v1 = false;
    bool saw_planner_schema = false;
    bool saw_planner_node = false;
    for (const value& row : planner_rows) {
        if (!is_string(row)) {
            continue;
        }
        const std::string line = string_value(row);
        if (line.find("\"type\":\"planner_v1\"") != std::string::npos) {
            saw_planner_v1 = true;
        }
        if (line.find("\"schema_version\":\"planner.v1\"") != std::string::npos) {
            saw_planner_schema = true;
        }
        if (line.find("\"node_name\":\"toy-plan\"") != std::string::npos) {
            saw_planner_node = true;
        }
    }
    check(saw_planner_v1, "events should include planner_v1");
    check(saw_planner_schema, "planner_v1 should include stable planner schema");
    check(saw_planner_node, "planner_v1 should include node name");

    (void)eval_text(
        "(define bad-tree "
        "  (bt.compile "
        "    '(plan-action :name \"bad\" :model_service \"toy-1d\" :state_key missing :action_key action)))",
        env);
    (void)eval_text("(define bad-inst (bt.new-instance bad-tree))", env);
    check(symbol_name(eval_text("(bt.tick bad-inst)", env)) == "failure",
          "plan-action should fail on missing state key");
}

void test_plan_action_node_with_all_planner_backends() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text(
        "(define tree-mcts "
        "  (bt.compile '(plan-action :name \"mcts-node\" :planner :mcts :budget_ms 24 :work_max 240 "
        "                          :model_service \"toy-1d\" :state_key state :action_key action)))",
        env);
    (void)eval_text(
        "(define tree-mppi "
        "  (bt.compile '(plan-action :name \"mppi-node\" :planner :mppi :budget_ms 24 :work_max 96 "
        "                          :horizon 10 :lambda 1.0 :sigma 0.3 :n_samples 96 "
        "                          :model_service \"toy-1d\" :state_key state :action_key action)))",
        env);
    (void)eval_text(
        "(define tree-ilqr "
        "  (bt.compile '(plan-action :name \"ilqr-node\" :planner :ilqr :budget_ms 24 :work_max 12 "
        "                          :horizon 10 :max_iters 12 :derivatives :analytic "
        "                          :model_service \"toy-1d\" :state_key state :action_key action)))",
        env);

    (void)eval_text("(define inst-mcts (bt.new-instance tree-mcts))", env);
    (void)eval_text("(define inst-mppi (bt.new-instance tree-mppi))", env);
    (void)eval_text("(define inst-ilqr (bt.new-instance tree-ilqr))", env);

    check(symbol_name(eval_text("(bt.tick inst-mcts '((state -1.0)))", env)) == "success",
          "plan-action mcts backend should succeed");
    check(symbol_name(eval_text("(bt.tick inst-mppi '((state -1.0)))", env)) == "success",
          "plan-action mppi backend should succeed");
    check(symbol_name(eval_text("(bt.tick inst-ilqr '((state -1.0)))", env)) == "success",
          "plan-action ilqr backend should succeed");

    bt::runtime_host& host = bt::default_runtime_host();
    const std::int64_t h_mcts = bt_handle(eval_text("inst-mcts", env));
    const std::int64_t h_mppi = bt_handle(eval_text("inst-mppi", env));
    const std::int64_t h_ilqr = bt_handle(eval_text("inst-ilqr", env));
    bt::instance* i_mcts = host.find_instance(h_mcts);
    bt::instance* i_mppi = host.find_instance(h_mppi);
    bt::instance* i_ilqr = host.find_instance(h_ilqr);
    check(i_mcts && i_mppi && i_ilqr, "backend test instances should exist");
    check(i_mcts->bb.get("action") != nullptr, "mcts backend should publish action");
    check(i_mppi->bb.get("action") != nullptr, "mppi backend should publish action");
    check(i_ilqr->bb.get("action") != nullptr, "ilqr backend should publish action");
}

void test_hash64_builtin() {
    using namespace muslisp;

    env_ptr env = create_global_env();
    value h1 = eval_text("(hash64 \"planner-seed\")", env);
    value h2 = eval_text("(hash64 \"planner-seed\")", env);
    value h3 = eval_text("(hash64 \"planner-seed-2\")", env);

    check(is_integer(h1) && is_integer(h2) && is_integer(h3), "hash64 should return integer");
    check(integer_value(h1) == integer_value(h2), "hash64 should be deterministic");
    check(integer_value(h1) != integer_value(h3), "hash64 should vary with input");
}

void test_json_and_handle_builtins() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    value img = eval_text("(image.make 320 240 3 \"rgb8\" 1234 \"cam0\")", env);
    check(is_image_handle(img), "image.make should return image_handle");
    value img_info = eval_text("(image.info (image.make 64 48 1 \"gray8\" 222 \"cam1\"))", env);
    check(is_map(img_info), "image.info should return map");
    check(integer_value(eval_text("(map.get (image.info (image.make 10 20 3 \"rgb8\" 555 \"cam2\")) 'w -1)", env)) == 10,
          "image.info width mismatch");

    value blob = eval_text("(blob.make 1024 \"application/octet-stream\" 777 \"snapshot\")", env);
    check(is_blob_handle(blob), "blob.make should return blob_handle");
    check(integer_value(eval_text("(map.get (blob.info (blob.make 99 \"text/plain\" 111 \"note\")) 'size_bytes -1)", env)) == 99,
          "blob.info size mismatch");

    value json_out = eval_text(
        "(begin "
        "  (define m (map.make)) "
        "  (map.set! m 'a 1) "
        "  (map.set! m 'b (list 2 3)) "
        "  (define s (json.encode m)) "
        "  (define d (json.decode s)) "
        "  (list s (map.get d \"a\" -1) (map.get d \"b\" nil)))",
        env);
    const std::vector<value> fields = vector_from_list(json_out);
    check(fields.size() == 3, "json roundtrip shape mismatch");
    check(is_string(fields[0]), "json.encode should return string");
    check(integer_value(fields[1]) == 1, "json.decode object key/value mismatch");
    check(print_value(fields[2]) == "(2 3)", "json.decode array mismatch");
}

void test_vla_builtins_submit_poll_cancel_and_caps() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    value caps = eval_text("(cap.list)", env);
    check(is_proper_list(caps), "cap.list should return list");
    check(string_value(eval_text("(car (cap.list))", env)).find("vla.") != std::string::npos,
          "cap.list should include vla capability");
    check(is_map(eval_text("(cap.describe \"vla.rt2\")", env)), "cap.describe should return map");

    (void)eval_text(
        "(define req (map.make))"
        "(map.set! req 'task_id \"task-demo\")"
        "(map.set! req 'instruction \"move right\")"
        "(map.set! req 'deadline_ms 250)"
        "(map.set! req 'seed 42)"
        "(let ((obs (map.make)))"
        "  (map.set! obs 'state (list 0.1))"
        "  (map.set! obs 'timestamp_ms 1000)"
        "  (map.set! obs 'frame_id \"base\")"
        "  (map.set! req 'observation obs))"
        "(let ((space (map.make)))"
        "  (map.set! space 'type ':continuous)"
        "  (map.set! space 'dims 1)"
        "  (map.set! space 'bounds (list (list -1.0 1.0)))"
        "  (map.set! req 'action_space space))"
        "(let ((con (map.make)))"
        "  (map.set! con 'max_abs_value 1.0)"
        "  (map.set! con 'max_delta 1.0)"
        "  (map.set! req 'constraints con))"
        "(let ((m (map.make)))"
        "  (map.set! m 'name \"rt2-stub\")"
        "  (map.set! m 'version \"stub-1\")"
        "  (map.set! req 'model m))",
        env);

    value job = eval_text("(vla.submit req)", env);
    check(is_integer(job) && integer_value(job) > 0, "vla.submit should return positive job id");
    const std::int64_t job_id = integer_value(job);

    bool done = false;
    for (int i = 0; i < 250; ++i) {
        value st = eval_text("(map.get (vla.poll " + std::to_string(job_id) + ") 'status ':none)", env);
        check(is_symbol(st), "vla.poll status should be symbol");
        const std::string name = symbol_name(st);
        if (name == ":done") {
            done = true;
            break;
        }
        if (name == ":error" || name == ":timeout" || name == ":cancelled") {
            throw std::runtime_error("vla.poll unexpectedly reached terminal non-done state: " + name);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(done, "vla job should complete");

    value final_status =
        eval_text("(map.get (map.get (vla.poll " + std::to_string(job_id) + ") 'final (map.make)) 'status ':none)", env);
    check(is_symbol(final_status) && symbol_name(final_status) == ":ok", "vla final status should be :ok");
    value final_action =
        eval_text("(map.get (map.get (map.get (vla.poll " + std::to_string(job_id) + ") 'final (map.make)) 'action (map.make)) 'u nil)",
                  env);
    check(is_proper_list(final_action), "vla final action.u should be list");
    const std::vector<value> action_items = vector_from_list(final_action);
    check(action_items.size() == 1 && is_float(action_items[0]), "vla final action should be one float");
    check(float_value(action_items[0]) >= -1.0 && float_value(action_items[0]) <= 1.0, "vla final action out of bounds");

    value job2 = eval_text("(vla.submit req)", env);
    check(is_integer(job2) && integer_value(job2) > 0, "second vla.submit should return positive job id");
    value cancelled = eval_text("(vla.cancel " + std::to_string(integer_value(job2)) + ")", env);
    check(is_boolean(cancelled), "vla.cancel should return boolean");

    (void)eval_text(
        "(define bad-req (map.make))"
        "(map.set! bad-req 'task_id \"bad\")"
        "(map.set! bad-req 'deadline_ms 20)"
        "(let ((obs (map.make)))"
        "  (map.set! obs 'state (list 0.0))"
        "  (map.set! obs 'timestamp_ms 1000)"
        "  (map.set! obs 'frame_id \"base\")"
        "  (map.set! bad-req 'observation obs))"
        "(let ((space (map.make)))"
        "  (map.set! space 'type ':continuous)"
        "  (map.set! space 'dims 1)"
        "  (map.set! space 'bounds (list (list -1.0 1.0)))"
        "  (map.set! bad-req 'action_space space))"
        "(let ((con (map.make)))"
        "  (map.set! con 'max_abs_value 1.0)"
        "  (map.set! con 'max_delta 1.0)"
        "  (map.set! bad-req 'constraints con))"
        "(let ((m (map.make)))"
        "  (map.set! m 'name \"rt2-stub\")"
        "  (map.set! m 'version \"stub-1\")"
        "  (map.set! bad-req 'model m))",
        env);
    value bad_job = eval_text("(vla.submit bad-req)", env);
    check(is_integer(bad_job) && integer_value(bad_job) > 0, "bad request submit should still return job id");
    value bad_status = eval_text("(map.get (vla.poll " + std::to_string(integer_value(bad_job)) + ") 'status ':none)", env);
    check(is_symbol(bad_status) && symbol_name(bad_status) == ":error", "bad request should become error immediately");

    value vla_events = eval_text("(events.dump 80)", env);
    check(is_proper_list(vla_events), "events.dump should return list");
    const auto vla_rows = vector_from_list(vla_events);
    bool saw_vla_result = false;
    bool saw_task_id = false;
    bool saw_validation_error = false;
    for (const value& row : vla_rows) {
        if (!is_string(row)) {
            continue;
        }
        const std::string line = string_value(row);
        if (line.find("\"type\":\"vla_result\"") != std::string::npos) {
            saw_vla_result = true;
        }
        if (line.find("\"task_id\"") != std::string::npos) {
            saw_task_id = true;
        }
        if (line.find("request.instruction is required") != std::string::npos) {
            saw_validation_error = true;
        }
    }
    check(saw_vla_result, "events should include vla_result");
    check(saw_task_id, "vla events should contain task_id");
    check(saw_validation_error, "vla events should include immediate validation errors");
}

void test_vla_bt_nodes_flow_and_cancel() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text(
        "(define flow-tree "
        "  (bt.compile "
        "    '(sel "
        "       (seq "
        "         (vla-wait :name \"flow\" :job_key flow-job :action_key flow-action :meta_key flow-meta) "
        "         (succeed)) "
        "       (seq "
        "         (act bb-put-float fallback-action 0.0) "
        "         (vla-request :name \"flow\" :job_key flow-job :instruction \"move right\" "
        "                      :state_key state :deadline_ms 30 :dims 1 :bound_lo -1.0 :bound_hi 1.0) "
        "         (running)))))",
        env);
    (void)eval_text("(define flow-inst (bt.new-instance flow-tree))", env);

    bool reached_success = false;
    for (int i = 0; i < 80; ++i) {
        value st = eval_text("(bt.tick flow-inst '((state 0.0)))", env);
        check(is_symbol(st), "flow tree tick should return symbol");
        const std::string name = symbol_name(st);
        if (name == "success") {
            reached_success = true;
            break;
        }
        check(name == "running", "flow tree should be running until success");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(reached_success, "flow tree should eventually succeed");

    bt::runtime_host& host = bt::default_runtime_host();
    bt::instance* flow_inst = host.find_instance(bt_handle(eval_text("flow-inst", env)));
    check(flow_inst != nullptr, "flow instance should exist");
    const bt::bb_entry* action_entry = flow_inst->bb.get("flow-action");
    check(action_entry != nullptr, "vla-wait should write flow-action");

    (void)eval_text(
        "(define cancel-tree "
        "  (bt.compile "
        "    '(sel "
        "       (seq (cond bb-has stop) (vla-cancel :name \"cancel-flow\" :job_key c-job) (succeed)) "
        "       (vla-request :name \"cancel-flow\" :job_key c-job :instruction \"move\" "
        "                    :state_key state :deadline_ms 50 :dims 1 :bound_lo -1.0 :bound_hi 1.0))))",
        env);
    (void)eval_text("(define cancel-inst (bt.new-instance cancel-tree))", env);

    value first = eval_text("(bt.tick cancel-inst '((state 0.0)))", env);
    check(is_symbol(first) && symbol_name(first) == "running", "cancel tree first tick should run request");
    value second = eval_text("(bt.tick cancel-inst '((state 0.0) (stop #t)))", env);
    check(is_symbol(second) && symbol_name(second) == "success", "cancel tree second tick should cancel and succeed");
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

    try {
        (void)eval_text("(bt.compile '(plan-action :budget_ms))", env);
        throw std::runtime_error("expected bt.compile plan-action key/value validation failure");
    } catch (const lisp_error&) {
    }

    try {
        (void)eval_text("(bt.compile '(vla-request :instruction))", env);
        throw std::runtime_error("expected bt.compile vla-request key/value validation failure");
    } catch (const lisp_error&) {
    }

    (void)eval_text("(bt.compile '(mem-seq (succeed)))", env);
    (void)eval_text("(bt.compile '(mem-sel (succeed)))", env);
    (void)eval_text("(bt.compile '(async-seq (succeed)))", env);
    (void)eval_text("(bt.compile '(reactive-seq (succeed)))", env);
    (void)eval_text("(bt.compile '(reactive-sel (succeed)))", env);
}

void test_bt_new_composite_dsl_roundtrip() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    (void)eval_text(
        "(define tree "
        "  (bt.compile "
        "    '(reactive-sel "
        "       (reactive-seq (cond always-true) (async-seq (act always-success) (act always-success))) "
        "       (mem-seq (act always-success) (mem-sel (act always-fail) (succeed))))))",
        env);
    value dsl = eval_text("(bt.to-dsl tree)", env);
    check(is_cons(dsl), "bt.to-dsl should return a list");
    check(print_value(dsl).find("reactive-sel") != std::string::npos, "dsl should include reactive-sel");
    check(print_value(dsl).find("mem-seq") != std::string::npos, "dsl should include mem-seq");
    check(print_value(dsl).find("mem-sel") != std::string::npos, "dsl should include mem-sel");
    check(print_value(dsl).find("async-seq") != std::string::npos, "dsl should include async-seq");
    check(print_value(dsl).find("reactive-seq") != std::string::npos, "dsl should include reactive-seq");

    const std::filesystem::path dsl_path = temp_file_path("bt_new_nodes", ".lisp");
    const std::filesystem::path mbt_path = temp_file_path("bt_new_nodes", ".mbt");
    const std::string dsl_literal = lisp_string_literal(dsl_path.string());
    const std::string mbt_literal = lisp_string_literal(mbt_path.string());

    (void)eval_text("(bt.save-dsl tree " + dsl_literal + ")", env);
    (void)eval_text("(define tree-from-dsl (bt.load-dsl " + dsl_literal + "))", env);
    const value dsl_from_dsl = eval_text("(bt.to-dsl tree-from-dsl)", env);
    check(print_value(dsl_from_dsl) == print_value(dsl), "new nodes should roundtrip through bt.save-dsl/bt.load-dsl");

    (void)eval_text("(bt.save tree " + mbt_literal + ")", env);
    (void)eval_text("(define tree-from-mbt (bt.load " + mbt_literal + "))", env);
    const value dsl_from_mbt = eval_text("(bt.to-dsl tree-from-mbt)", env);
    check(print_value(dsl_from_mbt) == print_value(dsl), "new nodes should roundtrip through bt.save/bt.load");

    std::error_code ec;
    std::filesystem::remove(dsl_path, ec);
    std::filesystem::remove(mbt_path, ec);
}

void test_bt_mem_seq_semantics() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    int setup_calls = 0;
    int fail_once_calls = 0;
    int run_then_success_calls = 0;

    host.callbacks().register_action(
        "test-ms-setup",
        [&setup_calls](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
            ++setup_calls;
            return bt::status::success;
        });
    host.callbacks().register_action(
        "test-ms-run-then-success",
        [&run_then_success_calls](bt::tick_context&, bt::node_id, bt::node_memory& mem, std::span<const muslisp::value>) {
            ++run_then_success_calls;
            if (mem.i0 == 0) {
                mem.i0 = 1;
                return bt::status::running;
            }
            mem.i0 = 0;
            return bt::status::success;
        });
    host.callbacks().register_action(
        "test-ms-fail-once",
        [&fail_once_calls](bt::tick_context&, bt::node_id, bt::node_memory& mem, std::span<const muslisp::value>) {
            ++fail_once_calls;
            if (mem.i0 == 0) {
                mem.i0 = 1;
                return bt::status::failure;
            }
            mem.i0 = 0;
            return bt::status::success;
        });

    env_ptr env = create_global_env();

    (void)eval_text("(define tree-a (bt.compile '(mem-seq (act always-success) (act always-success) (act always-success))))", env);
    (void)eval_text("(define inst-a (bt.new-instance tree-a))", env);
    check(symbol_name(eval_text("(bt.tick inst-a)", env)) == "success", "mem-seq all-success should succeed");
    check(symbol_name(eval_text("(bt.tick inst-a)", env)) == "success", "mem-seq should reset after success");

    setup_calls = 0;
    run_then_success_calls = 0;
    (void)eval_text(
        "(define tree-b (bt.compile '(mem-seq (act test-ms-setup) (act test-ms-run-then-success) (act always-success))))",
        env);
    (void)eval_text("(define inst-b (bt.new-instance tree-b))", env);
    check(symbol_name(eval_text("(bt.tick inst-b)", env)) == "running", "mem-seq should return running");
    check(setup_calls == 1, "mem-seq should tick setup exactly once before running");
    check(symbol_name(eval_text("(bt.tick inst-b)", env)) == "success", "mem-seq should resume and succeed");
    check(setup_calls == 1, "mem-seq should resume at running child");
    check(run_then_success_calls == 2, "mem-seq running child should be revisited");

    setup_calls = 0;
    fail_once_calls = 0;
    (void)eval_text("(define tree-c (bt.compile '(mem-seq (act test-ms-setup) (act test-ms-fail-once) (act always-success))))", env);
    (void)eval_text("(define inst-c (bt.new-instance tree-c))", env);
    check(symbol_name(eval_text("(bt.tick inst-c)", env)) == "failure", "mem-seq should fail when child fails");
    check(symbol_name(eval_text("(bt.tick inst-c)", env)) == "success", "mem-seq should resume failing child next tick");
    check(setup_calls == 1, "mem-seq should not rerun prior success child after failure");
    check(fail_once_calls == 2, "mem-seq should retick failing child");
}

void test_bt_mem_sel_semantics() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    int fail_a_calls = 0;
    int fail_b_calls = 0;
    int run_then_success_calls = 0;

    host.callbacks().register_action(
        "test-msel-fail-a",
        [&fail_a_calls](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
            ++fail_a_calls;
            return bt::status::failure;
        });
    host.callbacks().register_action(
        "test-msel-fail-b",
        [&fail_b_calls](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
            ++fail_b_calls;
            return bt::status::failure;
        });
    host.callbacks().register_action(
        "test-msel-run-then-success",
        [&run_then_success_calls](bt::tick_context&, bt::node_id, bt::node_memory& mem, std::span<const muslisp::value>) {
            ++run_then_success_calls;
            if (mem.i0 == 0) {
                mem.i0 = 1;
                return bt::status::running;
            }
            mem.i0 = 0;
            return bt::status::success;
        });

    env_ptr env = create_global_env();

    (void)eval_text(
        "(define tree-a (bt.compile '(mem-sel (act test-msel-fail-a) (act test-msel-fail-b) (act test-msel-run-then-success))))",
        env);
    (void)eval_text("(define inst-a (bt.new-instance tree-a))", env);
    check(symbol_name(eval_text("(bt.tick inst-a)", env)) == "running", "mem-sel should run lower-priority child");
    check(fail_a_calls == 1 && fail_b_calls == 1, "mem-sel should evaluate failed higher priorities once");
    check(symbol_name(eval_text("(bt.tick inst-a)", env)) == "success", "mem-sel should resume running child");
    check(fail_a_calls == 1 && fail_b_calls == 1, "mem-sel should not retry earlier failed children while running");
    check(run_then_success_calls == 2, "mem-sel running child should complete");

    fail_a_calls = 0;
    fail_b_calls = 0;
    (void)eval_text("(define tree-b (bt.compile '(mem-sel (act test-msel-fail-a) (act test-msel-fail-b))))", env);
    (void)eval_text("(define inst-b (bt.new-instance tree-b))", env);
    check(symbol_name(eval_text("(bt.tick inst-b)", env)) == "failure", "mem-sel all-failure should fail");
    check(symbol_name(eval_text("(bt.tick inst-b)", env)) == "failure", "mem-sel should reset after all-failure");
    check(fail_a_calls == 2 && fail_b_calls == 2, "mem-sel reset should restart from child 0");
}

void test_bt_async_seq_semantics() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    int first_calls = 0;
    int second_calls = 0;

    host.callbacks().register_action(
        "test-async-seq-first",
        [&first_calls](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
            ++first_calls;
            return bt::status::success;
        });
    host.callbacks().register_action(
        "test-async-seq-second",
        [&second_calls](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
            ++second_calls;
            return bt::status::success;
        });

    env_ptr env = create_global_env();
    (void)eval_text("(define tree (bt.compile '(async-seq (act test-async-seq-first) (act test-async-seq-second))))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "running", "async-seq should yield running between children");
    check(first_calls == 1, "async-seq first child should run once");
    check(second_calls == 0, "async-seq should not tick second child on first tick");
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success", "async-seq should complete on second tick");
    check(first_calls == 1, "async-seq should not rerun first child on second tick");
    check(second_calls == 1, "async-seq should tick second child on resume");
}

void test_bt_reactive_preemption_and_memoryless_regressions() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    int seq_setup_calls = 0;
    int sel_fail_calls = 0;

    host.callbacks().register_action(
        "test-seq-setup",
        [&seq_setup_calls](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
            ++seq_setup_calls;
            return bt::status::success;
        });
    host.callbacks().register_action(
        "test-sel-fail",
        [&sel_fail_calls](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
            ++sel_fail_calls;
            return bt::status::failure;
        });
    host.callbacks().register_condition("test-gate",
                                        [](bt::tick_context& ctx, std::span<const muslisp::value>) -> bool {
                                            const bt::bb_entry* entry = ctx.bb_get("gate");
                                            if (!entry) {
                                                return false;
                                            }
                                            if (const bool* b = std::get_if<bool>(&entry->value)) {
                                                return *b;
                                            }
                                            return false;
                                        });
    host.callbacks().register_condition("test-high-priority",
                                        [](bt::tick_context& ctx, std::span<const muslisp::value>) -> bool {
                                            const bt::bb_entry* entry = ctx.bb_get("high");
                                            if (!entry) {
                                                return false;
                                            }
                                            if (const bool* b = std::get_if<bool>(&entry->value)) {
                                                return *b;
                                            }
                                            return false;
                                        });

    env_ptr env = create_global_env();

    (void)eval_text("(define seq-tree (bt.compile '(seq (act test-seq-setup) (act running-then-success))))", env);
    (void)eval_text("(define seq-inst (bt.new-instance seq-tree))", env);
    check(symbol_name(eval_text("(bt.tick seq-inst)", env)) == "running", "seq tick1 should be running");
    check(symbol_name(eval_text("(bt.tick seq-inst)", env)) == "success", "seq tick2 should succeed");
    check(seq_setup_calls == 2, "seq should remain memoryless and restart from child 0");

    (void)eval_text("(define sel-tree (bt.compile '(sel (act test-sel-fail) (act running-then-success))))", env);
    (void)eval_text("(define sel-inst (bt.new-instance sel-tree))", env);
    check(symbol_name(eval_text("(bt.tick sel-inst)", env)) == "running", "sel tick1 should be running");
    check(symbol_name(eval_text("(bt.tick sel-inst)", env)) == "success", "sel tick2 should succeed");
    check(sel_fail_calls == 2, "sel should remain memoryless and retry first child");

    (void)eval_text("(define rseq-tree (bt.compile '(reactive-seq (cond test-gate) (act async-sleep-ms 200))))", env);
    (void)eval_text("(define rseq-inst (bt.new-instance rseq-tree))", env);
    check(symbol_name(eval_text("(bt.tick rseq-inst '((gate #t)))", env)) == "running", "reactive-seq tick1 should run");
    check(symbol_name(eval_text("(bt.tick rseq-inst '((gate #f)))", env)) == "failure",
          "reactive-seq tick2 should fail and preempt");

    (void)eval_text("(define rsel-tree (bt.compile '(reactive-sel (cond test-high-priority) (act async-sleep-ms 200))))", env);
    (void)eval_text("(define rsel-inst (bt.new-instance rsel-tree))", env);
    check(symbol_name(eval_text("(bt.tick rsel-inst '((high #f)))", env)) == "running",
          "reactive-sel tick1 should run low-priority child");
    check(symbol_name(eval_text("(bt.tick rsel-inst '((high #t)))", env)) == "success",
          "reactive-sel tick2 should switch to high-priority success");
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

void test_bt_blackboard_events_and_stats_builtins() {
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

    value events_dump = eval_text("(events.dump 40)", env);
    check(is_proper_list(events_dump), "events.dump should return list");
    bool saw_tick_begin = false;
    bool saw_node_status = false;
    bool saw_bb_write = false;
    bool saw_tick_end = false;
    for (const value& row : vector_from_list(events_dump)) {
        if (!is_string(row)) {
            continue;
        }
        const std::string line = string_value(row);
        if (line.find("\"type\":\"tick_begin\"") != std::string::npos) {
            saw_tick_begin = true;
        }
        if (line.find("\"type\":\"node_status\"") != std::string::npos) {
            saw_node_status = true;
        }
        if (line.find("\"type\":\"bb_write\"") != std::string::npos) {
            saw_bb_write = true;
        }
        if (line.find("\"type\":\"tick_end\"") != std::string::npos) {
            saw_tick_end = true;
        }
    }
    check(saw_tick_begin, "events should include tick_begin");
    check(saw_node_status, "events should include node_status");
    check(saw_bb_write, "events should include bb_write");
    check(saw_tick_end, "events should include tick_end");

    value stats_dump = eval_text("(bt.stats inst)", env);
    check(is_string(stats_dump), "bt.stats should return string");
    check(string_value(stats_dump).find("tick_count=1") != std::string::npos, "stats should include tick_count=1");

    (void)eval_text("(bt.set-tick-budget-ms inst 1)", env);
    (void)eval_text("(bt.tick inst)", env);
    events_dump = eval_text("(events.dump 80)", env);
    check(is_proper_list(events_dump), "events.dump should return list after retick");
}

void test_bt_blackboard_get_builtin() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    (void)eval_text("(define tree (bt.compile '(seq (act bb-put-int foo 42) (succeed))))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success", "bb tree should tick to success");

    value foo_value = eval_text("(bt.blackboard.get inst 'foo -1)", env);
    check(is_integer(foo_value) && integer_value(foo_value) == 42, "bt.blackboard.get should read integer entry");

    value missing_default = eval_text("(bt.blackboard.get inst 'missing 77)", env);
    check(is_integer(missing_default) && integer_value(missing_default) == 77,
          "bt.blackboard.get should return caller default for missing key");

    value missing_nil = eval_text("(bt.blackboard.get inst 'missing)", env);
    check(is_nil(missing_nil), "bt.blackboard.get without default should return nil");
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

    value events_dump = eval_text("(events.dump 60)", env);
    bool saw_sched_submit = false;
    bool saw_sched_finish = false;
    for (const value& row : vector_from_list(events_dump)) {
        if (!is_string(row)) {
            continue;
        }
        const std::string line = string_value(row);
        if (line.find("\"type\":\"sched_submit\"") != std::string::npos) {
            saw_sched_submit = true;
        }
        if (line.find("\"type\":\"sched_finish\"") != std::string::npos) {
            saw_sched_finish = true;
        }
    }
    check(saw_sched_submit, "events should include sched_submit");
    check(saw_sched_finish, "events should include sched_finish");

    value sched_stats = eval_text("(bt.scheduler.stats)", env);
    check(is_string(sched_stats), "bt.scheduler.stats should return string");
    check(string_value(sched_stats).find("submitted=") != std::string::npos, "scheduler stats missing submitted");
}

void test_canonical_event_stream_builtins() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    check(is_nil(eval_text("(events.enable #t)", env)), "events.enable should return nil");
    check(is_nil(eval_text("(events.set-ring-size 256)", env)), "events.set-ring-size should return nil");

    (void)eval_text("(define tree (bt.compile '(seq (act bb-put-int foo 42) (cond bb-has foo))))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success", "event stream test tree should succeed");
    (void)eval_text("(events.snapshot-bb)", env);
    (void)eval_text("(bt.tick inst)", env);

    value dumped = eval_text("(events.dump 20)", env);
    check(is_proper_list(dumped), "events.dump should return list");
    const auto rows = vector_from_list(dumped);
    check(!rows.empty(), "events.dump should return at least one event");
    bool saw_schema = false;
    bool saw_tick_begin = false;
    bool saw_tick_end = false;
    bool saw_node_status = false;
    bool saw_bb_write = false;
    bool saw_bb_snapshot = false;
    for (const auto& row : rows) {
        check(is_string(row), "events.dump rows should be JSON strings");
        const std::string line = string_value(row);
        if (line.find("\"schema\":\"mbt.evt.v1\"") != std::string::npos) {
            saw_schema = true;
        }
        if (line.find("\"type\":\"tick_begin\"") != std::string::npos) {
            saw_tick_begin = true;
        }
        if (line.find("\"type\":\"tick_end\"") != std::string::npos) {
            saw_tick_end = true;
        }
        if (line.find("\"type\":\"node_status\"") != std::string::npos) {
            saw_node_status = true;
        }
        if (line.find("\"type\":\"bb_write\"") != std::string::npos) {
            saw_bb_write = true;
        }
        if (line.find("\"type\":\"bb_snapshot\"") != std::string::npos) {
            saw_bb_snapshot = true;
        }
    }

    check(saw_schema, "events should include schema envelope");
    check(saw_tick_begin, "events should include tick_begin");
    check(saw_tick_end, "events should include tick_end");
    check(saw_node_status, "events should include node_status");
    check(saw_bb_write, "events should include bb_write");
    check(saw_bb_snapshot, "events should include bb_snapshot");
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

void test_event_log_deterministic_mode_and_canonical_serialisation() {
    bt::event_log events(16);
    events.set_run_id("fixture-run");
    events.set_deterministic_time(1735689600000, 3);

    std::vector<std::string> callback_lines;
    events.set_line_listener([&callback_lines](const std::string& line) { callback_lines.push_back(line); });

    const std::uint64_t seq1 = events.emit("tick_begin", 1, "{\"root\":1}");
    const std::uint64_t seq2 = events.emit("tick_end", 1, "{\"status\":\"success\"}");
    check(seq1 == 1, "event log should start sequence at 1 after set_run_id");
    check(seq2 == 2, "event log should increment sequence for each event");
    check(callback_lines.size() == 2, "line listener should receive canonical line for each event");

    const std::string expected_first = bt::event_log::serialise_event_line(
        "tick_begin", "fixture-run", 1735689600000, 1, 1, "{\"root\":1}");
    const std::string expected_second = bt::event_log::serialise_event_line(
        "tick_end", "fixture-run", 1735689600003, 2, 1, "{\"status\":\"success\"}");
    check(callback_lines[0] == expected_first, "line listener should receive canonical serialised tick_begin line");
    check(callback_lines[1] == expected_second, "line listener should receive canonical serialised tick_end line");

    const auto ring = events.snapshot();
    check(ring.size() == 2, "event ring should contain emitted events");
    check(ring == callback_lines, "event ring and callback lines should match canonical serialisation");
}

void test_event_log_capture_stats_without_serialised_sink() {
    bt::event_log events(0);
    events.set_run_id("stats-run");
    events.set_deterministic_time(1735689602000, 5);
    events.set_capture_stats_enabled(true);

    const std::string payload = "{\"node_id\":7}";
    const std::uint64_t seq = events.emit("node_enter", 4, payload);
    check(seq == 1, "capture stats path should still advance event sequence");

    const bt::event_log_stats stats = events.capture_stats();
    check(stats.event_count == 1, "capture stats should count emitted events");
    const std::size_t expected_size =
        bt::event_log::serialise_event_line("node_enter", "stats-run", 1735689602000, 1, 4, payload).size();
    check(stats.byte_count == expected_size, "capture stats should match canonical serialised line size");
    check(events.snapshot().empty(), "zero-capacity ring should not retain canonical lines");
}

void test_runtime_host_deterministic_test_mode() {
    bt::runtime_host host;
    host.enable_deterministic_test_mode(4242, "deterministic-host", 1735689601000, 7);
    check(host.deterministic_test_mode_enabled(), "runtime host should report deterministic mode enabled");
    check(host.planner_ref().base_seed() == 4242, "deterministic mode should set fixed planner base seed");

    (void)host.events().emit("tick_begin", 1, "{\"root\":1}");
    const auto events = host.events().snapshot();
    check(events.size() == 1, "deterministic mode smoke should emit one event");
    check(events.front().find("\"run_id\":\"deterministic-host\"") != std::string::npos,
          "deterministic mode should pin event run_id");
    check(events.front().find("\"unix_ms\":1735689601000") != std::string::npos,
          "deterministic mode should pin event timestamp progression");
    check(events.front().find("\"seq\":1") != std::string::npos, "deterministic mode should preserve stable sequence ordering");

    host.disable_deterministic_test_mode();
    check(!host.deterministic_test_mode_enabled(), "runtime host should report deterministic mode disabled");
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

#if MUESLI_BT_WITH_PYBULLET_INTEGRATION
class mock_racecar_adapter final : public bt::racecar_sim_adapter {
public:
    bt::racecar_state get_state() override {
        ++get_state_calls;
        if (throw_get_state_at > 0 && get_state_calls == throw_get_state_at) {
            throw std::runtime_error("forced get_state failure");
        }
        bt::racecar_state state;
        state.state_schema = "racecar_state.v1";
        state.x = 0.05 * static_cast<double>(get_state_calls);
        state.y = 0.0;
        state.yaw = 0.0;
        state.speed = 0.1;
        state.goal = {10.0, 0.0};
        state.rays = {3.0, 3.0, 3.0, 3.0, 3.0};
        state.state_vec = {state.x, state.y, state.yaw, state.speed, state.goal[0], state.goal[1],
                           state.rays[0], state.rays[1], state.rays[2], state.rays[3], state.rays[4]};
        state.collision_imminent = false;
        state.collision_count = collision_count;
        state.t_ms = get_state_calls * 50;
        return state;
    }

    void apply_action(double steering, double throttle) override {
        ++apply_calls;
        actions.emplace_back(steering, throttle);
        last_action = {steering, throttle};
    }

    void step(std::int64_t steps) override {
        ++step_calls;
        step_args.push_back(steps);
    }

    void reset() override {}

    void on_tick_record(const bt::racecar_tick_record& record) override {
        ++tick_record_calls;
        tick_records.push_back(record);
    }

    std::int64_t throw_get_state_at = -1;
    std::int64_t get_state_calls = 0;
    std::int64_t apply_calls = 0;
    std::int64_t step_calls = 0;
    std::int64_t tick_record_calls = 0;
    std::int64_t collision_count = 0;
    std::vector<std::int64_t> step_args{};
    std::vector<std::pair<double, double>> actions{};
    std::array<double, 2> last_action{0.0, 0.0};
    std::vector<bt::racecar_tick_record> tick_records{};
};
#endif

muslisp::map_key test_symbol_key(const std::string& name) {
    muslisp::map_key key;
    key.type = muslisp::map_key_type::symbol;
    key.text_data = name;
    return key;
}

void test_map_set_symbol(muslisp::value map_obj, const std::string& key, muslisp::value v) {
    map_obj->map_data[test_symbol_key(key)] = v;
}

class test_loop_backend final : public muslisp::env_backend {
public:
    explicit test_loop_backend(bool supports_reset, std::int64_t done_after_steps)
        : supports_reset_(supports_reset), done_after_steps_(done_after_steps) {}

    [[nodiscard]] muslisp::env_backend_supports supports() const override {
        muslisp::env_backend_supports out;
        out.reset = supports_reset_;
        out.debug_draw = false;
        out.headless = true;
        out.realtime_pacing = false;
        out.deterministic_seed = true;
        return out;
    }

    void configure(muslisp::value opts) override {
        if (!muslisp::is_map(opts)) {
            throw std::runtime_error("configure: expected map");
        }
        ++configure_calls;
    }

    [[nodiscard]] muslisp::value reset(std::optional<std::int64_t> seed) override {
        if (!supports_reset_) {
            throw std::runtime_error("reset unsupported");
        }
        ++reset_calls;
        if (seed.has_value()) {
            last_seed = *seed;
        }
        steps_in_episode = 0;
        return observe();
    }

    [[nodiscard]] muslisp::value observe() override {
        ++observe_calls;
        muslisp::value obs = muslisp::make_map();
        muslisp::gc_root_scope roots(muslisp::default_gc());
        roots.add(&obs);

        test_map_set_symbol(obs, "obs_schema", muslisp::make_string("test.loop.obs.v1"));
        test_map_set_symbol(obs, "t_ms", muslisp::make_integer(global_steps * 10));
        test_map_set_symbol(obs, "done", muslisp::make_boolean(steps_in_episode >= done_after_steps_));
        return obs;
    }

    void act(muslisp::value action) override {
        if (!muslisp::is_map(action)) {
            throw std::runtime_error("act: expected action map");
        }
        ++act_calls;
    }

    [[nodiscard]] bool step() override {
        ++step_calls;
        ++steps_in_episode;
        ++global_steps;
        return true;
    }

    bool supports_reset_ = false;
    std::int64_t done_after_steps_ = 0;
    std::int64_t configure_calls = 0;
    std::int64_t reset_calls = 0;
    std::int64_t observe_calls = 0;
    std::int64_t act_calls = 0;
    std::int64_t step_calls = 0;
    std::int64_t steps_in_episode = 0;
    std::int64_t global_steps = 0;
    std::int64_t last_seed = 0;
};

class test_loop_extension final : public muslisp::extension {
public:
    explicit test_loop_extension(std::shared_ptr<muslisp::env_backend> backend) : backend_(std::move(backend)) {
        if (!backend_) {
            throw muslisp::lisp_error("test backend registration requires non-null backend");
        }
    }

    [[nodiscard]] std::string name() const override {
        return "tests.loop-backend";
    }

    void register_lisp(muslisp::registrar& reg) const override {
        (void)reg;
        muslisp::env_api_register_backend("loop-test", backend_);
    }

private:
    std::shared_ptr<muslisp::env_backend> backend_;
};

muslisp::env_ptr create_env_with_test_loop_backend(const std::shared_ptr<muslisp::env_backend>& backend) {
    muslisp::runtime_config config;
    config.register_extension(std::make_unique<test_loop_extension>(backend));
    return muslisp::create_global_env(std::move(config));
}

#if MUESLI_BT_WITH_PYBULLET_INTEGRATION
void test_racecar_loop_contract() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    bt::install_racecar_demo_callbacks(host);
    env_ptr env = create_global_env();

    (void)eval_text(
        "(define tree "
        "  (bt.compile '(seq (act constant-drive action 0.1 0.3) (act apply-action action) (running))))",
        env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    const std::int64_t inst_handle = bt_handle(eval_text("inst", env));

    auto adapter = std::make_shared<mock_racecar_adapter>();
    bt::set_racecar_sim_adapter(adapter);

    bt::racecar_loop_options opts;
    opts.tick_hz = 1000.0;
    opts.max_ticks = 5;
    opts.state_key = "state";
    opts.action_key = "action";
    opts.steps_per_tick = 3;
    opts.mode = "bt_basic";
    opts.run_id = "test-run";

    const bt::racecar_loop_result result = bt::run_racecar_loop(host, inst_handle, opts);
    check(result.status == bt::racecar_loop_status::stopped, "run-loop should stop on max ticks");
    check(result.ticks == 5, "run-loop should execute max ticks");
    check(adapter->get_state_calls == 5, "run-loop should observe exactly once per tick");
    check(adapter->apply_calls == 5, "run-loop should apply exactly once per tick");
    check(adapter->step_calls == 5, "run-loop should step exactly once per tick");
    check(adapter->tick_record_calls == 5, "run-loop should emit exactly one tick record per tick");
    check(result.fallback_count == 0, "run-loop should not use fallback when action is valid");
    for (std::int64_t arg : adapter->step_args) {
        check(arg == 3, "run-loop should preserve configured steps_per_tick");
    }
    check(!adapter->tick_records.empty(), "run-loop should collect tick records");
    check(adapter->tick_records.front().run_id == "test-run", "run-loop record should carry run_id");

    bt::clear_racecar_demo_state();
}

void test_racecar_loop_error_safe_action() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    bt::install_racecar_demo_callbacks(host);
    env_ptr env = create_global_env();

    (void)eval_text(
        "(define tree "
        "  (bt.compile '(seq (act constant-drive action 0.2 0.4) (act apply-action action) (running))))",
        env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    const std::int64_t inst_handle = bt_handle(eval_text("inst", env));

    auto adapter = std::make_shared<mock_racecar_adapter>();
    adapter->throw_get_state_at = 2;
    bt::set_racecar_sim_adapter(adapter);

    bt::racecar_loop_options opts;
    opts.tick_hz = 1000.0;
    opts.max_ticks = 10;
    opts.state_key = "state";
    opts.action_key = "action";
    opts.steps_per_tick = 2;
    opts.safe_action = {0.33, 0.0};
    opts.mode = "bt_basic";
    opts.run_id = "error-run";

    const bt::racecar_loop_result result = bt::run_racecar_loop(host, inst_handle, opts);
    check(result.status == bt::racecar_loop_status::error, "run-loop should return :error when adapter throws");
    check(result.ticks == 2, "run-loop error path should emit final error tick record");
    check(adapter->apply_calls == 2, "run-loop error path should apply safe action once");
    check(adapter->step_calls == 2, "run-loop error path should step once after safe action");
    check_close(adapter->last_action[0], 0.33, 1e-9, "safe action steering mismatch");
    check_close(adapter->last_action[1], 0.0, 1e-9, "safe action throttle mismatch");
    check(adapter->tick_records.size() == 2, "run-loop error path should emit final error record");
    check(adapter->tick_records.back().is_error_record, "final record should be marked as error");
    check(result.fallback_count == 0, "error-safe-action should not change fallback_count");

    bt::clear_racecar_demo_state();
}

void test_racecar_planner_model_and_env_api_contract() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    env_ptr env = create_env_with_pybullet_extension();
    check(host.planner_ref().has_model("racecar-kinematic-v1"), "planner should register racecar-kinematic-v1 model");

    auto adapter = std::make_shared<mock_racecar_adapter>();
    bt::set_racecar_sim_adapter(adapter);

    (void)eval_text("(env.attach \"pybullet\")", env);
    value state_meta = eval_text(
        "(begin "
        "  (define s (env.reset nil)) "
        "  (define info (map.get s 'info (map.make))) "
        "  (list (map.get info 'state_schema \"none\") "
        "        (map.get info 'x -1.0) "
        "        (map.get info 'collision_count -1)))",
        env);
    const auto state_fields = vector_from_list(state_meta);
    check(state_fields.size() == 3, "env.reset info metadata shape mismatch");
    check(is_string(state_fields[0]) && string_value(state_fields[0]) == "racecar_state.v1",
          "env.reset info should expose state_schema");
    check(is_float(state_fields[1]), "env.reset info should expose x as float");
    check(is_integer(state_fields[2]) && integer_value(state_fields[2]) == 0,
          "env.reset info should expose collision_count as int");

    (void)eval_text(
        "(define tree "
        "  (bt.compile "
        "    '(seq "
        "       (plan-action :name \"race\" :planner :mcts :budget_ms 8 :work_max 120 "
        "                    :model_service \"racecar-kinematic-v1\" :state_key state :action_key action :meta_key plan-meta) "
        "       (act apply-action action))))",
        env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    value st = eval_text("(bt.tick inst '((state (0.0 0.0 0.0 0.0 7.0 3.0 3.0 3.0 3.0))))", env);
    check(is_symbol(st) && symbol_name(st) == "success", "plan-action with racecar model should succeed");

    bt::instance* inst = host.find_instance(bt_handle(eval_text("inst", env)));
    check(inst != nullptr, "racecar plan-action instance should exist");
    const bt::bb_entry* action_entry = inst->bb.get("action");
    check(action_entry != nullptr, "racecar plan-action should publish action");
    const auto* action_vec = std::get_if<std::vector<double>>(&action_entry->value);
    check(action_vec && action_vec->size() >= 2, "racecar plan-action should output [steering throttle]");

    bt::clear_racecar_demo_state();
}
#endif

void test_env_run_loop_multi_episode_reset_true() {
    using namespace muslisp;

    reset_bt_runtime_host();
    auto backend = std::make_shared<test_loop_backend>(true, 1000);
    env_ptr env = create_env_with_test_loop_backend(backend);

    (void)eval_text("(env.attach \"loop-test\")", env);
    (void)eval_text(
        "(define on-tick-loop "
        "  (lambda (obs) "
        "    (begin "
        "      (define a (map.make)) "
        "      (map.set! a 'action_schema \"test.loop.action.v1\") "
        "      (map.set! a 'u (list 0.0)) "
        "      a)))",
        env);

    (void)eval_text(
        "(define loop-multi-result "
        "  (env.run-loop "
        "    (begin "
        "      (define cfg (map.make)) "
        "      (map.set! cfg 'tick_hz 1000) "
        "      (map.set! cfg 'max_ticks 99) "
        "      (map.set! cfg 'step_max 2) "
        "      (map.set! cfg 'episode_max 3) "
        "      (map.set! cfg 'stop_on_success #f) "
        "      cfg) "
        "    on-tick-loop))",
        env);

    check(symbol_name(eval_text("(map.get loop-multi-result 'status ':none)", env)) == ":stopped",
          "multi-episode run-loop should stop on episode_max");
    check(integer_value(eval_text("(map.get loop-multi-result 'episodes_completed -1)", env)) == 3,
          "multi-episode run-loop episodes_completed mismatch");
    check(integer_value(eval_text("(map.get loop-multi-result 'steps_total -1)", env)) == 6,
          "multi-episode run-loop steps_total mismatch");
    check(integer_value(eval_text("(map.get loop-multi-result 'last_episode_steps -1)", env)) == 2,
          "multi-episode run-loop last_episode_steps mismatch");
    check(integer_value(eval_text("(map.get loop-multi-result 'episodes -1)", env)) == 3,
          "multi-episode compatibility key episodes mismatch");
    check(integer_value(eval_text("(map.get loop-multi-result 'ticks -1)", env)) == 6,
          "multi-episode compatibility key ticks mismatch");
    check(backend->reset_calls == 3, "multi-episode run-loop should reset at episode start");
    check(backend->step_calls == 6, "multi-episode run-loop step count mismatch");
}

void test_env_run_loop_multi_episode_reset_false() {
    using namespace muslisp;

    reset_bt_runtime_host();
    auto backend = std::make_shared<test_loop_backend>(false, 1000);
    env_ptr env = create_env_with_test_loop_backend(backend);

    (void)eval_text("(env.attach \"loop-test\")", env);
    (void)eval_text(
        "(define on-tick-loop-unsupported "
        "  (lambda (obs) "
        "    (begin "
        "      (define a (map.make)) "
        "      (map.set! a 'action_schema \"test.loop.action.v1\") "
        "      (map.set! a 'u (list 0.0)) "
        "      a)))",
        env);

    (void)eval_text(
        "(define loop-unsupported-result "
        "  (env.run-loop "
        "    (begin "
        "      (define cfg (map.make)) "
        "      (map.set! cfg 'tick_hz 1000) "
        "      (map.set! cfg 'max_ticks 10) "
        "      (map.set! cfg 'step_max 2) "
        "      (map.set! cfg 'episode_max 2) "
        "      cfg) "
        "    on-tick-loop-unsupported))",
        env);

    check(symbol_name(eval_text("(map.get loop-unsupported-result 'status ':none)", env)) == ":unsupported",
          "run-loop should return :unsupported when episode_max > 1 without reset support");
    check(integer_value(eval_text("(map.get loop-unsupported-result 'episodes_completed -1)", env)) == 0,
          "unsupported run-loop should not complete episodes");
    check(integer_value(eval_text("(map.get loop-unsupported-result 'steps_total -1)", env)) == 0,
          "unsupported run-loop should not step");
    const std::string message = string_value(eval_text("(map.get loop-unsupported-result 'message \"\")", env));
    check(message.find("requires env.reset capability") != std::string::npos,
          "unsupported run-loop message should mention reset requirement");
}

void test_env_core_interface_unattached() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    value info = eval_text("(env.info)", env);
    check(is_map(info), "env.info should return map");
    check(is_string(eval_text("(map.get (env.info) 'api_version \"\")", env)), "env.info api_version must be string");
    check(string_value(eval_text("(map.get (env.info) 'api_version \"\")", env)) == "env.api.v1",
          "env.info api_version mismatch");
    check(!boolean_value(eval_text("(map.get (env.info) 'attached #t)", env)), "env.info should report unattached");
    check(is_nil(eval_text("(map.get (env.info) 'backend ':none)", env)), "env.info backend should be nil when unattached");

    try {
        (void)eval_text("(env.observe)", env);
        throw std::runtime_error("expected env.observe to fail when unattached");
    } catch (const lisp_error& e) {
        check(std::string(e.what()) == "env backend not attached", "env.observe unattached error mismatch");
    }
}

#if MUESLI_BT_WITH_PYBULLET_INTEGRATION
void test_env_generic_pybullet_backend_contract() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    bt::install_racecar_demo_callbacks(host);
    env_ptr env = create_env_with_pybullet_extension();

    auto adapter = std::make_shared<mock_racecar_adapter>();
    bt::set_racecar_sim_adapter(adapter);

    (void)eval_text("(env.attach \"pybullet\")", env);
    check(boolean_value(eval_text("(map.get (env.info) 'attached #f)", env)), "env.info should report attached after attach");
    check(string_value(eval_text("(map.get (env.info) 'backend \"\")", env)) == "pybullet", "env.info backend mismatch");
    check(boolean_value(eval_text("(map.get (map.get (env.info) 'supports (map.make)) 'reset #f)", env)),
          "env.info supports.reset should be true for pybullet backend");

    (void)eval_text(
        "(begin "
        "  (define cfg (map.make)) "
        "  (map.set! cfg 'tick_hz 1000) "
        "  (map.set! cfg 'steps_per_tick 2) "
        "  (map.set! cfg 'realtime #f) "
        "  (env.configure cfg))",
        env);
    (void)eval_text("(define obs0 (env.reset 7))", env);
    value obs0 = eval_text("obs0", env);
    check(is_map(obs0), "env.reset should return observation map");
    check(string_value(eval_text("(map.get obs0 'obs_schema \"\")", env)) == "racecar.obs.v1", "env.reset obs_schema mismatch");
    check(is_integer(eval_text("(map.get obs0 't_ms -1)", env)), "env.reset observation should include t_ms");
    check(integer_value(eval_text("(map.get obs0 'episode -1)", env)) == 1, "env.reset should set episode to 1");
    check(integer_value(eval_text("(map.get obs0 'step -1)", env)) == 0, "env.reset should set step to 0");

    (void)eval_text(
        "(begin "
        "  (define a (map.make)) "
        "  (map.set! a 'action_schema \"racecar.action.v1\") "
        "  (map.set! a 'u (list 0.2 0.3)) "
        "  (env.act a))",
        env);
    check(adapter->apply_calls == 1, "env.act should call adapter once");

    value step_ok = eval_text("(env.step)", env);
    check(is_boolean(step_ok) && boolean_value(step_ok), "env.step should return true");
    check(adapter->step_calls == 1, "env.step should call adapter step");
    check(!adapter->step_args.empty() && adapter->step_args.back() == 2, "env.step should use configured steps_per_tick");

    (void)eval_text("(define obs1 (env.observe))", env);
    value obs1 = eval_text("obs1", env);
    check(is_map(obs1), "env.observe should return map");
    check(integer_value(eval_text("(map.get obs1 'step -1)", env)) == 1, "env.observe should expose incremented step");

    (void)eval_text(
        "(define on-tick "
        "  (lambda (obs) "
        "    (begin "
        "      (define a (map.make)) "
        "      (map.set! a 'action_schema \"racecar.action.v1\") "
        "      (map.set! a 'u (list 0.0 0.1)) "
        "      a)))",
        env);
    (void)eval_text(
        "(define loop-result "
        "  (env.run-loop "
        "    (begin "
        "      (define cfg (map.make)) "
        "      (define safe (map.make)) "
        "      (map.set! safe 'action_schema \"racecar.action.v1\") "
        "      (map.set! safe 'u (list 0.0 0.0)) "
        "      (map.set! cfg 'tick_hz 1000) "
        "      (map.set! cfg 'max_ticks 3) "
        "      (map.set! cfg 'safe_action safe) "
        "      cfg) "
        "    on-tick))",
        env);
    check(symbol_name(eval_text("(map.get loop-result 'status ':none)", env)) == ":stopped",
          "env.run-loop should stop on max ticks");
    check(integer_value(eval_text("(map.get loop-result 'ticks -1)", env)) == 3, "env.run-loop ticks mismatch");
    check(integer_value(eval_text("(map.get loop-result 'episodes -1)", env)) == 1, "env.run-loop episodes mismatch");

    bt::clear_racecar_demo_state();
}

void test_env_run_loop_log_record_shape() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    bt::install_racecar_demo_callbacks(host);
    env_ptr env = create_env_with_pybullet_extension();

    auto adapter = std::make_shared<mock_racecar_adapter>();
    bt::set_racecar_sim_adapter(adapter);

    (void)eval_text("(env.attach \"pybullet\")", env);

    const std::filesystem::path log_path = temp_file_path("env_runloop_record", ".jsonl");
    const std::string log_lisp = lisp_string_literal(log_path.string());

    (void)eval_text(
        "(define on-tick-log-shape "
        "  (lambda (obs) "
        "    (begin "
        "      (define a (map.make)) "
        "      (map.set! a 'action_schema \"racecar.action.v1\") "
        "      (map.set! a 'u (list 0.0 0.1)) "
        "      (define btm (map.make)) "
        "      (map.set! btm 'active_path (list \"root\" \"node\")) "
        "      (define pm (map.make)) "
        "      (map.set! pm 'used #t) "
        "      (map.set! pm 'confidence 0.5) "
        "      (define out (map.make)) "
        "      (map.set! out 'schema_version \"epuck_demo.v1\") "
        "      (map.set! out 'action a) "
        "      (map.set! out 'bt btm) "
        "      (map.set! out 'planner pm) "
        "      out)))",
        env);

    const std::string run_expr =
        "(define run-result-log-shape "
        "  (env.run-loop "
        "    (begin "
        "      (define cfg (map.make)) "
        "      (define safe (map.make)) "
        "      (map.set! safe 'action_schema \"racecar.action.v1\") "
        "      (map.set! safe 'u (list 0.0 0.0)) "
        "      (map.set! cfg 'tick_hz 1000) "
        "      (map.set! cfg 'max_ticks 1) "
        "      (map.set! cfg 'realtime #f) "
        "      (map.set! cfg 'safe_action safe) "
        "      (map.set! cfg 'schema_version \"epuck_demo.v1\") "
        "      (map.set! cfg 'log_path " +
        log_lisp +
        ") "
        "      cfg) "
        "    on-tick-log-shape))";
    (void)eval_text(run_expr, env);
    value run_result = eval_text("run-result-log-shape", env);

    check(is_map(run_result), "env.run-loop result should be map");
    check(symbol_name(eval_text("(map.get run-result-log-shape 'status ':none)", env)) == ":stopped",
          "env.run-loop should stop on max_ticks=1");

    std::ifstream in(log_path);
    check(in.good(), "expected env.run-loop log file to exist");
    std::string line;
    std::getline(in, line);
    check(!line.empty(), "expected at least one env.run-loop log record");
    check(line.find("\"schema_version\":\"epuck_demo.v1\"") != std::string::npos, "log record missing schema_version");
    check(line.find("\"t_ms\":") != std::string::npos, "log record missing t_ms");
    check(line.find("\"budget\":") != std::string::npos, "log record missing budget block");
    check(line.find("\"tick_budget_ms\"") != std::string::npos, "log record missing tick_budget_ms");
    check(line.find("\"tick_time_ms\"") != std::string::npos, "log record missing tick_time_ms");
    check(line.find("\"bt\":") != std::string::npos, "log record missing bt map");
    check(line.find("\"planner\":") != std::string::npos, "log record missing planner map");

    std::error_code ec;
    std::filesystem::remove(log_path, ec);

    bt::clear_racecar_demo_state();
}
#endif

void test_pybullet_backend_absent_in_core_env() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    try {
        (void)eval_text("(env.attach \"pybullet\")", env);
        throw std::runtime_error("expected env.attach to reject unknown pybullet backend without extension");
    } catch (const lisp_error& e) {
        const std::string msg = e.what();
        check(msg.find("unknown backend") != std::string::npos, "missing unknown backend error for pybullet attach");
    }
}

void test_ros2_backend_absent_in_core_env() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    try {
        (void)eval_text("(env.attach \"ros2\")", env);
        throw std::runtime_error("expected env.attach to reject unknown ros2 backend without extension");
    } catch (const lisp_error& e) {
        const std::string msg = e.what();
        check(msg.find("unknown backend") != std::string::npos, "missing unknown backend error for ros2 attach");
    }
}

#if MUESLI_BT_WITH_PYBULLET_INTEGRATION
void test_pybullet_backend_present_with_extension() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_pybullet_extension();
    (void)eval_text("(env.attach \"pybullet\")", env);
    check(string_value(eval_text("(map.get (env.info) 'backend \"\")", env)) == "pybullet",
          "env.info backend should be pybullet when extension is installed");
}
#endif

#if MUESLI_BT_WITH_ROS2_INTEGRATION
void test_env_generic_ros2_backend_contract() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();
    test_support::ros2_test_harness harness("/robot");

    (void)eval_text("(env.attach \"ros2\")", env);
    check(boolean_value(eval_text("(map.get (env.info) 'attached #f)", env)),
          "env.info should report attached after ros2 attach");
    check(string_value(eval_text("(map.get (env.info) 'backend \"\")", env)) == "ros2", "env.info backend mismatch for ros2");
    check(!boolean_value(eval_text("(map.get (map.get (env.info) 'supports (map.make)) 'reset #t)", env)),
          "env.info supports.reset should default to false for ros2 backend");
    check(string_value(eval_text("(map.get (env.info) 'env_api \"\")", env)) == "env.api.v1",
          "ros2 env.info env_api mismatch");
    check(string_value(eval_text("(map.get (env.info) 'backend_version \"\")", env)) == "ros2.transport.v1",
          "ros2 env.info backend_version mismatch");

    (void)eval_text(ros2_configure_script(
                         harness.topic_ns(),
                         "  (map.set! cfg 'backend_version \"ros2.transport.v1\") "
                         "  (map.set! cfg 'obs_schema \"ros2.obs.test.v1\") "
                         "  (map.set! cfg 'state_schema \"ros2.state.test.v1\") "
                         "  (map.set! cfg 'action_schema \"ros2.action.test.v1\") "
                         "  (map.set! cfg 'reset_mode \"stub\") "),
                     env);
    check(harness.wait_for_transport_ready(std::chrono::milliseconds(500)),
          "ros2 test harness transport should be ready after configure");
    check(string_value(eval_text("(map.get (env.info) 'obs_schema \"\")", env)) == "ros2.obs.test.v1",
          "ros2 env.info obs_schema mismatch");
    check(string_value(eval_text("(map.get (env.info) 'state_schema \"\")", env)) == "ros2.state.test.v1",
          "ros2 env.info state_schema mismatch");
    check(string_value(eval_text("(map.get (env.info) 'action_schema \"\")", env)) == "ros2.action.test.v1",
          "ros2 env.info action_schema mismatch");
    check(boolean_value(eval_text("(map.get (env.info) 'run_loop_supported #f)", env)),
          "ros2 env.info run_loop_supported mismatch");
    check(string_value(eval_text("(car (map.get (env.info) 'capabilities nil))", env)) == "observe",
          "ros2 env.info capabilities should begin with observe");
    check(integer_value(eval_text("(map.get (map.get (env.info) 'config (map.make)) 'control_hz -1)", env)) == 50,
          "ros2 env.info config control_hz mismatch");
    check(string_value(eval_text("(map.get (map.get (env.info) 'config (map.make)) 'topic_ns \"\")", env)) == "/robot",
          "ros2 env.info config topic_ns mismatch");
    check(boolean_value(eval_text("(map.get (env.info) 'reset_supported #f)", env)),
          "ros2 env.info reset_supported should reflect stub reset mode");
    check(string_value(eval_text("(map.get (env.info) 'obs_topic \"\")", env)) == "/robot/odom",
          "ros2 env.info obs_topic mismatch");
    check(string_value(eval_text("(map.get (env.info) 'action_topic \"\")", env)) == "/robot/cmd_vel",
          "ros2 env.info action_topic mismatch");

    (void)eval_text("(define obs0 (env.reset 42))", env);
    value obs0 = eval_text("obs0", env);
    check(is_map(obs0), "env.reset should return observation map for ros2");
    check(string_value(eval_text("(map.get obs0 'obs_schema \"\")", env)) == "ros2.obs.test.v1", "ros2 reset obs_schema mismatch");
    check(string_value(eval_text("(map.get obs0 'state_schema \"\")", env)) == "ros2.state.test.v1",
          "ros2 reset top-level state_schema mismatch");
    check(integer_value(eval_text("(map.get obs0 'episode -1)", env)) == 1, "ros2 reset should set episode to 1");
    check(integer_value(eval_text("(map.get obs0 'step -1)", env)) == 0, "ros2 reset should set step to 0");
    check(string_value(eval_text("(map.get (map.get obs0 'state (map.make)) 'state_schema \"\")", env)) == "ros2.state.test.v1",
          "ros2 reset state_schema mismatch");
    check(string_value(eval_text("(map.get (map.get obs0 'state (map.make)) 'frame_id \"\")", env)) == "map",
          "ros2 reset frame_id mismatch");
    check(integer_value(eval_text("(map.get (map.get obs0 'info (map.make)) 'seed -1)", env)) == 42,
          "ros2 reset should persist provided seed");

    harness.publish_odom(1.25, -0.5, 0.3, 0.15, -0.05, 0.2);
    (void)eval_text("(define obs1 (env.observe))", env);
    check_close(float_value(eval_text("(map.get (map.get (map.get obs1 'state (map.make)) 'pose (map.make)) 'x 0.0)", env)),
                1.25,
                1e-6,
                "ros2 observe should expose odom pose.x");
    check_close(float_value(eval_text("(map.get (map.get (map.get obs1 'state (map.make)) 'twist (map.make)) 'vx 0.0)", env)),
                0.15,
                1e-6,
                "ros2 observe should expose canonical twist.vx");
    check(boolean_value(eval_text("(map.get (map.get obs1 'flags (map.make)) 'fresh_obs #f)", env)),
          "ros2 observe should mark the first received odom sample as fresh");

    (void)eval_text(
        "(begin "
        "  (define a (map.make)) "
        "  (define u (map.make)) "
        "  (map.set! a 'action_schema \"ros2.action.test.v1\") "
        "  (map.set! a 't_ms 7) "
        "  (map.set! u 'linear_x 0.2) "
        "  (map.set! u 'linear_y -0.1) "
        "  (map.set! u 'angular_z 0.4) "
        "  (map.set! a 'u u) "
        "  (env.act a))",
        env);
    check(harness.wait_for_command_count(1, std::chrono::milliseconds(250)),
          "ros2 env.act should publish a cmd_vel command");
    const auto first_command = harness.last_command();
    check_close(first_command.linear.x, 0.2, 1e-6, "ros2 env.act linear.x publish mismatch");
    check_close(first_command.linear.y, -0.1, 1e-6, "ros2 env.act linear.y publish mismatch");
    check_close(first_command.angular.z, 0.4, 1e-6, "ros2 env.act angular.z publish mismatch");

    value step_ok = eval_text("(env.step)", env);
    check(is_boolean(step_ok) && boolean_value(step_ok), "ros2 env.step should return true");
    (void)eval_text("(define obs2 (env.observe))", env);
    check(integer_value(eval_text("(map.get obs2 'step -1)", env)) == 1,
          "ros2 env.step should advance the runtime step counter");
    check_close(float_value(eval_text("(map.get (map.get (map.get obs2 'state (map.make)) 'twist (map.make)) 'vx 0.0)", env)),
                0.15,
                1e-6,
                "ros2 observe should preserve the latest received odom twist");

    (void)eval_text(
        "(define on-tick-ros2 "
        "  (lambda (obs) "
        "    (begin "
        "      (define a (map.make)) "
        "      (define u (map.make)) "
        "      (map.set! a 'action_schema \"ros2.action.test.v1\") "
        "      (map.set! a 't_ms (map.get obs 't_ms 0)) "
        "      (map.set! u 'linear_x 0.0) "
        "      (map.set! u 'linear_y 0.0) "
        "      (map.set! u 'angular_z 0.1) "
        "      (map.set! a 'u u) "
        "      a)))",
        env);
    harness.publish_odom(1.5, -0.25, 0.4, 0.1, 0.0, 0.05);
    (void)eval_text(
        "(define loop-result-ros2 "
        "  (env.run-loop "
        "    (begin "
        "      (define cfg (map.make)) "
        "      (define safe (map.make)) "
        "      (define safe-u (map.make)) "
        "      (map.set! safe 'action_schema \"ros2.action.test.v1\") "
        "      (map.set! safe 't_ms 0) "
        "      (map.set! safe-u 'linear_x 0.0) "
        "      (map.set! safe-u 'linear_y 0.0) "
        "      (map.set! safe-u 'angular_z 0.0) "
        "      (map.set! safe 'u safe-u) "
        "      (map.set! cfg 'tick_hz 1000) "
        "      (map.set! cfg 'max_ticks 2) "
        "      (map.set! cfg 'safe_action safe) "
        "      cfg) "
        "    on-tick-ros2))",
        env);
    check(symbol_name(eval_text("(map.get loop-result-ros2 'status ':none)", env)) == ":stopped",
          "ros2 env.run-loop should stop on max ticks");
    check(integer_value(eval_text("(map.get loop-result-ros2 'ticks -1)", env)) == 2,
          "ros2 env.run-loop ticks mismatch");
    check(integer_value(eval_text("(map.get loop-result-ros2 'episodes -1)", env)) == 1,
          "ros2 env.run-loop episodes mismatch");
    check(harness.wait_for_command_count(3, std::chrono::milliseconds(250)),
          "ros2 env.run-loop should publish actions through cmd_vel");
}

void test_ros2_backend_config_validation_and_reset_policy() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();
    (void)eval_text("(env.attach \"ros2\")", env);

    try {
        (void)eval_text(
            "(begin "
            "  (define cfg (map.make)) "
            "  (map.set! cfg 'unknown_key 1) "
            "  (env.configure cfg))",
            env);
        throw std::runtime_error("expected ros2 env.configure to reject unknown keys");
    } catch (const lisp_error& e) {
        check(std::string(e.what()).find("unknown option") != std::string::npos,
              "ros2 env.configure unknown-key error mismatch");
    }

    try {
        (void)eval_text(
            "(begin "
            "  (define cfg (map.make)) "
            "  (map.set! cfg 'backend_version \"ros2.transport.v2\") "
            "  (env.configure cfg))",
            env);
        throw std::runtime_error("expected ros2 env.configure to reject unsupported backend_version");
    } catch (const lisp_error& e) {
        check(std::string(e.what()).find("unsupported backend_version") != std::string::npos,
              "ros2 env.configure backend_version error mismatch");
    }

    try {
        (void)eval_text(
            "(begin "
            "  (define cfg (map.make)) "
            "  (map.set! cfg 'obs_schema \"racecar.obs.v1\") "
            "  (env.configure cfg))",
            env);
        throw std::runtime_error("expected ros2 env.configure to reject non-ros2 obs_schema family");
    } catch (const lisp_error& e) {
        check(std::string(e.what()).find("ros2.obs") != std::string::npos,
              "ros2 env.configure obs_schema family error mismatch");
    }

    try {
        (void)eval_text(
            "(begin "
            "  (define cfg (map.make)) "
            "  (map.set! cfg 'action_schema \"ros2.action.v2\") "
            "  (env.configure cfg))",
            env);
        throw std::runtime_error("expected ros2 env.configure to reject unsupported action_schema major");
    } catch (const lisp_error& e) {
        check(std::string(e.what()).find("expected v1") != std::string::npos,
              "ros2 env.configure action_schema major error mismatch");
    }

    (void)eval_text(
        "(begin "
        "  (define cfg (map.make)) "
        "  (map.set! cfg 'backend_version \"ros2.transport.v1\") "
        "  (map.set! cfg 'action_clamp \"reject\") "
        "  (map.set! cfg 'reset_mode \"unsupported\") "
        "  (env.configure cfg))",
        env);
    check(!boolean_value(eval_text("(map.get (map.get (env.info) 'supports (map.make)) 'reset #t)", env)),
          "ros2 env.info supports.reset should track reset_mode");
    check(!boolean_value(eval_text("(map.get (env.info) 'reset_supported #t)", env)),
          "ros2 env.info reset_supported should track reset_mode");

    try {
        (void)eval_text("(env.reset nil)", env);
        throw std::runtime_error("expected ros2 env.reset to fail when reset_mode is unsupported");
    } catch (const lisp_error& e) {
        check(std::string(e.what()).find("does not support reset") != std::string::npos,
              "ros2 env.reset unsupported error mismatch");
    }

    try {
        (void)eval_text(
            "(begin "
            "  (define a (map.make)) "
            "  (define u (map.make)) "
            "  (map.set! a 'action_schema \"ros2.action.v1\") "
            "  (map.set! a 't_ms 1) "
            "  (map.set! u 'linear_x 2.0) "
            "  (map.set! u 'linear_y 0.0) "
            "  (map.set! u 'angular_z 0.0) "
            "  (map.set! a 'u u) "
            "  (env.act a))",
            env);
        throw std::runtime_error("expected ros2 env.act reject policy to reject out-of-range actions");
    } catch (const lisp_error& e) {
        check(std::string(e.what()).find("out of range") != std::string::npos,
              "ros2 env.act reject-policy error mismatch");
    }

    (void)eval_text(
        "(define loop-result-ros2-unsupported "
        "  (env.run-loop "
        "    (begin "
        "      (define cfg (map.make)) "
        "      (map.set! cfg 'tick_hz 1000) "
        "      (map.set! cfg 'max_ticks 1) "
        "      (map.set! cfg 'episode_max 2) "
        "      cfg) "
        "    (lambda (obs) #t)))",
        env);
    check(symbol_name(eval_text("(map.get loop-result-ros2-unsupported 'status ':none)", env)) == ":unsupported",
          "ros2 env.run-loop should report unsupported when reset_mode disables reset");
}

void test_ros2_backend_invalid_action_fallback() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();
    test_support::ros2_test_harness harness("/invalid");
    (void)eval_text("(env.attach \"ros2\")", env);
    (void)eval_text(ros2_configure_script(harness.topic_ns()), env);
    check(harness.wait_for_transport_ready(std::chrono::milliseconds(500)),
          "ros2 invalid-action transport should be ready after configure");
    harness.publish_odom(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

    (void)eval_text(
        "(define loop-result-ros2-invalid "
        "  (env.run-loop "
        "    (begin "
        "      (define cfg (map.make)) "
        "      (define safe (map.make)) "
        "      (define safe-u (map.make)) "
        "      (map.set! safe 'action_schema \"ros2.action.v1\") "
        "      (map.set! safe 't_ms 0) "
        "      (map.set! safe-u 'linear_x 0.0) "
        "      (map.set! safe-u 'linear_y 0.0) "
        "      (map.set! safe-u 'angular_z 0.0) "
        "      (map.set! safe 'u safe-u) "
        "      (map.set! cfg 'tick_hz 1) "
        "      (map.set! cfg 'max_ticks 2) "
        "      (map.set! cfg 'safe_action safe) "
        "      cfg) "
        "    (lambda (obs) "
        "      (begin "
        "        (define bad (map.make)) "
        "        (map.set! bad 'action_schema \"ros2.action.v1\") "
        "        (map.set! bad 't_ms (map.get obs 't_ms 0)) "
        "        (map.set! bad 'u 1) "
        "        bad))))",
        env);

    const std::string invalid_status = symbol_name(eval_text("(map.get loop-result-ros2-invalid 'status ':none)", env));
    check(invalid_status == ":error",
          "ros2 env.run-loop should return error when on_tick action is malformed (got " + invalid_status + ")");
    check(integer_value(eval_text("(map.get loop-result-ros2-invalid 'fallback_count -1)", env)) == 1,
          "ros2 env.run-loop should count safe-action fallback on malformed action");
    const std::string message = string_value(eval_text("(map.get loop-result-ros2-invalid 'message \"\")", env));
    check(message.find("u must be a map") != std::string::npos,
          "ros2 env.run-loop malformed-action message mismatch");
    check(harness.wait_for_command_count(1, std::chrono::milliseconds(250)),
          "ros2 invalid-action fallback should still publish the safe command");
    const auto safe_command = harness.last_command();
    check_close(safe_command.linear.x, 0.0, 1e-6, "ros2 safe fallback linear.x mismatch");
    check_close(safe_command.angular.z, 0.0, 1e-6, "ros2 safe fallback angular.z mismatch");
}

void test_ros2_h1_demo_success_path() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();
    test_support::ros2_test_harness harness("/h1_01");

    const std::filesystem::path repo_root = find_repo_root();
    const std::filesystem::path demo_runtime = repo_root / "examples/isaac_h1_ros2_demo/lisp/demo_runtime.lisp";
    const std::filesystem::path run_log = temp_file_path("isaac_h1_demo_run", ".jsonl");
    const std::filesystem::path event_log = temp_file_path("isaac_h1_demo_events", ".jsonl");

    check(std::filesystem::exists(demo_runtime), "expected H1 demo runtime script to exist");

    std::jthread publisher([&harness](std::stop_token stop_token) {
        if (!harness.wait_for_transport_ready(std::chrono::milliseconds(3000))) {
            return;
        }
        const std::vector<std::vector<double>> samples = {
            {0.00, 0.00, 0.00},
            {0.10, 0.00, 0.00},
            {0.20, 0.00, 0.00},
            {0.30, 0.00, 0.00},
            {0.40, 0.00, 0.00},
            {0.40, 0.00, 0.40},
            {0.40, 0.00, 0.90},
            {0.40, 0.00, 1.20},
            {0.40, 0.00, 1.57},
            {0.40, 0.10, 1.57},
            {0.40, 0.20, 1.57},
            {0.40, 0.30, 1.57},
            {0.40, 0.30, 1.57},
        };
        for (const auto& sample : samples) {
            for (int repeat = 0; repeat < 3; ++repeat) {
                if (stop_token.stop_requested()) {
                    return;
                }
                harness.publish_odom(sample[0], sample[1], sample[2], 0.0, 0.0, 0.0);
            }
        }
    });

    const std::string demo_script =
        "(begin "
        "  (load " + lisp_string_literal(demo_runtime.string()) + ") "
        "  (define demo-cfg (make-default-h1-demo-config)) "
        "  (map.set! demo-cfg 'topic_ns \"/h1_01\") "
        "  (map.set! demo-cfg 'max_ticks 80) "
        "  (map.set! demo-cfg 'step_max 80) "
        "  (map.set! demo-cfg 'stand_ticks 1) "
        "  (map.set! demo-cfg 'obs_timeout_ms 500) "
        "  (map.set! demo-cfg 'goal_tol 0.10) "
        "  (map.set! demo-cfg 'turn_tol 0.12) "
        "  (map.set! demo-cfg 'walk_speed 0.25) "
        "  (map.set! demo-cfg 'log_path " + lisp_string_literal(run_log.string()) + ") "
        "  (map.set! demo-cfg 'event_log_path " + lisp_string_literal(event_log.string()) + ") "
        "  (map.set! demo-cfg 'waypoints "
        "    (list "
        "      (make-waypoint \"forward\" 0.40 0.00 0.0) "
        "      (make-waypoint \"left\" 0.40 0.30 1.5707963267948966))) "
        "  (define demo-success (run-h1-demo demo-cfg)))";
    (void)eval_text(demo_script, env);
    if (publisher.joinable()) {
        publisher.join();
    }

    const std::string status = symbol_name(eval_text("(map.get (map.get demo-success 'result (map.make)) 'status ':none)", env));
    const std::string reason =
        string_value(eval_text("(map.get (map.get demo-success 'result (map.make)) 'reason \"\")", env));
    check(status == ":ok", "H1 demo success path should finish with :ok (got " + status + ": " + reason + ")");
    check(integer_value(eval_text("(map.get (map.get demo-success 'runtime (map.make)) 'waypoint_index -1)", env)) == 2,
          "H1 demo success path should complete both waypoints");
    check(string_value(eval_text("(map.get (map.get demo-success 'runtime (map.make)) 'last_branch_name \"\")", env)) == "goal_stop",
          "H1 demo success path should end on goal_stop");
    check(harness.wait_for_command_count(1, std::chrono::milliseconds(250)),
          "H1 demo success path should publish at least one command");
    const auto last_command = harness.last_command();
    check_close(last_command.linear.x, 0.0, 1e-6, "H1 demo success path should end with zero forward velocity");
    check_close(last_command.angular.z, 0.0, 1e-6, "H1 demo success path should end with zero angular velocity");

    std::ifstream run_in(run_log);
    check(run_in.good(), "expected H1 demo run-loop log to exist");
    std::string run_line;
    std::getline(run_in, run_line);
    check(!run_line.empty(), "expected H1 demo run-loop log to contain at least one line");

    std::ifstream event_in(event_log);
    check(event_in.good(), "expected H1 demo event log to exist");
    std::string event_contents((std::istreambuf_iterator<char>(event_in)), std::istreambuf_iterator<char>());
    check(event_contents.find("\"schema\":\"mbt.evt.v1\"") != std::string::npos,
          "expected H1 demo event log to contain canonical event records");
}

void test_ros2_h1_demo_timeout_stop() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();
    test_support::ros2_test_harness harness("/h1_01");

    const std::filesystem::path repo_root = find_repo_root();
    const std::filesystem::path demo_runtime = repo_root / "examples/isaac_h1_ros2_demo/lisp/demo_runtime.lisp";
    const std::filesystem::path run_log = temp_file_path("isaac_h1_demo_timeout_run", ".jsonl");
    const std::filesystem::path event_log = temp_file_path("isaac_h1_demo_timeout_events", ".jsonl");

    check(std::filesystem::exists(demo_runtime), "expected H1 demo runtime script to exist");

    std::jthread publisher([&harness]() {
        if (!harness.wait_for_transport_ready(std::chrono::milliseconds(3000))) {
            return;
        }
        harness.publish_odom(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    });

    const std::string demo_script =
        "(begin "
        "  (load " + lisp_string_literal(demo_runtime.string()) + ") "
        "  (define demo-cfg (make-default-h1-demo-config)) "
        "  (map.set! demo-cfg 'topic_ns \"/h1_01\") "
        "  (map.set! demo-cfg 'max_ticks 4) "
        "  (map.set! demo-cfg 'step_max 4) "
        "  (map.set! demo-cfg 'stand_ticks 0) "
        "  (map.set! demo-cfg 'obs_timeout_ms 10) "
        "  (map.set! demo-cfg 'log_path " + lisp_string_literal(run_log.string()) + ") "
        "  (map.set! demo-cfg 'event_log_path " + lisp_string_literal(event_log.string()) + ") "
        "  (map.set! demo-cfg 'waypoints "
        "    (list (make-waypoint \"forward\" 0.40 0.00 0.0))) "
        "  (define demo-timeout (run-h1-demo demo-cfg)))";
    (void)eval_text(demo_script, env);
    if (publisher.joinable()) {
        publisher.join();
    }

    const std::string status = symbol_name(eval_text("(map.get (map.get demo-timeout 'result (map.make)) 'status ':none)", env));
    check(status == ":stopped", "H1 demo timeout path should stop on max_ticks after issuing timeout stop commands");
    const std::string branch_name =
        string_value(eval_text("(map.get (map.get demo-timeout 'runtime (map.make)) 'last_branch_name \"\")", env));
    check(branch_name == "timeout_stop", "H1 demo timeout path should end on timeout_stop (got " + branch_name + ")");
    check(harness.wait_for_command_count(1, std::chrono::milliseconds(250)),
          "H1 demo timeout path should publish a stop command");
    const auto last_command = harness.last_command();
    check_close(last_command.linear.x, 0.0, 1e-6, "H1 demo timeout path should hold zero forward velocity");
    check_close(last_command.angular.z, 0.0, 1e-6, "H1 demo timeout path should hold zero angular velocity");

    std::ifstream run_in(run_log);
    check(run_in.good(), "expected H1 demo timeout run-loop log to exist");
}

void test_ros2_backend_present_with_extension() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();
    test_support::ros2_test_harness harness("/present");
    (void)eval_text("(env.attach \"ros2\")", env);
    check(string_value(eval_text("(map.get (env.info) 'backend \"\")", env)) == "ros2",
          "env.info backend should be ros2 when extension is installed");
    (void)eval_text(ros2_configure_script(harness.topic_ns()), env);
    check(harness.wait_for_transport_ready(std::chrono::milliseconds(500)),
          "ros2 backend presence transport should be ready after configure");
    harness.publish_odom(0.75, 0.1, 0.2, 0.05, 0.0, 0.1);

    check(string_value(eval_text("(map.get (env.observe) 'obs_schema \"\")", env)) == "ros2.obs.v1",
          "ros2 observe obs_schema mismatch");
    check(string_value(eval_text("(map.get (env.observe) 'state_schema \"\")", env)) == "ros2.state.v1",
          "ros2 observe state_schema mismatch");

    (void)eval_text(
        "(define a "
        "  (begin "
        "    (define m (map.make)) "
        "    (define u (map.make)) "
        "    (map.set! m 'action_schema \"ros2.action.v1\") "
        "    (map.set! m 't_ms 1) "
        "    (map.set! u 'linear_x 0.25) "
        "    (map.set! u 'linear_y 0.0) "
        "    (map.set! u 'angular_z 0.5) "
        "    (map.set! m 'u u) "
        "    m))",
        env);
    (void)eval_text("(env.act a)", env);
    check(is_truthy(eval_text("(env.step)", env)), "ros2 env.step should continue");
    check(harness.wait_for_command_count(1, std::chrono::milliseconds(250)),
          "ros2 backend presence smoke should publish one command");

    check(integer_value(eval_text("(map.get (env.observe) 'step -1)", env)) >= 1,
          "ros2 backend step counter should advance");
}

void test_ros2_cleanup_with_live_transport_peer() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();
    test_support::ros2_test_harness harness("/cleanup");
    (void)eval_text("(env.attach \"ros2\")", env);
    (void)eval_text(ros2_configure_script(harness.topic_ns()), env);
    check(harness.wait_for_transport_ready(std::chrono::milliseconds(500)),
          "ros2 cleanup transport should be ready after configure");
    harness.publish_odom(0.25, 0.0, 0.0, 0.1, 0.0, 0.0);
    check(string_value(eval_text("(map.get (env.observe) 'obs_schema \"\")", env)) == "ros2.obs.v1",
          "ros2 cleanup observe should return canonical obs_schema");
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
    muslisp::env_api_reset();
    env = nullptr;
}
#endif

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"reader basics", test_reader_basics},
        {"repl support commands and history path", test_repl_support_commands_and_history_path},
        {"repl support history entry normalisation", test_repl_support_history_entry_normalisation},
        {"environment shadowing", test_environment_shadowing},
        {"error hierarchy basics", test_error_hierarchy_basics},
        {"eval special forms and arithmetic", test_eval_special_forms_and_arithmetic},
        {"numeric rules, predicates, and printing", test_numeric_rules_predicates_and_printing},
        {"integer overflow checks", test_integer_overflow_checks},
        {"closures and function define sugar", test_closures_and_function_define_sugar},
        {"quasiquote semantics and errors", test_quasiquote_semantics_and_errors},
        {"let and cond forms", test_let_and_cond_forms},
        {"and/or forms", test_and_or_forms},
        {"evaluator tail-position readiness", test_evaluator_tail_position_readiness},
        {"tail-call optimisation smoke", test_tail_call_optimisation_smoke},
        {"tail-call optimisation deep recursion", test_tail_call_optimisation_deep_recursion},
        {"compiled closure path", test_compiled_closure_path},
        {"tail-call optimisation through and/or", test_tail_call_optimisation_and_or},
        {"gc env root stack regression", test_gc_env_root_stack_regression},
        {"gc duplicate env roots are stack-like", test_gc_duplicate_env_roots_are_stack_like},
        {"evaluator error messages stable", test_evaluator_error_messages_stable},
        {"bt authoring sugar", test_bt_authoring_sugar},
        {"load/write/save and roundtrip", test_load_write_save_and_roundtrip},
        {"load resolves nested relative paths", test_load_resolves_nested_relative_paths_from_loaded_file},
        {"bt dsl save/load roundtrip", test_bt_dsl_save_load_roundtrip},
        {"bt export-dot builtin", test_bt_export_dot_builtin},
        {"bt binary save/load roundtrip and validation", test_bt_binary_save_load_roundtrip_and_validation},
        {"list and predicate builtins", test_list_and_predicate_builtins},
        {"gc and stats builtins", test_gc_and_stats_builtins},
        {"gc during argument evaluation", test_gc_during_argument_evaluation},
        {"math/time builtins and domain errors", test_math_time_and_domain_errors},
        {"rng determinism and ranges", test_rng_determinism_and_ranges},
        {"vec gc/growth/fuzz", test_vec_gc_growth_and_fuzz},
        {"map gc/rehash/ops", test_map_gc_rehash_and_ops},
        {"pq builtins gc/errors", test_pq_builtins_gc_and_errors},
        {"continuous mcts smoke deterministic", test_continuous_mcts_smoke_deterministic},
        {"planner.plan determinism/bounds/budget/sanity", test_planner_plan_builtin_determinism_bounds_budget_and_sanity},
        {"plan-action node blackboard/meta/logs", test_plan_action_node_blackboard_meta_and_logs},
        {"plan-action node all planner backends", test_plan_action_node_with_all_planner_backends},
        {"hash64 builtin", test_hash64_builtin},
        {"json and handle builtins", test_json_and_handle_builtins},
        {"vla builtins submit/poll/cancel/caps", test_vla_builtins_submit_poll_cancel_and_caps},
        {"vla bt nodes flow and cancel", test_vla_bt_nodes_flow_and_cancel},
        {"bt compile checks", test_bt_compile_checks},
        {"bt new composite dsl roundtrip", test_bt_new_composite_dsl_roundtrip},
        {"bt mem-seq semantics", test_bt_mem_seq_semantics},
        {"bt mem-sel semantics", test_bt_mem_sel_semantics},
        {"bt async-seq semantics", test_bt_async_seq_semantics},
        {"bt reactive preemption + memoryless regressions", test_bt_reactive_preemption_and_memoryless_regressions},
        {"bt seq/running semantics", test_bt_seq_and_running_semantics},
        {"bt decorator semantics", test_bt_decorator_semantics},
        {"bt reset clears phase4 state", test_bt_reset_clears_phase4_state},
        {"bt blackboard/events/stats builtins", test_bt_blackboard_events_and_stats_builtins},
        {"bt blackboard.get builtin", test_bt_blackboard_get_builtin},
        {"bt scheduler-backed action", test_bt_scheduler_backed_action},
        {"canonical event stream builtins", test_canonical_event_stream_builtins},
        {"bt tick with blackboard input", test_bt_tick_with_blackboard_input},
        {"env core interface unattached", test_env_core_interface_unattached},
        {"env run-loop multi-episode reset=true", test_env_run_loop_multi_episode_reset_true},
        {"env run-loop multi-episode reset=false", test_env_run_loop_multi_episode_reset_false},
        {"event log deterministic mode + canonical serialisation", test_event_log_deterministic_mode_and_canonical_serialisation},
        {"event log capture stats without serialised sink", test_event_log_capture_stats_without_serialised_sink},
        {"runtime host deterministic test mode", test_runtime_host_deterministic_test_mode},
        {"pybullet backend absent in core env", test_pybullet_backend_absent_in_core_env},
        {"ros2 backend absent in core env", test_ros2_backend_absent_in_core_env},
#if MUESLI_BT_WITH_PYBULLET_INTEGRATION
        {"env generic pybullet backend contract", test_env_generic_pybullet_backend_contract},
        {"env run-loop log record shape", test_env_run_loop_log_record_shape},
        {"pybullet backend present with extension", test_pybullet_backend_present_with_extension},
        {"racecar run-loop contract", test_racecar_loop_contract},
        {"racecar run-loop error safe-action", test_racecar_loop_error_safe_action},
        {"racecar planner model + env.api contract", test_racecar_planner_model_and_env_api_contract},
#endif
#if MUESLI_BT_WITH_ROS2_INTEGRATION
        {"env generic ros2 backend contract", test_env_generic_ros2_backend_contract},
        {"ros2 backend config validation and reset policy", test_ros2_backend_config_validation_and_reset_policy},
        {"ros2 backend invalid action fallback", test_ros2_backend_invalid_action_fallback},
        {"ros2 H1 demo success path", test_ros2_h1_demo_success_path},
        {"ros2 H1 demo timeout stop", test_ros2_h1_demo_timeout_stop},
        {"ros2 backend present with extension", test_ros2_backend_present_with_extension},
        {"ros2 cleanup with live transport peer", test_ros2_cleanup_with_live_transport_peer},
#endif
        {"phase5 ring buffer bounds", test_phase5_ring_buffer_bounds},
        {"phase6 sample wrappers tree", test_phase6_sample_wrappers_tree},
        {"phase6 custom robot interface", test_phase6_custom_robot_interface},
    };

    const auto cleanup = []() {
#if MUESLI_BT_WITH_ROS2_INTEGRATION
        if (rclcpp::ok()) {
            rclcpp::shutdown();
        }
        muslisp::env_api_reset();
#endif
    };

    std::size_t passed = 0;
    for (const auto& [name, test_fn] : tests) {
        try {
            test_fn();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& e) {
            std::cerr << "[FAIL] " << name << ": " << e.what() << '\n';
            cleanup();
            return 1;
        }
    }

    std::cout << "All tests passed (" << passed << "/" << tests.size() << ").\n";
    cleanup();
    return 0;
}
