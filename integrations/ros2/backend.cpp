#include "ros2/backend.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <geometry_msgs/msg/twist.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>

#include "muslisp/gc.hpp"

namespace muslisp::integrations::ros2 {
namespace {

std::string normalize_option_key(std::string key) {
    if (!key.empty() && key.front() == ':') {
        key.erase(key.begin());
    }
    for (char& c : key) {
        if (c == '-') {
            c = '_';
        }
    }
    return key;
}

std::optional<value> map_lookup_option(value map_obj, const std::string& normalized_key) {
    if (!is_map(map_obj)) {
        return std::nullopt;
    }
    for (const auto& [key, val] : map_obj->map_data) {
        if (key.type != map_key_type::symbol && key.type != map_key_type::string) {
            continue;
        }
        if (normalize_option_key(key.text_data) == normalized_key) {
            return val;
        }
    }
    return std::nullopt;
}

map_key symbol_key(const std::string& name) {
    map_key key;
    key.type = map_key_type::symbol;
    key.text_data = name;
    return key;
}

void map_set_symbol(value map_obj, const std::string& key_name, value v) {
    map_obj->map_data[symbol_key(key_name)] = v;
}

double require_number_value(value v, const std::string& where) {
    if (is_integer(v)) {
        return static_cast<double>(integer_value(v));
    }
    if (is_float(v)) {
        return float_value(v);
    }
    throw std::runtime_error(where + ": expected numeric value");
}

std::string require_text_value(value v, const std::string& where) {
    if (is_string(v)) {
        return string_value(v);
    }
    if (is_symbol(v)) {
        return symbol_name(v);
    }
    throw std::runtime_error(where + ": expected string or symbol");
}

bool require_bool_value(value v, const std::string& where) {
    if (!is_boolean(v)) {
        throw std::runtime_error(where + ": expected boolean");
    }
    return boolean_value(v);
}

value numeric_vector_to_lisp_list(const std::vector<double>& values) {
    std::vector<value> out;
    out.reserve(values.size());
    gc_root_scope roots(default_gc());
    for (double v : values) {
        out.push_back(make_float(v));
        roots.add(&out.back());
    }
    return list_from_vector(out);
}

value string_vector_to_lisp_list(const std::vector<std::string>& values) {
    std::vector<value> out;
    out.reserve(values.size());
    gc_root_scope roots(default_gc());
    for (const std::string& v : values) {
        out.push_back(make_string(v));
        roots.add(&out.back());
    }
    return list_from_vector(out);
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

std::string resolve_topic_name(const std::string& topic_ns, const std::string& name) {
    if (name.empty()) {
        throw std::runtime_error("topic name must not be empty");
    }
    if (name.front() == '/') {
        return name;
    }
    const std::string ns = normalise_topic_ns(topic_ns);
    if (ns.empty()) {
        return "/" + name;
    }
    return ns + "/" + name;
}

struct parsed_schema_id {
    std::string prefix;
    std::int64_t major = 0;
};

bool is_schema_segment_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

parsed_schema_id parse_schema_id(const std::string& schema_id, const std::string& where) {
    if (schema_id.empty()) {
        throw std::runtime_error(where + ": schema id must not be empty");
    }
    for (char c : schema_id) {
        if (c != '.' && !is_schema_segment_char(c)) {
            throw std::runtime_error(where + ": invalid schema id: " + schema_id);
        }
    }

    const std::size_t suffix_pos = schema_id.rfind(".v");
    if (suffix_pos == std::string::npos || suffix_pos == 0 || suffix_pos + 2 >= schema_id.size()) {
        throw std::runtime_error(where + ": schema id must end with .v<major>: " + schema_id);
    }
    if (schema_id[suffix_pos - 1] == '.') {
        throw std::runtime_error(where + ": schema id contains empty segment: " + schema_id);
    }
    for (std::size_t i = 0; i < suffix_pos; ++i) {
        if (schema_id[i] == '.' && (i == 0 || i + 1 == suffix_pos || schema_id[i + 1] == '.')) {
            throw std::runtime_error(where + ": schema id contains empty segment: " + schema_id);
        }
    }
    for (std::size_t i = suffix_pos + 2; i < schema_id.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(schema_id[i]))) {
            throw std::runtime_error(where + ": schema id major version must be numeric: " + schema_id);
        }
    }

