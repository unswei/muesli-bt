#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "bt/approach_pose_validator.hpp"
#include "bt/instance.hpp"
#include "bt/event_log.hpp"
#include "bt/event_payload.hpp"
#include "bt/logging.hpp"
#include "bt/model_service.hpp"
#include "bt/node_options.hpp"
#include "bt/planner.hpp"
#include "bt/registry.hpp"
#include "bt/runtime.hpp"
#include "bt/runtime_host.hpp"
#include "bt/status.hpp"
#include "bt/trace.hpp"
#include "../src/compiled_eval.hpp"
#include "../src/repl_support.hpp"
#include "muslisp/cap_api.hpp"
#if MUESLI_BT_WITH_PYBULLET_INTEGRATION
#include "pybullet/extension.hpp"
#include "pybullet/racecar_demo.hpp"
#endif
#if MUESLI_BT_WITH_ROS2_INTEGRATION
#include "ros2/extension.hpp"
#include "ros2_nav2_test_harness.hpp"
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
    host.set_vla_commit_validator(nullptr);
    host.set_walking_target_dispatcher(nullptr);
    bt::install_demo_callbacks(host);
}

class controlled_vla_backend final : public bt::vla_backend {
public:
    explicit controlled_vla_backend(std::shared_ptr<std::atomic<bool>> release) : release_(std::move(release)) {}

    bt::vla_response infer(const bt::vla_request& request,
                           std::function<bool(const bt::vla_partial&)>,
                           std::atomic<bool>& cancel_flag) override {
        while (!release_->load()) {
            if (cancel_flag.load()) {
                bt::vla_response cancelled;
                cancelled.status = bt::vla_status::cancelled;
                cancelled.model = request.model;
                cancelled.explanation = "controlled test backend cancelled";
                return cancelled;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        bt::vla_response response;
        response.status = bt::vla_status::ok;
        response.model = request.model;
        response.confidence = 1.0;
        response.action.type = bt::vla_action_type::continuous;
        response.action.frame_id = request.action_space.frame_id;
        response.action.u.assign(static_cast<std::size_t>(request.action_space.dims), 0.25);
        return response;
    }

private:
    std::shared_ptr<std::atomic<bool>> release_;
};

class controlled_vla_commit_validator final : public bt::vla_commit_validator {
public:
    explicit controlled_vla_commit_validator(bt::vla_commit_validation result) : result_(std::move(result)) {}

    bt::vla_commit_validation validate(const bt::vla_commit_context& context,
                                       const bt::vla_action& action) override {
        ++calls;
        last_context = context;
        last_action = action;
        return result_;
    }

    std::size_t calls = 0;
    bt::vla_commit_context last_context;
    bt::vla_action last_action;

private:
    bt::vla_commit_validation result_;
};

class controlled_walking_target_dispatcher final : public bt::walking_target_dispatcher {
public:
    explicit controlled_walking_target_dispatcher(bt::walking_target_dispatch_result result)
        : result_(std::move(result)) {}

    bt::walking_target_dispatch_result dispatch(const bt::walking_target_dispatch_context& context,
                                                 const bt::walking_target& target) override {
        ++calls;
        last_context = context;
        last_target = target;
        return result_;
    }

    void set_result(bt::walking_target_dispatch_result result) { result_ = std::move(result); }

    std::size_t calls = 0;
    bt::walking_target_dispatch_context last_context;
    bt::walking_target last_target;

private:
    bt::walking_target_dispatch_result result_;
};

class manual_test_clock final : public bt::clock_interface {
public:
    explicit manual_test_clock(std::chrono::steady_clock::time_point now) : now_(now) {}

    std::chrono::steady_clock::time_point now() const override { return now_; }
    void advance(std::chrono::milliseconds delta) { now_ += delta; }

private:
    std::chrono::steady_clock::time_point now_;
};

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

void test_bt_dsl_roundtrip_representative_shapes() {
    using namespace muslisp;

    struct roundtrip_case {
        std::string name;
        std::string source;
        std::vector<std::string> expected_fragments;
    };

    const std::vector<roundtrip_case> cases = {
        {
            "sequence",
            "(seq (act bb-put-int foo 42) (cond bb-has foo))",
            {"seq", "act", "cond"},
        },
        {
            "selector",
            "(sel (cond bb-has missing) (act bb-put-int recovered 1) (succeed))",
            {"sel", "cond", "succeed"},
        },
        {
            "reactive",
            "(reactive-sel "
            "  (reactive-seq (cond always-true) (async-seq (act always-success) (running))) "
            "  (act always-success))",
            {"reactive-sel", "reactive-seq", "async-seq"},
        },
        {
            "planner",
            "(seq "
            "  (plan-action :name \"toy-plan\" :planner :mcts :budget_ms 20 :work_max 64 "
            "               :model_service \"toy-1d\" :state_key state :action_key action :meta_key plan-meta) "
            "  (succeed))",
            {"plan-action", ":planner", ":model_service"},
        },
        {
            "async-vla",
            "(reactive-sel "
            "  (seq (vla-wait :name \"policy\" :job_key policy-job :action_key policy-action :meta_key policy-meta) "
            "       (succeed)) "
            "  (seq (vla-request :name \"policy\" :job_key policy-job :instruction \"move\" "
            "                    :state_key state :deadline_ms 50 :dims 2) "
            "       (running)) "
            "  (vla-cancel :name \"policy\" :job_key policy-job))",
            {"vla-request", "vla-wait", "vla-cancel"},
        },
    };

    std::size_t pass_count = 0;
    for (const roundtrip_case& tc : cases) {
        reset_bt_runtime_host();
        env_ptr env = create_global_env();

        (void)eval_text("(define source-dsl (quote " + tc.source + "))", env);
        (void)eval_text("(define tree (bt.compile source-dsl))", env);
        const std::string canonical = string_value(eval_text("(write-to-string (bt.to-dsl tree))", env));
        check(!canonical.empty(), tc.name + ": canonical DSL should not be empty");
        for (const std::string& fragment : tc.expected_fragments) {
            check(canonical.find(fragment) != std::string::npos,
                  tc.name + ": canonical DSL missing fragment: " + fragment + " in " + canonical);
        }

        (void)eval_text("(define tree-from-canonical (bt.compile (bt.to-dsl tree)))", env);
        const std::string canonical_again =
            string_value(eval_text("(write-to-string (bt.to-dsl tree-from-canonical))", env));
        check(canonical_again == canonical,
              tc.name + ": source DSL -> parsed form -> compiled bt_def -> canonical DSL -> compiled bt_def changed");

        const std::filesystem::path dsl_path = temp_file_path("bt_dsl_roundtrip_" + tc.name, ".lisp");
        const std::string dsl_literal = lisp_string_literal(dsl_path.string());
        value save_ok = eval_text("(bt.save-dsl tree " + dsl_literal + ")", env);
        check(is_boolean(save_ok) && boolean_value(save_ok), tc.name + ": bt.save-dsl should return #t");

        (void)eval_text("(define tree-from-file (bt.load-dsl " + dsl_literal + "))", env);
        const std::string canonical_from_file =
            string_value(eval_text("(write-to-string (bt.to-dsl tree-from-file))", env));
        check(canonical_from_file == canonical,
              tc.name + ": bt.save-dsl/bt.load-dsl changed canonical DSL structure");

        std::error_code ec;
        std::filesystem::remove(dsl_path, ec);
        ++pass_count;
    }

    check(pass_count == cases.size(), "representative DSL roundtrip pass count mismatch");
}

void test_bt_dsl_hashes_are_logged_for_compiled_and_loaded_definitions() {
    using namespace muslisp;

    auto first_bt_def_event = [](value dumped) -> std::string {
        for (const value& row : vector_from_list(dumped)) {
            check(is_string(row), "events.dump rows should be JSON strings");
            const std::string line = string_value(row);
            if (line.find("\"type\":\"bt_def\"") != std::string::npos) {
                return line;
            }
        }
        return {};
    };

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    (void)eval_text("(define tree (bt.compile '(seq (act bb-put-int foo 42) (cond bb-has foo))))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    const std::string compiled_event = first_bt_def_event(eval_text("(events.dump 20)", env));
    check(!compiled_event.empty(), "compiled DSL should emit a bt_def event");
    check(compiled_event.find("\"source_hash\":\"fnv1a64:") != std::string::npos,
          "compiled DSL bt_def should include source_hash");
    check(compiled_event.find("\"canonical_dsl_hash\":\"fnv1a64:") != std::string::npos,
          "compiled DSL bt_def should include canonical_dsl_hash");
    check(compiled_event.find("\"tree_hash\":\"fnv1a64:") != std::string::npos,
          "compiled DSL bt_def should include tree_hash");
    check(compiled_event.find("\"dsl\":\"(seq (act bb-put-int foo 42) (cond bb-has foo))\"") != std::string::npos,
          "compiled DSL bt_def should include canonical DSL");

    reset_bt_runtime_host();
    env = create_global_env();
    const std::filesystem::path dsl_path = temp_file_path("bt_loaded_hash_identity", ".lisp");
    write_text_file(dsl_path, "(sel (cond bb-has ready) (succeed))");
    const std::string dsl_literal = lisp_string_literal(dsl_path.string());
    (void)eval_text("(define loaded-tree (bt.load-dsl " + dsl_literal + "))", env);
    (void)eval_text("(define loaded-inst (bt.new-instance loaded-tree))", env);
    const std::string loaded_event = first_bt_def_event(eval_text("(events.dump 20)", env));
    check(!loaded_event.empty(), "loaded DSL should emit a bt_def event");
    check(loaded_event.find("\"source_hash\":\"fnv1a64:") != std::string::npos,
          "loaded DSL bt_def should include source_hash");
    check(loaded_event.find("\"canonical_dsl_hash\":\"fnv1a64:") != std::string::npos,
          "loaded DSL bt_def should include canonical_dsl_hash");
    check(loaded_event.find("\"tree_hash\":\"fnv1a64:") != std::string::npos,
          "loaded DSL bt_def should include tree_hash");
    check(loaded_event.find("\"dsl\":\"(sel (cond bb-has ready) (succeed))\"") != std::string::npos,
          "loaded DSL bt_def should include canonical DSL");

    std::error_code ec;
    std::filesystem::remove(dsl_path, ec);
}

void test_bt_slot_dsl_roundtrip_and_tick() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    (void)eval_text(
        "(define tree "
        "  (bt.compile "
        "    '(slot recovery-policy "
        "       :contract guarded-recovery.v1 "
        "       :install at-tick-boundary "
        "       :fallback safe-stop "
        "       (seq (cond always-true) (act always-success)))))",
        env);
    const std::string canonical = string_value(eval_text("(write-to-string (bt.to-dsl tree))", env));
    check(canonical.find("(slot recovery-policy") != std::string::npos, "bt.to-dsl should preserve slot form");
    check(canonical.find(":contract guarded-recovery.v1") != std::string::npos, "bt.to-dsl should preserve slot contract");
    check(canonical.find(":install at-tick-boundary") != std::string::npos, "bt.to-dsl should preserve slot install mode");
    check(canonical.find(":fallback safe-stop") != std::string::npos, "bt.to-dsl should preserve slot fallback");

    (void)eval_text("(define tree2 (bt.compile (bt.to-dsl tree)))", env);
    const std::string canonical_again = string_value(eval_text("(write-to-string (bt.to-dsl tree2))", env));
    check(canonical_again == canonical, "slot DSL round-trip should be stable");

    (void)eval_text("(define inst (bt.new-instance tree2))", env);
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success", "slot should tick transparently through its child");
}

bt::arg_value bt_symbol_arg(std::string text) {
    bt::arg_value arg;
    arg.kind = bt::arg_kind::symbol;
    arg.text = std::move(text);
    return arg;
}

bt::arg_value bt_string_arg(std::string text) {
    bt::arg_value arg;
    arg.kind = bt::arg_kind::string;
    arg.text = std::move(text);
    return arg;
}

bt::arg_value bt_int_arg(std::int64_t value) {
    bt::arg_value arg;
    arg.kind = bt::arg_kind::integer;
    arg.int_v = value;
    return arg;
}

bt::node bt_composite_node(bt::node_id id, bt::node_kind kind, std::vector<bt::node_id> children) {
    bt::node n;
    n.kind = kind;
    n.id = id;
    n.children = std::move(children);
    return n;
}

bt::node bt_condition_node(bt::node_id id, std::string name) {
    bt::node n;
    n.kind = bt::node_kind::cond;
    n.id = id;
    n.leaf_name = std::move(name);
    return n;
}

bt::node bt_action_node(bt::node_id id, std::string name, std::vector<bt::arg_value> args = {}) {
    bt::node n;
    n.kind = bt::node_kind::act;
    n.id = id;
    n.leaf_name = std::move(name);
    n.args = std::move(args);
    return n;
}

bt::node bt_constant_node(bt::node_id id, bt::node_kind kind) {
    bt::node n;
    n.kind = kind;
    n.id = id;
    return n;
}

bt::node bt_plan_action_node(bt::node_id id, std::string name, std::string state_key, std::string action_key) {
    bt::node n;
    n.kind = bt::node_kind::plan_action;
    n.id = id;
    n.args = {
        bt_symbol_arg(":name"),
        bt_string_arg(std::move(name)),
        bt_symbol_arg(":planner"),
        bt_symbol_arg(":mcts"),
        bt_symbol_arg(":budget_ms"),
        bt_int_arg(20),
        bt_symbol_arg(":work_max"),
        bt_int_arg(64),
        bt_symbol_arg(":state_key"),
        bt_symbol_arg(std::move(state_key)),
        bt_symbol_arg(":action_key"),
        bt_symbol_arg(std::move(action_key)),
        bt_symbol_arg(":action_schema"),
        bt_string_arg("flagship.cmd.v1"),
    };
    return n;
}

bt::node bt_slot_node(bt::node_id id, std::string slot, std::vector<bt::node_id> children) {
    bt::node n;
    n.kind = bt::node_kind::slot;
    n.id = id;
    n.leaf_name = std::move(slot);
    n.args = {bt_symbol_arg(":contract"),
              bt_symbol_arg("guarded-recovery.v1"),
              bt_symbol_arg(":install"),
              bt_symbol_arg("at-tick-boundary"),
              bt_symbol_arg(":fallback"),
              bt_symbol_arg("safe-stop")};
    n.children = std::move(children);
    return n;
}

bt::definition make_slot_action_definition(std::string slot,
                                           std::string contract,
                                           std::string fallback,
                                           std::string action_name) {
    bt::definition def;
    bt::node slot_node;
    slot_node.kind = bt::node_kind::slot;
    slot_node.id = 0;
    slot_node.leaf_name = std::move(slot);
    slot_node.args = {bt_symbol_arg(":contract"),
                      bt_symbol_arg(std::move(contract)),
                      bt_symbol_arg(":install"),
                      bt_symbol_arg("at-tick-boundary"),
                      bt_symbol_arg(":fallback"),
                      bt_symbol_arg(std::move(fallback))};
    slot_node.children.push_back(1);
    def.nodes.push_back(std::move(slot_node));
    def.nodes.push_back(bt_action_node(1, std::move(action_name)));
    def.root = 0;
    return def;
}

bt::definition make_plain_action_definition(std::string action_name) {
    bt::definition def;
    def.nodes.push_back(bt_action_node(0, std::move(action_name)));
    def.root = 0;
    return def;
}

bt::subtree_install_request make_test_subtree_install_request(std::string proposal_id,
                                                              std::string slot,
                                                              std::string contract,
                                                              std::string action_name) {
    bt::subtree_install_request request;
    request.proposal_id = std::move(proposal_id);
    request.source = "unit-test";
    request.slot = std::move(slot);
    request.fragment_contract = std::move(contract);
    request.install_mode = "at-tick-boundary";
    request.validation_status = "accepted";
    request.source_hash = "fnv1a64:test-source";
    request.canonical_dsl_hash = "fnv1a64:test-canonical-" + action_name;
    request.validation_result_hash = "fnv1a64:test-validation";
    request.fragment =
        make_slot_action_definition(request.slot, request.fragment_contract, "safe-stop", std::move(action_name));
    return request;
}

bool event_lines_contain(const bt::event_log& events, std::string_view type) {
    for (const std::string& line : events.snapshot()) {
        if (line.find("\"type\":\"" + std::string(type) + "\"") != std::string::npos) {
            return true;
        }
    }
    return false;
}

void test_bt_live_subtree_install_and_rollback() {
    bt::definition base =
        make_slot_action_definition("recovery-policy", "guarded-recovery.v1", "safe-stop", "old-recovery");
    bt::instance inst(&base);
    bt::registry reg;
    int old_calls = 0;
    int new_calls = 0;
    reg.register_action("old-recovery",
                        [&](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                            ++old_calls;
                            return bt::status::success;
                        });
    reg.register_action("new-recovery",
                        [&](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                            ++new_calls;
                            return bt::status::success;
                        });

    bt::event_log events;
    events.set_deterministic_time(1000);
    bt::services svc;
    svc.obs.events = &events;

    check(bt::tick(inst, reg, svc) == bt::status::success, "baseline slot child should tick");
    check(old_calls == 1 && new_calls == 0, "baseline should tick old recovery only");

    bt::subtree_install_result install =
        bt::request_subtree_install(inst, svc, make_test_subtree_install_request("proposal-1",
                                                                                 "recovery-policy",
                                                                                 "guarded-recovery.v1",
                                                                                 "new-recovery"));
    check(install.queued, "accepted subtree install should queue");
    check(inst.pending_subtree_install.has_value(), "install should be pending until the next tick boundary");
    check(old_calls == 1 && new_calls == 0, "queued install should not tick proposed subtree immediately");

    check(bt::tick(inst, reg, svc) == bt::status::success, "installed subtree should tick after boundary");
    check(old_calls == 1 && new_calls == 1, "installed subtree should replace old slot child");
    check(!inst.pending_subtree_install.has_value(), "install should be consumed at tick boundary");
    check(event_lines_contain(events, "subtree_install_requested"), "install request event should be emitted");
    check(event_lines_contain(events, "subtree_installed"), "install commit event should be emitted");

    const auto rollback_it = inst.subtree_rollbacks.find("recovery-policy");
    check(rollback_it != inst.subtree_rollbacks.end(), "install should keep rollback state for the slot");
    bt::subtree_rollback_request rollback;
    rollback.rollback_id = rollback_it->second.rollback_id;
    rollback.slot = "recovery-policy";
    rollback.installed_subtree_hash = rollback_it->second.installed_subtree_hash;
    rollback.previous_subtree_hash = rollback_it->second.previous_subtree_hash;
    bt::subtree_install_result rollback_result = bt::request_subtree_rollback(inst, svc, rollback);
    check(rollback_result.queued, "valid rollback should queue");

    check(bt::tick(inst, reg, svc) == bt::status::success, "rolled back subtree should tick after boundary");
    check(old_calls == 2 && new_calls == 1, "rollback should restore old slot child");
    check(inst.subtree_rollbacks.find("recovery-policy") == inst.subtree_rollbacks.end(),
          "completed rollback should consume rollback state");
    check(event_lines_contain(events, "subtree_rollback_requested"), "rollback request event should be emitted");
    check(event_lines_contain(events, "subtree_rolled_back"), "rollback commit event should be emitted");
}

void test_bt_live_subtree_install_rejections_are_non_destructive() {
    bt::definition base =
        make_slot_action_definition("recovery-policy", "guarded-recovery.v1", "safe-stop", "old-recovery");
    bt::instance inst(&base);
    bt::registry reg;
    int old_calls = 0;
    int rejected_calls = 0;
    int accepted_calls = 0;
    reg.register_action("old-recovery",
                        [&](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                            ++old_calls;
                            return bt::status::success;
                        });
    reg.register_action("rejected-recovery",
                        [&](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                            ++rejected_calls;
                            return bt::status::success;
                        });
    reg.register_action("accepted-recovery",
                        [&](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                            ++accepted_calls;
                            return bt::status::success;
                        });

    bt::event_log events;
    events.set_deterministic_time(2000);
    bt::services svc;
    svc.obs.events = &events;

    bt::subtree_install_request unknown =
        make_test_subtree_install_request("proposal-missing-slot", "unknown-slot", "guarded-recovery.v1", "rejected-recovery");
    check(!bt::request_subtree_install(inst, svc, std::move(unknown)).queued, "unknown slot should be rejected");

    bt::subtree_install_request mismatch =
        make_test_subtree_install_request("proposal-contract", "recovery-policy", "other-contract.v1", "rejected-recovery");
    check(!bt::request_subtree_install(inst, svc, std::move(mismatch)).queued, "contract mismatch should be rejected");

    bt::subtree_install_request rejected =
        make_test_subtree_install_request("proposal-rejected", "recovery-policy", "guarded-recovery.v1", "rejected-recovery");
    rejected.validation_status = "rejected";
    check(!bt::request_subtree_install(inst, svc, std::move(rejected)).queued,
          "rejected validation status should be rejected");

    bt::subtree_install_request non_slot =
        make_test_subtree_install_request("proposal-non-slot", "recovery-policy", "guarded-recovery.v1", "rejected-recovery");
    non_slot.fragment = make_plain_action_definition("rejected-recovery");
    check(!bt::request_subtree_install(inst, svc, std::move(non_slot)).queued, "non-slot fragment root should be rejected");

    check(bt::tick(inst, reg, svc) == bt::status::success, "old subtree should remain active after rejected installs");
    check(old_calls == 1, "old subtree should tick after rejected installs");
    check(rejected_calls == 0, "rejected proposal subtree must not reach host execution");

    bt::subtree_install_result queued =
        bt::request_subtree_install(inst, svc, make_test_subtree_install_request("proposal-accepted",
                                                                                 "recovery-policy",
                                                                                 "guarded-recovery.v1",
                                                                                 "accepted-recovery"));
    check(queued.queued, "first accepted proposal should queue");
    bt::subtree_install_result duplicate =
        bt::request_subtree_install(inst, svc, make_test_subtree_install_request("proposal-duplicate",
                                                                                 "recovery-policy",
                                                                                 "guarded-recovery.v1",
                                                                                 "rejected-recovery"));
    check(!duplicate.queued && duplicate.reason_code == "pending_request_exists",
          "duplicate pending install should be rejected");
    check(bt::tick(inst, reg, svc) == bt::status::success, "queued install should still commit after duplicate rejection");
    check(accepted_calls == 1 && rejected_calls == 0, "duplicate rejected proposal must not tick");
    check(event_lines_contain(events, "subtree_install_rejected"), "install rejection event should be emitted");
}

void test_bt_live_subtree_install_cleans_replaced_running_subtree() {
    bt::definition base =
        make_slot_action_definition("recovery-policy", "guarded-recovery.v1", "safe-stop", "running-recovery");
    bt::instance inst(&base);
    bt::registry reg;
    int running_calls = 0;
    int replacement_calls = 0;
    int halt_calls = 0;
    reg.register_action(
        "running-recovery",
        [&](bt::tick_context&, bt::node_id, bt::node_memory& mem, std::span<const muslisp::value>) {
            ++running_calls;
            mem.b0 = true;
            return bt::status::running;
        },
        [&](bt::tick_context&, bt::node_id, bt::node_memory&) { ++halt_calls; });
    reg.register_action("replacement-recovery",
                        [&](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                            ++replacement_calls;
                            return bt::status::success;
                        });

    bt::event_log events;
    events.set_deterministic_time(3000);
    bt::services svc;
    svc.obs.events = &events;

    check(bt::tick(inst, reg, svc) == bt::status::running, "old running subtree should start");
    check(running_calls == 1, "running subtree should tick once");
    inst.active_vla_jobs[1] = 42;
    inst.halt_warning_emitted.insert(1);
    check(inst.memory.find(1) != inst.memory.end(), "running node should have memory before replacement");
    check(inst.node_stats.find(1) != inst.node_stats.end(), "running node should have profile state before replacement");

    bt::subtree_install_result install =
        bt::request_subtree_install(inst, svc, make_test_subtree_install_request("proposal-cleanup",
                                                                                 "recovery-policy",
                                                                                 "guarded-recovery.v1",
                                                                                 "replacement-recovery"));
    check(install.queued, "replacement install should queue");
    check(bt::tick(inst, reg, svc) == bt::status::success, "replacement subtree should tick after install");
    check(halt_calls == 1, "replacing a running subtree should call halt");
    check(running_calls == 1 && replacement_calls == 1, "replacement should tick without re-ticking old subtree");
    check(inst.memory.find(1) == inst.memory.end(), "old subtree memory should be erased");
    check(inst.active_vla_jobs.find(1) == inst.active_vla_jobs.end(), "old subtree active VLA job should be erased");
    check(inst.halt_warning_emitted.find(1) == inst.halt_warning_emitted.end(),
          "old subtree halt warning state should be erased");
    check(inst.node_stats.find(1) == inst.node_stats.end(), "old subtree profile state should be erased");
}

void test_shared_flagship_generated_recovery_variant_compiles_and_preserves_fixed_recovery() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    const std::filesystem::path repo = find_repo_root();
    const std::string variant_path =
        lisp_string_literal((repo / "examples" / "flagship_wheeled" / "lisp" /
                             "bt_goal_flagship_generated_recovery.lisp")
                                .string());
    (void)eval_text("(load " + variant_path + ")", env);

    const std::string canonical =
        string_value(eval_text("(write-to-string (bt.to-dsl wheeled-goal-flagship-generated-recovery))", env));
    check(canonical.find("(slot recovery-policy") != std::string::npos,
          "experimental flagship variant should expose recovery-policy slot");
    check(canonical.find(":contract guarded-recovery.v1") != std::string::npos,
          "experimental flagship variant should preserve guarded recovery contract");
    check(canonical.find("(act select-action act_avoid 1 action_cmd)") != std::string::npos,
          "default slot child should preserve the fixed collision recovery action");

    (void)eval_text("(define flagship-recovery-inst (bt.new-instance wheeled-goal-flagship-generated-recovery))", env);
    const value status = eval_text(
        "(bt.tick flagship-recovery-inst "
        "  '((goal_reached #f) "
        "    (collision_imminent #t) "
        "    (act_avoid (0.10 -0.35)) "
        "    (act_goal_direct (0.45 0.0)) "
        "    (planner_state (1.0 0.0 0.9 0.0))))",
        env);
    check(symbol_name(status) == "running", "fixed recovery slot branch should keep the flagship running");

    bt::runtime_host& host = bt::default_runtime_host();
    bt::instance* inst = host.find_instance(bt_handle(eval_text("flagship-recovery-inst", env)));
    check(inst != nullptr, "experimental flagship instance should exist");

    const bt::bb_entry* branch = inst->bb.get("active_branch");
    check(branch && std::get<std::int64_t>(branch->value) == 1, "fixed recovery branch should preserve active_branch=1");

    const bt::bb_entry* action = inst->bb.get("action_cmd");
    check(action != nullptr, "fixed recovery branch should write action_cmd");
    const auto* action_vec = std::get_if<std::vector<double>>(&action->value);
    check(action_vec && action_vec->size() == 2u, "action_cmd should be a two-value flagship command");
    check_close((*action_vec)[0], 0.10, 1e-9, "fixed recovery should copy avoid linear command");
    check_close((*action_vec)[1], -0.35, 1e-9, "fixed recovery should copy avoid angular command");
}

void test_shared_flagship_navigation_capability_variant_uses_cap_navigation() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    const std::filesystem::path repo = find_repo_root();
    const std::string variant_path =
        lisp_string_literal((repo / "examples" / "flagship_wheeled" / "lisp" /
                             "bt_goal_flagship_nav_capability.lisp")
                                .string());
    (void)eval_text("(load " + variant_path + ")", env);

