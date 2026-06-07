#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include "bt/event_log.hpp"
#include "bt/runtime_host.hpp"
#include "muslisp/cap_api.hpp"
#include "muslisp/env.hpp"
#include "muslisp/env_api.hpp"
#include "muslisp/eval.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"
#include "muslisp/value.hpp"
#include "ros2/extension.hpp"
#include "tests/ros2_nav2_test_harness.hpp"

namespace {

using muslisp::env_ptr;
using muslisp::float_value;
using muslisp::gc_root_scope;
using muslisp::integer_value;
using muslisp::is_boolean;
using muslisp::is_float;
using muslisp::is_integer;
using muslisp::is_map;
using muslisp::is_string;
using muslisp::is_symbol;
using muslisp::make_nil;
using muslisp::map_key;
using muslisp::map_key_type;
using muslisp::string_value;
using muslisp::symbol_name;
using muslisp::value;
using muslisp::boolean_value;

struct scenario_summary {
    std::string name;
    std::vector<std::string> operations;
    std::vector<std::string> statuses;
    std::vector<bool> host_reached;
    std::vector<std::string> request_hashes;
    std::vector<std::string> response_hashes;
    std::string job_id;
    std::string progress_summary;
    std::size_t fake_server_goal_count = 0;
    std::size_t fake_server_cancel_count = 0;
    bool has_received_pose = false;
    double received_x = 0.0;
    double received_y = 0.0;
    double received_qz = 0.0;
    double received_qw = 1.0;
    std::vector<std::string> events;
};

std::string json_escape(std::string_view text) {
    return bt::event_log::json_escape(text);
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to write " + path.string());
    }
    out << text;
}

std::string lisp_string_literal(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (const char c : text) {
        switch (c) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << c;
                break;
        }
    }
    out << '"';
    return out.str();
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

bool map_bool_value(value map_obj, const std::string& key_name, bool default_value = false) {
    const std::optional<value> found = map_lookup_symbol_value(map_obj, key_name);
    if (!found.has_value() || !is_boolean(*found)) {
        return default_value;
    }
    return boolean_value(*found);
}

