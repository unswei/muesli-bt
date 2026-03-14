#include <chrono>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "bt/compiler.hpp"
#include "bt/runtime_host.hpp"
#if MUESLI_BT_WITH_ROS2_INTEGRATION
#include "muslisp/env.hpp"
#include "muslisp/env_api.hpp"
#include "muslisp/error.hpp"
#include "muslisp/eval.hpp"
#include "ros2/extension.hpp"
#include "tests/ros2_test_harness.hpp"
#endif
#include "muslisp/reader.hpp"
#include "muslisp/value.hpp"
#include "tests/conformance/mock_backend.hpp"

namespace {

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

#if MUESLI_BT_WITH_ROS2_INTEGRATION
muslisp::value eval_text(const std::string& source, muslisp::env_ptr env) {
    return muslisp::eval_source(source, env);
}

muslisp::env_ptr create_env_with_ros2_extension() {
    muslisp::runtime_config config;
    config.register_extension(muslisp::integrations::ros2::make_extension());
    return muslisp::create_global_env(std::move(config));
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

std::string ros2_configure_script(const std::string& topic_ns, const std::string& extra_lines = {}) {
    return "(begin "
           "  (define cfg (map.make)) "
           "  (map.set! cfg 'control_hz 25) "
           "  (map.set! cfg 'observe_timeout_ms 50) "
           "  (map.set! cfg 'step_timeout_ms 50) "
           "  (map.set! cfg 'topic_ns " +
           lisp_string_literal(topic_ns) +
           ") "
           "  (map.set! cfg 'obs_source \"odom\") "
           "  (map.set! cfg 'action_sink \"cmd_vel\") "
           "  (map.set! cfg 'use_sim_time #f) "
           "  (map.set! cfg 'require_fresh_obs #f) "
           "  (map.set! cfg 'action_clamp \"clamp\") " +
           extra_lines +
           "  (env.configure cfg))";
}
#endif

bt::definition compile_bt(std::string_view source) {
    return bt::compile_definition(muslisp::read_one(source));
}

std::int64_t create_instance(bt::runtime_host& host, std::string_view source) {
    const std::int64_t def = host.store_definition(compile_bt(source));
    return host.create_instance(def);
}

bool line_has_type(const std::string& line, std::string_view type) {
    return line.find("\"type\":\"" + std::string(type) + "\"") != std::string::npos;
}

std::size_t count_type(const std::vector<std::string>& lines, std::string_view type) {
    std::size_t count = 0;
    for (const auto& line : lines) {
        if (line_has_type(line, type)) {
            ++count;
        }
    }
    return count;
}

bool has_type(const std::vector<std::string>& lines, std::string_view type) {
    return count_type(lines, type) > 0;
}

std::vector<std::string> filter_types(const std::vector<std::string>& lines, const std::vector<std::string>& types) {
    std::vector<std::string> out;
    for (const auto& line : lines) {
        for (const auto& type : types) {
            if (line_has_type(line, type)) {
                out.push_back(line);
                break;
            }
        }
    }
    return out;
}

std::string extract_quoted_field(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\":\"";
    const std::size_t start = line.find(needle);
    if (start == std::string::npos) {
        return "";
    }
    const std::size_t value_start = start + needle.size();
    const std::size_t value_end = line.find('"', value_start);
    if (value_end == std::string::npos) {
        return "";
    }
    return line.substr(value_start, value_end - value_start);
}

std::string extract_number_field(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    const std::size_t start = line.find(needle);
    if (start == std::string::npos) {
        return "";
    }
    std::size_t pos = start + needle.size();
    std::size_t end = pos;
    while (end < line.size() && (std::isdigit(static_cast<unsigned char>(line[end])) || line[end] == '-')) {
        ++end;
    }
    if (end == pos) {
        return "";
    }
    return line.substr(pos, end - pos);
}

std::vector<std::string> stable_trace_signature(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    for (const auto& line : lines) {
        if (line_has_type(line, "planner_call_end")) {
            const std::string planner = extract_quoted_field(line, "planner");
            const std::string status = extract_quoted_field(line, "status");
            out.push_back("planner_call_end:" + planner + ":" + status);
            continue;
        }
        if (line_has_type(line, "node_exit")) {
            const std::string node_id = extract_number_field(line, "node_id");
            const std::string status = extract_quoted_field(line, "status");
            out.push_back("node_exit:" + node_id + ":" + status);
            continue;
        }
        if (line_has_type(line, "tick_end")) {
            const std::string root_status = extract_quoted_field(line, "root_status");
            out.push_back("tick_end:" + root_status);
            continue;
        }
    }
    return out;
}

bt::vla_request make_request(const std::string& model_name,
                             const std::string& task_id,
                             std::int64_t deadline_ms,
                             std::uint64_t tick_index) {
    bt::vla_request req;
    req.capability = "vla.rt2";
    req.task_id = task_id;
    req.instruction = "move";
    req.deadline_ms = deadline_ms;
    req.model.name = model_name;
    req.model.version = "test";
    req.run_id = "conformance-run";
    req.node_name = "conformance-node";
    req.tick_index = tick_index;
    req.observation.state = {0.0};
    req.observation.timestamp_ms = 1;
    req.action_space.type = "continuous";
    req.action_space.dims = 1;
    req.action_space.bounds = {{-1.0, 1.0}};
    req.constraints.max_abs_value = 1.0;
    req.constraints.max_delta = 1.0;
    req.seed = 7;
    return req;
}

void install_burn_action(bt::runtime_host& host, conformance::stepped_clock& clock) {
    host.callbacks().register_action(
        "burn-ms",
        [&clock](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value> args) {
            const std::int64_t ms = (args.empty() || !muslisp::is_integer(args[0])) ? 1 : muslisp::integer_value(args[0]);
            clock.advance_ms(ms);
            return bt::status::success;
        });
}

void test_tick_semantics_events_balanced() {
    bt::runtime_host host;
    bt::install_demo_callbacks(host);

    const std::int64_t inst = create_instance(host, "(seq (cond always-true) (act always-success))");
    const bt::status st = host.tick_instance(inst);
    check(st == bt::status::success, "tick semantics: expected success");

    const auto lines = host.events().snapshot();
    check(!lines.empty(), "tick semantics: expected non-empty event stream");
    check(lines.front().find("\"contract_version\":\"1.0.0\"") != std::string::npos,
          "tick semantics: contract_version must be present in event envelopes");
    check(count_type(lines, "tick_begin") == 1, "tick semantics: expected one tick_begin");
    check(count_type(lines, "tick_end") == 1, "tick semantics: expected one tick_end");

    const std::size_t enters = count_type(lines, "node_enter");
    const std::size_t exits = count_type(lines, "node_exit");
    check(enters > 0, "tick semantics: expected node_enter events");
    check(enters == exits, "tick semantics: node_enter/node_exit should be balanced");
}

void test_budget_gate_blocks_planner_start() {
    bt::runtime_host host;
    bt::install_demo_callbacks(host);
    conformance::stepped_clock clock;
    host.set_clock_interface(&clock);
    install_burn_action(host, clock);

    const std::int64_t inst = create_instance(host,
                                              "(seq "
                                              "  (act burn-ms 5) "
                                              "  (plan-action :name \"budget-plan\" :planner :mcts :state_key state "
                                              "               :action_key action :budget_ms 8 :work_max 96))");

    bt::instance* inst_ptr = host.find_instance(inst);
    check(inst_ptr != nullptr, "budget gate: missing instance");
    bt::set_tick_budget_ms(*inst_ptr, 2);
    inst_ptr->bb.put("state", bt::bb_value{0.0}, 0, clock.now(), 0, "conformance");

    const bt::status st = host.tick_instance(inst);
    check(st == bt::status::failure, "budget gate: expected failure when planner call is blocked");

    const auto lines = host.events().snapshot();
    check(has_type(lines, "budget_warning"), "budget gate: expected budget_warning event");
    check(!has_type(lines, "planner_call_start"), "budget gate: planner call should not start");
}

void test_deadline_overrun_requests_async_cancellation() {
    bt::runtime_host host;
    bt::install_demo_callbacks(host);
    conformance::stepped_clock clock;
    host.set_clock_interface(&clock);
    install_burn_action(host, clock);

    auto backend = std::make_shared<conformance::scripted_vla_backend>(conformance::scripted_vla_behaviour{
        .steps_before_complete = 200,
        .step_sleep_ms = 2,
        .honour_cancel = true,
        .return_ok_after_cancel = false,
        .action_value = 0.1,
    });
    host.vla_ref().register_backend("stall-backend", backend);

    const std::int64_t inst = create_instance(
        host,
        "(sel "
        "  (seq (cond bb-has burn) (act burn-ms 8) (fail)) "
        "  (vla-request :name \"deadline-job\" :job_key job :instruction \"move\" :state_key state "
        "               :deadline_ms 400 :dims 1 :bound_lo -1.0 :bound_hi 1.0 :model_name \"stall-backend\"))");

    bt::instance* inst_ptr = host.find_instance(inst);
    check(inst_ptr != nullptr, "deadline overrun: missing instance");
    bt::set_tick_budget_ms(*inst_ptr, 2);
    inst_ptr->bb.put("state", bt::bb_value{0.0}, 0, clock.now(), 0, "conformance");

    const bt::status tick1 = host.tick_instance(inst);
    check(tick1 == bt::status::running, "deadline overrun: first tick should start async job");

    inst_ptr->bb.put("burn", bt::bb_value{true}, 1, clock.now(), 0, "conformance");
    (void)host.tick_instance(inst);

    const auto lines = host.events().snapshot();
    check(has_type(lines, "deadline_exceeded"), "deadline overrun: missing deadline_exceeded event");
    check(has_type(lines, "async_cancel_requested"), "deadline overrun: missing async_cancel_requested event");
    check(has_type(lines, "async_cancel_acknowledged"), "deadline overrun: missing async_cancel_acknowledged event");
}

void test_async_lifecycle_and_idempotent_cancel() {
    bt::thread_pool_scheduler scheduler(2);
    bt::vla_service vla(&scheduler);

    vla.register_backend("fast-ok",
                         std::make_shared<conformance::scripted_vla_backend>(conformance::scripted_vla_behaviour{
                             .steps_before_complete = 8,
                             .step_sleep_ms = 2,
                             .honour_cancel = true,
                             .return_ok_after_cancel = false,
                             .action_value = 0.3,
                         }));
    vla.register_backend("slow-cancel",
                         std::make_shared<conformance::scripted_vla_backend>(conformance::scripted_vla_behaviour{
                             .steps_before_complete = 100,
                             .step_sleep_ms = 2,
                             .honour_cancel = true,
                             .return_ok_after_cancel = false,
                             .action_value = 0.4,
                         }));
    vla.register_backend("ignore-cancel",
                         std::make_shared<conformance::scripted_vla_backend>(conformance::scripted_vla_behaviour{
                             .steps_before_complete = 16,
                             .step_sleep_ms = 2,
                             .honour_cancel = false,
                             .return_ok_after_cancel = true,
                             .action_value = 0.5,
                         }));

    const auto ok_job = vla.submit(make_request("fast-ok", "ok-task", 200, 1));
    bool saw_in_flight = false;
    bool saw_done = false;
    for (int i = 0; i < 100; ++i) {
        const bt::vla_poll poll = vla.poll(ok_job);
        saw_in_flight = saw_in_flight || poll.status == bt::vla_job_status::queued || poll.status == bt::vla_job_status::running ||
                        poll.status == bt::vla_job_status::streaming;
        if (poll.status == bt::vla_job_status::done) {
            saw_done = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(saw_in_flight, "async lifecycle: expected queued/running/streaming before completion");
    check(saw_done, "async lifecycle: expected done status");

    const auto cancel_job = vla.submit(make_request("slow-cancel", "cancel-task", 400, 2));
    check(vla.cancel(cancel_job), "async lifecycle: first cancel should succeed");
    check(vla.cancel(cancel_job), "async lifecycle: second cancel should be idempotent");

    bool cancelled_terminal = false;
    for (int i = 0; i < 120; ++i) {
        const bt::vla_poll poll = vla.poll(cancel_job);
        if (poll.status == bt::vla_job_status::cancelled) {
            cancelled_terminal = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(cancelled_terminal, "async lifecycle: expected cancelled terminal state");

    const auto deadline_job = vla.submit(make_request("slow-cancel", "deadline-task", 5, 3));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    bool saw_deadline_trigger = false;
    bool saw_deadline_cancelled = false;
    for (int i = 0; i < 120; ++i) {
        const bt::vla_poll poll = vla.poll(deadline_job);
        saw_deadline_trigger = saw_deadline_trigger || poll.status == bt::vla_job_status::timeout;
        if (poll.status == bt::vla_job_status::cancelled) {
            saw_deadline_cancelled = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(saw_deadline_trigger || saw_deadline_cancelled, "async lifecycle: expected deadline to trigger timeout/cancel");

    const auto late_job = vla.submit(make_request("ignore-cancel", "late-task", 300, 4));
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    check(vla.cancel(late_job), "async lifecycle: cancel should succeed for late-completion case");

    bool late_cancelled = false;
    for (int i = 0; i < 160; ++i) {
        const bt::vla_poll poll = vla.poll(late_job);
        if (poll.status == bt::vla_job_status::cancelled) {
            late_cancelled = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(late_cancelled, "async lifecycle: late completion should still resolve as cancelled");

    bool saw_completion_dropped = false;
    for (const auto& rec : vla.recent_records(200)) {
        if (rec.completion_dropped) {
            saw_completion_dropped = true;
            break;
        }
    }
    check(saw_completion_dropped, "async lifecycle: expected completion_dropped record for late completion");
}

std::vector<std::string> run_deterministic_trace_once() {
    bt::runtime_host host;
    bt::install_demo_callbacks(host);
    host.enable_deterministic_test_mode(424242, "conformance-determinism", 1735689603000, 1);

    const std::int64_t inst = create_instance(host,
                                              "(seq "
                                              "  (plan-action :name \"det-plan\" :planner :mcts :state_key state "
                                              "               :action_key action :budget_ms 12 :work_max 128) "
                                              "  (succeed))");
    bt::instance* inst_ptr = host.find_instance(inst);
    check(inst_ptr != nullptr, "determinism: missing instance");
    inst_ptr->bb.put("state", bt::bb_value{0.0}, 0, std::chrono::steady_clock::time_point{}, 0, "conformance");

    const bt::status st = host.tick_instance(inst);
    check(st == bt::status::success, "determinism: expected success tick");

    const auto lines = host.events().snapshot();
    return filter_types(lines, {"planner_call_start", "planner_call_end", "planner_v1", "node_exit", "tick_end"});
}

void test_determinism_trace_reproducibility() {
    const auto first = run_deterministic_trace_once();
    const auto second = run_deterministic_trace_once();
    check(!first.empty(), "determinism: expected non-empty filtered trace");
    const auto first_sig = stable_trace_signature(first);
    const auto second_sig = stable_trace_signature(second);
    check(first_sig == second_sig, "determinism: expected identical stable traces for identical seeds/inputs");
}

#if MUESLI_BT_WITH_ROS2_INTEGRATION
void test_ros2_info_surface_conformance() {
    using namespace muslisp;

    env_ptr env = create_env_with_ros2_extension();
    (void)eval_text("(env.attach \"ros2\")", env);
    (void)eval_text(
        "(begin "
        "  (define cfg (map.make)) "
        "  (map.set! cfg 'obs_schema \"ros2.obs.conformance.v1\") "
        "  (map.set! cfg 'state_schema \"ros2.state.conformance.v1\") "
        "  (map.set! cfg 'action_schema \"ros2.action.conformance.v1\") "
        "  (map.set! cfg 'control_hz 25) "
        "  (map.set! cfg 'obs_source \"odom\") "
        "  (map.set! cfg 'action_sink \"cmd_vel\") "
        "  (map.set! cfg 'reset_mode \"stub\") "
        "  (env.configure cfg))",
        env);

    check(string_value(eval_text("(map.get (env.info) 'backend \"\")", env)) == "ros2",
          "ros2 info conformance: backend mismatch");
    check(string_value(eval_text("(map.get (env.info) 'env_api \"\")", env)) == "env.api.v1",
          "ros2 info conformance: env_api mismatch");
    check(string_value(eval_text("(map.get (env.info) 'backend_version \"\")", env)) == "ros2.transport.v1",
          "ros2 info conformance: backend_version mismatch");
    check(string_value(eval_text("(map.get (env.info) 'obs_schema \"\")", env)) == "ros2.obs.conformance.v1",
          "ros2 info conformance: obs_schema mismatch");
    check(string_value(eval_text("(map.get (env.info) 'state_schema \"\")", env)) == "ros2.state.conformance.v1",
          "ros2 info conformance: state_schema mismatch");
    check(string_value(eval_text("(map.get (env.info) 'action_schema \"\")", env)) == "ros2.action.conformance.v1",
          "ros2 info conformance: action_schema mismatch");
    check(boolean_value(eval_text("(map.get (env.info) 'reset_supported #f)", env)),
          "ros2 info conformance: reset_supported mismatch");
    check(integer_value(eval_text("(map.get (map.get (env.info) 'config (map.make)) 'control_hz -1)", env)) == 25,
          "ros2 info conformance: control_hz mismatch");
}

void test_ros2_reset_policy_conformance() {
    using namespace muslisp;

    env_ptr env = create_env_with_ros2_extension();
    (void)eval_text("(env.attach \"ros2\")", env);
    (void)eval_text(
        "(begin "
        "  (define cfg (map.make)) "
        "  (map.set! cfg 'reset_mode \"unsupported\") "
        "  (env.configure cfg))",
        env);

    check(!boolean_value(eval_text("(map.get (env.info) 'reset_supported #t)", env)),
          "ros2 reset policy conformance: reset_supported should be false");

    (void)eval_text(
        "(define loop_result "
        "  (env.run-loop "
        "    (begin "
        "      (define cfg (map.make)) "
        "      (map.set! cfg 'tick_hz 1000) "
        "      (map.set! cfg 'max_ticks 1) "
        "      (map.set! cfg 'episode_max 2) "
        "      cfg) "
        "    (lambda (obs) #t)))",
        env);
    const value loop_result = eval_text("loop_result", env);
    check(is_map(loop_result), "ros2 reset policy conformance: env.run-loop should return map");
    check(symbol_name(eval_text("(map.get loop_result 'status ':none)", env)) == ":unsupported",
          "ros2 reset policy conformance: env.run-loop should return :unsupported");
}

void test_ros2_transport_conformance() {
    using namespace muslisp;

    test_support::ros2_test_harness harness("/conformance");
    env_ptr env = create_env_with_ros2_extension();
    (void)eval_text("(env.attach \"ros2\")", env);
    (void)eval_text(
        ros2_configure_script(
            harness.topic_ns(),
            "  (map.set! cfg 'obs_schema \"ros2.obs.conformance.v1\") "
            "  (map.set! cfg 'state_schema \"ros2.state.conformance.v1\") "
            "  (map.set! cfg 'action_schema \"ros2.action.conformance.v1\") "),
        env);
    check(harness.wait_for_transport_ready(std::chrono::milliseconds(500)),
          "ros2 transport conformance: transport should be ready after configure");

    harness.publish_odom(2.0, -1.0, 0.25, 0.3, -0.1, 0.05);
    (void)eval_text("(define obs_ros2 (env.observe))", env);
    check(string_value(eval_text("(map.get obs_ros2 'obs_schema \"\")", env)) == "ros2.obs.conformance.v1",
          "ros2 transport conformance: obs_schema mismatch");
    check_close(float_value(eval_text("(map.get (map.get (map.get obs_ros2 'state (map.make)) 'pose (map.make)) 'x 0.0)", env)),
                2.0,
                1e-6,
                "ros2 transport conformance: pose.x mismatch");
    check_close(float_value(eval_text("(map.get (map.get (map.get obs_ros2 'state (map.make)) 'twist (map.make)) 'vx 0.0)", env)),
                0.3,
                1e-6,
                "ros2 transport conformance: twist.vx mismatch");

    (void)eval_text(
        "(begin "
        "  (define a (map.make)) "
        "  (define u (map.make)) "
        "  (map.set! a 'action_schema \"ros2.action.conformance.v1\") "
        "  (map.set! a 't_ms 11) "
        "  (map.set! u 'linear_x 0.4) "
        "  (map.set! u 'linear_y -0.2) "
        "  (map.set! u 'angular_z 0.3) "
        "  (map.set! a 'u u) "
        "  (env.act a))",
        env);
    check(harness.wait_for_command_count(1, std::chrono::milliseconds(250)),
          "ros2 transport conformance: missing published cmd_vel");
    const auto command = harness.last_command();
    check_close(command.linear.x, 0.4, 1e-6, "ros2 transport conformance: linear.x mismatch");
    check_close(command.linear.y, -0.2, 1e-6, "ros2 transport conformance: linear.y mismatch");
    check_close(command.angular.z, 0.3, 1e-6, "ros2 transport conformance: angular.z mismatch");
}
#endif

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"tick semantics events balanced", test_tick_semantics_events_balanced},
        {"budget gate blocks planner start", test_budget_gate_blocks_planner_start},
        {"deadline overrun requests async cancellation", test_deadline_overrun_requests_async_cancellation},
        {"async lifecycle and idempotent cancel", test_async_lifecycle_and_idempotent_cancel},
        {"determinism trace reproducibility", test_determinism_trace_reproducibility},
#if MUESLI_BT_WITH_ROS2_INTEGRATION
        {"ros2 info surface conformance", test_ros2_info_surface_conformance},
        {"ros2 reset policy conformance", test_ros2_reset_policy_conformance},
        {"ros2 transport conformance", test_ros2_transport_conformance},
#endif
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
    for (const auto& [name, fn] : tests) {
        try {
            fn();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& e) {
            std::cerr << "[FAIL] " << name << ": " << e.what() << '\n';
            cleanup();
            return 1;
        }
    }

    std::cout << "All conformance tests passed (" << passed << "/" << tests.size() << ").\n";
    cleanup();
    return 0;
}
