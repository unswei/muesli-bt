#include <cmath>
#include <chrono>
#include <functional>
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
        {"list and predicate builtins", test_list_and_predicate_builtins},
        {"gc and stats builtins", test_gc_and_stats_builtins},
        {"gc during argument evaluation", test_gc_during_argument_evaluation},
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
