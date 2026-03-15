#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <geometry_msgs/msg/twist.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_storage/storage_options.hpp>

#include "muslisp/env.hpp"
#include "muslisp/env_api.hpp"
#include "muslisp/eval.hpp"
#include "muslisp/value.hpp"
#include "ros2/extension.hpp"
#include "tests/ros2_test_harness.hpp"

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_close(double actual, double expected, double epsilon, const std::string& message) {
    if (std::fabs(actual - expected) > epsilon) {
        std::ostringstream out;
        out << message << " (expected " << expected << ", got " << actual << ')';
        throw std::runtime_error(out.str());
    }
}

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

std::string normalise_topic_ns(std::string topic_ns) {
    if (topic_ns.empty() || topic_ns == "/") {
        return {};
    }
    if (topic_ns.front() != '/') {
        topic_ns.insert(topic_ns.begin(), '/');
    }
    while (!topic_ns.empty() && topic_ns.back() == '/') {
        topic_ns.pop_back();
    }
    return topic_ns;
}

std::string resolve_topic_name(const std::string& topic_ns, const std::string& leaf) {
    if (!leaf.empty() && leaf.front() == '/') {
        return leaf;
    }
    const std::string ns = normalise_topic_ns(topic_ns);
    if (ns.empty()) {
        return "/" + leaf;
    }
    return ns + "/" + leaf;
}

std::filesystem::path l2_artifact_root() {
    if (const char* env = std::getenv("MUESLI_BT_L2_ARTIFACT_DIR")) {
        if (*env != '\0') {
            std::filesystem::path root(env);
            std::filesystem::create_directories(root);
            return root;
        }
    }
    std::filesystem::path root = std::filesystem::current_path() / "ros2_l2_artifacts";
    std::filesystem::create_directories(root);
    return root;
}