    const std::string canonical =
        string_value(eval_text("(write-to-string (bt.to-dsl wheeled-goal-flagship-nav-capability))", env));
    check(canonical.find("(act cap-navigation-tick)") != std::string::npos,
          "navigation capability variant should expose cap-navigation-tick action");
    check(canonical.find("(act cap-navigation-cancel)") != std::string::npos,
          "navigation capability variant should cancel active jobs before fixed recovery");
    check(canonical.find("(act select-action act_avoid 1 action_cmd)") != std::string::npos,
          "navigation capability variant should preserve fixed collision recovery action");

    (void)eval_text("(define nav-inst (bt.new-instance wheeled-goal-flagship-nav-capability))", env);
    value first_status = eval_text(
        "(bt.tick nav-inst "
        " '((goal_reached #f) "
        "   (collision_imminent #f) "
        "   (nav_goal_frame \"map\") "
        "   (nav_goal_x 1.0) "
        "   (nav_goal_y 2.0) "
        "   (nav_goal_yaw 0.25) "
        "   (nav_timeout_ms 1000) "
        "   (nav_mock_status \"accepted\")))",
        env);
    check(symbol_name(first_status) == "running", "accepted navigation submit should keep the variant running");

    bt::runtime_host& host = bt::default_runtime_host();
    bt::instance* inst = host.find_instance(bt_handle(eval_text("nav-inst", env)));
    check(inst != nullptr, "navigation capability flagship instance should exist");
    const bt::bb_entry* branch = inst->bb.get("active_branch");
    check(branch && std::get<std::int64_t>(branch->value) == 2, "navigation branch should set active_branch=2");
    const bt::bb_entry* nav_status = inst->bb.get("nav_status");
    check(nav_status && std::get<std::string>(nav_status->value) == "accepted",
          "accepted navigation submit should store nav_status");
    const bt::bb_entry* job_id = inst->bb.get("nav_job_id");
    check(job_id && !std::get<std::string>(job_id->value).empty(), "accepted navigation submit should store job id");
    check(inst->bb.get("nav_request_hash") != nullptr, "navigation submit should store request hash");
    check(inst->bb.get("nav_response_hash") != nullptr, "navigation submit should store response hash");
    const bt::bb_entry* host_reached = inst->bb.get("nav_host_reached");
    check(host_reached && std::get<bool>(host_reached->value), "accepted navigation submit should reach mock host");

    value success_status = eval_text(
        "(bt.tick nav-inst "
        " '((goal_reached #f) "
        "   (collision_imminent #f) "
        "   (nav_goal_frame \"map\") "
        "   (nav_goal_x 1.0) "
        "   (nav_goal_y 2.0) "
        "   (nav_mock_status \"ok\")))",
        env);
    check(symbol_name(success_status) == "success", "ok navigation status should complete the navigation branch");
    nav_status = inst->bb.get("nav_status");
    check(nav_status && std::get<std::string>(nav_status->value) == "ok", "ok navigation status should be stored");
    const bt::bb_entry* distance = inst->bb.get("nav_distance_remaining_m");
    check(distance && std::get<double>(distance->value) == 0.0, "ok navigation status should store zero distance");

    const auto tick_rejected_navigation = [&](const std::string& instance_name,
                                             const std::string& mock_status,
                                             const std::string& expected_status) {
        (void)eval_text("(define " + instance_name + " (bt.new-instance wheeled-goal-flagship-nav-capability))", env);
        value result = eval_text(
            "(bt.tick " + instance_name +
                " '((goal_reached #f) "
                "   (collision_imminent #f) "
                "   (nav_goal_x 1.0) "
                "   (nav_goal_y 2.0) "
                "   (nav_mock_status \"" +
                mock_status + "\")))",
            env);
        check(symbol_name(result) == "failure", expected_status + " navigation status should fail the branch");
        bt::instance* rejected_inst = host.find_instance(bt_handle(eval_text(instance_name, env)));
        check(rejected_inst != nullptr, "navigation rejection instance should exist");
        const bt::bb_entry* rejected_status = rejected_inst->bb.get("nav_status");
        check(rejected_status && std::get<std::string>(rejected_status->value) == expected_status,
              expected_status + " navigation status should be stored");
    };

    tick_rejected_navigation("nav-rejected-inst", "rejected", "rejected");
    tick_rejected_navigation("nav-timeout-inst", "timeout", "timeout");
    tick_rejected_navigation("nav-unavailable-inst", "unavailable", "unavailable");

    (void)eval_text("(define nav-cancel-inst (bt.new-instance wheeled-goal-flagship-nav-capability))", env);
    (void)eval_text(
        "(bt.tick nav-cancel-inst "
        " '((goal_reached #f) "
        "   (collision_imminent #f) "
        "   (nav_goal_x 1.0) "
        "   (nav_goal_y 2.0) "
        "   (nav_mock_status \"accepted\")))",
        env);
    value cancel_status = eval_text(
        "(bt.tick nav-cancel-inst "
        " '((goal_reached #f) "
        "   (collision_imminent #t) "
        "   (act_avoid (0.10 -0.35))))",
        env);
    check(symbol_name(cancel_status) == "running", "collision recovery should keep the navigation variant running");
    bt::instance* cancel_inst = host.find_instance(bt_handle(eval_text("nav-cancel-inst", env)));
    check(cancel_inst != nullptr, "navigation cancel instance should exist");
    nav_status = cancel_inst->bb.get("nav_status");
    check(nav_status && std::get<std::string>(nav_status->value) == "cancelled",
          "collision recovery should cancel the active navigation job");
    job_id = cancel_inst->bb.get("nav_job_id");
    check(job_id && std::get<std::string>(job_id->value).empty(),
          "cancelled navigation job should clear nav_job_id");
    branch = cancel_inst->bb.get("active_branch");
    check(branch && std::get<std::int64_t>(branch->value) == 1, "collision recovery should preserve active_branch=1");
    const bt::bb_entry* action = cancel_inst->bb.get("action_cmd");
    check(action != nullptr, "collision recovery should write action_cmd");
    const auto* action_vec = std::get_if<std::vector<double>>(&action->value);
    check(action_vec && action_vec->size() == 2u, "collision recovery action_cmd should be a two-value command");
    check_close((*action_vec)[0], 0.10, 1e-9, "collision recovery should copy avoid linear command");
    check_close((*action_vec)[1], -0.35, 1e-9, "collision recovery should copy avoid angular command");
}

bt::definition make_flagship_recovery_slot_definition() {
    bt::definition def;
    def.nodes.push_back(bt_composite_node(0, bt::node_kind::sel, {1, 3, 8}));
    def.nodes.push_back(bt_composite_node(1, bt::node_kind::seq, {2, 12}));
    def.nodes.push_back(bt_condition_node(2, "goal-reached?"));
    def.nodes.push_back(bt_slot_node(3, "recovery-policy", {4}));
    def.nodes.push_back(bt_composite_node(4, bt::node_kind::seq, {5, 6, 7}));
    def.nodes.push_back(bt_condition_node(5, "collision-imminent?"));
    def.nodes.push_back(bt_action_node(6, "fixed-recovery"));
    def.nodes.push_back(bt_constant_node(7, bt::node_kind::running));
    def.nodes.push_back(bt_composite_node(8, bt::node_kind::seq, {9, 10}));
    def.nodes.push_back(bt_action_node(9, "direct-goal"));
    def.nodes.push_back(bt_constant_node(10, bt::node_kind::running));
    def.nodes.push_back(bt_constant_node(11, bt::node_kind::fail));
    def.nodes.push_back(bt_constant_node(12, bt::node_kind::succeed));
    def.root = 0;
    return def;
}

bt::definition make_flagship_generated_recovery_fragment() {
    bt::definition def;
    def.nodes.push_back(bt_slot_node(0, "recovery-policy", {1}));
    def.nodes.push_back(bt_composite_node(1, bt::node_kind::reactive_sel, {2, 8}));
    def.nodes.push_back(bt_composite_node(2, bt::node_kind::seq, {3, 4, 5, 6, 7}));
    def.nodes.push_back(bt_condition_node(3, "blocked-path?"));
    def.nodes.push_back(bt_condition_node(4, "observation-fresh?"));
    def.nodes.push_back(bt_plan_action_node(5, "flagship-recovery-turn", "recovery-state", "recovery-action"));
    def.nodes.push_back(bt_action_node(6, "execute-recovery-turn"));
    def.nodes.push_back(bt_condition_node(7, "recovery-exit?"));
    def.nodes.push_back(bt_action_node(8, "safe-stop"));
    def.root = 0;
    return def;
}

bt::subtree_install_request make_flagship_generated_recovery_request(std::string proposal_id,
                                                                     std::string validation_status = "accepted") {
    bt::subtree_install_request request;
    request.proposal_id = std::move(proposal_id);
    request.source = "flagship-deterministic-fixture";
    request.slot = "recovery-policy";
    request.fragment_contract = "guarded-recovery.v1";
    request.install_mode = "at-tick-boundary";
    request.validation_status = std::move(validation_status);
    request.source_hash = "fnv1a64:flagship-source";
    request.canonical_dsl_hash = "fnv1a64:flagship-canonical";
    request.validation_result_hash = "fnv1a64:flagship-validation";
    request.fragment = make_flagship_generated_recovery_fragment();
    return request;
}

void seed_flagship_recovery_blackboard(bt::instance& inst) {
    const auto now = std::chrono::steady_clock::now();
    inst.bb.put("collision_imminent", bt::bb_value{true}, inst.tick_index, now, 0, "test");
    inst.bb.put("blocked_path", bt::bb_value{true}, inst.tick_index, now, 0, "test");
    inst.bb.put("observation_fresh", bt::bb_value{true}, inst.tick_index, now, 0, "test");
    inst.bb.put("recovery-state", bt::bb_value{std::vector<double>{1.0, 0.0, 0.9, 0.0}}, inst.tick_index, now, 0, "test");
    inst.bb.put("act_avoid", bt::bb_value{std::vector<double>{0.10, -0.35}}, inst.tick_index, now, 0, "test");
}

void test_flagship_generated_recovery_live_install_reject_and_rollback() {
    bt::definition base = make_flagship_recovery_slot_definition();
    bt::instance inst(&base);
    bt::registry reg;
    bt::planner_service planner;
    bt::event_log events;
    events.set_deterministic_time(4000);
    bt::services svc;
    svc.planner = &planner;
    svc.obs.events = &events;

    int fixed_calls = 0;
    int generated_calls = 0;
    int safe_stop_calls = 0;
    int direct_calls = 0;

    reg.register_condition("goal-reached?", [](bt::tick_context&, std::span<const muslisp::value>) { return false; });
    reg.register_condition("collision-imminent?", [](bt::tick_context& ctx, std::span<const muslisp::value>) {
        const bt::bb_entry* entry = ctx.bb_get("collision_imminent");
        return entry && std::get<bool>(entry->value);
    });
    reg.register_condition("blocked-path?", [](bt::tick_context& ctx, std::span<const muslisp::value>) {
        const bt::bb_entry* entry = ctx.bb_get("blocked_path");
        return entry && std::get<bool>(entry->value);
    });
    reg.register_condition("observation-fresh?", [](bt::tick_context& ctx, std::span<const muslisp::value>) {
        const bt::bb_entry* entry = ctx.bb_get("observation_fresh");
        return entry && std::get<bool>(entry->value);
    });
    reg.register_condition("recovery-exit?", [](bt::tick_context&, std::span<const muslisp::value>) { return true; });
    reg.register_action("fixed-recovery",
                        [&](bt::tick_context& ctx, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                            ++fixed_calls;
                            ctx.bb_put("action_cmd", bt::bb_value{std::vector<double>{0.10, -0.35}}, "fixed-recovery");
                            ctx.bb_put("active_branch", bt::bb_value{std::int64_t{1}}, "fixed-recovery");
                            return bt::status::success;
                        });
    reg.register_action("execute-recovery-turn",
                        [&](bt::tick_context& ctx, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                            ++generated_calls;
                            ctx.bb_put("action_cmd", bt::bb_value{std::vector<double>{0.20, -0.40}},
                                       "execute-recovery-turn");
                            ctx.bb_put("active_branch", bt::bb_value{std::int64_t{1}}, "execute-recovery-turn");
                            return bt::status::success;
                        });
    reg.register_action("safe-stop",
                        [&](bt::tick_context& ctx, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                            ++safe_stop_calls;
                            ctx.bb_put("action_cmd", bt::bb_value{std::vector<double>{0.0, 0.0}}, "safe-stop");
                            return bt::status::success;
                        });
    reg.register_action("direct-goal",
                        [&](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                            ++direct_calls;
                            return bt::status::success;
                        });

    seed_flagship_recovery_blackboard(inst);
    check(bt::tick(inst, reg, svc) == bt::status::running, "fixed flagship recovery should return running");
    check(fixed_calls == 1 && generated_calls == 0 && direct_calls == 0,
          "baseline tick should use only the fixed recovery branch");

    bt::subtree_install_result rejected =
        bt::request_subtree_install(inst, svc, make_flagship_generated_recovery_request("proposal-flagship-rejected", "rejected"));
    check(!rejected.queued && rejected.reason_code == "validation_not_accepted",
          "rejected flagship proposal should not queue");
    seed_flagship_recovery_blackboard(inst);
    check(bt::tick(inst, reg, svc) == bt::status::running, "rejected proposal should leave fixed recovery active");
    check(fixed_calls == 2 && generated_calls == 0 && safe_stop_calls == 0,
          "rejected proposal must not reach generated recovery callbacks");

    bt::subtree_install_result queued =
        bt::request_subtree_install(inst, svc, make_flagship_generated_recovery_request("proposal-flagship-accepted"));
    check(queued.queued, "accepted flagship generated recovery proposal should queue");
    check(generated_calls == 0, "queued proposal should not tick before the next boundary");

    seed_flagship_recovery_blackboard(inst);
    check(bt::tick(inst, reg, svc) == bt::status::success,
          "installed generated recovery should tick at the next boundary");
    check(fixed_calls == 2 && generated_calls == 1 && safe_stop_calls == 0,
          "installed generated recovery should replace the fixed recovery branch");
    check(event_lines_contain(events, "subtree_install_rejected"), "rejected install event should be emitted");
    check(event_lines_contain(events, "subtree_installed"), "accepted install event should be emitted");

    const bt::bb_entry* generated_action = inst.bb.get("action_cmd");
    check(generated_action != nullptr, "generated recovery should write action_cmd");
    const auto* generated_vec = std::get_if<std::vector<double>>(&generated_action->value);
    check(generated_vec && generated_vec->size() == 2u, "generated action_cmd should be a two-value command");
    check_close((*generated_vec)[0], 0.20, 1e-9, "generated recovery should write deterministic linear command");
    check_close((*generated_vec)[1], -0.40, 1e-9, "generated recovery should write deterministic angular command");

    const auto rollback_it = inst.subtree_rollbacks.find("recovery-policy");
    check(rollback_it != inst.subtree_rollbacks.end(), "generated install should keep rollback state");
    bt::subtree_rollback_request rollback;
    rollback.rollback_id = rollback_it->second.rollback_id;
    rollback.slot = "recovery-policy";
    rollback.installed_subtree_hash = rollback_it->second.installed_subtree_hash;
    rollback.previous_subtree_hash = rollback_it->second.previous_subtree_hash;
    check(bt::request_subtree_rollback(inst, svc, rollback).queued, "flagship generated recovery rollback should queue");

    seed_flagship_recovery_blackboard(inst);
    check(bt::tick(inst, reg, svc) == bt::status::running, "rollback should restore fixed recovery behaviour");
    check(fixed_calls == 3 && generated_calls == 1, "rolled-back tree should tick fixed recovery again");
    check(event_lines_contain(events, "subtree_rolled_back"), "rollback event should be emitted");
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
    default_gc().set_policy(gc_policy::default_policy);

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

    check(symbol_name(eval_text("(gc.policy)", env)) == ":default", "gc.policy default mismatch");
    check(symbol_name(eval_text("(gc.set-policy! \"between-ticks\")", env)) == ":between-ticks",
          "gc.set-policy! between-ticks mismatch");
    check(default_gc().policy() == gc_policy::between_ticks, "C++ GC policy should be between-ticks");
    check(symbol_name(eval_text("(gc.set-policy! \"manual\")", env)) == ":manual", "gc.set-policy! manual mismatch");
    check(default_gc().policy() == gc_policy::manual, "C++ GC policy should be manual");
    check(symbol_name(eval_text("(gc.set-policy! \"fail-on-tick-gc\")", env)) == ":fail-on-tick-gc",
          "gc.set-policy! fail-on-tick-gc mismatch");
    check(default_gc().policy() == gc_policy::fail_on_tick_gc, "C++ GC policy should be fail-on-tick-gc");
    expect_lisp_error_message("(gc.set-policy! \"sometimes\")",
                              env,
                              "gc.set-policy!: expected :default, :between-ticks, :manual, or :fail-on-tick-gc",
                              "gc.set-policy! invalid policy");
    (void)eval_text("(gc.set-policy! \"default\")", env);
    check(default_gc().policy() == gc_policy::default_policy, "GC policy should reset to default");
}

void test_gc_lifecycle_events() {
    using namespace muslisp;

    bt::runtime_host& host = bt::default_runtime_host();
    host.events().set_enabled(true);
    host.events().set_ring_capacity(64);
    host.events().clear_ring();
    default_gc().set_policy(gc_policy::default_policy);

    default_gc().collect();

    const std::vector<std::string> lines = host.events().snapshot();
    bool saw_begin = false;
    bool saw_end = false;
    bool saw_schema = false;
    bool saw_forced = false;
    for (const std::string& line : lines) {
        saw_begin = saw_begin || line.find("\"type\":\"gc_begin\"") != std::string::npos;
        saw_end = saw_end || line.find("\"type\":\"gc_end\"") != std::string::npos;
        saw_schema = saw_schema || line.find("\"schema_version\":\"gc.lifecycle.v1\"") != std::string::npos;
        saw_forced = saw_forced || line.find("\"reason\":\"forced\"") != std::string::npos;
    }
    check(saw_begin, "GC lifecycle should emit gc_begin");
    check(saw_end, "GC lifecycle should emit gc_end");
    check(saw_schema, "GC lifecycle should emit gc.lifecycle.v1 payload");
    check(saw_forced, "GC lifecycle should record forced collection reason");
}