    parsed_schema_id out;
    out.prefix = schema_id.substr(0, suffix_pos);
    out.major = std::stoll(schema_id.substr(suffix_pos + 2));
    return out;
}

void validate_backend_version_guard(const std::string& backend_version, const std::string& where) {
    const parsed_schema_id parsed = parse_schema_id(backend_version, where);
    if (parsed.prefix != "ros2.transport" || parsed.major != 1) {
        throw std::runtime_error(where + ": unsupported backend_version: " + backend_version +
                                 " (expected ros2.transport.v1)");
    }
}

void validate_ros2_schema_id_family(const std::string& schema_id,
                                    const std::string& expected_prefix,
                                    const std::string& where) {
    const parsed_schema_id parsed = parse_schema_id(schema_id, where);
    const std::string nested_prefix = expected_prefix + ".";
    if (parsed.prefix != expected_prefix && parsed.prefix.rfind(nested_prefix, 0) != 0) {
        throw std::runtime_error(where + ": schema id must stay within the " + expected_prefix +
                                 ".v1 family: " + schema_id);
    }
    if (parsed.major != 1) {
        throw std::runtime_error(where + ": unsupported schema major version: " + schema_id + " (expected v1)");
    }
}

std::int64_t stamp_to_ms(const builtin_interfaces::msg::Time& stamp) {
    return static_cast<std::int64_t>(stamp.sec) * 1000LL + static_cast<std::int64_t>(stamp.nanosec / 1000000U);
}

double yaw_from_quaternion(double qx, double qy, double qz, double qw) {
    const double siny_cosp = 2.0 * (qw * qz + qx * qy);
    const double cosy_cosp = 1.0 - 2.0 * (qy * qy + qz * qz);
    return std::atan2(siny_cosp, cosy_cosp);
}

value make_pose_map(double x, double y, double z, double qx, double qy, double qz, double qw) {
    value pose = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&pose);

    map_set_symbol(pose, "x", make_float(x));
    map_set_symbol(pose, "y", make_float(y));
    map_set_symbol(pose, "z", make_float(z));
    map_set_symbol(pose, "qx", make_float(qx));
    map_set_symbol(pose, "qy", make_float(qy));
    map_set_symbol(pose, "qz", make_float(qz));
    map_set_symbol(pose, "qw", make_float(qw));
    return pose;
}

value make_twist_map(double vx, double vy, double vz, double wx, double wy, double wz) {
    value twist = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&twist);

    map_set_symbol(twist, "vx", make_float(vx));
    map_set_symbol(twist, "vy", make_float(vy));
    map_set_symbol(twist, "vz", make_float(vz));
    map_set_symbol(twist, "wx", make_float(wx));
    map_set_symbol(twist, "wy", make_float(wy));
    map_set_symbol(twist, "wz", make_float(wz));
    return twist;
}

void shutdown_ros2_backend_process_runtime() {
    try {
        env_api_reset();
    } catch (const std::exception&) {
        // best effort during process shutdown
    }
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
}

void register_ros2_backend_process_cleanup() {
    static std::once_flag cleanup_once;
    std::call_once(cleanup_once, []() { std::atexit(shutdown_ros2_backend_process_runtime); });
}

struct action_command {
    std::int64_t t_ms = 0;
    double linear_x = 0.0;
    double linear_y = 0.0;
    double angular_z = 0.0;
};

struct observation_snapshot {
    bool available = false;
    std::uint64_t generation = 0;
    std::int64_t t_ms = 0;
    std::string frame_id = "map";
    std::string child_frame_id = "base_link";
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double qx = 0.0;
    double qy = 0.0;
    double qz = 0.0;
    double qw = 1.0;
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;
    double wx = 0.0;
    double wy = 0.0;
    double wz = 0.0;
    std::string source = "odom";
};