std::vector<std::string> read_non_empty_lines(const std::filesystem::path& path) {
    std::ifstream in(path);
    check(in.good(), "failed to open log file: " + path.string());

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

struct odom_sample {
    std::int64_t t_ns = 0;
    double x = 0.0;
    double y = 0.0;
    double yaw = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double wz = 0.0;
};

struct scenario_paths {
    std::filesystem::path scenario_dir;
    std::filesystem::path bag_dir;
    std::filesystem::path event_log_path;
    std::filesystem::path summary_path;
};

struct scenario_summary {
    std::string scenario;
    std::string status;
    std::string message;
    std::int64_t ticks = 0;
    std::int64_t fallback_count = 0;
    std::int64_t overrun_count = 0;
    std::int64_t backend_published_actions = 0;
    std::size_t command_count = 0;
    geometry_msgs::msg::Twist last_command{};
    std::size_t event_log_lines = 0;
    std::optional<double> final_obs_x{};
};

nav_msgs::msg::Odometry make_odom_message(const odom_sample& sample) {
    nav_msgs::msg::Odometry msg;
    msg.header.stamp = rclcpp::Time(sample.t_ns);
    msg.header.frame_id = "map";
    msg.child_frame_id = "base_link";
    msg.pose.pose.position.x = sample.x;
    msg.pose.pose.position.y = sample.y;
    msg.pose.pose.position.z = 0.0;
    msg.pose.pose.orientation.x = 0.0;
    msg.pose.pose.orientation.y = 0.0;
    msg.pose.pose.orientation.z = std::sin(sample.yaw * 0.5);
    msg.pose.pose.orientation.w = std::cos(sample.yaw * 0.5);
    msg.twist.twist.linear.x = sample.vx;
    msg.twist.twist.linear.y = sample.vy;
    msg.twist.twist.angular.z = sample.wz;
    return msg;
}

void write_odometry_bag(const std::filesystem::path& bag_dir,
                        const std::string& topic,
                        const std::vector<odom_sample>& samples) {
    std::error_code ec;
    std::filesystem::remove_all(bag_dir, ec);
    std::filesystem::create_directories(bag_dir.parent_path());

    rosbag2_storage::StorageOptions storage_options{};
    storage_options.uri = bag_dir.string();
    storage_options.storage_id = "sqlite3";

    rosbag2_cpp::Writer writer;
    writer.open(storage_options);
    writer.create_topic({topic, "nav_msgs/msg/Odometry", "cdr", ""});

    for (const auto& sample : samples) {
        writer.write(make_odom_message(sample), topic, rclcpp::Time(sample.t_ns));
    }
    writer.close();
}

class ros2_bag_replayer {
public:
    ros2_bag_replayer(std::filesystem::path bag_dir, std::string topic)
        : bag_dir_(std::move(bag_dir)),
          topic_(std::move(topic)) {
        static std::atomic<std::uint64_t> next_id{1};
        const std::uint64_t id = next_id.fetch_add(1, std::memory_order_relaxed);
        node_ = std::make_shared<rclcpp::Node>("muesli_bt_rosbag_replayer_" + std::to_string(id));
        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(node_);
        publisher_ = node_->create_publisher<nav_msgs::msg::Odometry>(topic_, rclcpp::SystemDefaultsQoS());
    }

    ~ros2_bag_replayer() {
        if (executor_ && node_) {
            executor_->remove_node(node_);
        }
    }

    void start() {
        completion_ = std::async(std::launch::async, [this]() {
            rosbag2_storage::StorageOptions storage_options{};
            storage_options.uri = bag_dir_.string();
            storage_options.storage_id = "sqlite3";

            rosbag2_cpp::Reader reader;
            reader.open(storage_options);

            const auto wait_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < wait_deadline) {
                executor_->spin_some(std::chrono::milliseconds(0));
                if (publisher_->get_subscription_count() > 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            std::optional<std::int64_t> previous_stamp{};
            rclcpp::Serialization<nav_msgs::msg::Odometry> serialiser;
            while (reader.has_next()) {
                auto bag_message = reader.read_next();
                if (bag_message->topic_name != topic_) {
                    continue;
                }

                if (previous_stamp.has_value() && bag_message->time_stamp > *previous_stamp) {
                    std::this_thread::sleep_for(
                        std::chrono::nanoseconds(bag_message->time_stamp - *previous_stamp));
                }

                rclcpp::SerializedMessage serialised(*bag_message->serialized_data);
                nav_msgs::msg::Odometry message;
                serialiser.deserialize_message(&serialised, &message);
                publisher_->publish(message);
                executor_->spin_some(std::chrono::milliseconds(0));
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                previous_stamp = bag_message->time_stamp;
            }
            return true;
        });
    }

    [[nodiscard]] bool wait_for_completion(std::chrono::milliseconds timeout) {
        if (!completion_.valid()) {
            return false;
        }
        if (completion_.wait_for(timeout) != std::future_status::ready) {
            return false;
        }
        return completion_.get();
    }

private:
    std::filesystem::path bag_dir_;
    std::string topic_;
    std::shared_ptr<rclcpp::Node> node_{};
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_{};
    rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr publisher_{};
    std::future<bool> completion_{};
};

std::string ros2_configure_script(const std::string& topic_ns, const std::string& extra_lines = {}) {
    return "(begin "
           "  (define cfg (map.make)) "
           "  (map.set! cfg 'control_hz 5) "
           "  (map.set! cfg 'observe_timeout_ms 250) "
           "  (map.set! cfg 'step_timeout_ms 25) "
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

scenario_paths prepare_scenario(const std::string& name) {
    const std::filesystem::path scenario_dir = l2_artifact_root() / name;
    std::error_code ec;
    std::filesystem::remove_all(scenario_dir, ec);
    std::filesystem::create_directories(scenario_dir);
    return {
        .scenario_dir = scenario_dir,
        .bag_dir = scenario_dir / "odom_bag",
        .event_log_path = scenario_dir / "events.jsonl",
        .summary_path = scenario_dir / "summary.json",
    };
}

void write_summary(const std::filesystem::path& summary_path, const scenario_summary& summary) {
    std::ofstream out(summary_path);
    check(out.good(), "failed to open summary path: " + summary_path.string());
    out << "{\n"
        << "  \"scenario\": \"" << summary.scenario << "\",\n"
        << "  \"status\": \"" << summary.status << "\",\n"
        << "  \"message\": " << lisp_string_literal(summary.message) << ",\n"
        << "  \"ticks\": " << summary.ticks << ",\n"
        << "  \"fallback_count\": " << summary.fallback_count << ",\n"
        << "  \"overrun_count\": " << summary.overrun_count << ",\n"
        << "  \"backend_published_actions\": " << summary.backend_published_actions << ",\n"
        << "  \"command_count\": " << summary.command_count << ",\n"
        << "  \"last_command\": {\n"
        << "    \"linear_x\": " << summary.last_command.linear.x << ",\n"
        << "    \"linear_y\": " << summary.last_command.linear.y << ",\n"
        << "    \"angular_z\": " << summary.last_command.angular.z << "\n"
        << "  },\n"
        << "  \"event_log_lines\": " << summary.event_log_lines << ",\n";
    if (summary.final_obs_x.has_value()) {
        out << "  \"final_obs_x\": " << *summary.final_obs_x << "\n";
    } else {
        out << "  \"final_obs_x\": null\n";
    }
    out << "}\n";
}

std::optional<muslisp::value> map_lookup_text_key(muslisp::value map_obj, const std::string& key) {
    if (!muslisp::is_map(map_obj)) {
        return std::nullopt;
    }
    for (const auto& [map_key, map_value] : map_obj->map_data) {
        if ((map_key.type == muslisp::map_key_type::string || map_key.type == muslisp::map_key_type::symbol) &&
            map_key.text_data == key) {
            return map_value;
        }
    }
    return std::nullopt;
}

muslisp::value decode_json_text(const std::string& text, muslisp::env_ptr env) {
    return eval_text("(json.decode " + lisp_string_literal(text) + ")", env);
}

std::vector<muslisp::value> decode_json_lines(const std::vector<std::string>& lines, muslisp::env_ptr env) {
    std::vector<muslisp::value> decoded;
    decoded.reserve(lines.size());
    for (const std::string& line : lines) {
        decoded.push_back(decode_json_text(line, env));
    }
    return decoded;
}

double number_value_checked(muslisp::value v, const std::string& where) {
    if (muslisp::is_integer(v)) {
        return static_cast<double>(muslisp::integer_value(v));
    }
    if (muslisp::is_float(v)) {
        return muslisp::float_value(v);
    }
    throw std::runtime_error(where + ": expected number");
}

muslisp::value require_map_field(muslisp::value map_obj, const std::string& key, const std::string& where) {
    const auto result = map_lookup_text_key(map_obj, key);
    if (!result.has_value() || !muslisp::is_map(*result)) {
        throw std::runtime_error(where + ": missing map field '" + key + "'");
    }
    return *result;
}

std::string require_text_field(muslisp::value map_obj, const std::string& key, const std::string& where) {
    const auto result = map_lookup_text_key(map_obj, key);
    if (!result.has_value()) {
        throw std::runtime_error(where + ": missing text field '" + key + "'");
    }
    if (muslisp::is_string(*result)) {
        return muslisp::string_value(*result);
    }
    if (muslisp::is_symbol(*result)) {
        return muslisp::symbol_name(*result);
    }
    throw std::runtime_error(where + ": expected text field '" + key + "'");
}

std::int64_t require_int_field(muslisp::value map_obj, const std::string& key, const std::string& where) {
    const auto result = map_lookup_text_key(map_obj, key);
    if (!result.has_value() || !muslisp::is_integer(*result)) {
        throw std::runtime_error(where + ": missing integer field '" + key + "'");
    }
    return muslisp::integer_value(*result);
}

double require_number_field(muslisp::value map_obj, const std::string& key, const std::string& where) {
    const auto result = map_lookup_text_key(map_obj, key);
    if (!result.has_value()) {
        throw std::runtime_error(where + ": missing numeric field '" + key + "'");
    }
    return number_value_checked(*result, where + "." + key);
}

bool require_bool_field(muslisp::value map_obj, const std::string& key, const std::string& where) {
    const auto result = map_lookup_text_key(map_obj, key);
    if (!result.has_value() || !muslisp::is_boolean(*result)) {
        throw std::runtime_error(where + ": missing boolean field '" + key + "'");
    }
    return muslisp::boolean_value(*result);
}

muslisp::value require_event_data(muslisp::value event_obj, const std::string& where) {
    return require_map_field(event_obj, "data", where);
}

void verify_canonical_event_envelope(muslisp::value event_obj,
                                     const std::string& expected_type,
                                     const std::string& where,
                                     std::optional<std::int64_t> expected_tick = std::nullopt) {
    check(require_text_field(event_obj, "schema", where) == "mbt.evt.v1", where + ": schema mismatch");
    check(require_text_field(event_obj, "contract_version", where) == "1.0.0", where + ": contract_version mismatch");
    check(require_text_field(event_obj, "type", where) == expected_type, where + ": type mismatch");
    check(require_int_field(event_obj, "seq", where) > 0, where + ": seq should be positive");
    if (expected_tick.has_value()) {
        check(require_int_field(event_obj, "tick", where) == *expected_tick, where + ": tick mismatch");
    }
}

std::vector<muslisp::value> select_events_by_type(const std::vector<muslisp::value>& events, const std::string& type) {
    std::vector<muslisp::value> filtered;
    for (muslisp::value event_obj : events) {
        if (require_text_field(event_obj, "type", "event") == type) {
            filtered.push_back(event_obj);
        }
    }
    return filtered;
}

void verify_run_start_time_policy(muslisp::value event_obj, const std::string& where) {
    verify_canonical_event_envelope(event_obj, "run_start", where);
    const muslisp::value data = require_event_data(event_obj, where);
    const muslisp::value caps = require_map_field(data, "capabilities", where + ".data");
    check(require_text_field(caps, "time_source", where + ".data.capabilities") == "ros_wall_time",
          where + ": time_source mismatch");
    check(!require_bool_field(caps, "use_sim_time", where + ".data.capabilities"),
          where + ": use_sim_time mismatch");
    check(require_text_field(caps, "obs_timestamp_source", where + ".data.capabilities") ==
              "message_header_or_node_clock",
          where + ": obs_timestamp_source mismatch");
}

void verify_common_record_shape(muslisp::value record,
                                const std::string& expected_schema,
                                std::int64_t expected_tick,
                                std::int64_t expected_t_ms,
                                bool expected_used_fallback,
                                bool expected_overrun) {
    check(require_text_field(record, "schema_version", "record") == expected_schema, "record schema_version mismatch");
    check(require_int_field(record, "tick", "record") == expected_tick, "record tick mismatch");
    check(require_int_field(record, "t_ms", "record") == expected_t_ms, "record t_ms mismatch");
    check(require_bool_field(record, "used_fallback", "record") == expected_used_fallback,
          "record used_fallback mismatch");
    check(require_bool_field(record, "overrun", "record") == expected_overrun, "record overrun mismatch");

    const muslisp::value budget = require_map_field(record, "budget", "record");
    check_close(require_number_field(budget, "tick_budget_ms", "record.budget"), 200.0, 1e-6,
                "record tick_budget_ms mismatch");
    check(require_number_field(budget, "tick_time_ms", "record.budget") >= 0.0,
          "record tick_time_ms should be non-negative");

    const muslisp::value obs = require_map_field(record, "obs", "record");
    check(require_text_field(obs, "obs_schema", "record.obs") == "ros2.obs.v1", "record obs_schema mismatch");
    check(require_text_field(obs, "state_schema", "record.obs") == "ros2.state.v1", "record state_schema mismatch");
    const muslisp::value state = require_map_field(obs, "state", "record.obs");
    check(require_text_field(state, "state_schema", "record.obs.state") == "ros2.state.v1",
          "record state.state_schema mismatch");

    const muslisp::value action = require_map_field(record, "action", "record");
    check(require_text_field(action, "action_schema", "record.action") == "ros2.action.v1",
          "record action_schema mismatch");
}

void verify_pose_x(muslisp::value record, double expected_x, const std::string& where) {
    const muslisp::value obs = require_map_field(record, "obs", where);
    const muslisp::value state = require_map_field(obs, "state", where + ".obs");
    const muslisp::value pose = require_map_field(state, "pose", where + ".obs.state");
    check_close(require_number_field(pose, "x", where + ".obs.state.pose"), expected_x, 1e-6, where + ": pose.x mismatch");
}

void verify_action_components(muslisp::value action,
                              double linear_x,
                              double linear_y,
                              double angular_z,
                              const std::string& where) {
    const muslisp::value u = require_map_field(action, "u", where);
    check_close(require_number_field(u, "linear_x", where + ".u"), linear_x, 1e-6, where + ": linear_x mismatch");
    check_close(require_number_field(u, "linear_y", where + ".u"), linear_y, 1e-6, where + ": linear_y mismatch");
    check_close(require_number_field(u, "angular_z", where + ".u"), angular_z, 1e-6, where + ": angular_z mismatch");
}

std::string result_message(muslisp::env_ptr env) {
    return muslisp::string_value(eval_text("(map.get rosbag-run-result 'message \"\")", env));
}

std::string safe_action_snippet() {
    return "(define safe (map.make)) "
           "(define safe-u (map.make)) "
           "(map.set! safe 'action_schema \"ros2.action.v1\") "
           "(map.set! safe 't_ms 0) "
           "(map.set! safe-u 'linear_x 0.0) "
           "(map.set! safe-u 'linear_y 0.0) "
           "(map.set! safe-u 'angular_z 0.0) "
           "(map.set! safe 'u safe-u) ";
}

void test_ros2_rosbag_replay_conformance() {
    using namespace muslisp;

    const scenario_paths paths = prepare_scenario("rosbag-replay-case");
    const std::string topic_ns = "/l2bag";
    const std::string odom_topic = resolve_topic_name(topic_ns, "odom");
    const std::vector<odom_sample> samples = {
        {.t_ns = 1'700'000'000'000'000'000LL, .x = 0.2, .y = 0.0, .yaw = 0.0, .vx = 0.1, .vy = 0.0, .wz = 0.0},
        {.t_ns = 1'700'000'000'040'000'000LL, .x = 1.6, .y = -0.1, .yaw = 0.2, .vx = 0.15, .vy = 0.0, .wz = 0.05},
    };
    write_odometry_bag(paths.bag_dir, odom_topic, samples);
    check(std::filesystem::exists(paths.bag_dir / "metadata.yaml"), "ros2 rosbag replay: missing bag metadata");

    test_support::ros2_test_harness harness(topic_ns);
    env_ptr env = create_env_with_ros2_extension();
    (void)eval_text("(env.attach \"ros2\")", env);
    (void)eval_text(ros2_configure_script(topic_ns), env);
    check(harness.wait_for_transport_ready(std::chrono::milliseconds(1000)),
          "ros2 rosbag replay: transport should be ready after configure");

    ros2_bag_replayer player(paths.bag_dir, odom_topic);
    player.start();

    const std::string run_expr =
        "(begin "
        + safe_action_snippet() +
        "  (define rosbag-run-result "
        "    (env.run-loop "
        "      (begin "
        "        (define cfg (map.make)) "
        "        (map.set! cfg 'tick_hz 5) "
        "        (map.set! cfg 'max_ticks 2) "
        "        (map.set! cfg 'step_max 2) "
        "        (map.set! cfg 'realtime #t) "
        "        (map.set! cfg 'safe_action safe) "
        "        (map.set! cfg 'schema_version \"ros2.l2.v1\") "
        "        (map.set! cfg 'event_log_path " +
        lisp_string_literal(paths.event_log_path.string()) +
        ") "
        "        cfg) "
        "      (lambda (obs) "
        "        (begin "
        "          (define a (map.make)) "
        "          (define u (map.make)) "
        "          (map.set! a 'action_schema \"ros2.action.v1\") "
        "          (map.set! a 't_ms (map.get obs 't_ms 0)) "
        "          (map.set! u 'linear_x 0.25) "
        "          (map.set! u 'linear_y 0.0) "
        "          (map.set! u 'angular_z 0.05) "
        "          (map.set! a 'u u) "
        "          a)))) "
        "  rosbag-run-result)";
    const value run_result = eval_text(run_expr, env);
    check(is_map(run_result), "ros2 rosbag replay: env.run-loop should return a map");

    check(player.wait_for_completion(std::chrono::seconds(3)),
          "ros2 rosbag replay: timed out waiting for rosbag playback");
    check(harness.wait_for_command_count(1, std::chrono::milliseconds(1000)),
          "ros2 rosbag replay: expected at least one published cmd_vel command");

    const std::string status = symbol_name(eval_text("(map.get rosbag-run-result 'status ':none)", env));
    check(status == ":stopped", "ros2 rosbag replay: expected :stopped status, got " + status);
    const std::string message = result_message(env);
    const std::int64_t ticks = integer_value(eval_text("(map.get rosbag-run-result 'ticks -1)", env));
    check(ticks == 2, "ros2 rosbag replay: expected two ticks");
    const std::int64_t fallback_count = integer_value(eval_text("(map.get rosbag-run-result 'fallback_count -1)", env));
    check(fallback_count == 0, "ros2 rosbag replay: unexpected fallback_count");
    const std::int64_t overrun_count = integer_value(eval_text("(map.get rosbag-run-result 'overrun_count -1)", env));
    check(overrun_count == 0, "ros2 rosbag replay: unexpected overrun_count");
    const std::int64_t backend_published_actions =
        integer_value(eval_text("(map.get (env.info) 'published_actions -1)", env));
    check(backend_published_actions == 2, "ros2 rosbag replay: backend should report two published actions");

    const double final_obs_x =
        float_value(eval_text(
            "(map.get (map.get (map.get (map.get rosbag-run-result 'final_obs (map.make)) "
            "'state (map.make)) 'pose (map.make)) 'x 0.0)",
            env));
    check_close(final_obs_x, 1.6, 1e-6, "ros2 rosbag replay: final_obs pose.x mismatch");

    const auto command = harness.last_command();
    check_close(command.linear.x, 0.25, 1e-6, "ros2 rosbag replay: linear.x mismatch");
    check_close(command.linear.y, 0.0, 1e-6, "ros2 rosbag replay: linear.y mismatch");
    check_close(command.angular.z, 0.05, 1e-6, "ros2 rosbag replay: angular.z mismatch");

    const auto lines = read_non_empty_lines(paths.event_log_path);
    check(lines.size() == 5, "ros2 rosbag replay: expected five canonical events");
    const auto events = decode_json_lines(lines, env);
    const auto run_start_events = select_events_by_type(events, "run_start");
    check(run_start_events.size() == 1, "ros2 rosbag replay: expected one run_start event");
    verify_run_start_time_policy(run_start_events[0], "ros2 rosbag replay run_start");
    const auto tick_begin_events = select_events_by_type(events, "tick_begin");
    const auto tick_end_events = select_events_by_type(events, "tick_end");
    check(tick_begin_events.size() == 2, "ros2 rosbag replay: expected two tick_begin events");
    check(tick_end_events.size() == 2, "ros2 rosbag replay: expected two tick_end events");
    verify_canonical_event_envelope(tick_begin_events[0], "tick_begin", "ros2 rosbag replay tick_begin1", 1);
    verify_canonical_event_envelope(tick_begin_events[1], "tick_begin", "ros2 rosbag replay tick_begin2", 2);
    verify_canonical_event_envelope(tick_end_events[0], "tick_end", "ros2 rosbag replay tick_end1", 1);
    verify_canonical_event_envelope(tick_end_events[1], "tick_end", "ros2 rosbag replay tick_end2", 2);
    const value record1 = require_event_data(tick_end_events[0], "ros2 rosbag replay tick_end1");
    const value record2 = require_event_data(tick_end_events[1], "ros2 rosbag replay tick_end2");
    verify_common_record_shape(record1, "ros2.l2.v1", 1, 1'700'000'000'000LL, false, false);
    verify_common_record_shape(record2, "ros2.l2.v1", 2, 1'700'000'000'040LL, false, false);
    check(require_text_field(record1, "status", "ros2 rosbag replay record1") == "running",
          "ros2 rosbag replay: record1 status mismatch");
    check(require_text_field(record2, "status", "ros2 rosbag replay record2") == "stopped",
          "ros2 rosbag replay: record2 status mismatch");
    verify_pose_x(record1, 0.2, "ros2 rosbag replay record1");
    verify_pose_x(record2, 1.6, "ros2 rosbag replay record2");
    verify_action_components(require_map_field(record1, "action", "ros2 rosbag replay record1"), 0.25, 0.0, 0.05,
                             "ros2 rosbag replay record1 action");
    verify_action_components(require_map_field(record2, "action", "ros2 rosbag replay record2"), 0.25, 0.0, 0.05,
                             "ros2 rosbag replay record2 action");
    check(!map_lookup_text_key(record1, "error").has_value(), "ros2 rosbag replay: unexpected error field");
    check(!map_lookup_text_key(record2, "error").has_value(), "ros2 rosbag replay: unexpected error field");

    write_summary(
        paths.summary_path,
        {
            .scenario = "rosbag-replay-case",
            .status = status,
            .message = message,
            .ticks = ticks,
            .fallback_count = fallback_count,
            .overrun_count = overrun_count,
            .backend_published_actions = backend_published_actions,
            .command_count = harness.command_count(),
            .last_command = command,
            .event_log_lines = lines.size(),
            .final_obs_x = final_obs_x,
        });
    check(std::filesystem::exists(paths.summary_path), "ros2 rosbag replay: missing summary artefact");
}

void test_ros2_rosbag_clamp_conformance() {
    using namespace muslisp;

    const scenario_paths paths = prepare_scenario("rosbag-clamped-action-case");
    const std::string topic_ns = "/l2clamp";
    const std::string odom_topic = resolve_topic_name(topic_ns, "odom");
    const std::vector<odom_sample> samples = {
        {.t_ns = 1'700'000'100'000'000'000LL, .x = -0.4, .y = 0.2, .yaw = -0.1, .vx = 0.05, .vy = 0.01, .wz = 0.0},
        {.t_ns = 1'700'000'100'040'000'000LL, .x = 0.8, .y = 0.4, .yaw = 0.3, .vx = 0.06, .vy = -0.02, .wz = 0.1},
    };
    write_odometry_bag(paths.bag_dir, odom_topic, samples);
    check(std::filesystem::exists(paths.bag_dir / "metadata.yaml"), "ros2 rosbag clamp: missing bag metadata");

    test_support::ros2_test_harness harness(topic_ns);
    env_ptr env = create_env_with_ros2_extension();
    (void)eval_text("(env.attach \"ros2\")", env);
    (void)eval_text(ros2_configure_script(topic_ns), env);
    check(harness.wait_for_transport_ready(std::chrono::milliseconds(1000)),
          "ros2 rosbag clamp: transport should be ready after configure");

    ros2_bag_replayer player(paths.bag_dir, odom_topic);
    player.start();

    const std::string run_expr =
        "(begin "
        + safe_action_snippet() +
        "  (define rosbag-run-result "
        "    (env.run-loop "
        "      (begin "
        "        (define cfg (map.make)) "
        "        (map.set! cfg 'tick_hz 5) "
        "        (map.set! cfg 'max_ticks 2) "
        "        (map.set! cfg 'step_max 2) "
        "        (map.set! cfg 'realtime #t) "
        "        (map.set! cfg 'safe_action safe) "
        "        (map.set! cfg 'schema_version \"ros2.l2.clamp.v1\") "
        "        (map.set! cfg 'event_log_path " +
        lisp_string_literal(paths.event_log_path.string()) +
        ") "
        "        cfg) "
        "      (lambda (obs) "
        "        (begin "
        "          (define a (map.make)) "
        "          (define u (map.make)) "
        "          (map.set! a 'action_schema \"ros2.action.v1\") "
        "          (map.set! a 't_ms (map.get obs 't_ms 0)) "
        "          (map.set! u 'linear_x 2.5) "
        "          (map.set! u 'linear_y -2.0) "
        "          (map.set! u 'angular_z 1.5) "
        "          (map.set! a 'u u) "
        "          a)))) "
        "  rosbag-run-result)";
    const value run_result = eval_text(run_expr, env);
    check(is_map(run_result), "ros2 rosbag clamp: env.run-loop should return a map");

    check(player.wait_for_completion(std::chrono::seconds(3)),
          "ros2 rosbag clamp: timed out waiting for rosbag playback");
    check(harness.wait_for_command_count(1, std::chrono::milliseconds(1000)),
          "ros2 rosbag clamp: expected at least one published cmd_vel command");

    const std::string status = symbol_name(eval_text("(map.get rosbag-run-result 'status ':none)", env));
    check(status == ":stopped", "ros2 rosbag clamp: expected :stopped status, got " + status);
    const std::string message = result_message(env);
    const std::int64_t ticks = integer_value(eval_text("(map.get rosbag-run-result 'ticks -1)", env));
    check(ticks == 2, "ros2 rosbag clamp: expected two ticks");
    const std::int64_t fallback_count = integer_value(eval_text("(map.get rosbag-run-result 'fallback_count -1)", env));
    check(fallback_count == 0, "ros2 rosbag clamp: unexpected fallback_count");
    const std::int64_t overrun_count = integer_value(eval_text("(map.get rosbag-run-result 'overrun_count -1)", env));
    check(overrun_count == 0, "ros2 rosbag clamp: unexpected overrun_count");
    const std::int64_t backend_published_actions =
        integer_value(eval_text("(map.get (env.info) 'published_actions -1)", env));
    check(backend_published_actions == 2, "ros2 rosbag clamp: backend should report two published actions");

    const double final_obs_x =
        float_value(eval_text(
            "(map.get (map.get (map.get (map.get rosbag-run-result 'final_obs (map.make)) "
            "'state (map.make)) 'pose (map.make)) 'x 0.0)",
            env));
    check_close(final_obs_x, 0.8, 1e-6, "ros2 rosbag clamp: final_obs pose.x mismatch");

    const auto command = harness.last_command();
    check_close(command.linear.x, 1.0, 1e-6, "ros2 rosbag clamp: linear.x should be clamped");
    check_close(command.linear.y, -1.0, 1e-6, "ros2 rosbag clamp: linear.y should be clamped");
    check_close(command.angular.z, 1.0, 1e-6, "ros2 rosbag clamp: angular.z should be clamped");

    const auto lines = read_non_empty_lines(paths.event_log_path);
    check(lines.size() == 5, "ros2 rosbag clamp: expected five canonical events");
    const auto events = decode_json_lines(lines, env);
    const auto run_start_events = select_events_by_type(events, "run_start");
    check(run_start_events.size() == 1, "ros2 rosbag clamp: expected one run_start event");
    verify_run_start_time_policy(run_start_events[0], "ros2 rosbag clamp run_start");
    const auto tick_begin_events = select_events_by_type(events, "tick_begin");
    const auto tick_end_events = select_events_by_type(events, "tick_end");
    check(tick_begin_events.size() == 2, "ros2 rosbag clamp: expected two tick_begin events");
    check(tick_end_events.size() == 2, "ros2 rosbag clamp: expected two tick_end events");
    verify_canonical_event_envelope(tick_begin_events[0], "tick_begin", "ros2 rosbag clamp tick_begin1", 1);
    verify_canonical_event_envelope(tick_begin_events[1], "tick_begin", "ros2 rosbag clamp tick_begin2", 2);
    verify_canonical_event_envelope(tick_end_events[0], "tick_end", "ros2 rosbag clamp tick_end1", 1);
    verify_canonical_event_envelope(tick_end_events[1], "tick_end", "ros2 rosbag clamp tick_end2", 2);
    const value record1 = require_event_data(tick_end_events[0], "ros2 rosbag clamp tick_end1");
    const value record2 = require_event_data(tick_end_events[1], "ros2 rosbag clamp tick_end2");
    verify_common_record_shape(record1, "ros2.l2.clamp.v1", 1, 1'700'000'100'000LL, false, false);
    verify_common_record_shape(record2, "ros2.l2.clamp.v1", 2, 1'700'000'100'040LL, false, false);
    check(require_text_field(record1, "status", "ros2 rosbag clamp record1") == "running",
          "ros2 rosbag clamp: record1 status mismatch");
    check(require_text_field(record2, "status", "ros2 rosbag clamp record2") == "stopped",
          "ros2 rosbag clamp: record2 status mismatch");
    verify_pose_x(record1, -0.4, "ros2 rosbag clamp record1");
    verify_pose_x(record2, 0.8, "ros2 rosbag clamp record2");
    verify_action_components(require_map_field(record1, "action", "ros2 rosbag clamp record1"), 2.5, -2.0, 1.5,
                             "ros2 rosbag clamp record1 action");
    verify_action_components(require_map_field(record2, "action", "ros2 rosbag clamp record2"), 2.5, -2.0, 1.5,
                             "ros2 rosbag clamp record2 action");
    check(!map_lookup_text_key(record1, "error").has_value(), "ros2 rosbag clamp: unexpected error field");
    check(!map_lookup_text_key(record2, "error").has_value(), "ros2 rosbag clamp: unexpected error field");

    write_summary(
        paths.summary_path,
        {
            .scenario = "rosbag-clamped-action-case",
            .status = status,
            .message = message,
            .ticks = ticks,
            .fallback_count = fallback_count,
            .overrun_count = overrun_count,
            .backend_published_actions = backend_published_actions,
            .command_count = harness.command_count(),
            .last_command = command,
            .event_log_lines = lines.size(),
            .final_obs_x = final_obs_x,
        });
    check(std::filesystem::exists(paths.summary_path), "ros2 rosbag clamp: missing summary artefact");
}

void test_ros2_rosbag_invalid_action_fallback_conformance() {
    using namespace muslisp;

    const scenario_paths paths = prepare_scenario("rosbag-invalid-action-fallback-case");
    const std::string topic_ns = "/l2invalid";
    const std::string odom_topic = resolve_topic_name(topic_ns, "odom");
    const std::vector<odom_sample> samples = {
        {.t_ns = 1'700'000'200'000'000'000LL, .x = 0.4, .y = 0.1, .yaw = 0.0, .vx = 0.02, .vy = 0.0, .wz = 0.0},
    };
    write_odometry_bag(paths.bag_dir, odom_topic, samples);
    check(std::filesystem::exists(paths.bag_dir / "metadata.yaml"),
          "ros2 rosbag invalid-action: missing bag metadata");

    test_support::ros2_test_harness harness(topic_ns);
    env_ptr env = create_env_with_ros2_extension();
    (void)eval_text("(env.attach \"ros2\")", env);
    (void)eval_text(ros2_configure_script(topic_ns), env);
    check(harness.wait_for_transport_ready(std::chrono::milliseconds(1000)),
          "ros2 rosbag invalid-action: transport should be ready after configure");

    ros2_bag_replayer player(paths.bag_dir, odom_topic);
    player.start();

    const std::string run_expr =
        "(begin "
        + safe_action_snippet() +
        "  (define rosbag-run-result "
        "    (env.run-loop "
        "      (begin "
        "        (define cfg (map.make)) "
        "        (map.set! cfg 'tick_hz 5) "
        "        (map.set! cfg 'max_ticks 2) "
        "        (map.set! cfg 'step_max 2) "
        "        (map.set! cfg 'realtime #t) "
        "        (map.set! cfg 'safe_action safe) "
        "        (map.set! cfg 'schema_version \"ros2.l2.invalid.v1\") "
        "        (map.set! cfg 'event_log_path " +
        lisp_string_literal(paths.event_log_path.string()) +
        ") "
        "        cfg) "
        "      (lambda (obs) "
        "        (begin "
        "          (define bad (map.make)) "
        "          (map.set! bad 'action_schema \"ros2.action.v1\") "
        "          (map.set! bad 't_ms (map.get obs 't_ms 0)) "
        "          (map.set! bad 'u 1) "
        "          bad)))) "
        "  rosbag-run-result)";
    const value run_result = eval_text(run_expr, env);
    check(is_map(run_result), "ros2 rosbag invalid-action: env.run-loop should return a map");

    check(player.wait_for_completion(std::chrono::seconds(3)),
          "ros2 rosbag invalid-action: timed out waiting for rosbag playback");
    check(harness.wait_for_command_count(1, std::chrono::milliseconds(1000)),
          "ros2 rosbag invalid-action: expected one published safe command");

    const std::string status = symbol_name(eval_text("(map.get rosbag-run-result 'status ':none)", env));
    check(status == ":error", "ros2 rosbag invalid-action: expected :error status, got " + status);
    const std::string message = result_message(env);
    check(message.find("u must be a map") != std::string::npos,
          "ros2 rosbag invalid-action: unexpected message: " + message);
    const std::int64_t ticks = integer_value(eval_text("(map.get rosbag-run-result 'ticks -1)", env));
    check(ticks == 1, "ros2 rosbag invalid-action: expected one tick");
    const std::int64_t fallback_count = integer_value(eval_text("(map.get rosbag-run-result 'fallback_count -1)", env));
    check(fallback_count == 1, "ros2 rosbag invalid-action: expected fallback_count=1");
    const std::int64_t overrun_count = integer_value(eval_text("(map.get rosbag-run-result 'overrun_count -1)", env));
    check(overrun_count == 0, "ros2 rosbag invalid-action: unexpected overrun_count");
    const std::int64_t backend_published_actions =
        integer_value(eval_text("(map.get (env.info) 'published_actions -1)", env));
    check(backend_published_actions == 1, "ros2 rosbag invalid-action: backend should report one published action");

    const double final_obs_x =
        float_value(eval_text(
            "(map.get (map.get (map.get (map.get rosbag-run-result 'final_obs (map.make)) "
            "'state (map.make)) 'pose (map.make)) 'x 0.0)",
            env));
    check_close(final_obs_x, 0.4, 1e-6, "ros2 rosbag invalid-action: final_obs pose.x mismatch");

    const auto command = harness.last_command();
    check_close(command.linear.x, 0.0, 1e-6, "ros2 rosbag invalid-action: safe linear.x mismatch");
    check_close(command.linear.y, 0.0, 1e-6, "ros2 rosbag invalid-action: safe linear.y mismatch");
    check_close(command.angular.z, 0.0, 1e-6, "ros2 rosbag invalid-action: safe angular.z mismatch");

    const auto lines = read_non_empty_lines(paths.event_log_path);
    check(lines.size() == 4, "ros2 rosbag invalid-action: expected four canonical events");
    const auto events = decode_json_lines(lines, env);
    const auto run_start_events = select_events_by_type(events, "run_start");
    check(run_start_events.size() == 1, "ros2 rosbag invalid-action: expected one run_start event");
    verify_run_start_time_policy(run_start_events[0], "ros2 rosbag invalid-action run_start");
    const auto tick_begin_events = select_events_by_type(events, "tick_begin");
    const auto tick_end_events = select_events_by_type(events, "tick_end");
    const auto error_events = select_events_by_type(events, "error");
    check(tick_begin_events.size() == 1, "ros2 rosbag invalid-action: expected one tick_begin event");
    check(tick_end_events.size() == 1, "ros2 rosbag invalid-action: expected one tick_end event");
    check(error_events.size() == 1, "ros2 rosbag invalid-action: expected one error event");
    verify_canonical_event_envelope(tick_begin_events[0], "tick_begin", "ros2 rosbag invalid-action tick_begin", 1);
    verify_canonical_event_envelope(tick_end_events[0], "tick_end", "ros2 rosbag invalid-action tick_end", 1);
    verify_canonical_event_envelope(error_events[0], "error", "ros2 rosbag invalid-action error");
    const value record = require_event_data(tick_end_events[0], "ros2 rosbag invalid-action tick_end");
    verify_common_record_shape(record, "ros2.l2.invalid.v1", 1, 1'700'000'200'000LL, true, false);
    check(require_text_field(record, "status", "ros2 rosbag invalid-action record") == "error",
          "ros2 rosbag invalid-action: record status mismatch");
    verify_pose_x(record, 0.4, "ros2 rosbag invalid-action record");
    verify_action_components(require_map_field(record, "action", "ros2 rosbag invalid-action record"), 0.0, 0.0, 0.0,
                             "ros2 rosbag invalid-action record action");
    const auto error_field = map_lookup_text_key(record, "error");
    check(error_field.has_value() && muslisp::is_string(*error_field),
          "ros2 rosbag invalid-action: missing error field");
    check(muslisp::string_value(*error_field).find("u must be a map") != std::string::npos,
          "ros2 rosbag invalid-action: error field mismatch");
    const muslisp::value on_tick = require_map_field(record, "on_tick", "ros2 rosbag invalid-action record");
    const auto on_tick_u = map_lookup_text_key(on_tick, "u");
    check(on_tick_u.has_value() && muslisp::is_integer(*on_tick_u) && muslisp::integer_value(*on_tick_u) == 1,
          "ros2 rosbag invalid-action: expected malformed on_tick payload to be preserved");

    write_summary(
        paths.summary_path,
        {
            .scenario = "rosbag-invalid-action-fallback-case",
            .status = status,
            .message = message,
            .ticks = ticks,
            .fallback_count = fallback_count,
            .overrun_count = overrun_count,
            .backend_published_actions = backend_published_actions,
            .command_count = harness.command_count(),
            .last_command = command,
            .event_log_lines = lines.size(),
            .final_obs_x = final_obs_x,
        });
    check(std::filesystem::exists(paths.summary_path), "ros2 rosbag invalid-action: missing summary artefact");
}

void test_ros2_reset_unsupported_policy_artifact() {
    using namespace muslisp;

    const scenario_paths paths = prepare_scenario("reset-unsupported-case");
    env_ptr env = create_env_with_ros2_extension();
    (void)eval_text("(env.attach \"ros2\")", env);
    (void)eval_text(
        ros2_configure_script(
            "/l2reset",
            "  (map.set! cfg 'reset_mode \"unsupported\") "),
        env);

    const std::string run_expr =
        "(begin "
        "  (define rosbag-run-result "
        "    (env.run-loop "
        "      (begin "
        "        (define cfg (map.make)) "
        "        (map.set! cfg 'tick_hz 5) "
        "        (map.set! cfg 'max_ticks 1) "
        "        (map.set! cfg 'episode_max 2) "
        "        (map.set! cfg 'schema_version \"ros2.l2.reset-policy.v1\") "
        "        (map.set! cfg 'event_log_path " +
        lisp_string_literal(paths.event_log_path.string()) +
        ") "
        "        cfg) "
        "      (lambda (obs) #t))) "
        "  rosbag-run-result)";
    const value run_result = eval_text(run_expr, env);
    check(is_map(run_result), "ros2 reset policy artefact: env.run-loop should return a map");

    const std::string status = symbol_name(eval_text("(map.get rosbag-run-result 'status ':none)", env));
    check(status == ":unsupported", "ros2 reset policy artefact: expected :unsupported status, got " + status);
    const std::string message = result_message(env);
    check(message.find("episode_max>1 requires env.reset capability") != std::string::npos,
          "ros2 reset policy artefact: message mismatch");
    const std::int64_t ticks = integer_value(eval_text("(map.get rosbag-run-result 'ticks -1)", env));
    check(ticks == 0, "ros2 reset policy artefact: expected zero ticks");
    const std::int64_t fallback_count = integer_value(eval_text("(map.get rosbag-run-result 'fallback_count -1)", env));
    check(fallback_count == 0, "ros2 reset policy artefact: unexpected fallback_count");
    const std::int64_t overrun_count = integer_value(eval_text("(map.get rosbag-run-result 'overrun_count -1)", env));
    check(overrun_count == 0, "ros2 reset policy artefact: unexpected overrun_count");
    const std::int64_t backend_published_actions =
        integer_value(eval_text("(map.get (env.info) 'published_actions -1)", env));
    check(backend_published_actions == 0, "ros2 reset policy artefact: backend should not publish actions");
    const auto lines = read_non_empty_lines(paths.event_log_path);
    check(lines.size() == 2, "ros2 reset policy artefact: expected two canonical events");
    const auto events = decode_json_lines(lines, env);
    const auto run_start_events = select_events_by_type(events, "run_start");
    const auto error_events = select_events_by_type(events, "error");
    check(run_start_events.size() == 1, "ros2 reset policy artefact: expected one run_start event");
    verify_run_start_time_policy(run_start_events[0], "ros2 reset policy run_start");
    check(error_events.size() == 1, "ros2 reset policy artefact: expected one error event");
    verify_canonical_event_envelope(error_events[0], "error", "ros2 reset policy error");

    write_summary(
        paths.summary_path,
        {
            .scenario = "reset-unsupported-case",
            .status = status,
            .message = message,
            .ticks = ticks,
            .fallback_count = fallback_count,
            .overrun_count = overrun_count,
            .backend_published_actions = backend_published_actions,
            .command_count = 0,
            .last_command = geometry_msgs::msg::Twist{},
            .event_log_lines = lines.size(),
            .final_obs_x = std::nullopt,
        });
    check(std::filesystem::exists(paths.summary_path), "ros2 reset policy artefact: missing summary artefact");
}

void cleanup_runtime() {
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
    muslisp::env_api_reset();
}

void run_case(const std::string& label, const std::function<void()>& fn) {
    try {
        fn();
        std::cout << "[PASS] " << label << '\n';
    } catch (...) {
        cleanup_runtime();
        throw;
    }
    cleanup_runtime();
}

}  // namespace

int main() {
    try {
        run_case("ros2 rosbag replay conformance", test_ros2_rosbag_replay_conformance);
        run_case("ros2 rosbag clamp conformance", test_ros2_rosbag_clamp_conformance);
        run_case("ros2 rosbag invalid-action fallback conformance", test_ros2_rosbag_invalid_action_fallback_conformance);
        run_case("ros2 reset policy artefact", test_ros2_reset_unsupported_policy_artifact);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        cleanup_runtime();
        return 1;
    }
}