void test_gc_during_argument_evaluation() {
    using namespace muslisp;

    env_ptr env = create_global_env();
    default_gc().set_policy(gc_policy::default_policy);

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

    value atan2_value = eval_text("(atan2 1 1)", env);
    check(is_float(atan2_value), "atan2 should return float");
    check_close(float_value(atan2_value), 0.7853981633974483, 1e-12, "atan2(1, 1) mismatch");

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
    const auto sleep_start = std::chrono::steady_clock::now();
    value sleep_result = eval_text("(time.sleep-ms 2)", env);
    const auto sleep_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - sleep_start);
    check(is_nil(sleep_result), "time.sleep-ms should return nil");
    check(sleep_elapsed.count() >= 1, "time.sleep-ms should block for a measurable interval");

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
    try {
        (void)eval_text("(time.sleep-ms -1)", env);
        throw std::runtime_error("expected time.sleep-ms domain error");
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

void test_capability_registry_call_echo() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    class test_navigation_backend final : public cap_backend {
    public:
        [[nodiscard]] bt::capability_descriptor describe() const override {
            bt::capability_descriptor cap;
            cap.name = "cap.navigation.v1";
            cap.safety_class = "test";
            cap.cost_category = "low";
            cap.adapter_id = "test-nav";
            cap.operations = {"navigate-to-pose"};
            cap.supports_cancellation = true;
            cap.supports_replay = true;
            cap.request_schema = {
                {"schema_version", "string", true},
                {"capability", "string", true},
                {"operation", "string", true},
            };
            cap.response_schema = {
                {"schema_version", "string", true},
                {"capability", "string", true},
                {"operation", "string", true},
                {"status", "keyword", true},
                {"adapter", "string", true},
                {"host_reached", "boolean", true},
            };
            return cap;
        }

        [[nodiscard]] value call(value request_map) override {
            value out = make_map();
            gc_root_scope roots(default_gc());
            roots.add(&out);
            map_set(out, "schema_version", make_string("cap.navigation.result.v1"));
            map_set(out, "capability", make_string("cap.navigation.v1"));
            map_set(out, "operation", lookup(request_map, "operation"));
            map_set(out, "status", make_symbol(":accepted"));
            map_set(out, "adapter", make_string("test-nav"));
            map_set(out, "host_reached", make_boolean(true));
            return out;
        }

    private:
        static value lookup(value map_obj, const std::string& key_name) {
            map_key key;
            key.type = map_key_type::symbol;
            key.text_data = key_name;
            return map_obj->map_data[key];
        }

        static void map_set(value map_obj, const std::string& key_name, value v) {
            map_key key;
            key.type = map_key_type::symbol;
            key.text_data = key_name;
            map_obj->map_data[key] = v;
        }
    };

    cap_api_register_backend("cap.navigation.v1", std::make_shared<test_navigation_backend>());
    check(string_value(eval_text("(map.get (cap.describe \"cap.navigation.v1\") 'adapter_id \"\")", env)) == "test-nav",
          "registered cap.navigation.v1 descriptor should override built-in mock descriptor");
    value registered_nav = eval_text(
        "(define registered_nav "
        " (begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.navigation.request.v1\") "
        "  (map.set! req 'capability \"cap.navigation.v1\") "
        "  (map.set! req 'operation \"navigate-to-pose\") "
        "  (cap.call req)))",
        env);
    check(is_map(registered_nav), "registered capability should return map");
    check(string_value(eval_text("(map.get registered_nav 'adapter \"\")", env)) == "test-nav",
          "cap.call should dispatch to registered capability before built-in mock");
    check(!string_value(eval_text("(map.get registered_nav 'request_hash \"\")", env)).empty(),
          "registered capability wrapper should add request_hash");
    check(!string_value(eval_text("(map.get registered_nav 'response_hash \"\")", env)).empty(),
          "registered capability wrapper should add response_hash");

    env = create_global_env();

    value caps = eval_text("(cap.list)", env);
    check(is_proper_list(caps), "cap.list should return list");
    bool saw_echo = false;
    bool saw_navigation = false;
    bool saw_motion = false;
    bool saw_tamp = false;
    for (value cap_name : vector_from_list(caps)) {
        if (is_string(cap_name) && string_value(cap_name) == "cap.echo.v1") {
            saw_echo = true;
        } else if (is_string(cap_name) && string_value(cap_name) == "cap.navigation.v1") {
            saw_navigation = true;
        } else if (is_string(cap_name) && string_value(cap_name) == "cap.motion.v1") {
            saw_motion = true;
        } else if (is_string(cap_name) && string_value(cap_name) == "cap.tamp.v1") {
            saw_tamp = true;
        }
    }
    check(saw_echo, "cap.list should include cap.echo.v1");
    check(saw_navigation, "cap.list should include cap.navigation.v1");
    check(saw_motion, "cap.list should include cap.motion.v1");
    check(saw_tamp, "cap.list should include cap.tamp.v1");

    value desc = eval_text("(cap.describe \"cap.echo.v1\")", env);
    check(is_map(desc), "cap.describe cap.echo.v1 should return map");
    check(string_value(eval_text("(map.get (cap.describe \"cap.echo.v1\") 'name \"\")", env)) == "cap.echo.v1",
          "cap.describe echo name mismatch");
    check(string_value(eval_text("(map.get (cap.describe \"cap.echo.v1\") 'safety_class \"\")", env)) == "safe",
          "cap.describe echo safety_class mismatch");
    check(string_value(eval_text("(map.get (cap.describe \"cap.navigation.v1\") 'safety_class \"\")", env)) ==
              "mock_adapter",
          "cap.describe navigation safety_class mismatch");
    check(string_value(eval_text("(map.get (cap.describe \"cap.motion.v1\") 'name \"\")", env)) == "cap.motion.v1",
          "cap.describe motion name mismatch");
    check(string_value(eval_text("(map.get (cap.describe \"cap.tamp.v1\") 'cost_category \"\")", env)) == "low",
          "cap.describe tamp cost_category mismatch");
    check(string_value(eval_text("(map.get (cap.describe \"cap.navigation.v1\") 'adapter_id \"\")", env)) == "mock-nav2",
          "cap.describe navigation adapter_id mismatch");
    check(integer_value(eval_text("(map.get (cap.describe \"cap.navigation.v1\") 'default_timeout_ms 0)", env)) == 1000,
          "cap.describe navigation default timeout mismatch");
    check(boolean_value(eval_text("(map.get (cap.describe \"cap.navigation.v1\") 'supports_cancellation #f)", env)),
          "cap.describe navigation should expose cancellation support");
    check(boolean_value(eval_text("(map.get (cap.describe \"cap.navigation.v1\") 'supports_replay #f)", env)),
          "cap.describe navigation should expose replay support");
    const auto nav_ops = vector_from_list(eval_text("(map.get (cap.describe \"cap.navigation.v1\") 'operations nil)", env));
    bool saw_nav_to_pose = false;
    bool saw_nav_status = false;
    for (value op : nav_ops) {
        saw_nav_to_pose = saw_nav_to_pose || string_value(op) == "navigate-to-pose";
        saw_nav_status = saw_nav_status || string_value(op) == "status";
    }
    check(saw_nav_to_pose && saw_nav_status, "cap.describe navigation should expose supported operations");
    const auto motion_groups = vector_from_list(eval_text("(map.get (cap.describe \"cap.motion.v1\") 'groups nil)", env));
    bool saw_arm_group = false;
    for (value group : motion_groups) {
        saw_arm_group = saw_arm_group || string_value(group) == "arm";
    }
    check(saw_arm_group, "cap.describe motion should expose supported groups");

    value response = eval_text(
        "(begin "
        "  (define req (map.make)) "
        "  (define payload (map.make)) "
        "  (map.set! payload 'message \"hello\") "
        "  (map.set! payload 'n 7) "
        "  (map.set! req 'schema_version \"cap.echo.request.v1\") "
        "  (map.set! req 'capability \"cap.echo.v1\") "
        "  (map.set! req 'operation \"echo\") "
        "  (map.set! req 'request_id \"echo-1\") "
        "  (map.set! req 'payload payload) "
        "  (define response (cap.call req)) "
        "  response)",
        env);
    check(is_map(response), "cap.call should return map");
    check(symbol_name(eval_text("(map.get response 'status ':none)", env)) == ":ok", "cap.call echo status mismatch");
    check(string_value(eval_text("(map.get response 'request_id \"\")", env)) == "echo-1",
          "cap.call should preserve request_id");
    check(string_value(eval_text("(map.get (map.get response 'echo (map.make)) 'message \"\")", env)) == "hello",
          "cap.call should echo payload");
    check(integer_value(eval_text("(map.get (map.get response 'echo (map.make)) 'n -1)", env)) == 7,
          "cap.call should preserve payload fields");

    bt::default_runtime_host().events().clear_ring();
    value nav_response = eval_text(
        "(define nav_response "
        " (begin "
        "  (define target (map.make)) "
        "  (map.set! target 'frame \"map\") "
        "  (map.set! target 'x 1.0) "
        "  (map.set! target 'y 2.0) "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.navigation.request.v1\") "
        "  (map.set! req 'capability \"cap.navigation.v1\") "
        "  (map.set! req 'operation \"navigate-to-pose\") "
        "  (map.set! req 'request_id \"nav-1\") "
        "  (map.set! req 'target target) "
        "  (map.set! req 'timeout_ms 1000) "
        "  (cap.call req)))",
        env);
    check(is_map(nav_response), "cap.navigation.v1 result should be a map");
    check(symbol_name(eval_text("(map.get nav_response 'status ':none)", env)) == ":accepted",
          "cap.navigation.v1 navigate-to-pose should be accepted by mock adapter");
    check(string_value(eval_text("(map.get nav_response 'adapter \"\")", env)) == "mock-nav2",
          "cap.navigation.v1 should report mock-nav2 adapter");
    check(!string_value(eval_text("(map.get nav_response 'request_hash \"\")", env)).empty(),
          "cap.navigation.v1 should expose request hash");
    check(!string_value(eval_text("(map.get nav_response 'response_hash \"\")", env)).empty(),
          "cap.navigation.v1 should expose response hash");
    check(boolean_value(eval_text("(map.get nav_response 'host_reached #f)", env)),
          "accepted cap.navigation.v1 call should reach mock adapter");

    value motion_response = eval_text(
        "(define motion_response "
        " (begin "
        "  (define target (map.make)) "
        "  (map.set! target 'frame \"world\") "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.motion.request.v1\") "
        "  (map.set! req 'capability \"cap.motion.v1\") "
        "  (map.set! req 'operation \"validate-target\") "
        "  (map.set! req 'request_id \"motion-1\") "
        "  (map.set! req 'group \"arm\") "
        "  (map.set! req 'target target) "
        "  (cap.call req)))",
        env);
    check(is_map(motion_response), "cap.motion.v1 result should be a map");
    check(symbol_name(eval_text("(map.get motion_response 'status ':none)", env)) == ":ok",
          "cap.motion.v1 validate-target should return :ok by default");
    check(boolean_value(eval_text("(map.get (map.get motion_response 'result (map.make)) 'feasible #f)", env)),
          "cap.motion.v1 validate-target should report feasible target");

    value timed_motion = eval_text(
        "(define timed_motion "
        " (begin "
        "  (define target (map.make)) "
        "  (map.set! target 'frame \"world\") "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.motion.request.v1\") "
        "  (map.set! req 'capability \"cap.motion.v1\") "
        "  (map.set! req 'operation \"move-to-pose\") "
        "  (map.set! req 'request_id \"motion-timeout\") "
        "  (map.set! req 'target target) "
        "  (map.set! req 'mock_status \"timeout\") "
        "  (cap.call req)))",
        env);
    check(symbol_name(eval_text("(map.get timed_motion 'status ':none)", env)) == ":timeout",
          "cap.motion.v1 mock_status should force timeout result");
    check(boolean_value(eval_text("(map.get timed_motion 'host_reached #f)", env)),
          "timeout cap.motion.v1 call should still report host_reached");

    value tamp_response = eval_text(
        "(define tamp_response "
        " (begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.tamp.request.v1\") "
        "  (map.set! req 'capability \"cap.tamp.v1\") "
        "  (map.set! req 'operation \"solve\") "
        "  (map.set! req 'request_id \"tamp-1\") "
        "  (map.set! req 'planner \"pddlstream-pybullet\") "
        "  (cap.call req)))",
        env);
    check(is_map(tamp_response), "cap.tamp.v1 result should be a map");
    check(symbol_name(eval_text("(map.get tamp_response 'status ':none)", env)) == ":ok",
          "cap.tamp.v1 solve should return :ok by default");
    check(is_proper_list(eval_text("(map.get tamp_response 'plan nil)", env)), "cap.tamp.v1 should return a plan list");
    check(string_value(eval_text("(map.get (map.get tamp_response 'proposal (map.make)) 'fragment_contract \"\")", env)) ==
              "guarded-task-plan.v1",
          "cap.tamp.v1 should expose guarded-task-plan proposal metadata");

    value rejected_motion = eval_text(
        "(define rejected_motion "
        " (begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.motion.request.v1\") "
        "  (map.set! req 'capability \"cap.motion.v1\") "
        "  (map.set! req 'operation \"unsupported\") "
        "  (cap.call req)))",
        env);
    check(symbol_name(eval_text("(map.get rejected_motion 'status ':none)", env)) == ":rejected",
          "unsupported cap.motion.v1 operation should return :rejected");
    check(!boolean_value(eval_text("(map.get rejected_motion 'host_reached #t)", env)),
          "unsupported cap.motion.v1 operation should not reach adapter");

    value missing_nav_target = eval_text(
        "(define missing_nav_target "
        " (begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.navigation.request.v1\") "
        "  (map.set! req 'capability \"cap.navigation.v1\") "
        "  (map.set! req 'operation \"navigate-to-pose\") "
        "  (cap.call req)))",
        env);
    check(symbol_name(eval_text("(map.get missing_nav_target 'status ':none)", env)) == ":rejected",
          "missing navigation target should be rejected");
    check(string_value(eval_text("(map.get missing_nav_target 'validation_reason_code \"\")", env)) == "missing_target",
          "missing navigation target reason mismatch");
    check(!boolean_value(eval_text("(map.get missing_nav_target 'host_reached #t)", env)),
          "missing navigation target should not reach adapter");

    value invalid_nav_frame = eval_text(
        "(define invalid_nav_frame "
        " (begin "
        "  (define target (map.make)) "
        "  (map.set! target 'frame \"camera\") "
        "  (map.set! target 'x 1.0) "
        "  (map.set! target 'y 2.0) "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.navigation.request.v1\") "
        "  (map.set! req 'capability \"cap.navigation.v1\") "
        "  (map.set! req 'operation \"navigate-to-pose\") "
        "  (map.set! req 'target target) "
        "  (cap.call req)))",
        env);
    check(string_value(eval_text("(map.get invalid_nav_frame 'validation_reason_code \"\")", env)) == "invalid_frame",
          "invalid navigation frame reason mismatch");

    value invalid_motion_group = eval_text(
        "(define invalid_motion_group "
        " (begin "
        "  (define target (map.make)) "
        "  (map.set! target 'frame \"world\") "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.motion.request.v1\") "
        "  (map.set! req 'capability \"cap.motion.v1\") "
        "  (map.set! req 'operation \"validate-target\") "
        "  (map.set! req 'target target) "
        "  (map.set! req 'group \"leg\") "
        "  (cap.call req)))",
        env);
    check(string_value(eval_text("(map.get invalid_motion_group 'validation_reason_code \"\")", env)) == "invalid_group",
          "invalid motion group reason mismatch");

    value missing_motion_job = eval_text(
        "(define missing_motion_job "
        " (begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.motion.request.v1\") "
        "  (map.set! req 'capability \"cap.motion.v1\") "
        "  (map.set! req 'operation \"cancel\") "
        "  (cap.call req)))",
        env);
    check(string_value(eval_text("(map.get missing_motion_job 'validation_reason_code \"\")", env)) == "missing_job_id",
          "missing motion job_id reason mismatch");

    value invalid_timeout = eval_text(
        "(define invalid_timeout "
        " (begin "
        "  (define target (map.make)) "
        "  (map.set! target 'frame \"map\") "
        "  (map.set! target 'x 1.0) "
        "  (map.set! target 'y 2.0) "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.navigation.request.v1\") "
        "  (map.set! req 'capability \"cap.navigation.v1\") "
        "  (map.set! req 'operation \"navigate-to-pose\") "
        "  (map.set! req 'target target) "
        "  (map.set! req 'timeout_ms -1) "
        "  (cap.call req)))",
        env);
    check(string_value(eval_text("(map.get invalid_timeout 'validation_reason_code \"\")", env)) == "invalid_timeout",
          "invalid timeout reason mismatch");

    value invalid_status = eval_text(
        "(define invalid_status "
        " (begin "
        "  (define target (map.make)) "
        "  (map.set! target 'frame \"world\") "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.motion.request.v1\") "
        "  (map.set! req 'capability \"cap.motion.v1\") "
        "  (map.set! req 'operation \"validate-target\") "
        "  (map.set! req 'target target) "
        "  (map.set! req 'mock_status \"teleported\") "
        "  (cap.call req)))",
        env);
    check(string_value(eval_text("(map.get invalid_status 'validation_reason_code \"\")", env)) == "invalid_status",
          "invalid mock status reason mismatch");

    value adapter_mismatch = eval_text(
        "(define adapter_mismatch "
        " (begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.tamp.request.v1\") "
        "  (map.set! req 'capability \"cap.tamp.v1\") "
        "  (map.set! req 'operation \"solve\") "
        "  (map.set! req 'adapter \"nav2\") "
        "  (cap.call req)))",
        env);
    check(string_value(eval_text("(map.get adapter_mismatch 'validation_reason_code \"\")", env)) == "adapter_mismatch",
          "adapter mismatch reason mismatch");

    value invalid_tamp = eval_text(
        "(define invalid_tamp "
        " (begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.tamp.request.v1\") "
        "  (map.set! req 'capability \"cap.tamp.v1\") "
        "  (map.set! req 'operation \"validate-plan\") "
        "  (cap.call req)))",
        env);
    check(string_value(eval_text("(map.get invalid_tamp 'validation_reason_code \"\")", env)) == "missing_plan",
          "missing TAMP plan reason mismatch");

    const std::vector<std::string> adapter_events = bt::default_runtime_host().events().snapshot();
    std::size_t cap_start_count = 0;
    std::size_t cap_end_count = 0;
    bool saw_nav_event = false;
    bool saw_tamp_event = false;
    for (const std::string& line : adapter_events) {
        if (line.find("\"type\":\"cap_call_start\"") != std::string::npos) {
            ++cap_start_count;
        }
        if (line.find("\"type\":\"cap_call_end\"") != std::string::npos) {
            ++cap_end_count;
        }
        saw_nav_event = saw_nav_event || line.find("\"capability\":\"cap.navigation.v1\"") != std::string::npos;
        saw_tamp_event = saw_tamp_event || line.find("\"capability\":\"cap.tamp.v1\"") != std::string::npos;
    }
    check(cap_start_count >= 13, "mock adapter cap.call should emit cap_call_start events");
    check(cap_end_count >= 13, "mock adapter cap.call should emit cap_call_end events");
    check(saw_nav_event, "mock adapter events should include cap.navigation.v1");
    check(saw_tamp_event, "mock adapter events should include cap.tamp.v1");

    value model_desc = eval_text("(cap.describe \"cap.model.world.rollout.v1\")", env);
    check(is_map(model_desc), "cap.describe model rollout should return map");
    check(string_value(eval_text("(map.get (cap.describe \"cap.model.world.rollout.v1\") 'name \"\")", env)) ==
              "cap.model.world.rollout.v1",
          "cap.describe model rollout name mismatch");

    bt::default_runtime_host().events().clear_ring();
    (void)eval_text(
        "(define model_response "
        " (begin "
        "  (define input (map.make)) "
        "  (map.set! input 'state (map.make)) "
        "  (map.set! input 'actions (list)) "
        "  (define req (map.make)) "
        "  (map.set! req 'capability \"cap.model.world.rollout.v1\") "
        "  (map.set! req 'operation \"invoke\") "
        "  (map.set! req 'request_id \"rollout-1\") "
        "  (map.set! req 'deadline_ms 25) "
        "  (map.set! req 'input input) "
        "  (cap.call req)))",
        env);
    value model_response = eval_text("model_response", env);
    check(is_map(model_response), "model-service cap.call should return map");
    check(symbol_name(eval_text("(map.get model_response 'status ':none)", env)) == ":unavailable",
          "unconfigured model-service cap.call should return :unavailable");
    check(string_value(eval_text("(map.get model_response 'error_code \"\")", env)) == "model_service_unconfigured",
          "unconfigured model-service cap.call error code mismatch");
    check(!string_value(eval_text("(map.get model_response 'request_hash \"\")", env)).empty(),
          "model-service cap.call should expose request_hash");
    check(!string_value(eval_text("(map.get model_response 'response_hash \"\")", env)).empty(),
          "model-service cap.call should expose response_hash");
    check(!boolean_value(eval_text("(map.get model_response 'replay_cache_hit true)", env)),
          "unconfigured model-service cap.call should not be a replay cache hit");
    check(symbol_name(eval_text("(map.get model_response 'validation_status ':missing)", env)) == ":not_checked",
          "unconfigured model-service cap.call should not run output validation");
    check(!boolean_value(eval_text("(map.get model_response 'host_reached true)", env)),
          "unconfigured model-service cap.call must not reach host");
    const std::vector<std::string> cap_events = bt::default_runtime_host().events().snapshot();
    bool saw_cap_start = false;
    bool saw_cap_end = false;
    for (const std::string& line : cap_events) {
        saw_cap_start = saw_cap_start || line.find("\"type\":\"cap_call_start\"") != std::string::npos;
        saw_cap_end = saw_cap_end || line.find("\"type\":\"cap_call_end\"") != std::string::npos;
    }
    check(saw_cap_start, "model-service cap.call should emit cap_call_start");
    check(saw_cap_end, "model-service cap.call should emit cap_call_end");
    value check_result = eval_text("(model-service.check)", env);
    check(is_map(check_result), "model-service.check should return map");
    check(!boolean_value(eval_text("(map.get (model-service.check) 'compatible true)", env)),
          "unconfigured model-service.check should be incompatible");
    check(string_value(eval_text("(map.get (model-service.check) 'error_code \"\")", env)) ==
              "model_service_unconfigured",
          "unconfigured model-service.check should report model_service_unconfigured");

    value rejected = eval_text(
        "(begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.echo.request.v1\") "
        "  (map.set! req 'capability \"cap.echo.v1\") "
        "  (map.set! req 'operation \"nope\") "
        "  (define rejected (cap.call req)) "
        "  rejected)",
        env);
    check(is_map(rejected), "cap.call rejected operation should return map");
    check(symbol_name(eval_text("(map.get rejected 'status ':none)", env)) == ":rejected",
          "cap.call unsupported operation should return :rejected");

    expect_lisp_error_message(
        "(begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.echo.request.v1\") "
        "  (map.set! req 'capability \"cap.unknown.v1\") "
        "  (map.set! req 'operation \"echo\") "
        "  (cap.call req))",
        env,
        "cap.call: unknown capability",
        "cap.call unknown capability");
}