class ros2_env_backend final : public env_backend {
public:
    ros2_env_backend() = default;

    ~ros2_env_backend() override {
        if (executor_ && node_) {
            executor_->remove_node(node_);
        }
        subscription_.reset();
        publisher_.reset();
        node_.reset();
        executor_.reset();
    }

    [[nodiscard]] std::string backend_version() const override {
        return "ros2.transport.v1";
    }

    [[nodiscard]] env_backend_supports supports() const override {
        env_backend_supports out;
        out.reset = reset_mode_ == "stub";
        out.debug_draw = false;
        out.headless = true;
        out.realtime_pacing = true;
        out.deterministic_seed = false;
        return out;
    }

    [[nodiscard]] std::string notes() const override {
        return "ROS2 odom/cmd_vel backend with canonical env.api.v1 schemas and bounded executor progress";
    }

    [[nodiscard]] value info() const override {
        value out = make_map();
        value config = make_map();
        gc_root_scope roots(default_gc());
        roots.add(&out);
        roots.add(&config);

        const std::lock_guard<std::mutex> lock(mutex_);

        map_set_symbol(out, "env_api", make_string("env.api.v1"));
        map_set_symbol(out, "obs_schema", make_string(obs_schema_));
        map_set_symbol(out, "state_schema", make_string(state_schema_));
        map_set_symbol(out, "action_schema", make_string(action_schema_));
        map_set_symbol(out, "reset_supported", make_boolean(supports().reset));
        map_set_symbol(out, "run_loop_supported", make_boolean(true));
        map_set_symbol(out, "capabilities", string_vector_to_lisp_list(capability_tags()));
        map_set_symbol(out, "obs_topic", make_string(obs_topic_));
        map_set_symbol(out, "action_topic", make_string(action_topic_));
        map_set_symbol(out, "node_name", make_string(node_name_));
        map_set_symbol(out, "ros_distro", make_string(ros_distro_));
        map_set_symbol(out, "received_samples", make_integer(static_cast<std::int64_t>(observation_generation_)));
        map_set_symbol(out, "published_actions", make_integer(published_action_count_));

        map_set_symbol(config, "control_hz", make_integer(control_hz_));
        map_set_symbol(config, "observe_timeout_ms", make_integer(observe_timeout_ms_));
        map_set_symbol(config, "step_timeout_ms", make_integer(step_timeout_ms_));
        map_set_symbol(config, "use_sim_time", make_boolean(use_sim_time_));
        map_set_symbol(config, "require_fresh_obs", make_boolean(require_fresh_obs_));
        map_set_symbol(config, "action_clamp", make_string(action_clamp_));
        map_set_symbol(config, "topic_ns", make_string(topic_ns_));
        map_set_symbol(config, "obs_source", make_string(obs_source_));
        map_set_symbol(config, "action_sink", make_string(action_sink_));
        map_set_symbol(config, "reset_mode", make_string(reset_mode_));
        map_set_symbol(out, "config", config);

        return out;
    }