double map_number_value(value map_obj, const std::string& key_name, double default_value = 0.0) {
    const std::optional<value> found = map_lookup_symbol_value(map_obj, key_name);
    if (!found.has_value()) {
        return default_value;
    }
    if (is_float(*found)) {
        return float_value(*found);
    }
    if (is_integer(*found)) {
        return static_cast<double>(integer_value(*found));
    }
    return default_value;
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

env_ptr make_ros2_env(const std::string& run_id) {
    bt::runtime_host& host = bt::default_runtime_host();
    host.clear_all();
    bt::install_demo_callbacks(host);
    host.enable_deterministic_test_mode(0x4d6f6f736c694254ull, run_id, 1760000000000, 1);
    host.events().set_host_info("muesli-bt-nav2-evidence", "experimental", "ros2-humble");
    host.events().set_git_sha("fixture");
    muslisp::runtime_config config;
    config.register_extension(muslisp::integrations::ros2::make_extension());
    return muslisp::create_global_env(std::move(config));
}

void record_response(scenario_summary& summary, value response) {
    summary.operations.push_back(map_string_text(response, "operation"));
    summary.statuses.push_back(map_symbol_text(response, "status"));
    summary.host_reached.push_back(map_bool_value(response, "host_reached"));
    const std::string request_hash = map_string_text(response, "request_hash");
    if (!request_hash.empty()) {
        summary.request_hashes.push_back(request_hash);
    }
    const std::string response_hash = map_string_text(response, "response_hash");
    if (!response_hash.empty()) {
        summary.response_hashes.push_back(response_hash);
    }
    const std::string job_id = map_string_text(response, "job_id");
    if (!job_id.empty()) {
        summary.job_id = job_id;
    }
}

value eval_nav(env_ptr env, const std::string& script) {
    return muslisp::eval_source(script, env);
}

bool is_cap_call_event(const std::string& line) {
    return line.find("\"type\":\"cap_call_start\"") != std::string::npos ||
           line.find("\"type\":\"cap_call_end\"") != std::string::npos;
}

std::string replace_json_number_field(std::string line, const std::string& field, std::uint64_t value) {
    const std::string key = "\"" + field + "\":";
    const std::size_t key_pos = line.find(key);
    if (key_pos == std::string::npos) {
        return line;
    }
    const std::size_t value_begin = key_pos + key.size();
    std::size_t value_end = value_begin;
    while (value_end < line.size() && line[value_end] >= '0' && line[value_end] <= '9') {
        ++value_end;
    }
    line.replace(value_begin, value_end - value_begin, std::to_string(value));
    return line;
}

std::string normalise_representative_event(std::string line, std::size_t ordinal) {
    const std::uint64_t normalised_seq = static_cast<std::uint64_t>(ordinal);
    const std::uint64_t normalised_unix_ms = 1760000000000ull + normalised_seq - 1ull;
    line = replace_json_number_field(std::move(line), "unix_ms", normalised_unix_ms);
    return replace_json_number_field(std::move(line), "seq", normalised_seq);
}

void append_new_cap_events(scenario_summary& summary, std::size_t before_count) {
    const std::vector<std::string> snapshot = bt::default_runtime_host().events().snapshot();
    for (std::size_t i = before_count; i < snapshot.size(); ++i) {
        if (is_cap_call_event(snapshot[i])) {
            summary.events.push_back(normalise_representative_event(snapshot[i], summary.events.size() + 1));
        }
    }
}

value eval_recording(scenario_summary& summary, env_ptr env, const std::string& script) {
    const std::size_t before_count = bt::default_runtime_host().events().snapshot().size();
    value out = eval_nav(env, script);
    append_new_cap_events(summary, before_count);
    return out;
}

value wait_for_status(scenario_summary& summary,
                      env_ptr env,
                      const std::string& job_id,
                      const std::string& request_id,
                      const std::string& action_name,
                      const std::string& expected_status,
                      bool require_progress) {
    value last = make_nil();
    gc_root_scope roots(muslisp::default_gc());
    roots.add(&last);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
    while (std::chrono::steady_clock::now() < deadline) {
        const std::size_t before_count = bt::default_runtime_host().events().snapshot().size();
        last = eval_nav(env,
                        nav2_request_script("status",
                                            request_id,
                                            action_name,
                                            "  (map.set! req 'job_id " + lisp_string_literal(job_id) + ") ",
                                            false));
        const std::string status = map_symbol_text(last, "status");
        const std::optional<value> progress = map_lookup_symbol_value(last, "progress");
        const bool has_progress = progress.has_value() && is_map(*progress) &&
                                  map_lookup_symbol_value(*progress, "distance_remaining_m").has_value();
        if (status == expected_status && (!require_progress || has_progress)) {
            append_new_cap_events(summary, before_count);
            return last;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return last;
}

std::string progress_summary(value response) {
    const std::optional<value> progress = map_lookup_symbol_value(response, "progress");
    if (!progress.has_value() || !is_map(*progress)) {
        return "";
    }
    std::ostringstream out;
    out << "distance_remaining_m=" << map_number_value(*progress, "distance_remaining_m")
        << ";number_of_recoveries=" << static_cast<std::int64_t>(map_number_value(*progress, "number_of_recoveries"))
        << ";navigation_time_ms=" << static_cast<std::int64_t>(map_number_value(*progress, "navigation_time_ms"))
        << ";estimated_time_remaining_ms="
        << static_cast<std::int64_t>(map_number_value(*progress, "estimated_time_remaining_ms"));
    return out.str();
}

void record_pose(scenario_summary& summary, const nav2_msgs::action::NavigateToPose::Goal& goal) {
    summary.has_received_pose = true;
    summary.received_x = goal.pose.pose.position.x;
    summary.received_y = goal.pose.pose.position.y;
    summary.received_qz = goal.pose.pose.orientation.z;
    summary.received_qw = goal.pose.pose.orientation.w;
}

void finish_scenario(scenario_summary& summary) {
    (void)summary;
    muslisp::cap_api_reset();
    muslisp::env_api_reset();
}

scenario_summary run_accepted_success() {
    scenario_summary summary;
    summary.name = "accepted_success";
    const std::string action_name = "/muesli_bt_nav2_evidence/accepted_success/navigate_to_pose";
    env_ptr env = make_ros2_env("nav2-evidence-accepted-success");
    test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::accept_delay);
    gc_root_scope roots(muslisp::default_gc());

    value accepted = eval_recording(summary,
                                    env,
                                    nav2_request_script("navigate-to-pose",
                                                        "nav2-evidence-accepted-success",
                                                        action_name,
                                                        "  (map.set! req 'timeout_ms 500) "));
    roots.add(&accepted);
    record_response(summary, accepted);
    if (!server.wait_for_goal_count(1, std::chrono::milliseconds(500))) {
        throw std::runtime_error("accepted_success fake server did not receive goal");
    }
    record_pose(summary, server.last_goal());

    value running = wait_for_status(summary,
                                    env,
                                    summary.job_id,
                                    "nav2-evidence-running",
                                    action_name,
                                    ":running",
                                    true);
    roots.add(&running);
    record_response(summary, running);
    summary.progress_summary = progress_summary(running);

    value ok = wait_for_status(summary, env, summary.job_id, "nav2-evidence-ok", action_name, ":ok", false);
    roots.add(&ok);
    record_response(summary, ok);
    summary.fake_server_goal_count = server.goal_count();
    summary.fake_server_cancel_count = server.cancel_count();
    finish_scenario(summary);
    return summary;
}

scenario_summary run_rejected() {
    scenario_summary summary;
    summary.name = "rejected";
    const std::string action_name = "/muesli_bt_nav2_evidence/rejected/navigate_to_pose";
    env_ptr env = make_ros2_env("nav2-evidence-rejected");
    test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::reject_goal);
    gc_root_scope roots(muslisp::default_gc());
    value rejected = eval_recording(summary,
                                    env,
                                    nav2_request_script("navigate-to-pose",
                                                        "nav2-evidence-rejected",
                                                        action_name,
                                                        "  (map.set! req 'timeout_ms 500) "));
    roots.add(&rejected);
    record_response(summary, rejected);
    if (!server.wait_for_goal_count(1, std::chrono::milliseconds(500))) {
        throw std::runtime_error("rejected fake server did not receive goal");
    }
    record_pose(summary, server.last_goal());
    summary.fake_server_goal_count = server.goal_count();
    summary.fake_server_cancel_count = server.cancel_count();
    finish_scenario(summary);
    return summary;
}

scenario_summary run_abort_error() {
    scenario_summary summary;
    summary.name = "abort_error";
    const std::string action_name = "/muesli_bt_nav2_evidence/abort_error/navigate_to_pose";
    env_ptr env = make_ros2_env("nav2-evidence-abort-error");
    test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::accept_abort);
    gc_root_scope roots(muslisp::default_gc());
    value accepted = eval_recording(summary,
                                    env,
                                    nav2_request_script("navigate-to-pose",
                                                        "nav2-evidence-abort",
                                                        action_name,
                                                        "  (map.set! req 'timeout_ms 500) "));
    roots.add(&accepted);
    record_response(summary, accepted);
    if (!server.wait_for_goal_count(1, std::chrono::milliseconds(500))) {
        throw std::runtime_error("abort_error fake server did not receive goal");
    }
    record_pose(summary, server.last_goal());
    value error =
        wait_for_status(summary, env, summary.job_id, "nav2-evidence-abort-status", action_name, ":error", false);
    roots.add(&error);
    record_response(summary, error);
    summary.fake_server_goal_count = server.goal_count();
    summary.fake_server_cancel_count = server.cancel_count();
    finish_scenario(summary);
    return summary;
}