void test_model_service_protocol_skeleton() {
    bt::model_service_request request;
    request.id = "req-1";
    request.op = bt::model_service_operation::invoke;
    request.capability = "cap.model.world.rollout.v1";
    request.deadline_ms = 100;

    bt::unavailable_model_service_client client;
    bt::model_service_response response = client.call(request);

    check(std::string(bt::model_service_operation_name(request.op)) == "invoke",
          "model service operation name mismatch");
    const std::string envelope = bt::model_service_request_to_json(request);
    check(envelope.find("\"version\":\"0.2\"") != std::string::npos, "model service envelope version missing");
    check(envelope.find("\"op\":\"invoke\"") != std::string::npos, "model service envelope operation missing");
    check(envelope.find("\"capability\":\"cap.model.world.rollout.v1\"") != std::string::npos,
          "model service envelope capability missing");

    const bt::model_service_response parsed = bt::model_service_response_from_json(
        "{\"version\":\"0.2\",\"id\":\"req-1\",\"status\":\"action_chunk\","
        "\"output\":{\"actions\":[{\"type\":\"joint_targets\",\"values\":[0.1],\"dt_ms\":33}]},"
        "\"session_id\":\"sess-1\",\"error\":null,"
        "\"metadata\":{\"capability\":\"cap.vla.action_chunk.v1\",\"backend\":\"smolvla\"}}");
    check(parsed.id == "req-1", "model service parsed response id mismatch");
    check(parsed.status == bt::model_service_status::action_chunk, "model service parsed status mismatch");
    check(parsed.output_json.find("\"actions\"") != std::string::npos,
          "model service parsed output should preserve raw actions JSON");
    check(parsed.session_id == "sess-1", "model service parsed session id mismatch");
    check(parsed.metadata_json.find("\"smolvla\"") != std::string::npos,
          "model service parsed metadata should preserve raw JSON");

    check(std::string(bt::model_service_status_name(response.status)) == "unavailable",
          "model service unavailable status mismatch");
    check(bt::model_service_status_terminal(response.status), "unavailable should be terminal");
    check(response.id == "req-1", "model service response should preserve request id");
    check(response.error_code == "model_service_unconfigured",
          "model service unavailable error code mismatch");
    check(!response.host_reached, "unconfigured model service must not reach host execution");

    struct fake_describe_client final : bt::model_service_client {
        bt::model_service_response call(const bt::model_service_request& req) override {
            bt::model_service_response out;
            out.id = req.id;
            out.status = bt::model_service_status::success;
            out.output_json =
                "{\"capabilities\":["
                "{\"id\":\"cap.model.world.rollout.v1\",\"mode\":\"invoke\","
                "\"input_schema\":\"mms://schemas/cap.model.world.rollout.request.v1\","
                "\"output_schema\":\"mms://schemas/cap.model.world.rollout.result.v1\","
                "\"supports_cancel\":false,\"supports_deadline\":true,\"freshness\":{},"
                "\"replay\":{\"supported\":true}},"
                "{\"id\":\"cap.model.world.score_trajectory.v1\",\"mode\":\"invoke\","
                "\"input_schema\":\"mms://schemas/cap.model.world.score_trajectory.request.v1\","
                "\"output_schema\":\"mms://schemas/cap.model.world.score_trajectory.result.v1\","
                "\"supports_cancel\":false,\"supports_deadline\":true,\"freshness\":{},"
                "\"replay\":{\"supported\":true}},"
                "{\"id\":\"cap.vla.action_chunk.v1\",\"mode\":\"session\","
                "\"input_schema\":\"mms://schemas/cap.vla.action_chunk.request.v1\","
                "\"output_schema\":\"mms://schemas/cap.vla.action_chunk.result.v1\","
                "\"supports_cancel\":true,\"supports_deadline\":true,"
                "\"freshness\":{\"expects_fresh_observation\":true},"
                "\"replay\":{\"supported\":false}},"
                "{\"id\":\"cap.vla.propose_nav_goal.v1\",\"mode\":\"invoke\","
                "\"input_schema\":\"mms://schemas/cap.vla.propose_nav_goal.request.v1\","
                "\"output_schema\":\"mms://schemas/cap.vla.propose_nav_goal.result.v1\","
                "\"supports_cancel\":false,\"supports_deadline\":true,\"freshness\":{},"
                "\"replay\":{\"supported\":true}}"
                "]}";
            return out;
        }
    };
    fake_describe_client compatible_client;
    const bt::model_service_compatibility_result compatible =
        bt::check_model_service_compatibility(compatible_client);
    check(compatible.compatible, "complete describe response should be compatible");
    check(compatible.missing_capabilities.empty(), "compatible describe should not report missing capabilities");
    check(compatible.invalid_capabilities.empty(), "compatible describe should not report invalid capabilities");
    check(compatible.descriptor_errors.empty(), "compatible describe should not report descriptor errors");

    struct missing_describe_client final : bt::model_service_client {
        bt::model_service_response call(const bt::model_service_request& req) override {
            bt::model_service_response out;
            out.id = req.id;
            out.status = bt::model_service_status::success;
            out.output_json = "{\"capabilities\":[{\"id\":\"cap.model.world.rollout.v1\"}]}";
            return out;
        }
    };
    missing_describe_client missing_client;
    const bt::model_service_compatibility_result missing =
        bt::check_model_service_compatibility(missing_client);
    check(!missing.compatible, "missing describe response should be incompatible");
    check(missing.error_code == "model_service_capability_missing",
          "missing describe response should report capability_missing");
    check(!missing.missing_capabilities.empty(), "missing describe response should list missing capabilities");

    struct invalid_descriptor_client final : bt::model_service_client {
        bt::model_service_response call(const bt::model_service_request& req) override {
            bt::model_service_response out;
            out.id = req.id;
            out.status = bt::model_service_status::success;
            out.output_json =
                "{\"capabilities\":["
                "{\"id\":\"cap.model.world.rollout.v1\",\"mode\":\"invoke\","
                "\"input_schema\":\"mms://schemas/cap.model.world.rollout.request.v1\","
                "\"output_schema\":\"mms://schemas/cap.model.world.rollout.result.v1\","
                "\"supports_cancel\":false,\"supports_deadline\":true,\"freshness\":{},"
                "\"replay\":{\"supported\":true}},"
                "{\"id\":\"cap.model.world.score_trajectory.v1\",\"mode\":\"invoke\","
                "\"input_schema\":\"mms://schemas/cap.model.world.score_trajectory.request.v1\","
                "\"output_schema\":\"mms://schemas/cap.model.world.score_trajectory.result.v1\","
                "\"supports_cancel\":false,\"supports_deadline\":true,\"freshness\":{},"
                "\"replay\":{\"supported\":true}},"
                "{\"id\":\"cap.vla.action_chunk.v1\",\"mode\":\"invoke\","
                "\"input_schema\":\"mms://schemas/cap.vla.action_chunk.request.v1\","
                "\"output_schema\":\"mms://schemas/cap.vla.action_chunk.result.v1\","
                "\"supports_cancel\":false,\"supports_deadline\":true,"
                "\"freshness\":{\"expects_fresh_observation\":false},"
                "\"replay\":{\"supported\":false}},"
                "{\"id\":\"cap.vla.propose_nav_goal.v1\",\"mode\":\"invoke\","
                "\"input_schema\":\"mms://schemas/cap.vla.propose_nav_goal.request.v1\","
                "\"output_schema\":\"mms://schemas/cap.vla.propose_nav_goal.result.v1\","
                "\"supports_cancel\":false,\"supports_deadline\":true,\"freshness\":{},"
                "\"replay\":{\"supported\":true}}"
                "]}";
            return out;
        }
    };
    invalid_descriptor_client invalid_descriptor;
    const bt::model_service_compatibility_result invalid_compatibility =
        bt::check_model_service_compatibility(invalid_descriptor);
    check(!invalid_compatibility.compatible, "invalid descriptor response should be incompatible");
    check(invalid_compatibility.error_code == "model_service_descriptor_invalid",
          "invalid descriptor response should report descriptor_invalid");
    check(invalid_compatibility.invalid_capabilities.size() == 1 &&
              invalid_compatibility.invalid_capabilities.front() == "cap.vla.action_chunk.v1",
          "invalid descriptor response should report the invalid capability");
    check(!invalid_compatibility.descriptor_errors.empty(),
          "invalid descriptor response should include descriptor diagnostics");

    struct replay_fake_client final : bt::model_service_client {
        int calls = 0;
        bt::model_service_response call(const bt::model_service_request& req) override {
            ++calls;
            bt::model_service_response out;
            out.id = req.id;
            out.status = bt::model_service_status::success;
            out.output_json = "{\"predicted_states\":[{\"vector\":[0.0]}]}";
            out.metadata_json = "{\"backend\":\"fake\"}";
            out.raw_json = bt::model_service_response_to_json(out);
            return out;
        }
    };

    bt::runtime_host host;
    const std::filesystem::path cache_dir = temp_file_path("model_service_replay_cache", "");
    std::filesystem::remove_all(cache_dir);
    bt::model_service_config record_cfg;
    record_cfg.replay_mode = "record";
    record_cfg.replay_cache_path = cache_dir.string();
    auto record_client = std::make_unique<replay_fake_client>();
    replay_fake_client* record_client_ptr = record_client.get();
    host.set_model_service_client(record_cfg, std::move(record_client));

    bt::model_service_request cache_request;
    cache_request.id = "cache-1";
    cache_request.op = bt::model_service_operation::invoke;
    cache_request.capability = "cap.model.world.rollout.v1";
    cache_request.input_json = "{\"state\":{\"vector\":[0.0]},\"actions\":[]}";
    const bt::model_service_response recorded = host.call_model_service(cache_request);
    check(recorded.status == bt::model_service_status::success, "recorded model-service call should succeed");
    check(recorded.validation_checked, "recorded model-service call should run validation");
    check(recorded.validation_ok, "recorded model-service call should pass validation");
    check(!recorded.request_hash.empty(), "recorded model-service call should have request hash");
    check(!recorded.response_hash.empty(), "recorded model-service call should have response hash");
    check(record_client_ptr->calls == 1, "recording should call live model-service client once");
    check(std::filesystem::exists(cache_dir / (recorded.request_hash + ".json")),
          "recording should write replay cache file");

    bt::model_service_config replay_cfg;
    replay_cfg.replay_mode = "replay";
    replay_cfg.replay_cache_path = cache_dir.string();
    host.set_model_service_client(replay_cfg, nullptr);
    const bt::model_service_response replayed = host.call_model_service(cache_request);
    check(replayed.status == bt::model_service_status::success, "replayed model-service call should succeed");
    check(replayed.validation_checked, "replayed model-service call should run validation");
    check(replayed.validation_ok, "replayed model-service call should pass validation");
    check(replayed.replay_cache_hit, "replayed model-service call should report cache hit");
    check(replayed.request_hash == recorded.request_hash, "replayed model-service request hash mismatch");
    check(replayed.response_hash == recorded.response_hash, "replayed model-service response hash mismatch");
    std::filesystem::remove_all(cache_dir);

    struct invalid_output_client final : bt::model_service_client {
        bt::model_service_response call(const bt::model_service_request& req) override {
            bt::model_service_response out;
            out.id = req.id;
            out.status = bt::model_service_status::success;
            out.output_json = "{\"score\":1.0}";
            out.raw_json = bt::model_service_response_to_json(out);
            return out;
        }
    };
    host.set_model_service_client(bt::model_service_config{}, std::make_unique<invalid_output_client>());
    const bt::model_service_response invalid = host.call_model_service(cache_request);
    check(invalid.status == bt::model_service_status::invalid_output,
          "world rollout missing predicted_states should be invalid_output");
    check(invalid.validation_checked, "invalid model-service output should run validation");
    check(!invalid.validation_ok, "invalid model-service output should fail validation");
    check(invalid.validation_reason_code == "model_service_missing_predicted_states",
          "invalid model-service output reason mismatch");
    check(!invalid.host_reached, "invalid model-service output must not reach host");

    struct unsafe_output_client final : bt::model_service_client {
        bt::model_service_response call(const bt::model_service_request& req) override {
            bt::model_service_response out;
            out.id = req.id;
            out.status = bt::model_service_status::success;
            out.output_json = "{\"predicted_states\":[],\"unsafe\":true}";
            out.raw_json = bt::model_service_response_to_json(out);
            return out;
        }
    };
    host.set_model_service_client(bt::model_service_config{}, std::make_unique<unsafe_output_client>());
    const bt::model_service_response unsafe = host.call_model_service(cache_request);
    check(unsafe.status == bt::model_service_status::unsafe_output,
          "unsafe model-service output should be unsafe_output");
    check(unsafe.validation_reason_code == "model_service_unsafe_output",
          "unsafe model-service output reason mismatch");
    check(!unsafe.host_reached, "unsafe model-service output must not reach host");

    struct fault_schedule_live_client final : bt::model_service_client {
        int calls = 0;
        bt::model_service_response call(const bt::model_service_request& req) override {
            ++calls;
            bt::model_service_response out;
            out.id = req.id;
            out.status = bt::model_service_status::success;
            out.output_json = "{\"predicted_states\":[{\"vector\":[1.0]}]}";
            out.raw_json = bt::model_service_response_to_json(out);
            return out;
        }
    };
    bt::model_service_config fault_cfg;
    fault_cfg.fault_schedule = {
        "invalid_output",
        "unsafe_output",
        "stale_result",
        "timeout",
        "unavailable",
        "none",
    };
    auto fault_client = std::make_unique<fault_schedule_live_client>();
    fault_schedule_live_client* fault_client_ptr = fault_client.get();
    host.set_model_service_client(fault_cfg, std::move(fault_client));

    const bt::model_service_response fault_invalid = host.call_model_service(cache_request);
    check(fault_invalid.status == bt::model_service_status::invalid_output,
          "fault invalid_output should produce invalid_output");
    check(fault_invalid.validation_reason_code == "model_service_missing_predicted_states",
          "fault invalid_output reason mismatch");

    const bt::model_service_response fault_unsafe = host.call_model_service(cache_request);
    check(fault_unsafe.status == bt::model_service_status::unsafe_output,
          "fault unsafe_output should produce unsafe_output");
    check(fault_unsafe.validation_reason_code == "model_service_unsafe_output",
          "fault unsafe_output reason mismatch");

    const bt::model_service_response fault_stale = host.call_model_service(cache_request);
    check(fault_stale.status == bt::model_service_status::invalid_output,
          "fault stale_result should produce invalid_output");
    check(fault_stale.validation_reason_code == "model_service_stale_result",
          "fault stale_result reason mismatch");

    const bt::model_service_response fault_timeout = host.call_model_service(cache_request);
    check(fault_timeout.status == bt::model_service_status::timeout,
          "fault timeout should produce timeout");
    check(fault_timeout.error_code == "model_service_fault_timeout",
          "fault timeout error code mismatch");

    const bt::model_service_response fault_unavailable = host.call_model_service(cache_request);
    check(fault_unavailable.status == bt::model_service_status::unavailable,
          "fault unavailable should produce unavailable");
    check(fault_unavailable.error_code == "model_service_fault_unavailable",
          "fault unavailable error code mismatch");

    const bt::model_service_response fault_passthrough = host.call_model_service(cache_request);
    check(fault_passthrough.status == bt::model_service_status::success,
          "fault none should call live model-service client");
    check(fault_passthrough.validation_ok, "fault none live result should validate");
    check(fault_client_ptr->calls == 1, "fault schedule should only call live client for passthrough fault");
}

void test_vla_builtins_submit_poll_cancel_and_caps() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    value caps = eval_text("(cap.list)", env);
    check(is_proper_list(caps), "cap.list should return list");
    bool saw_vla = false;
    for (value cap_name : vector_from_list(caps)) {
        if (is_string(cap_name) && string_value(cap_name).find("vla.") != std::string::npos) {
            saw_vla = true;
        }
    }
    check(saw_vla, "cap.list should include vla capability");
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
        "  (map.set! space 'frame_id \"ball_context\")"
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
    value final_action_frame = eval_text(
        "(map.get (map.get (map.get (vla.poll " + std::to_string(job_id) +
            ") 'final (map.make)) 'action (map.make)) 'frame_id \"\")",
        env);
    check(is_string(final_action_frame) && string_value(final_action_frame) == "ball_context",
          "vla action frame should round-trip through submit and poll");

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

    value cancel_events = eval_text("(events.dump 120)", env);
    check(is_proper_list(cancel_events), "events.dump should return list for cancel outcome test");
    bool saw_cancel_acknowledged = false;
    for (const value& row : vector_from_list(cancel_events)) {
        if (!is_string(row)) {
            continue;
        }
        const std::string line = string_value(row);
        if (line.find("\"type\":\"cancel_acknowledged\"") != std::string::npos &&
            line.find("\"schema_version\":\"runtime_outcome.v1\"") != std::string::npos) {
            saw_cancel_acknowledged = true;
        }
    }
    check(saw_cancel_acknowledged, "VLA cancel should emit compact cancel_acknowledged outcome");
}

void test_approach_pose_validator_checks_bounds_frame_context_and_stability() {
    bt::approach_pose_host_state host_state{.ball_context_id = "ball-A", .robot_stable = true};
    bt::approach_pose_validator validator(
        bt::approach_pose_validator_config{
            .frame_id = "ball_context",
            .bounds = {.min_x_m = -1.0,
                       .max_x_m = 0.0,
                       .min_y_m = -0.5,
                       .max_y_m = 0.5,
                       .min_yaw_rad = -3.141593,
                       .max_yaw_rad = 3.141593}},
        [&host_state] { return host_state; });

    bt::vla_commit_context context;
    context.captured_context_id = "ball-A";
    context.current_context_id = "ball-A";
    context.expected_action_frame = "ball_context";
    bt::vla_action action;
    action.type = bt::vla_action_type::continuous;
    action.frame_id = "ball_context";
    action.u = {-0.45, 0.08, 0.0};

    check(validator.validate(context, action).accepted,
          "current, stable, in-bounds approach pose should pass host validation");

    action.u = {-0.45, 0.08};
    check(validator.validate(context, action).reason == "invalid_schema",
          "approach pose must have exactly three components");
    action.u = {-0.45, 0.08, 0.0};

    action.frame_id = "field";
    check(validator.validate(context, action).reason == "invalid_frame",
          "approach pose in the wrong result frame should be rejected");
    action.frame_id = "ball_context";

    context.expected_action_frame = "field";
    check(validator.validate(context, action).reason == "invalid_frame",
          "approach pose request in the wrong frame should be rejected");
    context.expected_action_frame = "ball_context";

    action.u[0] = 0.1;
    check(validator.validate(context, action).reason == "invalid_pose",
          "out-of-bounds approach pose should be rejected");
    action.u[0] = -0.45;

    host_state.ball_context_id = "ball-B";
    check(validator.validate(context, action).reason == "context_changed",
          "host ball context change should reject the old approach pose");
    host_state.ball_context_id.clear();
    check(validator.validate(context, action).reason == "ball_stale",
          "missing current ball context should reject the approach pose as stale");

    host_state.ball_context_id = "ball-A";
    host_state.robot_stable = false;
    check(validator.validate(context, action).reason == "robot_unstable",
          "unstable robot state should reject the approach pose");
}