    void configure(value opts) override {
        if (!is_map(opts)) {
            throw std::runtime_error("configure: expected map");
        }

        static const std::unordered_set<std::string> kKnownKeys = {
            "tick_hz",
            "max_ticks",
            "step_max",
            "episode_max",
            "steps_per_tick",
            "seed",
            "headless",
            "realtime",
            "log_path",
            "safe_action",
            "stop_on_success",
            "success_predicate",
            "observer",
            "schema_version",
            "backend_version",
            "obs_schema",
            "state_schema",
            "action_schema",
            "control_hz",
            "observe_timeout_ms",
            "step_timeout_ms",
            "use_sim_time",
            "require_fresh_obs",
            "action_clamp",
            "topic_ns",
            "obs_source",
            "action_sink",
            "reset_mode",
        };

        for (const auto& [key, _] : opts->map_data) {
            if (key.type != map_key_type::symbol && key.type != map_key_type::string) {
                throw std::runtime_error("configure: option keys must be strings or symbols");
            }
            const std::string normalized = normalize_option_key(key.text_data);
            if (kKnownKeys.find(normalized) == kKnownKeys.end()) {
                throw std::runtime_error("configure: unknown option: " + normalized);
            }
        }

        std::string obs_schema = obs_schema_;
        std::string state_schema = state_schema_;
        std::string action_schema = action_schema_;
        std::int64_t control_hz = control_hz_;
        std::int64_t observe_timeout_ms = observe_timeout_ms_;
        std::int64_t step_timeout_ms = step_timeout_ms_;
        bool use_sim_time = use_sim_time_;
        bool require_fresh_obs = require_fresh_obs_;
        std::string action_clamp = action_clamp_;
        std::string topic_ns = topic_ns_;
        std::string obs_source = obs_source_;
        std::string action_sink = action_sink_;
        std::string reset_mode = reset_mode_;

        if (const auto candidate = map_lookup_option(opts, "obs_schema")) {
            obs_schema = require_text_value(*candidate, "configure.obs_schema");
        }
        if (const auto candidate = map_lookup_option(opts, "state_schema")) {
            state_schema = require_text_value(*candidate, "configure.state_schema");
        }
        if (const auto candidate = map_lookup_option(opts, "action_schema")) {
            action_schema = require_text_value(*candidate, "configure.action_schema");
        }
        if (const auto candidate = map_lookup_option(opts, "backend_version")) {
            validate_backend_version_guard(require_text_value(*candidate, "configure.backend_version"),
                                           "configure.backend_version");
        }
        validate_ros2_schema_id_family(obs_schema, "ros2.obs", "configure.obs_schema");
        validate_ros2_schema_id_family(state_schema, "ros2.state", "configure.state_schema");
        validate_ros2_schema_id_family(action_schema, "ros2.action", "configure.action_schema");
        if (const auto candidate = map_lookup_option(opts, "control_hz")) {
            if (!is_integer(*candidate)) {
                throw std::runtime_error("configure: control_hz must be integer");
            }
            control_hz = integer_value(*candidate);
            if (control_hz <= 0) {
                throw std::runtime_error("configure: control_hz must be > 0");
            }
        }
        if (const auto candidate = map_lookup_option(opts, "observe_timeout_ms")) {
            if (!is_integer(*candidate)) {
                throw std::runtime_error("configure: observe_timeout_ms must be integer");
            }
            observe_timeout_ms = integer_value(*candidate);
            if (observe_timeout_ms <= 0) {
                throw std::runtime_error("configure: observe_timeout_ms must be > 0");
            }
        }
        if (const auto candidate = map_lookup_option(opts, "step_timeout_ms")) {
            if (!is_integer(*candidate)) {
                throw std::runtime_error("configure: step_timeout_ms must be integer");
            }
            step_timeout_ms = integer_value(*candidate);
            if (step_timeout_ms <= 0) {
                throw std::runtime_error("configure: step_timeout_ms must be > 0");
            }
        }
        if (const auto candidate = map_lookup_option(opts, "use_sim_time")) {
            use_sim_time = require_bool_value(*candidate, "configure.use_sim_time");
        }
        if (const auto candidate = map_lookup_option(opts, "require_fresh_obs")) {
            require_fresh_obs = require_bool_value(*candidate, "configure.require_fresh_obs");
        }
        if (const auto candidate = map_lookup_option(opts, "action_clamp")) {
            action_clamp = require_text_value(*candidate, "configure.action_clamp");
            if (action_clamp != "clamp" && action_clamp != "reject") {
                throw std::runtime_error("configure: action_clamp must be 'clamp' or 'reject'");
            }
        }
        if (const auto candidate = map_lookup_option(opts, "topic_ns")) {
            topic_ns = normalise_topic_ns(require_text_value(*candidate, "configure.topic_ns"));
        }
        if (const auto candidate = map_lookup_option(opts, "obs_source")) {
            obs_source = require_text_value(*candidate, "configure.obs_source");
        }
        if (const auto candidate = map_lookup_option(opts, "action_sink")) {
            action_sink = require_text_value(*candidate, "configure.action_sink");
        }
        if (const auto candidate = map_lookup_option(opts, "reset_mode")) {
            reset_mode = require_text_value(*candidate, "configure.reset_mode");
            if (reset_mode != "stub" && reset_mode != "unsupported") {
                throw std::runtime_error("configure: reset_mode must be 'stub' or 'unsupported'");
            }
        }

        obs_schema_ = std::move(obs_schema);
        state_schema_ = std::move(state_schema);
        action_schema_ = std::move(action_schema);
        control_hz_ = control_hz;
        observe_timeout_ms_ = observe_timeout_ms;
        step_timeout_ms_ = step_timeout_ms;
        use_sim_time_ = use_sim_time;
        require_fresh_obs_ = require_fresh_obs;
        action_clamp_ = std::move(action_clamp);
        topic_ns_ = std::move(topic_ns);
        obs_source_ = std::move(obs_source);
        action_sink_ = std::move(action_sink);
        reset_mode_ = std::move(reset_mode);

        ensure_runtime();
        rebuild_transport();
    }

