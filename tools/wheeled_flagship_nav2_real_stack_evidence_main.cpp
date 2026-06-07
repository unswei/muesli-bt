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
#include <variant>
#include <vector>

#include "bt/event_log.hpp"
#include "bt/runtime_host.hpp"
#include "muslisp/env.hpp"
#include "muslisp/eval.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/value.hpp"
#include "ros2/extension.hpp"

namespace {

struct options {
    std::filesystem::path out_dir;
    std::string scenario = "success";
    std::string action_name = "/navigate_to_pose";
    std::string frame = "map";
    double goal_x = 1.0;
    double goal_y = 0.0;
    double goal_yaw = 0.0;
    std::int64_t timeout_ms = 2000;
    std::int64_t tick_period_ms = 100;
    std::int64_t max_ticks = 120;
    std::int64_t cancel_after_ticks = 2;
    bool expect_available = true;
};

struct summary {
    std::string scenario;
    std::string final_bt_status;
    std::string nav_status;
    std::string nav_job_id;
    std::string nav_request_hash;
    std::string nav_response_hash;
    bool nav_host_reached = false;
    double distance_remaining_m = 0.0;
    std::int64_t number_of_recoveries = 0;
    std::int64_t navigation_time_ms = 0;
    std::int64_t estimated_time_remaining_ms = 0;
    std::int64_t elapsed_ms = 0;
    std::int64_t ticks = 0;
};

std::string json_escape(std::string_view text) {
    return bt::event_log::json_escape(text);
}

std::string lisp_string_literal(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (char c : text) {
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

[[noreturn]] void usage_error(const std::string& message) {
    throw std::runtime_error(
        message +
        "\nusage: wheeled_flagship_nav2_real_stack_evidence --out-dir DIR [--scenario success|cancel] "
        "[--action-name /navigate_to_pose] [--goal-x X] [--goal-y Y] [--goal-yaw YAW] "
        "[--timeout-ms MS] [--tick-period-ms MS] [--max-ticks N] [--cancel-after-ticks N] "
        "[--allow-unavailable]");
}

options parse_args(int argc, char** argv) {
    options out;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](std::string_view flag) -> std::string {
            if (i + 1 >= argc) {
                usage_error("missing value for " + std::string(flag));
            }
            return argv[++i];
        };
        if (arg == "--out-dir") {
            out.out_dir = require_value(arg);
        } else if (arg == "--scenario") {
            out.scenario = require_value(arg);
        } else if (arg == "--action-name") {
            out.action_name = require_value(arg);
        } else if (arg == "--frame") {
            out.frame = require_value(arg);
        } else if (arg == "--goal-x") {
            out.goal_x = std::stod(require_value(arg));
        } else if (arg == "--goal-y") {
            out.goal_y = std::stod(require_value(arg));
        } else if (arg == "--goal-yaw") {
            out.goal_yaw = std::stod(require_value(arg));
        } else if (arg == "--timeout-ms") {
            out.timeout_ms = std::stoll(require_value(arg));
        } else if (arg == "--tick-period-ms") {
            out.tick_period_ms = std::stoll(require_value(arg));
        } else if (arg == "--max-ticks") {
            out.max_ticks = std::stoll(require_value(arg));
        } else if (arg == "--cancel-after-ticks") {
            out.cancel_after_ticks = std::stoll(require_value(arg));
        } else if (arg == "--allow-unavailable") {
            out.expect_available = false;
        } else if (arg == "--help" || arg == "-h") {
            usage_error("");
        } else {
            usage_error("unknown argument: " + arg);
        }
    }
    if (out.out_dir.empty()) {
        usage_error("--out-dir is required");
    }
    if (out.scenario != "success" && out.scenario != "cancel") {
        usage_error("--scenario must be success or cancel");
    }
    if (out.timeout_ms < 0 || out.tick_period_ms < 0 || out.max_ticks <= 0 || out.cancel_after_ticks < 0) {
        usage_error("timing arguments must be non-negative and --max-ticks must be positive");
    }
    return out;
}

muslisp::env_ptr make_env(const std::filesystem::path& event_log_path) {
    bt::runtime_host& host = bt::default_runtime_host();
    host.clear_all();
    bt::install_demo_callbacks(host);
    host.events().set_host_info("muesli-bt-nav2-real-stack-evidence", "experimental", "ros2-humble");

    muslisp::runtime_config config;
    config.register_extension(muslisp::integrations::ros2::make_extension());
    muslisp::env_ptr env = muslisp::create_global_env(std::move(config));

    const std::string init =
        "(begin "
        "  (events.enable #t) "
        "  (events.set-path " +
        lisp_string_literal(event_log_path.string()) +
        ") "
        "  (load \"examples/flagship_wheeled/lisp/bt_goal_flagship_nav_capability.lisp\") "
        "  (define inst (bt.new-instance wheeled-goal-flagship-nav-capability)) "
        "  nil)";
    (void)muslisp::eval_source(init, env);
    return env;
}

std::string status_text(muslisp::value value) {
    if (muslisp::is_symbol(value)) {
        return muslisp::symbol_name(value);
    }
    if (muslisp::is_string(value)) {
        return muslisp::string_value(value);
    }
    return "";
}

std::string tick_expr(const options& opts, bool collision) {
    std::ostringstream out;
    out << "(bt.tick inst '((goal_reached #f) "
        << "(collision_imminent " << (collision ? "#t" : "#f") << ") "
        << "(nav_goal_frame " << lisp_string_literal(opts.frame) << ") "
        << "(nav_goal_x " << opts.goal_x << ") "
        << "(nav_goal_y " << opts.goal_y << ") "
        << "(nav_goal_yaw " << opts.goal_yaw << ") "
        << "(nav_timeout_ms " << opts.timeout_ms << ") "
        << "(nav_action_name " << lisp_string_literal(opts.action_name) << ") ";
    if (collision) {
        out << "(act_avoid (0.0 0.0)) ";
    }
    out << "))";
    return out.str();
}

template <typename T>
std::optional<T> blackboard_value(const bt::instance& inst, const std::string& key) {
    const bt::bb_entry* entry = inst.bb.get(key);
    if (!entry) {
        return std::nullopt;
    }
    if (const auto* found = std::get_if<T>(&entry->value)) {
        return *found;
    }
    return std::nullopt;
}

const bt::instance* find_instance(muslisp::env_ptr env) {
    muslisp::value inst_value = muslisp::eval_source("inst", env);
    if (!muslisp::is_bt_instance(inst_value)) {
        return nullptr;
    }
    return bt::default_runtime_host().find_instance(muslisp::bt_handle(inst_value));
}

summary capture_summary(muslisp::env_ptr env,
                        const options& opts,
                        const std::string& final_bt_status,
                        std::int64_t ticks,
                        std::int64_t elapsed_ms) {
    const bt::instance* inst = find_instance(env);
    if (inst == nullptr) {
        throw std::runtime_error("failed to find BT instance");
    }
    summary out;
    out.scenario = opts.scenario;
    out.final_bt_status = final_bt_status;
    out.ticks = ticks;
    out.elapsed_ms = elapsed_ms;
    out.nav_status = blackboard_value<std::string>(*inst, "nav_status").value_or("");
    out.nav_job_id = blackboard_value<std::string>(*inst, "nav_job_id").value_or("");
    out.nav_request_hash = blackboard_value<std::string>(*inst, "nav_request_hash").value_or("");
    out.nav_response_hash = blackboard_value<std::string>(*inst, "nav_response_hash").value_or("");
    out.nav_host_reached = blackboard_value<bool>(*inst, "nav_host_reached").value_or(false);
    out.distance_remaining_m = blackboard_value<double>(*inst, "nav_distance_remaining_m").value_or(0.0);
    out.number_of_recoveries = blackboard_value<std::int64_t>(*inst, "nav_number_of_recoveries").value_or(0);
    out.navigation_time_ms = blackboard_value<std::int64_t>(*inst, "nav_navigation_time_ms").value_or(0);
    out.estimated_time_remaining_ms =
        blackboard_value<std::int64_t>(*inst, "nav_estimated_time_remaining_ms").value_or(0);
    return out;
}

summary run_scenario(muslisp::env_ptr env, const options& opts) {
    const auto started = std::chrono::steady_clock::now();
    std::string final_bt_status;
    std::int64_t ticks = 0;

    for (; ticks < opts.max_ticks; ++ticks) {
        const bool collision = opts.scenario == "cancel" && ticks >= opts.cancel_after_ticks;
        muslisp::value tick_status = muslisp::eval_source(tick_expr(opts, collision), env);
        final_bt_status = status_text(tick_status);

        const bt::instance* inst = find_instance(env);
        const std::string nav_status =
            inst ? blackboard_value<std::string>(*inst, "nav_status").value_or("") : std::string{};
        if (opts.expect_available && nav_status == "unavailable") {
            throw std::runtime_error("Nav2 action server is unavailable at " + opts.action_name);
        }
        if (opts.scenario == "success" && final_bt_status == "success" && nav_status == "ok") {
            ++ticks;
            break;
        }
        if (opts.scenario == "cancel" && nav_status == "cancelled") {
            ++ticks;
            break;
        }
        if (nav_status == "rejected" || nav_status == "timeout" || nav_status == "error" ||
            nav_status == "unreachable") {
            ++ticks;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(opts.tick_period_ms));
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    return capture_summary(env, opts, final_bt_status, ticks, elapsed);
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to write " + path.string());
    }
    out << text;
}

std::string report_json(const summary& result, const options& opts, const std::string& event_log_name) {
    const std::string final_nav_status = result.nav_status.empty() ? "" : ":" + result.nav_status;
    std::ostringstream out;
    out << "{\n"
        << "  \"schema_version\": \"wheeled_flagship_nav2_real_stack_scenario.v1\",\n"
        << "  \"scenario\": \"" << json_escape(result.scenario) << "\",\n"
        << "  \"variant\": \"wheeled-goal-flagship-nav-capability\",\n"
        << "  \"capability\": \"cap.navigation.v1\",\n"
        << "  \"adapter\": \"nav2\",\n"
        << "  \"nav2_stack\": true,\n"
        << "  \"real_robot\": false,\n"
        << "  \"action_name\": \"" << json_escape(opts.action_name) << "\",\n"
        << "  \"goal_pose\": {\"frame\": \"" << json_escape(opts.frame) << "\", \"x\": " << opts.goal_x
        << ", \"y\": " << opts.goal_y << ", \"yaw\": " << opts.goal_yaw << "},\n"
        << "  \"final_bt_status\": \"" << json_escape(result.final_bt_status) << "\",\n"
        << "  \"final_status\": \"" << json_escape(final_nav_status) << "\",\n"
        << "  \"host_reached\": " << (result.nav_host_reached ? "true" : "false") << ",\n"
        << "  \"job_id\": \"" << json_escape(result.nav_job_id) << "\",\n"
        << "  \"request_hash\": \"" << json_escape(result.nav_request_hash) << "\",\n"
        << "  \"response_hash\": \"" << json_escape(result.nav_response_hash) << "\",\n"
        << "  \"distance_remaining_m\": " << result.distance_remaining_m << ",\n"
        << "  \"number_of_recoveries\": " << result.number_of_recoveries << ",\n"
        << "  \"navigation_time_ms\": " << result.navigation_time_ms << ",\n"
        << "  \"estimated_time_remaining_ms\": " << result.estimated_time_remaining_ms << ",\n"
        << "  \"elapsed_ms\": " << result.elapsed_ms << ",\n"
        << "  \"ticks\": " << result.ticks << ",\n"
        << "  \"event_log_path\": \"" << json_escape(event_log_name) << "\"\n"
        << "}\n";
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const options opts = parse_args(argc, argv);
        std::filesystem::create_directories(opts.out_dir);
        const std::filesystem::path events_path = opts.out_dir / "events.jsonl";
        muslisp::env_ptr env = make_env(events_path);
        const summary result = run_scenario(env, opts);
        write_text(opts.out_dir / "scenario_report.json", report_json(result, opts, "events.jsonl"));
        std::cout << report_json(result, opts, events_path.string());
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