scenario_summary run_cancelled() {
    scenario_summary summary;
    summary.name = "cancelled";
    const std::string action_name = "/muesli_bt_nav2_evidence/cancelled/navigate_to_pose";
    env_ptr env = make_ros2_env("nav2-evidence-cancelled");
    test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::accept_delay);
    gc_root_scope roots(muslisp::default_gc());
    value accepted = eval_recording(summary,
                                    env,
                                    nav2_request_script("navigate-to-pose",
                                                        "nav2-evidence-cancelled",
                                                        action_name,
                                                        "  (map.set! req 'timeout_ms 500) "));
    roots.add(&accepted);
    record_response(summary, accepted);
    if (!server.wait_for_goal_count(1, std::chrono::milliseconds(500))) {
        throw std::runtime_error("cancelled fake server did not receive goal");
    }
    record_pose(summary, server.last_goal());
    value cancelled = eval_recording(summary,
                                     env,
                                     nav2_request_script("cancel",
                                                         "nav2-evidence-cancel-request",
                                                         action_name,
                                                         "  (map.set! req 'job_id " +
                                                             lisp_string_literal(summary.job_id) + ") "
                                                                 "  (map.set! req 'timeout_ms 500) ",
                                                         false));
    roots.add(&cancelled);
    record_response(summary, cancelled);
    if (!server.wait_for_cancel_count(1, std::chrono::milliseconds(500))) {
        throw std::runtime_error("cancelled fake server did not observe cancel");
    }
    summary.fake_server_goal_count = server.goal_count();
    summary.fake_server_cancel_count = server.cancel_count();
    finish_scenario(summary);
    return summary;
}