    [[nodiscard]] value reset(std::optional<std::int64_t> seed) override {
        if (reset_mode_ != "stub") {
            throw std::runtime_error("reset is unsupported for the current reset_mode");
        }
        if (seed.has_value()) {
            seed_ = *seed;
        }

        ensure_runtime();
        publish_zero_twist();

        observation_snapshot snapshot;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            latest_observation_ = observation_snapshot{};
            latest_observation_->frame_id = frame_id_;
            latest_observation_->child_frame_id = child_frame_id_;
            latest_observation_->source = "reset_stub";
            latest_observation_->generation = ++observation_generation_;
            latest_observation_->t_ms = node_now_ms();
            last_observed_generation_ = latest_observation_->generation;
            last_action_ = action_command{};
            published_action_count_ = 0;
            snapshot = *latest_observation_;
        }

        return observation_to_value(snapshot, false);
    }

    [[nodiscard]] value observe() override {
        ensure_runtime();

        bool fresh = false;
        const auto baseline_generation = current_generation();
        if ((require_fresh_obs_ || !has_cached_observation()) &&
            wait_for_generation_change(baseline_generation, std::chrono::milliseconds(observe_timeout_ms_))) {
            fresh = true;
        } else {
            pump_executor_for(std::chrono::milliseconds(0));
        }

        observation_snapshot snapshot;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (latest_observation_.has_value()) {
                snapshot = *latest_observation_;
                fresh = snapshot.generation > last_observed_generation_;
                last_observed_generation_ = snapshot.generation;
            } else {
                if (require_fresh_obs_) {
                    throw std::runtime_error("no observation available within observe_timeout_ms");
                }
                snapshot = placeholder_observation();
            }
        }

        return observation_to_value(snapshot, fresh);
    }

    void act(value action) override {
        ensure_runtime();
        const action_command command = parse_action(action);

        geometry_msgs::msg::Twist twist;
        twist.linear.x = command.linear_x;
        twist.linear.y = command.linear_y;
        twist.angular.z = command.angular_z;
        publisher_->publish(twist);

        const std::lock_guard<std::mutex> lock(mutex_);
        last_action_ = command;
        ++published_action_count_;
    }

    [[nodiscard]] bool step() override {
        ensure_runtime();
        pump_executor_for(std::chrono::milliseconds(step_timeout_ms_));
        return true;
    }

