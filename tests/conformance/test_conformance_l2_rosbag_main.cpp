#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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

void write_summary(const std::filesystem::path& summary_path,
                   const std::string& status,
                   std::int64_t ticks,
                   std::int64_t fallback_count,
                   std::int64_t overrun_count,
                   std::int64_t backend_published_actions,
                   std::size_t command_count,
                   const geometry_msgs::msg::Twist& command,
                   std::size_t log_lines) {
    std::ofstream out(summary_path);
    check(out.good(), "failed to open summary path: " + summary_path.string());
    out << "{\n"
        << "  \"status\": \"" << status << "\",\n"
        << "  \"ticks\": " << ticks << ",\n"
        << "  \"fallback_count\": " << fallback_count << ",\n"
        << "  \"overrun_count\": " << overrun_count << ",\n"
        << "  \"backend_published_actions\": " << backend_published_actions << ",\n"
        << "  \"command_count\": " << command_count << ",\n"
        << "  \"last_command\": {\n"
        << "    \"linear_x\": " << command.linear.x << ",\n"
        << "    \"linear_y\": " << command.linear.y << ",\n"
        << "    \"angular_z\": " << command.angular.z << "\n"
        << "  },\n"
        << "  \"log_lines\": " << log_lines << "\n"
        << "}\n";
}

void test_ros2_rosbag_replay_conformance() {
    using namespace muslisp;

    const std::filesystem::path scenario_dir = l2_artifact_root() / "rosbag-replay-case";
    std::error_code ec;
    std::filesystem::remove_all(scenario_dir, ec);
    std::filesystem::create_directories(scenario_dir);

    const std::string topic_ns = "/l2bag";
    const std::string odom_topic = resolve_topic_name(topic_ns, "odom");
    const std::filesystem::path bag_dir = scenario_dir / "odom_bag";
    const std::filesystem::path log_path = scenario_dir / "run_loop_records.jsonl";
    const std::filesystem::path summary_path = scenario_dir / "summary.json";

    const std::vector<odom_sample> samples = {
        {.t_ns = 1'700'000'000'000'000'000LL, .x = 0.2, .y = 0.0, .yaw = 0.0, .vx = 0.1, .vy = 0.0, .wz = 0.0},
        {.t_ns = 1'700'000'000'040'000'000LL, .x = 1.6, .y = -0.1, .yaw = 0.2, .vx = 0.15, .vy = 0.0, .wz = 0.05},
    };
    write_odometry_bag(bag_dir, odom_topic, samples);

    check(std::filesystem::exists(bag_dir / "metadata.yaml"), "ros2 rosbag conformance: missing bag metadata");

    test_support::ros2_test_harness harness(topic_ns);
    env_ptr env = create_env_with_ros2_extension();
    (void)eval_text("(env.attach \"ros2\")", env);
    (void)eval_text(ros2_configure_script(topic_ns), env);
    check(harness.wait_for_transport_ready(std::chrono::milliseconds(1000)),
          "ros2 rosbag conformance: transport should be ready after configure");

    ros2_bag_replayer player(bag_dir, odom_topic);
    player.start();

    const std::string run_expr =
        "(begin "
        "  (define safe (map.make)) "
        "  (define safe-u (map.make)) "
        "  (map.set! safe 'action_schema \"ros2.action.v1\") "
        "  (map.set! safe 't_ms 0) "
        "  (map.set! safe-u 'linear_x 0.0) "
        "  (map.set! safe-u 'linear_y 0.0) "
        "  (map.set! safe-u 'angular_z 0.0) "
        "  (map.set! safe 'u safe-u) "
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
        "        (map.set! cfg 'log_path " +
        lisp_string_literal(log_path.string()) +
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
    check(is_map(run_result), "ros2 rosbag conformance: env.run-loop should return a map");

    check(player.wait_for_completion(std::chrono::seconds(3)),
          "ros2 rosbag conformance: timed out waiting for rosbag playback");
    check(harness.wait_for_command_count(1, std::chrono::milliseconds(750)),
          "ros2 rosbag conformance: expected at least one published cmd_vel command");

    const std::string status = symbol_name(eval_text("(map.get rosbag-run-result 'status ':none)", env));
    check(status == ":stopped", "ros2 rosbag conformance: expected :stopped status, got " + status);
    const std::int64_t ticks = integer_value(eval_text("(map.get rosbag-run-result 'ticks -1)", env));
    check(ticks == 2, "ros2 rosbag conformance: expected two ticks");
    const std::int64_t fallback_count = integer_value(eval_text("(map.get rosbag-run-result 'fallback_count -1)", env));
    check(fallback_count == 0, "ros2 rosbag conformance: unexpected fallback_count");
    const std::int64_t overrun_count = integer_value(eval_text("(map.get rosbag-run-result 'overrun_count -1)", env));
    check(overrun_count == 0, "ros2 rosbag conformance: unexpected overrun_count");
    const std::int64_t backend_published_actions =
        integer_value(eval_text("(map.get (env.info) 'published_actions -1)", env));
    check(backend_published_actions == 2,
          "ros2 rosbag conformance: backend should report two published actions");

    check_close(float_value(eval_text(
                    "(map.get (map.get (map.get (map.get rosbag-run-result 'final_obs (map.make)) "
                    "'state (map.make)) 'pose (map.make)) 'x 0.0)",
                    env)),
                1.6,
                1e-6,
                "ros2 rosbag conformance: final_obs pose.x mismatch");

    const auto command = harness.last_command();
    check_close(command.linear.x, 0.25, 1e-6, "ros2 rosbag conformance: linear.x mismatch");
    check_close(command.linear.y, 0.0, 1e-6, "ros2 rosbag conformance: linear.y mismatch");
    check_close(command.angular.z, 0.05, 1e-6, "ros2 rosbag conformance: angular.z mismatch");

    const auto lines = read_non_empty_lines(log_path);
    check(lines.size() == 2, "ros2 rosbag conformance: expected two run-loop records");
    check(lines.front().find("\"schema_version\":\"ros2.l2.v1\"") != std::string::npos,
          "ros2 rosbag conformance: first log record missing schema_version");
    check(lines.back().find("\"used_fallback\":false") != std::string::npos,
          "ros2 rosbag conformance: expected used_fallback=false");
    check(lines.back().find("\"overrun\":false") != std::string::npos,
          "ros2 rosbag conformance: expected overrun=false");
    check(lines.back().find("\"action_schema\":\"ros2.action.v1\"") != std::string::npos,
          "ros2 rosbag conformance: missing action schema in log record");

    write_summary(summary_path,
                  status,
                  ticks,
                  fallback_count,
                  overrun_count,
                  backend_published_actions,
                  harness.command_count(),
                  command,
                  lines.size());
    check(std::filesystem::exists(summary_path), "ros2 rosbag conformance: missing summary artifact");
}

}  // namespace

int main() {
    const auto cleanup = []() {
        muslisp::env_api_reset();
        if (rclcpp::ok()) {
            rclcpp::shutdown();
        }
    };

    try {
        test_ros2_rosbag_replay_conformance();
        std::cout << "[PASS] ros2 rosbag replay conformance\n";
        cleanup();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] ros2 rosbag replay conformance: " << e.what() << '\n';
        cleanup();
        return 1;
    }
}