scenario_summary run_unavailable() {
    scenario_summary summary;
    summary.name = "unavailable";
    const std::string action_name = "/muesli_bt_nav2_evidence/unavailable/navigate_to_pose";
    env_ptr env = make_ros2_env("nav2-evidence-unavailable");
    gc_root_scope roots(muslisp::default_gc());
    value unavailable = eval_recording(summary,
                                       env,
                                       nav2_request_script("navigate-to-pose",
                                                           "nav2-evidence-unavailable",
                                                           action_name,
                                                           "  (map.set! req 'timeout_ms 5) "));
    roots.add(&unavailable);
    record_response(summary, unavailable);
    finish_scenario(summary);
    return summary;
}

scenario_summary run_timeout() {
    scenario_summary summary;
    summary.name = "timeout";
    const std::string action_name = "/muesli_bt_nav2_evidence/timeout/navigate_to_pose";
    env_ptr env = make_ros2_env("nav2-evidence-timeout");
    test_support::nav2_fake_action_server server(action_name, test_support::nav2_fake_action_server::mode::slow_goal_accept);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    gc_root_scope roots(muslisp::default_gc());
    value timeout = eval_recording(summary,
                                   env,
                                   nav2_request_script("navigate-to-pose",
                                                       "nav2-evidence-timeout",
                                                       action_name,
                                                       "  (map.set! req 'timeout_ms 50) "));
    roots.add(&timeout);
    record_response(summary, timeout);
    summary.fake_server_goal_count = server.goal_count();
    summary.fake_server_cancel_count = server.cancel_count();
    if (summary.fake_server_goal_count > 0) {
        record_pose(summary, server.last_goal());
    }
    finish_scenario(summary);
    return summary;
}

std::string json_array_strings(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << '"' << json_escape(values[i]) << '"';
    }
    out << ']';
    return out.str();
}

std::string json_array_bools(const std::vector<bool>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << (values[i] ? "true" : "false");
    }
    out << ']';
    return out.str();
}

std::string scenario_json(const scenario_summary& scenario, unsigned indent = 4) {
    const std::string pad(indent, ' ');
    const std::string pad2(indent + 2, ' ');
    std::ostringstream out;
    out << pad << "{\n"
        << pad2 << "\"name\": \"" << json_escape(scenario.name) << "\",\n"
        << pad2 << "\"operations\": " << json_array_strings(scenario.operations) << ",\n"
        << pad2 << "\"statuses\": " << json_array_strings(scenario.statuses) << ",\n"
        << pad2 << "\"host_reached\": " << json_array_bools(scenario.host_reached) << ",\n"
        << pad2 << "\"job_id\": \"" << json_escape(scenario.job_id) << "\",\n"
        << pad2 << "\"request_hashes\": " << json_array_strings(scenario.request_hashes) << ",\n"
        << pad2 << "\"response_hashes\": " << json_array_strings(scenario.response_hashes) << ",\n"
        << pad2 << "\"progress_summary\": \"" << json_escape(scenario.progress_summary) << "\",\n"
        << pad2 << "\"fake_server_goal_count\": " << scenario.fake_server_goal_count << ",\n"
        << pad2 << "\"fake_server_cancel_count\": " << scenario.fake_server_cancel_count << ",\n"
        << pad2 << "\"received_pose\": ";
    if (scenario.has_received_pose) {
        out << "{\"x\": " << scenario.received_x << ", \"y\": " << scenario.received_y << ", \"qz\": "
            << scenario.received_qz << ", \"qw\": " << scenario.received_qw << "}\n";
    } else {
        out << "null\n";
    }
    out << pad << "}";
    return out.str();
}