private:
    [[nodiscard]] std::vector<std::string> capability_tags() const {
        std::vector<std::string> tags = {"observe", "act", "step", "run_loop", "event_log"};
        if (supports().reset) {
            tags.push_back("reset");
        }
        return tags;
    }

    void ensure_runtime() {
        register_ros2_backend_process_cleanup();
        if (!rclcpp::ok()) {
            int argc = 0;
            rclcpp::init(argc, nullptr);
        }
        if (node_) {
            if (node_->has_parameter("use_sim_time")) {
                node_->set_parameter(rclcpp::Parameter("use_sim_time", use_sim_time_));
            }
            return;
        }

        static std::atomic<std::uint64_t> next_backend_id{1};
        const std::uint64_t backend_id = next_backend_id.fetch_add(1, std::memory_order_relaxed);
        node_name_ = "muesli_bt_ros2_backend_" + std::to_string(backend_id);

        rclcpp::NodeOptions options;
        options.automatically_declare_parameters_from_overrides(true);
        options.append_parameter_override("use_sim_time", use_sim_time_);
        node_ = std::make_shared<rclcpp::Node>(node_name_, options);
        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(node_);
    }

    void rebuild_transport() {
        obs_topic_ = resolve_topic_name(topic_ns_, obs_source_);
        action_topic_ = resolve_topic_name(topic_ns_, action_sink_);

        subscription_.reset();
        publisher_.reset();

        publisher_ = node_->create_publisher<geometry_msgs::msg::Twist>(action_topic_, rclcpp::SystemDefaultsQoS());
        subscription_ = node_->create_subscription<nav_msgs::msg::Odometry>(
            obs_topic_,
            rclcpp::SystemDefaultsQoS(),
            [this](const nav_msgs::msg::Odometry::SharedPtr msg) { handle_odometry(*msg); });
    }

    void handle_odometry(const nav_msgs::msg::Odometry& msg) {
        observation_snapshot snapshot;
        snapshot.available = true;
        snapshot.generation = 0;
        if (msg.header.stamp.sec != 0 || msg.header.stamp.nanosec != 0) {
            snapshot.t_ms = stamp_to_ms(msg.header.stamp);
        } else {
            snapshot.t_ms = node_now_ms();
        }
        snapshot.frame_id = msg.header.frame_id.empty() ? frame_id_ : msg.header.frame_id;
        snapshot.child_frame_id = msg.child_frame_id.empty() ? child_frame_id_ : msg.child_frame_id;
        snapshot.x = msg.pose.pose.position.x;
        snapshot.y = msg.pose.pose.position.y;
        snapshot.z = msg.pose.pose.position.z;
        snapshot.qx = msg.pose.pose.orientation.x;
        snapshot.qy = msg.pose.pose.orientation.y;
        snapshot.qz = msg.pose.pose.orientation.z;
        snapshot.qw = msg.pose.pose.orientation.w;
        snapshot.vx = msg.twist.twist.linear.x;
        snapshot.vy = msg.twist.twist.linear.y;
        snapshot.vz = msg.twist.twist.linear.z;
        snapshot.wx = msg.twist.twist.angular.x;
        snapshot.wy = msg.twist.twist.angular.y;
        snapshot.wz = msg.twist.twist.angular.z;
        snapshot.source = obs_source_;

        const std::lock_guard<std::mutex> lock(mutex_);
        snapshot.generation = ++observation_generation_;
        latest_observation_ = snapshot;
    }

    void pump_executor_for(std::chrono::milliseconds budget) {
        if (!executor_) {
            return;
        }
        const auto deadline = std::chrono::steady_clock::now() + budget;
        do {
            executor_->spin_some(std::chrono::milliseconds(0));
            if (budget.count() <= 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
    }

    [[nodiscard]] bool wait_for_generation_change(std::uint64_t baseline, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            pump_executor_for(std::chrono::milliseconds(0));
            if (current_generation() > baseline) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        pump_executor_for(std::chrono::milliseconds(0));
        return current_generation() > baseline;
    }

    [[nodiscard]] bool has_cached_observation() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return latest_observation_.has_value();
    }

    [[nodiscard]] std::uint64_t current_generation() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return observation_generation_;
    }

    [[nodiscard]] action_command parse_action(value action) const {
        if (!is_map(action)) {
            throw std::runtime_error("env.act: expected action map");
        }
        const auto schema_value = map_lookup_option(action, "action_schema");
        if (!schema_value.has_value()) {
            throw std::runtime_error("env.act: missing action_schema");
        }
        if (!is_string(*schema_value)) {
            throw std::runtime_error("env.act: action_schema must be string");
        }
        if (string_value(*schema_value) != action_schema_) {
            throw std::runtime_error("env.act: action_schema mismatch");
        }

        const auto t_ms_value = map_lookup_option(action, "t_ms");
        if (!t_ms_value.has_value()) {
            throw std::runtime_error("env.act: missing t_ms");
        }
        if (!is_integer(*t_ms_value)) {
            throw std::runtime_error("env.act: t_ms must be integer");
        }

        const auto u_value = map_lookup_option(action, "u");
        if (!u_value.has_value()) {
            throw std::runtime_error("env.act: missing action key 'u'");
        }
        if (!is_map(*u_value)) {
            throw std::runtime_error("env.act: u must be a map with linear_x, linear_y, and angular_z");
        }

        const auto linear_x_value = map_lookup_option(*u_value, "linear_x");
        const auto linear_y_value = map_lookup_option(*u_value, "linear_y");
        const auto angular_z_value = map_lookup_option(*u_value, "angular_z");
        if (!linear_x_value.has_value() || !linear_y_value.has_value() || !angular_z_value.has_value()) {
            throw std::runtime_error("env.act: u must contain linear_x, linear_y, and angular_z");
        }

        const double raw_linear_x = require_number_value(*linear_x_value, "env.act :u.linear_x");
        const double raw_linear_y = require_number_value(*linear_y_value, "env.act :u.linear_y");
        const double raw_angular_z = require_number_value(*angular_z_value, "env.act :u.angular_z");

        if (!std::isfinite(raw_linear_x) || !std::isfinite(raw_linear_y) || !std::isfinite(raw_angular_z)) {
            throw std::runtime_error("env.act: action values must be finite");
        }

        if (action_clamp_ == "reject" &&
            (std::abs(raw_linear_x) > 1.0 || std::abs(raw_linear_y) > 1.0 || std::abs(raw_angular_z) > 1.0)) {
            throw std::runtime_error("env.act: action values out of range for reject policy");
        }

        action_command out;
        out.t_ms = integer_value(*t_ms_value);
        out.linear_x = std::clamp(raw_linear_x, -1.0, 1.0);
        out.linear_y = std::clamp(raw_linear_y, -1.0, 1.0);
        out.angular_z = std::clamp(raw_angular_z, -1.0, 1.0);
        return out;
    }

    [[nodiscard]] observation_snapshot placeholder_observation() const {
        observation_snapshot snapshot;
        snapshot.t_ms = node_now_ms();
        snapshot.frame_id = frame_id_;
        snapshot.child_frame_id = child_frame_id_;
        snapshot.source = obs_source_;
        return snapshot;
    }

    [[nodiscard]] value observation_to_value(const observation_snapshot& snapshot, bool fresh) const {
        value obs = make_map();
        value state = make_map();
        value pose = make_pose_map(snapshot.x, snapshot.y, snapshot.z, snapshot.qx, snapshot.qy, snapshot.qz, snapshot.qw);
        value twist = make_twist_map(snapshot.vx, snapshot.vy, snapshot.vz, snapshot.wx, snapshot.wy, snapshot.wz);
        value flags = make_map();
        value info = make_map();
        gc_root_scope roots(default_gc());
        roots.add(&obs);
        roots.add(&state);
        roots.add(&pose);
        roots.add(&twist);
        roots.add(&flags);
        roots.add(&info);

        const double yaw = yaw_from_quaternion(snapshot.qx, snapshot.qy, snapshot.qz, snapshot.qw);
        value state_vec = numeric_vector_to_lisp_list({snapshot.x, snapshot.y, yaw, snapshot.vx, snapshot.vy, snapshot.wz});
        roots.add(&state_vec);

        action_command last_action;
        std::int64_t published_actions = 0;
        std::uint64_t generations = 0;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            last_action = last_action_;
            published_actions = published_action_count_;
            generations = observation_generation_;
        }

        map_set_symbol(obs, "obs_schema", make_string(obs_schema_));
        map_set_symbol(obs, "state_schema", make_string(state_schema_));
        map_set_symbol(obs, "t_ms", make_integer(snapshot.t_ms));
        map_set_symbol(obs, "state", state);
        map_set_symbol(obs, "state_vec", state_vec);
        map_set_symbol(obs, "flags", flags);
        map_set_symbol(obs, "info", info);
        map_set_symbol(obs, "done", make_boolean(false));

        map_set_symbol(state, "state_schema", make_string(state_schema_));
        map_set_symbol(state, "frame_id", make_string(snapshot.frame_id));
        map_set_symbol(state, "child_frame_id", make_string(snapshot.child_frame_id));
        map_set_symbol(state, "t_ms", make_integer(snapshot.t_ms));
        map_set_symbol(state, "pose", pose);
        map_set_symbol(state, "twist", twist);
        map_set_symbol(state, "state_vec", state_vec);
        map_set_symbol(state, "source", make_string(snapshot.source));

        map_set_symbol(flags, "fresh_obs", make_boolean(fresh));
        map_set_symbol(flags, "has_sample", make_boolean(snapshot.available));
        map_set_symbol(flags, "require_fresh_obs", make_boolean(require_fresh_obs_));
        map_set_symbol(flags, "use_sim_time", make_boolean(use_sim_time_));

        map_set_symbol(info, "obs_source", make_string(obs_source_));
        map_set_symbol(info, "action_sink", make_string(action_sink_));
        map_set_symbol(info, "topic_ns", make_string(topic_ns_));
        map_set_symbol(info, "obs_topic", make_string(obs_topic_));
        map_set_symbol(info, "action_topic", make_string(action_topic_));
        map_set_symbol(info, "node_name", make_string(node_name_));
        map_set_symbol(info, "received_samples", make_integer(static_cast<std::int64_t>(generations)));
        map_set_symbol(info, "published_actions", make_integer(published_actions));
        map_set_symbol(info, "last_action_t_ms", make_integer(last_action.t_ms));
        map_set_symbol(info, "last_action_linear_x", make_float(last_action.linear_x));
        map_set_symbol(info, "last_action_linear_y", make_float(last_action.linear_y));
        map_set_symbol(info, "last_action_angular_z", make_float(last_action.angular_z));
        if (seed_.has_value()) {
            map_set_symbol(info, "seed", make_integer(*seed_));
        }

        return obs;
    }

    void publish_zero_twist() {
        if (!publisher_) {
            return;
        }
        geometry_msgs::msg::Twist twist;
        publisher_->publish(twist);
    }

    [[nodiscard]] std::int64_t node_now_ms() const {
        if (node_) {
            return static_cast<std::int64_t>(node_->get_clock()->now().nanoseconds() / 1000000LL);
        }
        return 0;
    }

    mutable std::mutex mutex_{};
    std::string obs_schema_ = "ros2.obs.v1";
    std::string state_schema_ = "ros2.state.v1";
    std::string action_schema_ = "ros2.action.v1";
    std::int64_t control_hz_ = 20;
    std::int64_t observe_timeout_ms_ = 50;
    std::int64_t step_timeout_ms_ = 50;
    std::optional<std::int64_t> seed_{};
    bool use_sim_time_ = false;
    bool require_fresh_obs_ = false;
    std::string action_clamp_ = "clamp";
    std::string topic_ns_{};
    std::string obs_source_ = "odom";
    std::string action_sink_ = "cmd_vel";
    std::string reset_mode_ = "unsupported";
    std::string frame_id_ = "map";
    std::string child_frame_id_ = "base_link";
    std::string node_name_{};
    std::string obs_topic_ = "/odom";
    std::string action_topic_ = "/cmd_vel";
    std::string ros_distro_ = "humble";
    std::shared_ptr<rclcpp::Node> node_{};
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_{};
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr subscription_{};
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr publisher_{};
    std::optional<observation_snapshot> latest_observation_{};
    std::uint64_t observation_generation_ = 0;
    std::uint64_t last_observed_generation_ = 0;
    action_command last_action_{};
    std::int64_t published_action_count_ = 0;
};

}  // namespace

std::shared_ptr<env_backend> make_backend() {
    return std::make_shared<ros2_env_backend>();
}

}  // namespace muslisp::integrations::ros2