void test_approach_pose_validator_registers_with_commit_gate() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    bt::runtime_host& host = bt::default_runtime_host();
    bt::approach_pose_host_state host_state{.ball_context_id = "ball-A", .robot_stable = true};
    bt::approach_pose_validator validator(
        bt::approach_pose_validator_config{
            .frame_id = "ball_context",
            .bounds = {.min_x_m = -1.0,
                       .max_x_m = 1.0,
                       .min_y_m = -1.0,
                       .max_y_m = 1.0,
                       .min_yaw_rad = -3.141593,
                       .max_yaw_rad = 3.141593}},
        [&host_state] { return host_state; });
    host.set_vla_commit_validator(&validator);
    controlled_walking_target_dispatcher dispatcher(
        bt::walking_target_dispatch_result{.accepted = false, .reason = "walking_controller_rejected"});
    host.set_walking_target_dispatcher(&dispatcher);
    auto release = std::make_shared<std::atomic<bool>>(false);
    host.vla_ref().register_backend("approach-pose-test", std::make_shared<controlled_vla_backend>(release));

    (void)eval_text(
        "(define approach-pose-tree "
        "  (bt.compile "
        "    '(reactive-sel "
        "       (seq (vla-wait :name \"approach-pose\" :job_key approach-job :action_key approach-action) "
        "            (succeed)) "
        "       (vla-request :name \"approach-pose\" :job_key approach-job :instruction \"approach\" "
        "                    :state_key state :model_name \"approach-pose-test\" :deadline_ms 1000 :dims 3 "
        "                    :action_frame ball_context :acceptance_policy invocation_scoped "
        "                    :context_key ball-context))))",
        env);
    (void)eval_text("(define approach-pose-inst (bt.new-instance approach-pose-tree))", env);

    (void)eval_text(
        "(bt.tick approach-pose-inst '((state (0.0 0.0 0.0)) (ball-context \"ball-A\")))", env);
    const std::int64_t instance_handle = bt_handle(eval_text("approach-pose-inst", env));
    bt::instance* inst = host.find_instance(instance_handle);
    check(inst != nullptr && inst->vla_invocations.size() == 1,
          "approach pose request should create one invocation");
    const std::uint64_t job_id = inst->vla_invocations.begin()->first;
    check(inst->vla_invocations.at(job_id).action_frame == "ball_context",
          "invocation should capture the requested action frame");

    (void)eval_text(
        "(bt.tick approach-pose-inst '((state (0.0 0.0 0.0)) (ball-context \"ball-A\")))", env);
    release->store(true);

    bool succeeded = false;
    for (int i = 0; i < 100; ++i) {
        value result = eval_text(
            "(bt.tick approach-pose-inst '((state (0.0 0.0 0.0)) (ball-context \"ball-A\")))", env);
        if (is_symbol(result) && symbol_name(result) == "success") {
            succeeded = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(succeeded, "registered approach pose validator should accept a valid current pose");
    const bt::bb_entry* accepted = inst->bb.get("approach-action");
    check(accepted != nullptr && std::holds_alternative<std::vector<double>>(accepted->value) &&
              std::get<std::vector<double>>(accepted->value).size() == 3,
          "accepted approach pose should write a three-component action");

    const bt::walking_target target{.frame_id = "ball_context", .x_m = 0.25, .y_m = 0.25, .yaw_rad = 0.25};
    const bt::walking_target_dispatch_result controller_rejection =
        host.dispatch_walking_target(instance_handle, job_id, 99, target);
    check(!controller_rejection.accepted && controller_rejection.reason == "walking_controller_rejected",
          "walking-controller rejection should retain its stable evidence reason");
    check(dispatcher.calls == 1, "a rejected controller hand-off should call the dispatcher once");

    dispatcher.set_result(bt::walking_target_dispatch_result{.accepted = true, .reason = {}});
    const bt::walking_target_dispatch_result dispatch =
        host.dispatch_walking_target(instance_handle, job_id, 99, target);
    check(dispatch.accepted, "accepted invocation should reach the registered walking-target dispatcher");
    check(dispatcher.calls == 2, "walking target should be accepted by the dispatcher exactly once");
    check(dispatcher.last_context.job_id == job_id && dispatcher.last_context.generation == 1 &&
              dispatcher.last_context.captured_context_id == "ball-A" &&
              dispatcher.last_context.current_context_id == "ball-A",
          "walking-target dispatcher should receive invocation generation and context");
    check(dispatcher.last_target.frame_id == "ball_context" && dispatcher.last_target.x_m == 0.25,
          "walking-target dispatcher should receive the validated target");

    const bt::walking_target_dispatch_result duplicate =
        host.dispatch_walking_target(instance_handle, job_id, 99, target);
    check(!duplicate.accepted && duplicate.reason == "duplicate_dispatch",
          "a second walking-target dispatch should be rejected deterministically");
    check(dispatcher.calls == 2, "duplicate dispatch must not call the walking controller again");

    bool saw_action_frame = false;
    bool saw_accepted_result_evidence = false;
    bool saw_dispatch_evidence = false;
    bool saw_controller_rejection_evidence = false;
    bool saw_duplicate_dispatch_evidence = false;
    for (const std::string& line : host.events().snapshot()) {
        saw_action_frame = saw_action_frame ||
                           (line.find("\"type\":\"vla_submit\"") != std::string::npos &&
                            line.find("\"action_frame\":\"ball_context\"") != std::string::npos);
        saw_accepted_result_evidence = saw_accepted_result_evidence ||
                                       (line.find("\"type\":\"vla_result\"") != std::string::npos &&
                                        line.find("\"generation\":1") != std::string::npos &&
                                        line.find("\"captured_context_id\":\"ball-A\"") != std::string::npos &&
                                        line.find("\"current_context_id\":\"ball-A\"") != std::string::npos &&
                                        line.find("\"decision\":\"accepted\"") != std::string::npos);
        saw_dispatch_evidence = saw_dispatch_evidence ||
                                (line.find("\"type\":\"walking_target_dispatch\"") != std::string::npos &&
                                 line.find("\"generation\":1") != std::string::npos &&
                                 line.find("\"decision\":\"accepted\"") != std::string::npos &&
                                 line.find("\"frame_id\":\"ball_context\"") != std::string::npos &&
                                 line.find("\"x_m\":0.25") != std::string::npos);
        saw_controller_rejection_evidence =
            saw_controller_rejection_evidence ||
            (line.find("\"type\":\"walking_target_dispatch\"") != std::string::npos &&
             line.find("\"decision\":\"rejected\"") != std::string::npos &&
             line.find("\"reason\":\"walking_controller_rejected\"") != std::string::npos &&
             line.find("\"dispatch_source\":\"host_callback\"") != std::string::npos);
        saw_duplicate_dispatch_evidence = saw_duplicate_dispatch_evidence ||
                                          (line.find("\"type\":\"walking_target_dispatch\"") !=
                                               std::string::npos &&
                                           line.find("\"decision\":\"rejected\"") != std::string::npos &&
                                           line.find("\"reason\":\"duplicate_dispatch\"") != std::string::npos);
    }
    check(saw_action_frame, "VLA invocation events should record the requested action frame");
    check(saw_accepted_result_evidence,
          "VLA result evidence should record generation, current context and acceptance");
    check(saw_dispatch_evidence,
          "walking-target dispatch evidence should record the correlated accepted target");
    check(saw_controller_rejection_evidence,
          "walking-target dispatch evidence should retain stable host rejection reasons");
    check(saw_duplicate_dispatch_evidence,
          "walking-target dispatch evidence should record rejected duplicate attempts and reasons");
    host.set_vla_commit_validator(nullptr);
    host.set_walking_target_dispatcher(nullptr);
}

void test_vla_invocation_scoped_authority_accepts_current_result() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    bt::runtime_host& host = bt::default_runtime_host();
    controlled_vla_commit_validator validator(bt::vla_commit_validation{.accepted = true, .reason = ""});
    host.set_vla_commit_validator(&validator);
    auto release = std::make_shared<std::atomic<bool>>(false);
    host.vla_ref().register_backend("authority-accept-test", std::make_shared<controlled_vla_backend>(release));

    (void)eval_text(
        "(define authority-accept-tree "
        "  (bt.compile "
        "    '(reactive-sel "
        "       (seq (vla-wait :name \"authority-accept\" :job_key authority-job :action_key authority-action "
        "                      :clear_job #f) "
        "            (succeed)) "
        "       (vla-request :name \"authority-accept\" :job_key authority-job :instruction \"approach\" "
        "                    :state_key state :model_name \"authority-accept-test\" :deadline_ms 1000 :dims 1 "
        "                    :acceptance_policy invocation_scoped :context_key ball-context))))",
        env);
    (void)eval_text("(define authority-accept-inst (bt.new-instance authority-accept-tree))", env);

    value first = eval_text("(bt.tick authority-accept-inst '((state 0.0) (ball-context \"ball-A\")))", env);
    check(is_symbol(first) && symbol_name(first) == "running", "invocation-scoped request should start running");

    bt::instance* inst = host.find_instance(bt_handle(eval_text("authority-accept-inst", env)));
    check(inst != nullptr, "invocation-scoped acceptance instance should exist");
    check(inst->vla_invocations.size() == 1, "request should create one invocation record");
    const bt::vla_invocation& submitted = inst->vla_invocations.begin()->second;
    const std::uint64_t job_id = submitted.job_id;
    check(job_id > 0, "invocation should track its job id");
    check(submitted.generation == 1, "first invocation generation should be one");
    check(submitted.requesting_node != 0, "invocation should track the requesting node");
    check(submitted.authority_node == submitted.requesting_node,
          "requesting node should initially own invocation authority");
    check(submitted.job_key == "authority-job", "invocation should track the configured job key");
    check(submitted.context_key == "ball-context", "invocation should track the context key");
    check(submitted.captured_context_id == "ball-A", "invocation should capture the context id");
    check(submitted.deadline > submitted.submitted_at, "invocation should track an absolute deadline");
    check(submitted.authority_state == bt::vla_authority_state::active,
          "new invocation authority should be active");

    value second = eval_text("(bt.tick authority-accept-inst '((state 0.0) (ball-context \"ball-A\")))", env);
    check(is_symbol(second) && symbol_name(second) == "running", "wait should remain running before backend release");
    const bt::vla_invocation& adopted = inst->vla_invocations.at(job_id);
    check(adopted.authority_node != adopted.requesting_node, "vla-wait should adopt authority before request-branch halt");
    check(adopted.authority_state == bt::vla_authority_state::active,
          "normal request-to-wait hand-off must not revoke authority");

    release->store(true);
    bool succeeded = false;
    for (int i = 0; i < 100; ++i) {
        value result = eval_text("(bt.tick authority-accept-inst '((state 0.0) (ball-context \"ball-A\")))", env);
        if (is_symbol(result) && symbol_name(result) == "success") {
            succeeded = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(succeeded, "current invocation-scoped result should be accepted");
    check(inst->bb.get("authority-action") != nullptr, "accepted result should update the action key");
    check(inst->vla_invocations.at(job_id).authority_state == bt::vla_authority_state::accepted,
          "accepted result should terminally consume invocation authority");
    const std::uint64_t accepted_write_tick = inst->bb.get("authority-action")->last_write_tick;
    value duplicate = eval_text("(bt.tick authority-accept-inst '((state 0.0) (ball-context \"ball-A\")))", env);
    check(is_symbol(duplicate) && symbol_name(duplicate) == "running",
          "duplicate result should fail its wait branch and leave the request branch running");
    check(inst->bb.get("authority-action")->last_write_tick == accepted_write_tick,
          "duplicate terminal result must not write the accepted action again");
    check(validator.calls == 1, "accepted invocation should run host validation exactly once");

    bool saw_rich_submit = false;
    bool saw_accepted_result = false;
    bool saw_duplicate_rejection = false;
    for (const std::string& line : host.events().snapshot()) {
        saw_rich_submit = saw_rich_submit ||
                          (line.find("\"type\":\"vla_submit\"") != std::string::npos &&
                           line.find("\"generation\":1") != std::string::npos &&
                           line.find("\"job_key\":\"authority-job\"") != std::string::npos &&
                           line.find("\"captured_context_id\":\"ball-A\"") != std::string::npos);
        saw_accepted_result = saw_accepted_result ||
                              (line.find("\"type\":\"vla_result\"") != std::string::npos &&
                               line.find("\"decision\":\"accepted\"") != std::string::npos &&
                               line.find("\"authority_state\":\"accepted\"") != std::string::npos &&
                               line.find("\"host_validation\":\"accepted\"") != std::string::npos);
        saw_duplicate_rejection = saw_duplicate_rejection ||
                                  (line.find("\"type\":\"vla_result\"") != std::string::npos &&
                                   line.find("\"decision\":\"rejected\"") != std::string::npos &&
                                   line.find("\"reason\":\"duplicate_terminal_result\"") != std::string::npos);
    }
    check(saw_rich_submit, "vla_submit should record invocation identity and captured context");
    check(saw_accepted_result, "vla_result should record authority and host validation acceptance");
    check(saw_duplicate_rejection, "accepted invocation should reject a duplicate terminal result");
    host.set_vla_commit_validator(nullptr);
}

void test_vla_commit_gate_rejects_superseded_generation() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    bt::runtime_host& host = bt::default_runtime_host();
    auto release = std::make_shared<std::atomic<bool>>(false);
    host.vla_ref().register_backend("commit-generation-test", std::make_shared<controlled_vla_backend>(release));

    (void)eval_text(
        "(define commit-generation-tree "
        "  (bt.compile "
        "    '(reactive-sel "
        "       (seq (vla-wait :name \"commit-generation\" :job_key generation-job "
        "                      :action_key generation-action) "
        "            (succeed)) "
        "       (seq (cond bb-truthy submit-enabled) "
        "            (vla-request :name \"commit-generation\" :job_key generation-job :instruction \"approach\" "
        "                         :state_key state :model_name \"commit-generation-test\" :deadline_ms 1000 :dims 1 "
        "                         :acceptance_policy invocation_scoped :context_key ball-context)))))",
        env);
    (void)eval_text("(define commit-generation-inst (bt.new-instance commit-generation-tree))", env);

    (void)eval_text(
        "(bt.tick commit-generation-inst '((state 0.0) (ball-context \"ball-A\") (submit-enabled #t)))", env);
    bt::instance* inst = host.find_instance(bt_handle(eval_text("commit-generation-inst", env)));
    check(inst != nullptr && inst->vla_invocations.size() == 1, "generation gate test should submit one invocation");
    const std::uint64_t job_id = inst->vla_invocations.begin()->first;

    (void)eval_text(
        "(bt.tick commit-generation-inst '((state 0.0) (ball-context \"ball-A\") (submit-enabled #f)))", env);
    inst->vla_generations["generation-job"] = 2;
    release->store(true);

    bool rejected = false;
    for (int i = 0; i < 100; ++i) {
        (void)eval_text(
            "(bt.tick commit-generation-inst '((state 0.0) (ball-context \"ball-A\") (submit-enabled #f)))", env);
        if (inst->vla_invocations.at(job_id).authority_state == bt::vla_authority_state::rejected) {
            rejected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    check(rejected, "commit gate should reject a result from a superseded generation");
    check(inst->vla_invocations.at(job_id).authority_reason == "superseded",
          "generation mismatch should use the stable superseded reason");
    check(inst->bb.get("generation-action") == nullptr,
          "superseded result must not update the action blackboard key");
}

void test_vla_commit_gate_requires_host_validation_and_runs_it_once() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    bt::runtime_host& host = bt::default_runtime_host();
    controlled_vla_commit_validator validator(
        bt::vla_commit_validation{.accepted = false, .reason = "robot_unstable"});
    host.set_vla_commit_validator(&validator);
    check(host.vla_commit_validator_ptr() == &validator, "runtime host should expose the configured commit validator");
    auto release = std::make_shared<std::atomic<bool>>(false);
    host.vla_ref().register_backend("commit-host-test", std::make_shared<controlled_vla_backend>(release));

    (void)eval_text(
        "(define commit-host-tree "
        "  (bt.compile "
        "    '(reactive-sel "
        "       (seq (vla-wait :name \"commit-host\" :job_key host-job :action_key host-action :clear_job #f) "
        "            (succeed)) "
        "       (seq (cond bb-truthy submit-enabled) "
        "            (vla-request :name \"commit-host\" :job_key host-job :instruction \"approach\" "
        "                         :state_key state :model_name \"commit-host-test\" :deadline_ms 1000 :dims 1 "
        "                         :acceptance_policy invocation_scoped :context_key ball-context)))))",
        env);
    (void)eval_text("(define commit-host-inst (bt.new-instance commit-host-tree))", env);

    (void)eval_text("(bt.tick commit-host-inst "
                    "'((state 0.0) (ball-context \"ball-A\") (submit-enabled #t)))",
                    env);
    bt::instance* inst = host.find_instance(bt_handle(eval_text("commit-host-inst", env)));
    check(inst != nullptr && inst->vla_invocations.size() == 1, "host gate test should submit one invocation");
    const std::uint64_t job_id = inst->vla_invocations.begin()->first;
    (void)eval_text("(bt.tick commit-host-inst "
                    "'((state 0.0) (ball-context \"ball-A\") (submit-enabled #f)))",
                    env);

    release->store(true);
    bool rejected = false;
    for (int i = 0; i < 100; ++i) {
        (void)eval_text("(bt.tick commit-host-inst "
                        "'((state 0.0) (ball-context \"ball-A\") (submit-enabled #f)))",
                        env);
        if (inst->vla_invocations.at(job_id).authority_state == bt::vla_authority_state::rejected) {
            rejected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    check(rejected, "host policy should reject the otherwise current result");
    check(inst->vla_invocations.at(job_id).authority_reason == "robot_unstable",
          "host rejection should preserve its stable reason");
    check(inst->bb.get("host-action") == nullptr, "host-rejected result must not update the action key");
    check(validator.calls == 1, "host validator should run once for the terminal proposal");
    check(validator.last_context.job_id == job_id, "host validator should receive the backend job id");
    check(validator.last_context.generation == 1, "host validator should receive the invocation generation");
    check(validator.last_context.job_key == "host-job", "host validator should receive the job key");
    check(validator.last_context.captured_context_id == "ball-A" &&
              validator.last_context.current_context_id == "ball-A",
          "host validator should receive captured and current context identity");
    check(!validator.last_context.early_result, "final result should be identified as final to the host validator");
    check(validator.last_action.u.size() == 1, "host validator should receive the proposed action");

    (void)eval_text("(bt.tick commit-host-inst "
                    "'((state 0.0) (ball-context \"ball-A\") (submit-enabled #f)))",
                    env);
    check(validator.calls == 1, "exactly-once gate must not repeat host validation after terminal rejection");
    check(inst->bb.get("host-action") == nullptr, "halted rejected result must remain unable to write an action");
    const bt::bb_entry* halted_host_job = inst->bb.get("host-job");
    check(halted_host_job && std::holds_alternative<std::monostate>(halted_host_job->value),
          "reactive branch halt should clear the rejected invocation job key");

    bool saw_host_invalid = false;
    bool saw_host_rejection = false;
    for (const std::string& line : host.events().snapshot()) {
        saw_host_invalid = saw_host_invalid ||
                           (line.find("\"type\":\"host_action_invalid\"") != std::string::npos &&
                            line.find("\"reason\":\"robot_unstable\"") != std::string::npos);
        saw_host_rejection = saw_host_rejection ||
                             (line.find("\"type\":\"vla_result\"") != std::string::npos &&
                              line.find("\"decision\":\"rejected\"") != std::string::npos &&
                              line.find("\"reason\":\"robot_unstable\"") != std::string::npos &&
                              line.find("\"host_validation\":\"rejected\"") != std::string::npos &&
                              line.find("\"host_validation_source\":\"host_callback\"") != std::string::npos);
    }
    check(saw_host_invalid, "host rejection should emit canonical host_action_invalid evidence");
    check(saw_host_rejection, "vla_result should record the host rejection and validation source");

    host.set_vla_commit_validator(nullptr);
    inst->bb.put("host-job",
                 bt::bb_value{std::monostate{}},
                 inst->tick_index,
                 std::chrono::steady_clock::now(),
                 0,
                 "commit-gate-test");
    (void)eval_text("(bt.tick commit-host-inst "
                    "'((state 0.0) (ball-context \"ball-A\") (submit-enabled #t)))",
                    env);
    check(inst->vla_invocations.size() == 1, "new generation should replace the earlier host-rejected invocation");
    const std::uint64_t unvalidated_job_id = inst->vla_invocations.begin()->first;
    check(unvalidated_job_id != job_id, "new generation should have a distinct backend job id");

    bool rejected_without_validator = false;
    for (int i = 0; i < 100; ++i) {
        (void)eval_text("(bt.tick commit-host-inst "
                        "'((state 0.0) (ball-context \"ball-A\") (submit-enabled #f)))",
                        env);
        if (inst->vla_invocations.at(unvalidated_job_id).authority_state == bt::vla_authority_state::rejected) {
            rejected_without_validator = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(rejected_without_validator, "invocation-scoped commit should fail closed without a host validator");
    check(inst->vla_invocations.at(unvalidated_job_id).authority_reason == "host_policy_rejected",
          "missing host validator should use host_policy_rejected");
    check(inst->bb.get("host-action") == nullptr, "unvalidated result must not update the action key");

    bool saw_unavailable_rejection = false;
    for (const std::string& line : host.events().snapshot()) {
        if (line.find("\"type\":\"vla_result\"") != std::string::npos &&
            line.find("\"job_id\":\"" + std::to_string(unvalidated_job_id) + "\"") != std::string::npos &&
            line.find("\"reason\":\"host_policy_rejected\"") != std::string::npos &&
            line.find("\"host_validation_source\":\"unavailable\"") != std::string::npos) {
            saw_unavailable_rejection = true;
        }
    }
    check(saw_unavailable_rejection, "vla_result should explain fail-closed missing host validation");
}

void test_vla_invocation_scoped_authority_rejects_expired_deadline() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    bt::runtime_host& host = bt::default_runtime_host();
    manual_test_clock clock(std::chrono::steady_clock::time_point{std::chrono::seconds(100)});
    host.set_clock_interface(&clock);
    auto release = std::make_shared<std::atomic<bool>>(false);
    host.vla_ref().register_backend("authority-deadline-test", std::make_shared<controlled_vla_backend>(release));

    (void)eval_text(
        "(define authority-deadline-tree "
        "  (bt.compile "
        "    '(reactive-sel "
        "       (seq (vla-wait :name \"authority-deadline\" :job_key deadline-job :action_key deadline-action) "
        "            (succeed)) "
        "       (seq (cond bb-truthy submit-enabled) "
        "            (vla-request :name \"authority-deadline\" :job_key deadline-job :instruction \"approach\" "
        "                         :state_key state :model_name \"authority-deadline-test\" :deadline_ms 1000 :dims 1 "
        "                         :acceptance_policy invocation_scoped :context_key ball-context)))))",
        env);
    (void)eval_text("(define authority-deadline-inst (bt.new-instance authority-deadline-tree))", env);

    (void)eval_text(
        "(bt.tick authority-deadline-inst '((state 0.0) (ball-context \"ball-A\") (submit-enabled #t)))", env);
    bt::instance* inst = host.find_instance(bt_handle(eval_text("authority-deadline-inst", env)));
    check(inst != nullptr && inst->vla_invocations.size() == 1, "deadline test should submit one invocation");
    const std::uint64_t job_id = inst->vla_invocations.begin()->first;
    (void)eval_text(
        "(bt.tick authority-deadline-inst '((state 0.0) (ball-context \"ball-A\") (submit-enabled #f)))", env);

    clock.advance(std::chrono::milliseconds(1001));
    release->store(true);
    bool rejected = false;
    for (int i = 0; i < 100; ++i) {
        (void)eval_text(
            "(bt.tick authority-deadline-inst '((state 0.0) (ball-context \"ball-A\") (submit-enabled #f)))", env);
        const bt::vla_invocation& invocation = inst->vla_invocations.at(job_id);
        if (invocation.authority_state == bt::vla_authority_state::rejected) {
            rejected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(rejected, "result after the tracked monotonic deadline should be rejected");
    check(inst->vla_invocations.at(job_id).authority_reason == "deadline_expired",
          "expired invocation should use the stable deadline_expired reason");
    check(inst->bb.get("deadline-action") == nullptr, "expired result must not update the action key");
    host.set_clock_interface(nullptr);
}

void test_vla_invocation_scoped_authority_rejects_changed_context_and_increments_generation() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    bt::runtime_host& host = bt::default_runtime_host();
    auto release = std::make_shared<std::atomic<bool>>(false);
    host.vla_ref().register_backend("authority-context-test", std::make_shared<controlled_vla_backend>(release));

    (void)eval_text(
        "(define authority-context-tree "
        "  (bt.compile "
        "    '(reactive-sel "
        "       (seq (vla-wait :name \"authority-context\" :job_key context-job :action_key context-action) "
        "            (succeed)) "
        "       (seq (cond bb-truthy submit-enabled) "
        "            (vla-request :name \"authority-context\" :job_key context-job :instruction \"approach\" "
        "                         :state_key state :model_name \"authority-context-test\" :deadline_ms 1000 :dims 1 "
        "                         :acceptance_policy invocation_scoped :context_key ball-context)))))",
        env);
    (void)eval_text("(define authority-context-inst (bt.new-instance authority-context-tree))", env);

    (void)eval_text(
        "(bt.tick authority-context-inst '((state 0.0) (ball-context \"ball-A\") (submit-enabled #t)))", env);
    bt::instance* inst = host.find_instance(bt_handle(eval_text("authority-context-inst", env)));
    check(inst != nullptr && inst->vla_invocations.size() == 1, "changed-context test should submit one invocation");
    const std::uint64_t first_job_id = inst->vla_invocations.begin()->first;

    (void)eval_text(
        "(bt.tick authority-context-inst '((state 0.0) (ball-context \"ball-B\") (submit-enabled #f)))", env);
    check(inst->vla_invocations.at(first_job_id).authority_state == bt::vla_authority_state::active,
          "context change while running should be decided at the result commit point");

    release->store(true);
    bool rejected = false;
    for (int i = 0; i < 100; ++i) {
        (void)eval_text(
            "(bt.tick authority-context-inst '((state 0.0) (ball-context \"ball-B\") (submit-enabled #f)))", env);
        const bt::vla_invocation& first_invocation = inst->vla_invocations.at(first_job_id);
        if (first_invocation.authority_state == bt::vla_authority_state::rejected) {
            rejected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(rejected, "result captured for ball-A should be rejected when ball-B is current");
    check(inst->vla_invocations.at(first_job_id).authority_reason == "context_changed",
          "changed context should use the stable context_changed reason");
    check(inst->bb.get("context-action") == nullptr, "rejected result must not update the action key");

    (void)eval_text(
        "(bt.tick authority-context-inst '((state 0.0) (ball-context \"ball-B\") (submit-enabled #t)))", env);
    check(inst->vla_generations.at("context-job") == 2, "re-entry with the same job key should increment generation");

    bool saw_context_rejection = false;
    for (const std::string& line : host.events().snapshot()) {
        if (line.find("\"type\":\"vla_result\"") != std::string::npos &&
            line.find("\"decision\":\"rejected\"") != std::string::npos &&
            line.find("\"reason\":\"context_changed\"") != std::string::npos &&
            line.find("\"captured_context_id\":\"ball-A\"") != std::string::npos &&
            line.find("\"current_context_id\":\"ball-B\"") != std::string::npos) {
            saw_context_rejection = true;
        }
    }
    check(saw_context_rejection, "canonical result event should explain the changed-context rejection");
}

void test_vla_invocation_scoped_authority_revokes_on_higher_priority_preemption() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    bt::runtime_host& host = bt::default_runtime_host();
    auto release = std::make_shared<std::atomic<bool>>(false);
    host.vla_ref().register_backend("authority-preempt-test", std::make_shared<controlled_vla_backend>(release));

    (void)eval_text(
        "(define authority-preempt-tree "
        "  (bt.compile "
        "    '(reactive-sel "
        "       (seq (cond bb-truthy emergency) (succeed)) "
        "       (seq (vla-wait :name \"authority-preempt\" :job_key preempt-job :action_key preempt-action "
        "                      :meta_key preempt-meta) "
        "            (succeed)) "
        "       (vla-request :name \"authority-preempt\" :job_key preempt-job :instruction \"approach\" "
        "                    :state_key state :model_name \"authority-preempt-test\" :deadline_ms 1000 :dims 1 "
        "                    :acceptance_policy invocation_scoped :context_key ball-context))))",
        env);
    (void)eval_text("(define authority-preempt-inst (bt.new-instance authority-preempt-tree))", env);

    (void)eval_text(
        "(bt.tick authority-preempt-inst '((state 0.0) (ball-context \"ball-A\") (emergency #f)))", env);
    bt::instance* inst = host.find_instance(bt_handle(eval_text("authority-preempt-inst", env)));
    check(inst != nullptr && inst->vla_invocations.size() == 1, "pre-emption test should submit one invocation");
    const std::uint64_t job_id = inst->vla_invocations.begin()->first;

    (void)eval_text(
        "(bt.tick authority-preempt-inst '((state 0.0) (ball-context \"ball-A\") (emergency #f)))", env);
    check(inst->vla_invocations.at(job_id).authority_state == bt::vla_authority_state::active,
          "request-to-wait hand-off should keep authority active");
    check(inst->vla_invocations.at(job_id).action_key == "preempt-action" &&
              inst->vla_invocations.at(job_id).meta_key == "preempt-meta",
          "vla-wait should attach result keys to the invocation");
    inst->bb.put("preempt-action",
                 bt::bb_value{0.75},
                 inst->tick_index,
                 std::chrono::steady_clock::now(),
                 0,
                 "pre-emption-test");

    value interrupted = eval_text(
        "(bt.tick authority-preempt-inst '((state 0.0) (ball-context \"ball-A\") (emergency #t)))", env);
    check(is_symbol(interrupted) && symbol_name(interrupted) == "success",
          "higher-priority emergency branch should take control");
    const bt::vla_invocation& revoked = inst->vla_invocations.at(job_id);
    check(revoked.authority_state == bt::vla_authority_state::revoked,
          "higher-priority pre-emption should revoke invocation authority");
    check(revoked.authority_reason == "branch_revoked", "pre-emption should use the stable branch_revoked reason");
    check(revoked.cancel_requested, "authority revocation should request best-effort backend cancellation");
    check(inst->active_vla_jobs.empty(), "pre-emption should remove the job from active VLA tracking");
    const bt::bb_entry* cleared_job = inst->bb.get("preempt-job");
    const bt::bb_entry* cleared_action = inst->bb.get("preempt-action");
    const bt::bb_entry* cleared_meta = inst->bb.get("preempt-meta");
    check(cleared_job && std::holds_alternative<std::monostate>(cleared_job->value),
          "pre-emption should clear the invocation job key");
    check(cleared_action && std::holds_alternative<std::monostate>(cleared_action->value),
          "pre-emption should clear the invocation action key");
    check(cleared_meta && std::holds_alternative<std::monostate>(cleared_meta->value),
          "pre-emption should clear the invocation metadata key");

    bool saw_revocation = false;
    bool saw_job_delete = false;
    bool saw_action_delete = false;
    bool saw_meta_delete = false;
    for (const std::string& line : host.events().snapshot()) {
        if (line.find("\"type\":\"async_authority_revoked\"") != std::string::npos &&
            line.find("\"reason\":\"branch_revoked\"") != std::string::npos &&
            line.find("\"generation\":1") != std::string::npos &&
            line.find("\"authority_state\":\"revoked\"") != std::string::npos) {
            saw_revocation = true;
        }
        saw_job_delete = saw_job_delete ||
                         (line.find("\"type\":\"bb_delete\"") != std::string::npos &&
                          line.find("\"key\":\"preempt-job\"") != std::string::npos);
        saw_action_delete = saw_action_delete ||
                            (line.find("\"type\":\"bb_delete\"") != std::string::npos &&
                             line.find("\"key\":\"preempt-action\"") != std::string::npos);
        saw_meta_delete = saw_meta_delete ||
                          (line.find("\"type\":\"bb_delete\"") != std::string::npos &&
                           line.find("\"key\":\"preempt-meta\"") != std::string::npos);
    }
    check(saw_revocation, "canonical event stream should record branch authority revocation");
    check(saw_job_delete && saw_action_delete && saw_meta_delete,
          "canonical blackboard events should record all pre-emption cleanup");
}

void test_vla_reset_revokes_running_work_and_clears_keys() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();
    bt::runtime_host& host = bt::default_runtime_host();
    auto release = std::make_shared<std::atomic<bool>>(false);
    host.vla_ref().register_backend("reset-cleanup-test", std::make_shared<controlled_vla_backend>(release));

    (void)eval_text(
        "(define reset-cleanup-tree "
        "  (bt.compile "
        "    '(reactive-sel "
        "       (seq (vla-wait :name \"reset-cleanup\" :job_key reset-job :action_key reset-action "
        "                      :meta_key reset-meta) "
        "            (succeed)) "
        "       (vla-request :name \"reset-cleanup\" :job_key reset-job :instruction \"approach\" "
        "                    :state_key state :model_name \"reset-cleanup-test\" :deadline_ms 1000 :dims 1))))",
        env);
    (void)eval_text("(define reset-cleanup-inst (bt.new-instance reset-cleanup-tree))", env);
    const std::int64_t instance_handle = bt_handle(eval_text("reset-cleanup-inst", env));

    (void)eval_text("(bt.tick reset-cleanup-inst '((state 0.0)))", env);
    bt::instance* inst = host.find_instance(instance_handle);
    check(inst != nullptr && inst->vla_invocations.size() == 1, "reset cleanup test should submit one invocation");
    const std::uint64_t job_id = inst->vla_invocations.begin()->first;
    (void)eval_text("(bt.tick reset-cleanup-inst '((state 0.0)))", env);
    check(inst->vla_invocations.at(job_id).acceptance_policy == bt::vla_acceptance_policy::deadline_only,
          "reset cleanup should also cover the compatibility policy");
    check(inst->vla_invocations.at(job_id).authority_node != inst->vla_invocations.at(job_id).requesting_node,
          "wait node should own the running job before reset");
    inst->bb.put("reset-action",
                 bt::bb_value{0.5},
                 inst->tick_index,
                 std::chrono::steady_clock::now(),
                 0,
                 "reset-test");

    host.reset_instance(instance_handle);

    check(inst->vla_invocations.empty(), "reset should clear VLA invocation records after revocation");
    check(inst->vla_generations.empty(), "reset should clear VLA generation records");
    check(inst->active_vla_jobs.empty(), "reset should clear active VLA job tracking");
    check(inst->bb.get("reset-job") == nullptr && inst->bb.get("reset-action") == nullptr &&
              inst->bb.get("reset-meta") == nullptr,
          "reset should clear job, action and metadata blackboard keys");

    bool backend_cancelled = false;
    for (int i = 0; i < 100; ++i) {
        if (host.vla_ref().poll(job_id).status == bt::vla_job_status::cancelled) {
            backend_cancelled = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(backend_cancelled, "reset should request cancellation of the running backend job");

    bool saw_reset_revocation = false;
    bool saw_cancel_request = false;
    bool saw_cancel_acknowledgement = false;
    bool saw_job_delete = false;
    bool saw_action_delete = false;
    bool saw_meta_delete = false;
    for (const std::string& line : host.events().snapshot()) {
        saw_reset_revocation = saw_reset_revocation ||
                               (line.find("\"type\":\"async_authority_revoked\"") != std::string::npos &&
                                line.find("\"job_id\":\"" + std::to_string(job_id) + "\"") != std::string::npos &&
                                line.find("\"acceptance_policy\":\"deadline_only\"") != std::string::npos &&
                                line.find("\"detail\":\"reset\"") != std::string::npos);
        saw_cancel_request = saw_cancel_request ||
                             (line.find("\"type\":\"async_cancel_requested\"") != std::string::npos &&
                              line.find("\"job_id\":\"" + std::to_string(job_id) + "\"") != std::string::npos);
        saw_cancel_acknowledgement = saw_cancel_acknowledgement ||
                                     (line.find("\"type\":\"async_cancel_acknowledged\"") != std::string::npos &&
                                      line.find("\"job_id\":\"" + std::to_string(job_id) + "\"") !=
                                          std::string::npos);
        saw_job_delete = saw_job_delete ||
                         (line.find("\"type\":\"bb_delete\"") != std::string::npos &&
                          line.find("\"key\":\"reset-job\"") != std::string::npos);
        saw_action_delete = saw_action_delete ||
                            (line.find("\"type\":\"bb_delete\"") != std::string::npos &&
                             line.find("\"key\":\"reset-action\"") != std::string::npos);
        saw_meta_delete = saw_meta_delete ||
                          (line.find("\"type\":\"bb_delete\"") != std::string::npos &&
                           line.find("\"key\":\"reset-meta\"") != std::string::npos);
    }
    check(saw_reset_revocation, "reset should record logical revocation before deleting invocation state");
    check(saw_cancel_request && saw_cancel_acknowledgement,
          "reset should record backend cancellation request and acknowledgement");
    check(saw_job_delete && saw_action_delete && saw_meta_delete,
          "reset should record deletion of every tracked invocation key");
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

void test_bt_node_option_metadata() {
    const bt::node_option_schema* plan = bt::find_node_option_schema("plan-action");
    check(plan != nullptr, "plan-action option schema should be registered");
    check(bt::find_node_option_spec(*plan, ":planner") != nullptr, "plan-action should expose :planner option");
    const bt::node_option_spec* budget = bt::find_node_option_spec(*plan, ":budget_ms");
    check(budget != nullptr, "plan-action should expose :budget_ms option");
    check(budget->kind == bt::option_value_kind::integer, "plan-action :budget_ms should be integer");
    check(budget->default_value == "20", "plan-action :budget_ms default should be 20");
    check(bt::canonical_node_option_name(*plan, ":iters_max") == ":work_max",
          "plan-action should canonicalise :iters_max to :work_max");
    check(bt::canonical_node_option_name(*plan, ":fallback_action") == ":safe_action",
          "plan-action should canonicalise :fallback_action to :safe_action");

    const bt::node_option_schema* request = bt::find_node_option_schema("vla-request");
    check(request != nullptr, "vla-request option schema should be registered");
    check(bt::canonical_node_option_name(*request, ":budget_ms") == ":deadline_ms",
          "vla-request should canonicalise :budget_ms to :deadline_ms");
    const bt::node_option_spec* capability = bt::find_node_option_spec(*request, ":capability");
    check(capability != nullptr, "vla-request should expose :capability option");
    check(capability->default_value == "vla.rt2", "vla-request :capability default should be vla.rt2");
    const bt::node_option_spec* acceptance_policy = bt::find_node_option_spec(*request, ":acceptance_policy");
    check(acceptance_policy != nullptr, "vla-request should expose :acceptance_policy option");
    check(acceptance_policy->default_value == "deadline_only",
          "vla-request :acceptance_policy default should preserve deadline-only behaviour");
    check(bt::find_node_option_spec(*request, ":context_key") != nullptr,
          "vla-request should expose :context_key option");
    check(bt::find_node_option_spec(*request, ":action_frame") != nullptr,
          "vla-request should expose :action_frame option");

    const bt::node_option_schema* wait = bt::find_node_option_schema("vla-wait");
    check(wait != nullptr, "vla-wait option schema should be registered");
    const bt::node_option_spec* clear_job = bt::find_node_option_spec(*wait, ":clear_job");
    check(clear_job != nullptr, "vla-wait should expose :clear_job option");
    check(clear_job->kind == bt::option_value_kind::boolean, "vla-wait :clear_job should be boolean");
    check(clear_job->default_value == "true", "vla-wait :clear_job default should be true");

    const bt::node_option_schema* cancel = bt::find_node_option_schema("vla-cancel");
    check(cancel != nullptr, "vla-cancel option schema should be registered");
    check(bt::find_node_option_spec(*cancel, ":job_key") != nullptr, "vla-cancel should expose :job_key option");
    check(bt::find_node_option_schema("seq") == nullptr, "plain composites should not expose option schemas");
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
    check(is_nil(eval_text("(events.set-flush-each-message #t)", env)),
          "events.set-flush-each-message should return nil");
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

void test_tick_audit_event_emission() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_global_env();

    check(is_nil(eval_text("(events.enable #t)", env)), "events.enable should return nil for tick audit test");
    check(is_nil(eval_text("(events.enable-tick-audit #t)", env)),
          "events.enable-tick-audit should return nil when enabling");
    check(is_nil(eval_text("(events.set-ring-size 128)", env)), "events.set-ring-size should return nil");

    (void)eval_text("(define tree (bt.compile '(seq (act bb-put-int foo 42) (cond bb-has foo))))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success", "tick audit test tree should succeed");

    value dumped = eval_text("(events.dump 80)", env);
    check(is_proper_list(dumped), "events.dump should return list for tick audit test");
    bool saw_tick_audit = false;
    bool saw_schema = false;
    bool saw_tick_id = false;
    bool saw_root_status = false;
    bool saw_node_path = false;
    bool saw_logging_mode = false;
    bool saw_audit_mode = false;
    bool saw_tick_ok = false;
    for (const auto& row : vector_from_list(dumped)) {
        check(is_string(row), "tick audit events.dump rows should be JSON strings");
        const std::string line = string_value(row);
        if (line.find("\"type\":\"tick_ok\"") != std::string::npos) {
            saw_tick_ok = true;
        }
        if (line.find("\"type\":\"tick_audit\"") == std::string::npos) {
            continue;
        }
        saw_tick_audit = true;
        saw_schema = saw_schema || line.find("\"schema_version\":\"tick_audit.v1\"") != std::string::npos;
        saw_tick_id = saw_tick_id || line.find("\"tick_id\":1") != std::string::npos;
        saw_root_status = saw_root_status || line.find("\"root_status\":\"success\"") != std::string::npos;
        saw_node_path = saw_node_path || line.find("\"node_path\":[") != std::string::npos;
        saw_logging_mode = saw_logging_mode || line.find("\"logging_mode\":{") != std::string::npos;
        saw_audit_mode = saw_audit_mode || line.find("\"audit_mode\":{") != std::string::npos;
    }

    check(saw_tick_audit, "events should include tick_audit when enabled");
    check(saw_schema, "tick_audit should include schema version");
    check(saw_tick_id, "tick_audit should include tick_id");
    check(saw_root_status, "tick_audit should include root status");
    check(saw_node_path, "tick_audit should include node_path");
    check(saw_logging_mode, "tick_audit should include logging_mode");
    check(saw_audit_mode, "tick_audit should include audit_mode");
    check(saw_tick_ok, "events should include compact tick_ok outcome");

    check(is_nil(eval_text("(events.enable-tick-audit #f)", env)),
          "events.enable-tick-audit should return nil when disabling");
}

void test_tick_audit_marks_in_tick_gc_as_violation() {
    using namespace muslisp;

    reset_bt_runtime_host();
    default_gc().set_policy(gc_policy::default_policy);
    bt::runtime_host& host = bt::default_runtime_host();
    host.callbacks().register_action(
        "test-force-gc",
        [](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
            muslisp::default_gc().collect();
            return bt::status::success;
        });

    env_ptr env = create_global_env();
    check(is_nil(eval_text("(events.enable #t)", env)), "events.enable should return nil for in-tick GC audit test");
    check(is_nil(eval_text("(events.enable-tick-audit #t)", env)),
          "events.enable-tick-audit should return nil for in-tick GC audit test");
    check(is_nil(eval_text("(events.set-ring-size 256)", env)), "events.set-ring-size should return nil");

    (void)eval_text("(define tree (bt.compile '(seq (act test-force-gc) (succeed))))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);
    check(symbol_name(eval_text("(bt.tick inst)", env)) == "success", "forced-GC tick should complete in default policy");

    bool saw_gc_begin_in_tick = false;
    bool saw_gc_end_in_tick = false;
    bool saw_tick_gc_violation = false;
    for (const auto& row : vector_from_list(eval_text("(events.dump 120)", env))) {
        check(is_string(row), "in-tick GC audit rows should be strings");
        const std::string line = string_value(row);
        if (line.find("\"type\":\"gc_begin\"") != std::string::npos &&
            line.find("\"in_tick\":true") != std::string::npos) {
            saw_gc_begin_in_tick = true;
        }
        if (line.find("\"type\":\"gc_end\"") != std::string::npos &&
            line.find("\"in_tick\":true") != std::string::npos) {
            saw_gc_end_in_tick = true;
        }
        if (line.find("\"type\":\"tick_audit\"") != std::string::npos &&
            line.find("\"gc_collections_delta\":1") != std::string::npos &&
            line.find("\"violation\":\"tick_gc\"") != std::string::npos) {
            saw_tick_gc_violation = true;
        }
    }

    check(saw_gc_begin_in_tick, "forced GC should emit gc_begin with in_tick=true");
    check(saw_gc_end_in_tick, "forced GC should emit gc_end with in_tick=true");
    check(saw_tick_gc_violation, "tick_audit should classify in-tick GC as tick_gc violation");
    default_gc().set_policy(gc_policy::default_policy);
}

void test_fail_on_tick_gc_prevents_in_tick_gc_lifecycle() {
    using namespace muslisp;

    reset_bt_runtime_host();
    default_gc().set_policy(gc_policy::default_policy);
    bt::runtime_host& host = bt::default_runtime_host();
    host.callbacks().register_action(
        "test-force-gc-strict",
        [](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
            muslisp::default_gc().collect();
            return bt::status::success;
        });

    env_ptr env = create_global_env();
    check(is_nil(eval_text("(events.enable #t)", env)), "events.enable should return nil for strict GC test");
    check(is_nil(eval_text("(events.enable-tick-audit #t)", env)),
          "events.enable-tick-audit should return nil for strict GC test");
    check(is_nil(eval_text("(events.set-ring-size 256)", env)), "events.set-ring-size should return nil");
    (void)eval_text("(define tree (bt.compile '(seq (act test-force-gc-strict) (succeed))))", env);
    (void)eval_text("(define inst (bt.new-instance tree))", env);

    default_gc().collect();
    host.events().clear_ring();
    default_gc().set_policy(gc_policy::fail_on_tick_gc);

    check(symbol_name(eval_text("(bt.tick inst)", env)) == "failure",
          "fail-on-tick-gc callback error should fail the tick");
    default_gc().set_policy(gc_policy::default_policy);

    bool saw_in_tick_gc_lifecycle = false;
    bool saw_strict_audit = false;
    bool saw_gc_contract_error = false;
    for (const auto& row : vector_from_list(eval_text("(events.dump 120)", env))) {
        check(is_string(row), "strict GC audit rows should be strings");
        const std::string line = string_value(row);
        if ((line.find("\"type\":\"gc_begin\"") != std::string::npos ||
             line.find("\"type\":\"gc_end\"") != std::string::npos) &&
            line.find("\"in_tick\":true") != std::string::npos) {
            saw_in_tick_gc_lifecycle = true;
        }
        if (line.find("\"type\":\"tick_audit\"") != std::string::npos &&
            line.find("\"strict_gc\":true") != std::string::npos &&
            line.find("\"gc_policy\":\"fail-on-tick-gc\"") != std::string::npos &&
            line.find("\"gc_collections_delta\":0") != std::string::npos) {
            saw_strict_audit = true;
        }
        if (line.find("\"type\":\"error\"") != std::string::npos &&
            line.find("forced collection during tick under fail-on-tick-gc policy") != std::string::npos) {
            saw_gc_contract_error = true;
        }
    }

    check(saw_gc_contract_error, "fail-on-tick-gc rejection should be logged as a runtime error");
    check(!saw_in_tick_gc_lifecycle, "fail-on-tick-gc must prevent in-tick GC lifecycle events");
    check(saw_strict_audit, "strict GC rejection should still leave a zero-GC tick_audit record");
}

void test_strict_gc_representative_ticks_have_zero_gc_delta() {
    using namespace muslisp;

    reset_bt_runtime_host();
    default_gc().set_policy(gc_policy::default_policy);
    bt::runtime_host& host = bt::default_runtime_host();
    env_ptr env = create_global_env();
    check(is_nil(eval_text("(events.enable #t)", env)), "events.enable should return nil for strict representative test");
    check(is_nil(eval_text("(events.enable-tick-audit #t)", env)),
          "events.enable-tick-audit should return nil for strict representative test");
    check(is_nil(eval_text("(events.set-ring-size 512)", env)), "events.set-ring-size should return nil");

    (void)eval_text("(define seq-tree (bt.compile '(seq (act always-success) (cond always-true) (succeed))))", env);
    (void)eval_text("(define sel-tree (bt.compile '(sel (cond always-false) (act always-success))))", env);
    (void)eval_text(
        "(define reactive-tree "
        "  (bt.compile '(reactive-seq (cond always-true) (mem-sel (act always-fail) (act always-success)))))",
        env);
    (void)eval_text("(define seq-inst (bt.new-instance seq-tree))", env);
    (void)eval_text("(define sel-inst (bt.new-instance sel-tree))", env);
    (void)eval_text("(define reactive-inst (bt.new-instance reactive-tree))", env);

    default_gc().collect();
    host.events().clear_ring();
    default_gc().set_policy(gc_policy::fail_on_tick_gc);
    check(symbol_name(eval_text("(bt.tick seq-inst)", env)) == "success", "strict seq representative tick should succeed");
    check(symbol_name(eval_text("(bt.tick sel-inst)", env)) == "success", "strict sel representative tick should succeed");
    check(symbol_name(eval_text("(bt.tick reactive-inst)", env)) == "success",
          "strict reactive representative tick should succeed");
    default_gc().set_policy(gc_policy::default_policy);

    std::size_t audit_count = 0;
    bool saw_in_tick_gc_lifecycle = false;
    bool saw_tick_gc_violation = false;
    bool all_strict_zero_delta = true;
    for (const auto& row : vector_from_list(eval_text("(events.dump 200)", env))) {
        check(is_string(row), "strict representative event rows should be strings");
        const std::string line = string_value(row);
        if ((line.find("\"type\":\"gc_begin\"") != std::string::npos ||
             line.find("\"type\":\"gc_end\"") != std::string::npos) &&
            line.find("\"in_tick\":true") != std::string::npos) {
            saw_in_tick_gc_lifecycle = true;
        }
        if (line.find("\"type\":\"tick_audit\"") == std::string::npos) {
            continue;
        }
        ++audit_count;
        if (line.find("\"violation\":\"tick_gc\"") != std::string::npos) {
            saw_tick_gc_violation = true;
        }
        all_strict_zero_delta = all_strict_zero_delta &&
                                line.find("\"strict_gc\":true") != std::string::npos &&
                                line.find("\"gc_policy\":\"fail-on-tick-gc\"") != std::string::npos &&
                                line.find("\"gc_collections_delta\":0") != std::string::npos;
    }

    check(audit_count == 3, "strict representative run should emit one tick_audit per tick");
    check(!saw_in_tick_gc_lifecycle, "strict representative run should not emit in-tick GC lifecycle events");
    check(!saw_tick_gc_violation, "strict representative run should not report tick_gc violations");
    check(all_strict_zero_delta, "strict representative tick_audit rows should report zero GC delta");
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

void test_event_payload_builders() {
    check(bt::event_payload::job_node_status("job-1", 7, "running") ==
              "{\"job_id\":\"job-1\",\"node_id\":7,\"status\":\"running\"}",
          "job_node_status payload mismatch");
    check(bt::event_payload::job_node_reason("job-2", 9, "explicit_cancel") ==
              "{\"job_id\":\"job-2\",\"node_id\":9,\"reason\":\"explicit_cancel\"}",
          "job_node_reason payload mismatch");
    check(bt::event_payload::job_node_accepted("job-2", 9, true) ==
              "{\"job_id\":\"job-2\",\"node_id\":9,\"accepted\":true}",
          "job_node_accepted payload mismatch");
    check(bt::event_payload::vla_result("job-3", 10, "ok", "fnv1a64:abc") ==
              "{\"job_id\":\"job-3\",\"node_id\":10,\"status\":\"ok\",\"digest\":\"fnv1a64:abc\"}",
          "vla_result payload mismatch");
    check(bt::event_payload::planner_call_start(3, "mcts", 20) ==
              "{\"node_id\":3,\"planner\":\"mcts\",\"budget_ms\":20}",
          "planner_call_start payload mismatch");
}

void test_event_log_file_sink_reuses_stream_and_reopens_on_path_change() {
    const std::filesystem::path first_path = temp_file_path("event_log_first", ".jsonl");
    const std::filesystem::path second_path = temp_file_path("event_log_second", ".jsonl");

    bt::event_log events(0);
    events.set_run_id("file-run");
    events.set_deterministic_time(1735689603000, 1);
    events.set_path(first_path.string());
    events.set_file_enabled(true);
    events.set_flush_on_tick_end(false);
    events.set_flush_each_message(true);

    (void)events.emit("tick_begin", 1, "{\"status\":\"one\"}");
    {
        std::ifstream in(first_path);
        check(in.good(), "expected flushed event log file to exist after first write");
        std::string first_line;
        std::getline(in, first_line);
        check(first_line.find("\"status\":\"one\"") != std::string::npos,
              "flush-each-message should make the first event visible immediately");
    }
    (void)events.emit("tick_end", 1, "{\"status\":\"two\"}");

    events.set_path(second_path.string());
    (void)events.emit("tick_end", 2, "{\"status\":\"three\"}");
    events.set_file_enabled(false);
    (void)events.emit("tick_end", 3, "{\"status\":\"four\"}");

    auto read_lines = [](const std::filesystem::path& path) {
        std::ifstream in(path);
        check(in.good(), "expected event log file to exist: " + path.string());
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        return lines;
    };

    const auto first_lines = read_lines(first_path);
    const auto second_lines = read_lines(second_path);

    check(first_lines.size() == 2, "first event log file should contain two events");
    check(second_lines.size() == 1, "second event log file should contain one event");
    check(first_lines[0].find("\"status\":\"one\"") != std::string::npos, "first file first event mismatch");
    check(first_lines[1].find("\"status\":\"two\"") != std::string::npos, "first file second event mismatch");
    check(second_lines[0].find("\"status\":\"three\"") != std::string::npos, "second file event mismatch");
    check(second_lines[0].find("\"status\":\"four\"") == std::string::npos,
          "disabled file logging should not append further events");

    std::error_code ec;
    std::filesystem::remove(first_path, ec);
    std::filesystem::remove(second_path, ec);
}

void test_event_log_concurrent_emission_preserves_sequence_order() {
    constexpr std::size_t thread_count = 8;
    constexpr std::size_t events_per_thread = 200;
    constexpr std::size_t expected_count = thread_count * events_per_thread;

    bt::event_log events(expected_count);
    events.set_run_id("concurrent-order");
    events.set_deterministic_time(1735689604000, 1);
    events.set_line_listener_queue_capacity(expected_count);
    std::vector<std::string> callback_lines;
    callback_lines.reserve(expected_count);
    events.set_line_listener(
        [&callback_lines](const std::string& line) { callback_lines.push_back(line); });

    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        workers.emplace_back([&events, &start, thread_index]() {
            while (!start.load()) {
                std::this_thread::yield();
            }
            for (std::size_t event_index = 0; event_index < events_per_thread; ++event_index) {
                const std::string payload = "{\"thread\":" + std::to_string(thread_index) +
                                            ",\"event\":" + std::to_string(event_index) + "}";
                (void)events.emit("bb_write", std::nullopt, payload);
            }
        });
    }
    start.store(true);
    for (std::thread& worker : workers) {
        worker.join();
    }

    const std::vector<std::string> lines = events.snapshot();
    check(lines.size() == expected_count, "concurrent event emission should retain every line");
    check(callback_lines.size() == expected_count,
          "concurrent listener delivery should retain every line");
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string expected_sequence = "\"seq\":" + std::to_string(index + 1) + ',';
        check(lines[index].find(expected_sequence) != std::string::npos,
              "concurrent event emission should append in sequence order at index " + std::to_string(index));
        check(callback_lines[index] == lines[index],
              "concurrent listener delivery should match ring order at index " + std::to_string(index));
    }
}

void test_event_log_listener_does_not_block_concurrent_emitters() {
    bt::event_log events(4);
    events.set_run_id("listener-concurrency");
    events.set_deterministic_time(1735689605000, 1);

    std::mutex coordination_mutex;
    std::condition_variable coordination;
    bool first_listener_started = false;
    bool second_emit_returned = false;
    bool listener_observed_second_return = false;
    std::vector<std::string> callback_lines;
    events.set_line_listener([&](const std::string& line) {
        if (line.find("\"seq\":1,") != std::string::npos) {
            std::unique_lock<std::mutex> lock(coordination_mutex);
            first_listener_started = true;
            coordination.notify_all();
            listener_observed_second_return = coordination.wait_for(
                lock, std::chrono::seconds(2), [&]() { return second_emit_returned; });
        }
        callback_lines.push_back(line);
    });

    std::thread first([&]() { (void)events.emit("bb_write", std::nullopt, "{\"source\":1}"); });
    bool listener_started = false;
    {
        std::unique_lock<std::mutex> lock(coordination_mutex);
        listener_started = coordination.wait_for(
            lock, std::chrono::seconds(2), [&]() { return first_listener_started; });
    }

    std::thread second;
    if (listener_started) {
        second = std::thread([&]() {
            (void)events.emit("bb_write", std::nullopt, "{\"source\":2}");
            {
                std::lock_guard<std::mutex> lock(coordination_mutex);
                second_emit_returned = true;
            }
            coordination.notify_all();
        });
    } else {
        std::lock_guard<std::mutex> lock(coordination_mutex);
        second_emit_returned = true;
        coordination.notify_all();
    }

    first.join();
    if (second.joinable()) {
        second.join();
    }

    check(listener_started, "first listener callback should start");
    check(listener_observed_second_return,
          "a listener callback must not prevent another emitter from returning");
    check(callback_lines.size() == 2, "listener queue should deliver both concurrent lines");
    check(callback_lines[0].find("\"seq\":1,") != std::string::npos &&
              callback_lines[1].find("\"seq\":2,") != std::string::npos,
          "listener queue should preserve canonical sequence order");
}

void test_event_log_clear_listener_waits_and_discards_queued_callbacks() {
    bt::event_log events(8);
    events.set_run_id("listener-clear");

    std::mutex coordination_mutex;
    std::condition_variable coordination;
    bool callback_started = false;
    bool release_callback = false;
    bool clear_returned = false;
    bool callback_finished = false;
    bool clear_observed_callback_finished = false;
    std::vector<std::string> callback_lines;
    events.set_line_listener([&](const std::string& line) {
        callback_lines.push_back(line);
        std::unique_lock<std::mutex> lock(coordination_mutex);
        callback_started = true;
        coordination.notify_all();
        coordination.wait(lock, [&]() { return release_callback; });
        callback_finished = true;
        coordination.notify_all();
    });

    std::thread first([&]() { (void)events.emit("bb_write", std::nullopt, "{\"source\":1}"); });
    bool callback_did_start = false;
    {
        std::unique_lock<std::mutex> lock(coordination_mutex);
        callback_did_start = coordination.wait_for(
            lock, std::chrono::seconds(2), [&]() { return callback_started; });
        if (!callback_did_start) {
            release_callback = true;
        }
    }
    coordination.notify_all();
    if (!callback_did_start) {
        first.join();
        check(false, "listener callback should start before clear synchronisation test");
    }

    std::thread second([&]() { (void)events.emit("bb_write", std::nullopt, "{\"source\":2}"); });
    second.join();
    std::thread clearer([&]() {
        events.clear_line_listener();
        {
            std::lock_guard<std::mutex> lock(coordination_mutex);
            clear_observed_callback_finished = callback_finished;
            clear_returned = true;
        }
        coordination.notify_all();
    });

    const auto clear_start_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (events.has_line_listener() && std::chrono::steady_clock::now() < clear_start_deadline) {
        std::this_thread::yield();
    }
    const bool clear_began_while_callback_active = !events.has_line_listener();
    {
        std::lock_guard<std::mutex> lock(coordination_mutex);
        release_callback = true;
    }
    coordination.notify_all();
    first.join();
    clearer.join();

    check(clear_began_while_callback_active,
          "clear_line_listener should begin while the callback remains active");
    check(clear_observed_callback_finished,
          "clear_line_listener should not return before the in-flight callback finishes");
    check(clear_returned, "clear_line_listener should return after the callback finishes");
    check(callback_lines.size() == 1,
          "clear_line_listener should discard queued callback snapshots");
    (void)events.emit("bb_write", std::nullopt, "{\"source\":3}");
    check(callback_lines.size() == 1, "cleared listener should not receive later events");

    events.set_line_listener([](const std::string&) { throw std::runtime_error("transport failed"); });
    (void)events.emit("bb_write", std::nullopt, "{\"source\":4}");
    events.clear_line_listener();
    check(events.snapshot().size() == 4,
          "listener exceptions should not prevent canonical ring delivery");
}

void test_event_log_listener_queue_is_bounded_and_reports_drops() {
    bt::event_log events(8);
    events.set_run_id("listener-overflow");
    events.set_line_listener_queue_capacity(3);
    check(events.line_listener_queue_capacity() == 3,
          "listener queue should expose its configured capacity");

    bool rejected_zero_capacity = false;
    try {
        events.set_line_listener_queue_capacity(0);
    } catch (const std::invalid_argument&) {
        rejected_zero_capacity = true;
    }
    check(rejected_zero_capacity, "listener queue should reject a zero capacity");

    std::mutex coordination_mutex;
    std::condition_variable coordination;
    bool first_callback_started = false;
    bool release_first_callback = false;
    std::vector<std::string> callback_lines;
    events.set_line_listener([&](const std::string& line) {
        {
            std::lock_guard<std::mutex> lock(coordination_mutex);
            callback_lines.push_back(line);
        }
        if (line.find("\"seq\":1,") != std::string::npos) {
            std::unique_lock<std::mutex> lock(coordination_mutex);
            first_callback_started = true;
            coordination.notify_all();
            coordination.wait(lock, [&]() { return release_first_callback; });
        }
    });

    std::thread first([&]() { (void)events.emit("bb_write", std::nullopt, "{\"source\":1}"); });
    bool callback_did_start = false;
    {
        std::unique_lock<std::mutex> lock(coordination_mutex);
        callback_did_start = coordination.wait_for(
            lock, std::chrono::seconds(2), [&]() { return first_callback_started; });
        if (!callback_did_start) {
            release_first_callback = true;
        }
    }
    coordination.notify_all();
    if (!callback_did_start) {
        first.join();
        check(false, "listener callback should start before overflow test");
    }

    (void)events.emit("bb_write", std::nullopt, "{\"source\":2}");
    (void)events.emit("bb_write", std::nullopt, "{\"source\":3}");
    (void)events.emit("bb_write", std::nullopt, "{\"source\":4}");
    (void)events.emit("bb_write", std::nullopt, "{\"source\":5}");
    const std::uint64_t dropped_count_after_overflow = events.line_listener_dropped_count();
    events.set_line_listener_queue_capacity(1);
    const std::uint64_t dropped_count_after_shrink = events.line_listener_dropped_count();

    {
        std::lock_guard<std::mutex> lock(coordination_mutex);
        release_first_callback = true;
    }
    coordination.notify_all();
    first.join();
    (void)events.emit("bb_write", std::nullopt, "{\"source\":6}");
    events.clear_line_listener();

    check(dropped_count_after_overflow == 1,
          "a full listener queue should report one dropped newest delivery");
    check(events.line_listener_queue_capacity() == 1,
          "listener queue should expose a reduced capacity");
    check(dropped_count_after_shrink == 3,
          "shrinking a populated listener queue should count discarded excess deliveries");
    const std::vector<std::string> ring_lines = events.snapshot();
    check(ring_lines.size() == 6,
          "listener overflow should not remove canonical ring records");
    check(callback_lines.size() == 3,
          "listener overflow and shrink should retain only the active, oldest queued and later delivery");
    check(callback_lines[0].find("\"seq\":1,") != std::string::npos &&
              callback_lines[1].find("\"seq\":2,") != std::string::npos &&
              callback_lines[2].find("\"seq\":6,") != std::string::npos,
          "listener shrink should remove newest queued lines and later delivery should resume in order");
    events.clear_line_listener_dropped_count();
    check(events.line_listener_dropped_count() == 0,
          "listener drop accounting should be explicitly resettable");
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
    check(host.planner_ref().has_model("flagship-goal-shared-v1"),
          "planner should register flagship-goal-shared-v1 model");

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
        "       (plan-action :name \"race\" :planner :mcts :budget_ms 20 :work_max 240 "
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

    (void)eval_text(
        "(define flagship-tree "
        "  (bt.compile "
        "    '(seq "
        "       (plan-action :name \"flagship-race\" :planner :mcts :budget_ms 20 :work_max 240 "
        "                    :model_service \"flagship-goal-shared-v1\" :state_key planner_state "
        "                    :action_key shared_action :meta_key plan-meta :action_schema \"flagship.cmd.v1\") "
        "       (act apply-action shared_action))))",
        env);
    (void)eval_text("(define flagship-inst (bt.new-instance flagship-tree))", env);
    value flagship_st = eval_text("(bt.tick flagship-inst '((planner_state (1.2 0.35 0.25 0.10))))", env);
    check(is_symbol(flagship_st) && symbol_name(flagship_st) == "success",
          "plan-action with flagship shared model should succeed");

    bt::instance* flagship_inst = host.find_instance(bt_handle(eval_text("flagship-inst", env)));
    check(flagship_inst != nullptr, "flagship plan-action instance should exist");
    const bt::bb_entry* shared_action_entry = flagship_inst->bb.get("shared_action");
    check(shared_action_entry != nullptr, "flagship plan-action should publish shared action");
    const auto* shared_action_vec = std::get_if<std::vector<double>>(&shared_action_entry->value);
    check(shared_action_vec && shared_action_vec->size() >= 2, "flagship plan-action should output [linear_x angular_z]");
    check((*shared_action_vec)[0] >= -1.0 && (*shared_action_vec)[0] <= 1.0,
          "flagship linear_x should stay within shared command range");
    check((*shared_action_vec)[1] >= -1.0 && (*shared_action_vec)[1] <= 1.0,
          "flagship angular_z should stay within shared command range");

    bt::clear_racecar_demo_state();
}

#endif

void test_shared_flagship_planner_model_in_core_runtime() {
    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();

    check(host.planner_ref().has_model("flagship-goal-shared-v1"),
          "core runtime should register flagship-goal-shared-v1 model");

    bt::planner_request request;
    request.schema_version = "planner.request.v1";
    request.planner = bt::planner_backend::mcts;
    request.model_service = "flagship-goal-shared-v1";
    request.action_schema = "flagship.cmd.v1";
    request.state = {1.0, 0.2, 0.1, 0.0};
    // Keep this check robust under sanitiser overhead. The intent is to validate
    // that the shared model is registered and returns a valid shared action, not
    // to exercise a tight wall-clock budget.
    request.budget_ms = 40;
    request.work_max = 240;

    const bt::planner_result result = host.planner_ref().plan(request);
    check(result.status == bt::planner_status::ok, "core runtime flagship planner request should succeed");
    check(result.action.u.size() == 2, "core runtime flagship planner should emit [linear_x angular_z]");
    check(result.action.u[0] >= -1.0 && result.action.u[0] <= 1.0,
          "core runtime flagship planner linear_x should stay in range");
    check(result.action.u[1] >= -1.0 && result.action.u[1] <= 1.0,
          "core runtime flagship planner angular_z should stay in range");
}

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

void test_env_run_loop_multi_episode_canonical_summary_events() {
    using namespace muslisp;

    reset_bt_runtime_host();
    auto backend = std::make_shared<test_loop_backend>(true, 1000);
    env_ptr env = create_env_with_test_loop_backend(backend);

    (void)eval_text("(env.attach \"loop-test\")", env);
    (void)eval_text(
        "(define on-tick-loop-events "
        "  (lambda (obs) "
        "    (begin "
        "      (define a (map.make)) "
        "      (map.set! a 'action_schema \"test.loop.action.v1\") "
        "      (map.set! a 'u (list 0.0)) "
        "      a)))",
        env);

    const std::filesystem::path event_log_path = temp_file_path("env_runloop_multi_episode", ".jsonl");
    const std::string event_log_lisp = lisp_string_literal(event_log_path.string());

    (void)eval_text(
        "(env.run-loop "
        "  (begin "
        "    (define cfg (map.make)) "
        "    (map.set! cfg 'tick_hz 1000) "
        "    (map.set! cfg 'max_ticks 99) "
        "    (map.set! cfg 'step_max 2) "
        "    (map.set! cfg 'episode_max 3) "
        "    (map.set! cfg 'stop_on_success #f) "
        "    (map.set! cfg 'event_log_path " +
            event_log_lisp +
            ") "
            "    cfg) "
            "  on-tick-loop-events)",
        env);

    std::ifstream in(event_log_path);
    check(in.good(), "expected env.run-loop multi-episode canonical event log to exist");
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }

    auto count_type = [&](std::string_view type) {
        std::size_t count = 0;
        const std::string needle = "\"type\":\"" + std::string(type) + "\"";
        for (const std::string& item : lines) {
            if (item.find(needle) != std::string::npos) {
                ++count;
            }
        }
        return count;
    };

    check(count_type("run_start") == 1, "multi-episode canonical log should emit one run_start");
    check(count_type("episode_begin") == 3, "multi-episode canonical log should emit three episode_begin events");
    check(count_type("tick_begin") == 6, "multi-episode canonical log should emit six tick_begin events");
    check(count_type("tick_end") == 6, "multi-episode canonical log should emit six tick_end events");
    check(count_type("episode_end") == 3, "multi-episode canonical log should emit three episode_end events");
    check(count_type("run_end") == 1, "multi-episode canonical log should emit one run_end");

    const std::string& last = lines.back();
    check(last.find("\"type\":\"run_end\"") != std::string::npos, "last event should be run_end");
    check(last.find("\"episodes_completed\":3") != std::string::npos, "run_end should record episodes_completed");
    check(last.find("\"steps_total\":6") != std::string::npos, "run_end should record steps_total");
    check(last.find("\"last_episode_steps\":2") != std::string::npos, "run_end should record last_episode_steps");
    check(last.find("\"status\":\"stopped\"") != std::string::npos, "run_end should record final status");
    check(last.find("\"reason\":\"episode_max reached\"") != std::string::npos, "run_end should record final reason");

    std::error_code ec;
    std::filesystem::remove(event_log_path, ec);
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

void test_env_run_loop_emits_canonical_event_log() {
    using namespace muslisp;

    reset_bt_runtime_host();
    bt::runtime_host& host = bt::default_runtime_host();
    bt::install_racecar_demo_callbacks(host);
    env_ptr env = create_env_with_pybullet_extension();

    auto adapter = std::make_shared<mock_racecar_adapter>();
    bt::set_racecar_sim_adapter(adapter);

    (void)eval_text("(env.attach \"pybullet\")", env);

    const std::filesystem::path event_log_path = temp_file_path("env_runloop_events", ".jsonl");
    const std::string event_log_lisp = lisp_string_literal(event_log_path.string());

    (void)eval_text(
        "(define on-tick-canonical "
        "  (lambda (obs) "
        "    (begin "
        "      (define a (map.make)) "
        "      (map.set! a 'action_schema \"racecar.action.v1\") "
        "      (map.set! a 'u (list 0.0 0.1)) "
        "      a)))",
        env);

    const std::string run_expr =
        "(define run-result-canonical "
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
        "      (map.set! cfg 'schema_version \"racecar.loop.v1\") "
        "      (map.set! cfg 'event_log_path " +
        event_log_lisp +
        ") "
        "      cfg) "
        "    on-tick-canonical))";
    (void)eval_text(run_expr, env);

    std::ifstream in(event_log_path);
    check(in.good(), "expected env.run-loop canonical event log to exist");
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    check(lines.size() == 6,
          "expected run_start + episode_begin + tick_begin + tick_end + episode_end + run_end canonical events");
    check(lines[0].find("\"schema\":\"mbt.evt.v1\"") != std::string::npos, "run_start line should use canonical schema");
    check(lines[0].find("\"type\":\"run_start\"") != std::string::npos, "first canonical event should be run_start");
    check(lines[1].find("\"type\":\"episode_begin\"") != std::string::npos,
          "second canonical event should be episode_begin");
    check(lines[2].find("\"type\":\"tick_begin\"") != std::string::npos, "third canonical event should be tick_begin");
    check(lines[3].find("\"type\":\"tick_end\"") != std::string::npos, "fourth canonical event should be tick_end");
    check(lines[4].find("\"type\":\"episode_end\"") != std::string::npos,
          "fifth canonical event should be episode_end");
    check(lines[5].find("\"type\":\"run_end\"") != std::string::npos, "sixth canonical event should be run_end");
    check(lines[3].find("\"schema_version\":\"racecar.loop.v1\"") != std::string::npos,
          "tick_end canonical event should include schema_version");

    std::error_code ec;
    std::filesystem::remove(event_log_path, ec);

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
using namespace muslisp;

std::string unique_nav2_action_name(const std::string& stem) {
    static std::atomic<std::uint64_t> next_id{1};
    return "/muesli_bt_" + stem + "_" + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed)) +
           "/navigate_to_pose";
}

std::string nav2_request_script(const std::string& operation,
                                const std::string& request_id,
                                const std::string& action_name,
                                const std::string& extra_fields,
                                bool include_target = true) {
    std::string script =
        "(begin "
        "  (define req (map.make)) "
        "  (map.set! req 'schema_version \"cap.navigation.request.v1\") "
        "  (map.set! req 'capability \"cap.navigation.v1\") "
        "  (map.set! req 'operation " +
        lisp_string_literal(operation) +
        ") "
        "  (map.set! req 'request_id " +
        lisp_string_literal(request_id) +
        ") "
        "  (map.set! req 'action_name " +
        lisp_string_literal(action_name) +
        ") ";
    if (include_target) {
        script +=
            "  (define target (map.make)) "
            "  (map.set! target 'frame \"map\") "
            "  (map.set! target 'x 1.25) "
            "  (map.set! target 'y -0.5) "
            "  (map.set! target 'yaw 0.5) "
            "  (map.set! req 'target target) ";
    }
    script += extra_fields + "  (cap.call req))";
    return script;
}

void check_nav2_cap_events(bool expected_host_reached) {
    const std::vector<std::string> events = bt::default_runtime_host().events().snapshot();
    bool saw_start = false;
    bool saw_end = false;
    bool saw_adapter = false;
    bool saw_host = false;
    for (const std::string& line : events) {
        saw_start = saw_start || line.find("\"type\":\"cap_call_start\"") != std::string::npos;
        saw_end = saw_end || line.find("\"type\":\"cap_call_end\"") != std::string::npos;
        saw_adapter = saw_adapter || line.find("\"adapter\":\"nav2\"") != std::string::npos;
        saw_host = saw_host || line.find(expected_host_reached ? "\"host_reached\":true" : "\"host_reached\":false") !=
                                   std::string::npos;
    }
    check(saw_start, "Nav2 capability call should emit cap_call_start");
    check(saw_end, "Nav2 capability call should emit cap_call_end");
    check(saw_adapter, "Nav2 capability events should include adapter id");
    check(saw_host, "Nav2 capability events should include host_reached state");
}

std::optional<value> map_lookup_symbol_value(value map_obj, const std::string& key_name) {
    if (!is_map(map_obj)) {
        return std::nullopt;
    }
    map_key key;
    key.type = map_key_type::symbol;
    key.text_data = key_name;
    const auto it = map_obj->map_data.find(key);
    if (it == map_obj->map_data.end()) {
        return std::nullopt;
    }
    return it->second;
}

value wait_for_nav2_status(muslisp::env_ptr env,
                           const std::string& job_id,
                           const std::string& request_id,
                           const std::string& action_name,
                           const std::string& expected_status,
                           std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    value last = make_nil();
    gc_root_scope roots(default_gc());
    roots.add(&last);
    while (std::chrono::steady_clock::now() < deadline) {
        last = eval_text(
            nav2_request_script("status",
                                request_id,
                                action_name,
                                "  (map.set! req 'job_id " + lisp_string_literal(job_id) + ") ",
                                false),
            env);
        const std::optional<value> status_value = map_lookup_symbol_value(last, "status");
        const std::string status = status_value.has_value() && is_symbol(*status_value) ? symbol_name(*status_value) : "";
        if (status == expected_status) {
            return last;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return last;
}

value wait_for_nav2_status_with_progress(muslisp::env_ptr env,
                                         const std::string& job_id,
                                         const std::string& request_id,
                                         const std::string& action_name,
                                         const std::string& expected_status,
                                         const std::string& required_progress_field,
                                         std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    value last = make_nil();
    gc_root_scope roots(default_gc());
    roots.add(&last);
    while (std::chrono::steady_clock::now() < deadline) {
        last = eval_text(
            nav2_request_script("status",
                                request_id,
                                action_name,
                                "  (map.set! req 'job_id " + lisp_string_literal(job_id) + ") ",
                                false),
            env);
        const std::optional<value> status_value = map_lookup_symbol_value(last, "status");
        const std::string status = status_value.has_value() && is_symbol(*status_value) ? symbol_name(*status_value) : "";
        const std::optional<value> progress = map_lookup_symbol_value(last, "progress");
        if (status == expected_status && progress.has_value() && is_map(*progress) &&
            map_lookup_symbol_value(*progress, required_progress_field).has_value()) {
            return last;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return last;
}

std::string map_symbol_text(value map_obj, const std::string& key_name) {
    const std::optional<value> found = map_lookup_symbol_value(map_obj, key_name);
    if (!found.has_value()) {
        return "";
    }
    if (is_symbol(*found)) {
        return symbol_name(*found);
    }
    if (is_string(*found)) {
        return string_value(*found);
    }
    return "";
}

std::string map_string_text(value map_obj, const std::string& key_name) {
    const std::optional<value> found = map_lookup_symbol_value(map_obj, key_name);
    if (!found.has_value() || !is_string(*found)) {
        return "";
    }
    return string_value(*found);
}

bool map_bool_value(value map_obj, const std::string& key_name, bool default_value) {
    const std::optional<value> found = map_lookup_symbol_value(map_obj, key_name);
    if (!found.has_value() || !is_boolean(*found)) {
        return default_value;
    }
    return boolean_value(*found);
}

void test_ros2_nav2_capability_descriptor_and_unavailable() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();

    value desc = eval_text("(cap.describe \"cap.navigation.v1\")", env);
    check(is_map(desc), "cap.describe cap.navigation.v1 should return map for ROS2 Nav2 adapter");
    check(string_value(eval_text("(map.get (cap.describe \"cap.navigation.v1\") 'adapter_id \"\")", env)) == "nav2",
          "Nav2 capability descriptor should report nav2 adapter id");
    const auto operations = vector_from_list(eval_text("(map.get (cap.describe \"cap.navigation.v1\") 'operations nil)", env));
    bool saw_navigate = false;
    bool saw_status = false;
    bool saw_cancel = false;
    for (value op : operations) {
        saw_navigate = saw_navigate || string_value(op) == "navigate-to-pose";
        saw_status = saw_status || string_value(op) == "status";
        saw_cancel = saw_cancel || string_value(op) == "cancel";
    }
    check(saw_navigate && saw_status && saw_cancel, "Nav2 descriptor should expose navigate/status/cancel operations");

    bt::default_runtime_host().events().clear_ring();
    const std::string action_name = unique_nav2_action_name("unavailable");
    value unavailable =
        eval_text(nav2_request_script("navigate-to-pose",
                                      "nav2-unavailable",
                                      action_name,
                                      "  (map.set! req 'timeout_ms 5) "),
                  env);
    check(is_map(unavailable), "Nav2 unavailable response should be a map");
    check(map_symbol_text(unavailable, "status") == ":unavailable", "Nav2 missing server should return :unavailable");
    check(!map_bool_value(unavailable, "host_reached", true), "Nav2 missing server should not reach host");
    check_nav2_cap_events(false);
}

void test_ros2_nav2_fake_server_accept_running_and_success() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();
    const std::string action_name = unique_nav2_action_name("success");
    test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::accept_delay);

    bt::default_runtime_host().events().clear_ring();
    value accepted = eval_text(nav2_request_script("navigate-to-pose",
                                                   "nav2-accepted",
                                                   action_name,
                                                   "  (map.set! req 'timeout_ms 500) "),
                               env);
    check(map_symbol_text(accepted, "status") == ":accepted", "Nav2 accepted fake goal should return :accepted");
    const std::string job_id = map_string_text(accepted, "job_id");
    check(!job_id.empty(), "Nav2 accepted fake goal should return job_id");
    check(server.wait_for_goal_count(1, std::chrono::milliseconds(500)), "fake Nav2 server should receive one goal");
    const auto goal = server.last_goal();
    check_close(goal.pose.pose.position.x, 1.25, 1e-6, "Nav2 fake server received pose.x mismatch");
    check_close(goal.pose.pose.position.y, -0.5, 1e-6, "Nav2 fake server received pose.y mismatch");

    value running = wait_for_nav2_status_with_progress(env,
                                                       job_id,
                                                       "nav2-status-running",
                                                       action_name,
                                                       ":running",
                                                       "distance_remaining_m",
                                                       std::chrono::milliseconds(500));
    check(map_symbol_text(running, "status") == ":running", "Nav2 status should report :running before delayed success");
    const std::optional<value> progress = map_lookup_symbol_value(running, "progress");
    check(progress.has_value() && is_map(*progress), "Nav2 running status should include progress");
    const std::optional<value> distance = map_lookup_symbol_value(*progress, "distance_remaining_m");
    check(distance.has_value() && is_float(*distance), "Nav2 running status should include distance_remaining_m");
    check_close(float_value(*distance), 0.75, 1e-6, "Nav2 running status distance_remaining_m mismatch");
    const std::optional<value> recoveries = map_lookup_symbol_value(*progress, "number_of_recoveries");
    check(recoveries.has_value() && is_integer(*recoveries) && integer_value(*recoveries) == 1,
          "Nav2 running status should include number_of_recoveries");
    check(map_bool_value(running, "host_reached", false), "Nav2 running status should reach host");

    value ok = wait_for_nav2_status(env, job_id, "nav2-status-ok", action_name, ":ok", std::chrono::milliseconds(1000));
    check(map_symbol_text(ok, "status") == ":ok", "Nav2 fake server success should map to :ok");
    check_nav2_cap_events(true);
}

void test_ros2_nav2_fake_server_reject_abort_cancel_and_timeout() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();

    {
        const std::string action_name = unique_nav2_action_name("reject");
        test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::reject_goal);
        value rejected = eval_text(nav2_request_script("navigate-to-pose",
                                                       "nav2-rejected",
                                                       action_name,
                                                       "  (map.set! req 'timeout_ms 500) "),
                                   env);
        check(map_symbol_text(rejected, "status") == ":rejected", "Nav2 fake server rejection should map to :rejected");
        check(map_bool_value(rejected, "host_reached", false), "Nav2 fake server rejection should reach host");
    }

    {
        const std::string action_name = unique_nav2_action_name("abort");
        test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::accept_abort);
        value accepted = eval_text(nav2_request_script("navigate-to-pose",
                                                       "nav2-abort",
                                                       action_name,
                                                       "  (map.set! req 'timeout_ms 500) "),
                                   env);
        const std::string job_id = map_string_text(accepted, "job_id");
        value aborted = wait_for_nav2_status(env, job_id, "nav2-status-abort", action_name, ":error", std::chrono::milliseconds(1000));
        check(map_symbol_text(aborted, "status") == ":error", "Nav2 fake server abort should map to :error");
    }

    {
        const std::string action_name = unique_nav2_action_name("cancel");
        test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::accept_delay);
        value accepted = eval_text(nav2_request_script("navigate-to-pose",
                                                       "nav2-cancel",
                                                       action_name,
                                                       "  (map.set! req 'timeout_ms 500) "),
                                   env);
        const std::string job_id = map_string_text(accepted, "job_id");
        value cancelled = eval_text(
            nav2_request_script("cancel",
                                "nav2-cancel-request",
                                action_name,
                                "  (map.set! req 'job_id " + lisp_string_literal(job_id) + ") "
                                "  (map.set! req 'timeout_ms 500) ",
                                false),
            env);
        check(map_symbol_text(cancelled, "status") == ":cancelled", "Nav2 fake server cancel should map to :cancelled");
        check(server.wait_for_cancel_count(1, std::chrono::milliseconds(500)), "fake Nav2 server should observe cancel request");
    }

    {
        const std::string action_name = unique_nav2_action_name("timeout");
        test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::slow_goal_accept);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        value timeout = eval_text(nav2_request_script("navigate-to-pose",
                                                      "nav2-timeout",
                                                      action_name,
                                                      "  (map.set! req 'timeout_ms 50) "),
                                  env);
        check(map_symbol_text(timeout, "status") == ":timeout", "Nav2 slow goal acceptance should map to :timeout");
        check(map_bool_value(timeout, "host_reached", false), "Nav2 goal-accept timeout should report host reached");
        check(!map_string_text(timeout, "request_hash").empty(), "Nav2 timeout should expose request hash");
        check(!map_string_text(timeout, "response_hash").empty(), "Nav2 timeout should expose response hash");
    }
}

void test_ros2_wheeled_flagship_navigation_capability_variant_fake_server() {
    using namespace muslisp;

    reset_bt_runtime_host();
    env_ptr env = create_env_with_ros2_extension();
    const std::filesystem::path repo = find_repo_root();
    const std::string variant_path =
        lisp_string_literal((repo / "examples" / "flagship_wheeled" / "lisp" /
                             "bt_goal_flagship_nav_capability.lisp")
                                .string());
    (void)eval_text("(load " + variant_path + ")", env);

    {
        const std::string action_name = unique_nav2_action_name("flagship_success");
        test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::accept_delay);
        (void)eval_text("(define ros2-nav-flagship-inst (bt.new-instance wheeled-goal-flagship-nav-capability))", env);
        value accepted = eval_text(
            "(bt.tick ros2-nav-flagship-inst "
            " '((goal_reached #f) "
            "   (collision_imminent #f) "
            "   (nav_goal_frame \"map\") "
            "   (nav_goal_x 1.25) "
            "   (nav_goal_y -0.5) "
            "   (nav_goal_yaw 0.5) "
            "   (nav_timeout_ms 500) "
            "   (nav_action_name " +
                lisp_string_literal(action_name) +
                ")))",
            env);
        check(symbol_name(accepted) == "running", "flagship Nav2 submit should keep tree running");
        check(server.wait_for_goal_count(1, std::chrono::milliseconds(500)),
              "flagship Nav2 fake server should receive one goal");
        const auto goal = server.last_goal();
        check_close(goal.pose.pose.position.x, 1.25, 1e-6, "flagship Nav2 received pose.x mismatch");
        check_close(goal.pose.pose.position.y, -0.5, 1e-6, "flagship Nav2 received pose.y mismatch");

        value final_status = make_nil();
        gc_root_scope roots(default_gc());
        roots.add(&final_status);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        while (std::chrono::steady_clock::now() < deadline) {
            final_status = eval_text(
                "(bt.tick ros2-nav-flagship-inst "
                " '((goal_reached #f) "
                "   (collision_imminent #f)))",
                env);
            if (symbol_name(final_status) == "success") {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(symbol_name(final_status) == "success", "flagship Nav2 status should reach success");
        bt::instance* inst = bt::default_runtime_host().find_instance(bt_handle(eval_text("ros2-nav-flagship-inst", env)));
        check(inst != nullptr, "flagship Nav2 instance should exist");
        const bt::bb_entry* nav_status = inst->bb.get("nav_status");
        check(nav_status && std::get<std::string>(nav_status->value) == "ok",
              "flagship Nav2 success should store nav_status=ok");
        const bt::bb_entry* host_reached = inst->bb.get("nav_host_reached");
        check(host_reached && std::get<bool>(host_reached->value), "flagship Nav2 success should reach host");
    }

    {
        const std::string action_name = unique_nav2_action_name("flagship_cancel");
        test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::accept_delay);
        (void)eval_text("(define ros2-nav-cancel-inst (bt.new-instance wheeled-goal-flagship-nav-capability))", env);
        (void)eval_text(
            "(bt.tick ros2-nav-cancel-inst "
            " '((goal_reached #f) "
            "   (collision_imminent #f) "
            "   (nav_goal_x 1.25) "
            "   (nav_goal_y -0.5) "
            "   (nav_timeout_ms 500) "
            "   (nav_action_name " +
                lisp_string_literal(action_name) +
                ")))",
            env);
        check(server.wait_for_goal_count(1, std::chrono::milliseconds(500)),
              "flagship Nav2 cancel scenario should submit one goal");
        value cancelled = eval_text(
            "(bt.tick ros2-nav-cancel-inst "
            " '((goal_reached #f) "
            "   (collision_imminent #t) "
            "   (act_avoid (0.10 -0.35))))",
            env);
        check(symbol_name(cancelled) == "running", "flagship Nav2 collision recovery should keep running");
        check(server.wait_for_cancel_count(1, std::chrono::milliseconds(500)),
              "flagship Nav2 fake server should observe cancel request");
    }
}

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
    check(string_value(eval_text("(map.get (env.info) 'time_source \"\")", env)) == "ros_wall_time",
          "ros2 env.info time_source mismatch");
    check(string_value(eval_text("(map.get (env.info) 'obs_timestamp_source \"\")", env)) == "message_header_or_node_clock",
          "ros2 env.info obs_timestamp_source mismatch");
    check(!boolean_value(eval_text("(map.get (map.get (env.info) 'config (map.make)) 'use_sim_time #t)", env)),
          "ros2 env.info config use_sim_time mismatch");
    check(string_value(eval_text("(map.get (map.get (env.info) 'config (map.make)) 'time_source \"\")", env)) ==
              "ros_wall_time",
          "ros2 env.info config time_source mismatch");

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
        "      (map.set! cfg 'event_log_path \"logs/ros2_test_run.mbt.evt.v1.jsonl\") "
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
    value ros2_events = eval_text("(events.dump 8)", env);
    check(is_proper_list(ros2_events), "events.dump should return list after ros2 run-loop");
    const auto ros2_event_rows = vector_from_list(ros2_events);
    bool saw_run_start = false;
    bool saw_time_source = false;
    bool saw_obs_timestamp_source = false;
    for (value row : ros2_event_rows) {
        if (!is_string(row)) {
            continue;
        }
        const std::string text = string_value(row);
        if (text.find("\"type\":\"run_start\"") != std::string::npos) {
            saw_run_start = true;
            if (text.find("\"time_source\":\"ros_wall_time\"") != std::string::npos) {
                saw_time_source = true;
            }
            if (text.find("\"obs_timestamp_source\":\"message_header_or_node_clock\"") != std::string::npos) {
                saw_obs_timestamp_source = true;
            }
        }
    }
    check(saw_run_start, "ros2 env.run-loop should emit canonical run_start");
    check(saw_time_source, "ros2 env.run-loop run_start should record time_source");
    check(saw_obs_timestamp_source, "ros2 env.run-loop run_start should record obs_timestamp_source");
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
        for (int repeat = 0; repeat < 3; ++repeat) {
            harness.publish_odom(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        }
    });

    const std::string demo_script =
        "(begin "
        "  (load " + lisp_string_literal(demo_runtime.string()) + ") "
        "  (define demo-cfg (make-default-h1-demo-config)) "
        "  (map.set! demo-cfg 'topic_ns \"/h1_01\") "
        "  (map.set! demo-cfg 'realtime #t) "
        "  (map.set! demo-cfg 'tick_hz 20) "
        "  (map.set! demo-cfg 'max_ticks 8) "
        "  (map.set! demo-cfg 'step_max 8) "
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
    muslisp::cap_api_reset();
    muslisp::env_api_reset();
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
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
        {"bt representative dsl roundtrip shapes", test_bt_dsl_roundtrip_representative_shapes},
        {"bt dsl hashes logged for compiled and loaded definitions",
         test_bt_dsl_hashes_are_logged_for_compiled_and_loaded_definitions},
        {"bt slot dsl roundtrip and tick", test_bt_slot_dsl_roundtrip_and_tick},
        {"bt live subtree install and rollback", test_bt_live_subtree_install_and_rollback},
        {"bt live subtree install rejections are non destructive",
         test_bt_live_subtree_install_rejections_are_non_destructive},
        {"bt live subtree install cleans replaced running subtree",
         test_bt_live_subtree_install_cleans_replaced_running_subtree},
        {"shared flagship generated recovery variant compiles and preserves fixed recovery",
         test_shared_flagship_generated_recovery_variant_compiles_and_preserves_fixed_recovery},
        {"shared flagship navigation capability variant uses cap.navigation.v1",
         test_shared_flagship_navigation_capability_variant_uses_cap_navigation},
        {"flagship generated recovery live install reject and rollback",
         test_flagship_generated_recovery_live_install_reject_and_rollback},
        {"bt export-dot builtin", test_bt_export_dot_builtin},
        {"bt binary save/load roundtrip and validation", test_bt_binary_save_load_roundtrip_and_validation},
        {"list and predicate builtins", test_list_and_predicate_builtins},
        {"gc and stats builtins", test_gc_and_stats_builtins},
        {"gc lifecycle events", test_gc_lifecycle_events},
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
        {"capability registry call echo", test_capability_registry_call_echo},
        {"model service protocol skeleton", test_model_service_protocol_skeleton},
        {"vla builtins submit/poll/cancel/caps", test_vla_builtins_submit_poll_cancel_and_caps},
        {"vla bt nodes flow and cancel", test_vla_bt_nodes_flow_and_cancel},
        {"bt compile checks", test_bt_compile_checks},
        {"bt node option metadata", test_bt_node_option_metadata},
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
        {"tick audit event emission", test_tick_audit_event_emission},
        {"tick audit marks in-tick GC as violation", test_tick_audit_marks_in_tick_gc_as_violation},
        {"fail-on-tick-gc prevents in-tick GC lifecycle", test_fail_on_tick_gc_prevents_in_tick_gc_lifecycle},
        {"strict GC representative ticks have zero GC delta", test_strict_gc_representative_ticks_have_zero_gc_delta},
        {"bt tick with blackboard input", test_bt_tick_with_blackboard_input},
        {"env core interface unattached", test_env_core_interface_unattached},
        {"env run-loop multi-episode reset=true", test_env_run_loop_multi_episode_reset_true},
        {"env run-loop multi-episode reset=false", test_env_run_loop_multi_episode_reset_false},
        {"env run-loop multi-episode canonical summary events",
         test_env_run_loop_multi_episode_canonical_summary_events},
        {"event log deterministic mode + canonical serialisation", test_event_log_deterministic_mode_and_canonical_serialisation},
        {"event log capture stats without serialised sink", test_event_log_capture_stats_without_serialised_sink},
        {"event payload builders", test_event_payload_builders},
        {"event log file sink reuses stream and reopens on path change", test_event_log_file_sink_reuses_stream_and_reopens_on_path_change},
        {"event log concurrent emission preserves sequence order", test_event_log_concurrent_emission_preserves_sequence_order},
        {"event log listener does not block concurrent emitters", test_event_log_listener_does_not_block_concurrent_emitters},
        {"event log clear listener waits and discards queued callbacks", test_event_log_clear_listener_waits_and_discards_queued_callbacks},
        {"event log listener queue is bounded and reports drops", test_event_log_listener_queue_is_bounded_and_reports_drops},
        {"runtime host deterministic test mode", test_runtime_host_deterministic_test_mode},
        {"pybullet backend absent in core env", test_pybullet_backend_absent_in_core_env},
        {"ros2 backend absent in core env", test_ros2_backend_absent_in_core_env},
#if MUESLI_BT_WITH_PYBULLET_INTEGRATION
        {"env generic pybullet backend contract", test_env_generic_pybullet_backend_contract},
        {"env run-loop log record shape", test_env_run_loop_log_record_shape},
        {"env run-loop canonical event log", test_env_run_loop_emits_canonical_event_log},
        {"pybullet backend present with extension", test_pybullet_backend_present_with_extension},
        {"racecar run-loop contract", test_racecar_loop_contract},
        {"racecar run-loop error safe-action", test_racecar_loop_error_safe_action},
        {"racecar planner model + env.api contract", test_racecar_planner_model_and_env_api_contract},
#endif
        {"shared flagship planner model in core runtime", test_shared_flagship_planner_model_in_core_runtime},
#if MUESLI_BT_WITH_ROS2_INTEGRATION
        {"ros2 Nav2 capability descriptor and unavailable path", test_ros2_nav2_capability_descriptor_and_unavailable},
        {"ros2 Nav2 fake action server accept running and success", test_ros2_nav2_fake_server_accept_running_and_success},
        {"ros2 Nav2 fake action server reject abort cancel and timeout",
         test_ros2_nav2_fake_server_reject_abort_cancel_and_timeout},
        {"ros2 wheeled flagship navigation capability fake server",
         test_ros2_wheeled_flagship_navigation_capability_variant_fake_server},
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
        {"approach pose validator checks bounds, frame, context and stability",
         test_approach_pose_validator_checks_bounds_frame_context_and_stability},
        {"approach pose validator registers with commit gate",
         test_approach_pose_validator_registers_with_commit_gate},
        {"vla invocation authority accepts current result", test_vla_invocation_scoped_authority_accepts_current_result},
        {"vla commit gate rejects superseded generation", test_vla_commit_gate_rejects_superseded_generation},
        {"vla commit gate requires host validation and runs it once",
         test_vla_commit_gate_requires_host_validation_and_runs_it_once},
        {"vla invocation authority rejects expired deadline",
         test_vla_invocation_scoped_authority_rejects_expired_deadline},
        {"vla invocation authority rejects changed context and increments generation",
         test_vla_invocation_scoped_authority_rejects_changed_context_and_increments_generation},
        {"vla invocation authority revokes on higher-priority pre-emption",
         test_vla_invocation_scoped_authority_revokes_on_higher_priority_preemption},
        {"vla reset revokes running work and clears keys", test_vla_reset_revokes_running_work_and_clears_keys},
    };

    const auto cleanup = []() {
#if MUESLI_BT_WITH_ROS2_INTEGRATION
        muslisp::cap_api_reset();
        muslisp::env_api_reset();
        if (rclcpp::ok()) {
            rclcpp::shutdown();
        }
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