std::string report_json(const std::vector<scenario_summary>& scenarios) {
    std::ostringstream out;
    out << "{\n"
        << "  \"schema_version\": \"nav2_capability_evidence_report.v1\",\n"
        << "  \"adapter_id\": \"nav2\",\n"
        << "  \"capability\": \"cap.navigation.v1\",\n"
        << "  \"status\": \"experimental\",\n"
        << "  \"fake_action_server\": true,\n"
        << "  \"real_nav2_stack\": false,\n"
        << "  \"scenarios\": [\n";
    for (std::size_t i = 0; i < scenarios.size(); ++i) {
        if (i > 0) {
            out << ",\n";
        }
        out << scenario_json(scenarios[i], 4);
    }
    out << "\n  ]\n"
        << "}\n";
    return out.str();
}

std::string manifest_json(const std::vector<scenario_summary>& scenarios) {
    std::ostringstream out;
    out << "{\n"
        << "  \"schema_version\": \"nav2_capability_evidence_manifest.v1\",\n"
        << "  \"status\": \"experimental\",\n"
        << "  \"capability\": \"cap.navigation.v1\",\n"
        << "  \"adapter_id\": \"nav2\",\n"
        << "  \"action\": \"nav2_msgs/action/NavigateToPose\",\n"
        << "  \"fixture_scope\": \"fake_action_server\",\n"
        << "  \"real_nav2_stack\": false,\n"
        << "  \"artefacts\": {\n"
        << "    \"report\": \"nav2_capability_report.json\",\n"
        << "    \"scenarios\": [\n";
    for (std::size_t i = 0; i < scenarios.size(); ++i) {
        if (i > 0) {
            out << ",\n";
        }
        out << "      {\"name\": \"" << json_escape(scenarios[i].name) << "\", \"events\": \""
            << json_escape(scenarios[i].name) << "/events.jsonl\"}";
    }
    out << "\n    ]\n"
        << "  },\n"
        << "  \"non_goals\": [\n"
        << "    \"real Nav2 lifecycle stack\",\n"
        << "    \"map server\",\n"
        << "    \"planner server\",\n"
        << "    \"simulator evidence\",\n"
        << "    \"physical robot evidence\"\n"
        << "  ]\n"
        << "}\n";
    return out.str();
}

void write_scenario(const std::filesystem::path& out_dir, const scenario_summary& scenario) {
    std::ostringstream events;
    for (const std::string& line : scenario.events) {
        events << line << '\n';
    }
    write_text(out_dir / scenario.name / "events.jsonl", events.str());
}

std::filesystem::path parse_out_dir(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out-dir" && i + 1 < argc) {
            return std::filesystem::path(argv[++i]);
        }
        if (arg == "--help" || arg == "-h") {
            std::cout << "usage: nav2_capability_evidence --out-dir <dir>\n";
            std::exit(0);
        }
    }
    throw std::runtime_error("missing required --out-dir <dir>");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path out_dir = parse_out_dir(argc, argv);
        std::filesystem::create_directories(out_dir);

        std::vector<scenario_summary> scenarios;
        scenarios.push_back(run_accepted_success());
        scenarios.push_back(run_rejected());
        scenarios.push_back(run_abort_error());
        scenarios.push_back(run_cancelled());
        scenarios.push_back(run_unavailable());
        scenarios.push_back(run_timeout());

        for (const scenario_summary& scenario : scenarios) {
            write_scenario(out_dir, scenario);
        }
        write_text(out_dir / "nav2_capability_report.json", report_json(scenarios));
        write_text(out_dir / "evidence_manifest.json", manifest_json(scenarios));

        muslisp::cap_api_reset();
        muslisp::env_api_reset();
        if (rclcpp::ok()) {
            rclcpp::shutdown();
        }
    } catch (const std::exception& e) {
        std::cerr << "nav2 capability evidence failed: " << e.what() << '\n';
        muslisp::cap_api_reset();
        muslisp::env_api_reset();
        if (rclcpp::ok()) {
            rclcpp::shutdown();
        }
        return 1;
    }
    return 0;
}
