#include "ros2/nav2_capability.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "muslisp/gc.hpp"

namespace muslisp::integrations::ros2 {
namespace {

using NavigateToPose = nav2_msgs::action::NavigateToPose;
using NavigateGoalHandle = rclcpp_action::ClientGoalHandle<NavigateToPose>;

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

std::string require_text_value(value v, const std::string& where) {
    if (is_string(v)) {
        return string_value(v);
    }
    if (is_symbol(v)) {
        return symbol_name(v);
    }
    throw std::runtime_error(where + ": expected string or symbol");
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

double require_map_number(value map_obj, const std::string& key, const std::string& where) {
    const std::optional<value> found = map_lookup_option(map_obj, key);
    if (!found.has_value()) {
        throw std::runtime_error(where + ": missing numeric field");
    }
    return require_number_value(*found, where);
}

std::string map_lookup_text_or(value map_obj, const std::string& key, std::string default_value, const std::string& where) {
    const std::optional<value> found = map_lookup_option(map_obj, key);
    if (!found.has_value()) {
        return default_value;
    }
    return require_text_value(*found, where);
}

std::int64_t map_lookup_int_or(value map_obj, const std::string& key, std::int64_t default_value, const std::string& where) {
    const std::optional<value> found = map_lookup_option(map_obj, key);
    if (!found.has_value()) {
        return default_value;
    }
    if (!is_integer(*found)) {
        throw std::runtime_error(where + ": expected integer");
    }
    return integer_value(*found);
}

std::int64_t request_timeout_ms(value request_map, std::int64_t fallback) {
    const std::int64_t deadline = map_lookup_int_or(request_map, "deadline_ms", -1, "cap.navigation deadline_ms");
    if (deadline >= 0) {
        return deadline;
    }
    const std::int64_t timeout = map_lookup_int_or(request_map, "timeout_ms", -1, "cap.navigation timeout_ms");
    if (timeout >= 0) {
        return timeout;
    }
    return fallback;
}

value make_pose_map(const geometry_msgs::msg::PoseStamped& pose) {
    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);
    map_set_symbol(out, "frame", make_string(pose.header.frame_id));
    map_set_symbol(out, "x", make_float(pose.pose.position.x));
    map_set_symbol(out, "y", make_float(pose.pose.position.y));
    map_set_symbol(out, "z", make_float(pose.pose.position.z));
    map_set_symbol(out, "qx", make_float(pose.pose.orientation.x));
    map_set_symbol(out, "qy", make_float(pose.pose.orientation.y));
    map_set_symbol(out, "qz", make_float(pose.pose.orientation.z));
    map_set_symbol(out, "qw", make_float(pose.pose.orientation.w));
    return out;
}

value make_progress_map(const NavigateToPose::Feedback* feedback) {
    value progress = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&progress);
    if (feedback == nullptr) {
        map_set_symbol(progress, "status", make_symbol(":running"));
        return progress;
    }
    value current_pose = make_pose_map(feedback->current_pose);
    roots.add(&current_pose);
    const std::int64_t navigation_time_ms =
        static_cast<std::int64_t>(feedback->navigation_time.sec) * 1000LL +
        static_cast<std::int64_t>(feedback->navigation_time.nanosec / 1000000U);
    const std::int64_t estimated_time_remaining_ms =
        static_cast<std::int64_t>(feedback->estimated_time_remaining.sec) * 1000LL +
        static_cast<std::int64_t>(feedback->estimated_time_remaining.nanosec / 1000000U);
    map_set_symbol(progress, "status", make_symbol(":running"));
    map_set_symbol(progress, "current_pose", current_pose);
    map_set_symbol(progress, "navigation_time_ms", make_integer(navigation_time_ms));
    map_set_symbol(progress, "estimated_time_remaining_ms", make_integer(estimated_time_remaining_ms));
    map_set_symbol(progress, "number_of_recoveries", make_integer(feedback->number_of_recoveries));
    map_set_symbol(progress, "distance_remaining_m", make_float(feedback->distance_remaining));
    return progress;
}

geometry_msgs::msg::PoseStamped target_to_pose(value request_map, rclcpp::Node& node) {
    const std::optional<value> target = map_lookup_option(request_map, "target");
    if (!target.has_value() || !is_map(*target)) {
        throw std::runtime_error("cap.navigation: target must be a map");
    }
    geometry_msgs::msg::PoseStamped out;
    out.header.stamp = node.get_clock()->now();
    out.header.frame_id = map_lookup_text_or(*target, "frame", "map", "cap.navigation target.frame");
    out.pose.position.x = require_map_number(*target, "x", "cap.navigation target.x");
    out.pose.position.y = require_map_number(*target, "y", "cap.navigation target.y");
    if (const std::optional<value> z = map_lookup_option(*target, "z"); z.has_value()) {
        out.pose.position.z = require_number_value(*z, "cap.navigation target.z");
    }
    out.pose.orientation.w = 1.0;
    if (const std::optional<value> yaw = map_lookup_option(*target, "yaw"); yaw.has_value()) {
        const double half_yaw = require_number_value(*yaw, "cap.navigation target.yaw") * 0.5;
        out.pose.orientation.z = std::sin(half_yaw);
        out.pose.orientation.w = std::cos(half_yaw);
    }
    if (const std::optional<value> qx = map_lookup_option(*target, "qx"); qx.has_value()) {
        out.pose.orientation.x = require_number_value(*qx, "cap.navigation target.qx");
    }
    if (const std::optional<value> qy = map_lookup_option(*target, "qy"); qy.has_value()) {
        out.pose.orientation.y = require_number_value(*qy, "cap.navigation target.qy");
    }
    if (const std::optional<value> qz = map_lookup_option(*target, "qz"); qz.has_value()) {
        out.pose.orientation.z = require_number_value(*qz, "cap.navigation target.qz");
    }
    if (const std::optional<value> qw = map_lookup_option(*target, "qw"); qw.has_value()) {
        out.pose.orientation.w = require_number_value(*qw, "cap.navigation target.qw");
    }
    return out;
}

value result_map(const std::string& operation,
                 const std::string& request_id,
                 const std::string& status,
                 bool host_reached,
                 const std::string& job_id = {},
                 value progress = make_nil(),
                 const std::string& error_code = {},
                 const std::string& error = {}) {
    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);
    roots.add(&progress);
    map_set_symbol(out, "schema_version", make_string("cap.navigation.result.v1"));
    map_set_symbol(out, "capability", make_string("cap.navigation.v1"));
    map_set_symbol(out, "operation", make_string(operation));
    map_set_symbol(out, "request_id", make_string(request_id));
    map_set_symbol(out, "status", make_symbol(":" + status));
    map_set_symbol(out, "adapter", make_string("nav2"));
    map_set_symbol(out, "adapter_schema", make_string("cap.navigation.v1.nav2.adapter.v1"));
    map_set_symbol(out, "host_reached", make_boolean(host_reached));
    map_set_symbol(out, "validation_status", make_symbol(error_code.empty() ? ":accepted" : ":rejected"));
    if (!job_id.empty()) {
        map_set_symbol(out, "job_id", make_string(job_id));
    }
    if (!is_nil(progress)) {
        map_set_symbol(out, "progress", progress);
    }
    if (!error_code.empty()) {
        map_set_symbol(out, "validation_reason_code", make_string(error_code));
        map_set_symbol(out, "error_code", make_string(error_code));
        map_set_symbol(out, "error", make_string(error));
    }
    return out;
}

std::string next_job_id(const std::string& request_id) {
    static std::atomic<std::uint64_t> next_id{1};
    return request_id + "-nav2-" + std::to_string(next_id.fetch_add(1, std::memory_order_relaxed));
}

std::string result_status(const NavigateGoalHandle::WrappedResult& result) {
    switch (result.code) {
        case rclcpp_action::ResultCode::SUCCEEDED:
            return "ok";
        case rclcpp_action::ResultCode::CANCELED:
            return "cancelled";
        case rclcpp_action::ResultCode::ABORTED:
            return "error";
        case rclcpp_action::ResultCode::UNKNOWN:
        default:
            return "error";
    }
}

class nav2_navigation_capability final : public cap_backend {
public:
    nav2_navigation_capability() {
        if (!rclcpp::ok()) {
            int argc = 0;
            rclcpp::init(argc, nullptr);
        }
        node_ = std::make_shared<rclcpp::Node>("muesli_bt_nav2_capability");
        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(node_);
    }

    ~nav2_navigation_capability() override {
        if (executor_ && node_) {
            executor_->cancel();
            executor_->remove_node(node_);
        }
    }

    [[nodiscard]] bt::capability_descriptor describe() const override {
        bt::capability_descriptor cap;
        cap.name = "cap.navigation.v1";
        cap.safety_class = "ros_action";
        cap.cost_category = "navigation";
        cap.adapter_id = "nav2";
        cap.operations = {"navigate-to-pose", "status", "cancel"};
        cap.frames = {"map", "odom"};
        cap.default_timeout_ms = 100;
        cap.supports_cancellation = true;
        cap.supports_replay = true;
        cap.request_schema = {
            {"schema_version", "string", true},
            {"capability", "string", true},
            {"operation", "string", true},
            {"request_id", "string", false},
            {"target", "map", false},
            {"job_id", "string", false},
            {"timeout_ms", "int", false},
            {"deadline_ms", "int", false},
            {"action_name", "string", false},
        };
        cap.response_schema = {
            {"schema_version", "string", true},
            {"capability", "string", true},
            {"operation", "string", true},
            {"request_id", "string", false},
            {"status", "keyword", true},
            {"adapter", "string", true},
            {"adapter_schema", "string", true},
            {"host_reached", "boolean", true},
            {"job_id", "string", false},
            {"progress", "map", false},
            {"request_hash", "string", true},
            {"response_hash", "string", true},
        };
        return cap;
    }

    [[nodiscard]] value call(value request_map) override {
        const std::string operation = map_lookup_text_or(request_map, "operation", "", "cap.navigation operation");
        const std::string request_id = map_lookup_text_or(request_map, "request_id", "nav2-request", "cap.navigation request_id");
        try {
            if (operation == "navigate-to-pose") {
                return navigate_to_pose(request_map, request_id);
            }
            if (operation == "status") {
                return status(request_map, request_id);
            }
            if (operation == "cancel") {
                return cancel(request_map, request_id);
            }
            return result_map(operation, request_id, "rejected", false, {}, make_nil(), "unsupported_operation",
                              "operation is not supported by the Nav2 adapter");
        } catch (const std::exception& e) {
            return result_map(operation.empty() ? "unknown" : operation,
                              request_id,
                              "rejected",
                              false,
                              {},
                              make_nil(),
                              "invalid_request",
                              e.what());
        }
    }

private:
    struct job_record {
        std::string job_id;
        std::string action_name;
        NavigateGoalHandle::SharedPtr handle;
        std::shared_ptr<const NavigateToPose::Feedback> last_feedback;
        std::optional<NavigateGoalHandle::WrappedResult> result;
    };

    rclcpp_action::Client<NavigateToPose>::SharedPtr client_for(const std::string& action_name) {
        auto it = clients_.find(action_name);
        if (it != clients_.end()) {
            return it->second;
        }
        auto client = rclcpp_action::create_client<NavigateToPose>(node_, action_name);
        clients_[action_name] = client;
        return client;
    }

    value navigate_to_pose(value request_map, const std::string& request_id) {
        const std::string action_name = map_lookup_text_or(request_map, "action_name", "/navigate_to_pose", "cap.navigation action_name");
        const std::int64_t timeout_ms = request_timeout_ms(request_map, 100);
        auto client = client_for(action_name);
        if (!client->wait_for_action_server(std::chrono::milliseconds(timeout_ms))) {
            return result_map("navigate-to-pose", request_id, "unavailable", false, {}, make_nil(), "server_unavailable",
                              "NavigateToPose action server is unavailable");
        }

        NavigateToPose::Goal goal;
        goal.pose = target_to_pose(request_map, *node_);
        const std::string job_id = next_job_id(request_id);

        rclcpp_action::Client<NavigateToPose>::SendGoalOptions options;
        options.feedback_callback =
            [this, job_id](NavigateGoalHandle::SharedPtr, const std::shared_ptr<const NavigateToPose::Feedback> feedback) {
                const std::lock_guard<std::mutex> lock(mutex_);
                auto it = jobs_.find(job_id);
                if (it != jobs_.end()) {
                    it->second.last_feedback = feedback;
                }
            };
        options.result_callback = [this, job_id](const NavigateGoalHandle::WrappedResult& result) {
            const std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(job_id);
            if (it != jobs_.end()) {
                it->second.result = result;
            }
        };

        auto goal_future = client->async_send_goal(goal, options);
        if (executor_->spin_until_future_complete(goal_future, std::chrono::milliseconds(timeout_ms)) !=
            rclcpp::FutureReturnCode::SUCCESS) {
            return result_map("navigate-to-pose", request_id, "timeout", true, {}, make_nil(), "goal_accept_timeout",
                              "NavigateToPose goal acceptance timed out");
        }
        NavigateGoalHandle::SharedPtr handle = goal_future.get();
        if (!handle) {
            return result_map("navigate-to-pose", request_id, "rejected", true, {}, make_nil(), "goal_rejected",
                              "NavigateToPose action server rejected the goal");
        }

        {
            const std::lock_guard<std::mutex> lock(mutex_);
            jobs_[job_id] = job_record{
                .job_id = job_id,
                .action_name = action_name,
                .handle = handle,
                .last_feedback = {},
                .result = std::nullopt,
            };
        }
        return result_map("navigate-to-pose", request_id, "accepted", true, job_id, make_progress_map(nullptr));
    }

    std::optional<job_record> find_job(const std::string& job_id) {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(job_id);
        if (it == jobs_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    value status(value request_map, const std::string& request_id) {
        const std::string job_id = map_lookup_text_or(request_map, "job_id", "", "cap.navigation job_id");
        if (job_id.empty()) {
            return result_map("status", request_id, "rejected", false, {}, make_nil(), "missing_job_id",
                              "status requires job_id");
        }
        executor_->spin_some(std::chrono::milliseconds(0));
        const std::optional<job_record> job = find_job(job_id);
        if (!job.has_value()) {
            return result_map("status", request_id, "rejected", false, {}, make_nil(), "unknown_job_id",
                              "job_id is not known");
        }
        value progress = make_progress_map(job->last_feedback.get());
        if (job->result.has_value()) {
            return result_map("status", request_id, result_status(*job->result), true, job_id, progress);
        }
        return result_map("status", request_id, "running", true, job_id, progress);
    }

    value cancel(value request_map, const std::string& request_id) {
        const std::string job_id = map_lookup_text_or(request_map, "job_id", "", "cap.navigation job_id");
        if (job_id.empty()) {
            return result_map("cancel", request_id, "rejected", false, {}, make_nil(), "missing_job_id",
                              "cancel requires job_id");
        }
        const std::optional<job_record> job = find_job(job_id);
        if (!job.has_value() || !job->handle) {
            return result_map("cancel", request_id, "rejected", false, {}, make_nil(), "unknown_job_id",
                              "job_id is not known");
        }
        const std::int64_t timeout_ms = request_timeout_ms(request_map, 100);
        auto client = client_for(job->action_name);
        auto cancel_future = client->async_cancel_goal(job->handle);
        if (executor_->spin_until_future_complete(cancel_future, std::chrono::milliseconds(timeout_ms)) !=
            rclcpp::FutureReturnCode::SUCCESS) {
            return result_map("cancel", request_id, "timeout", true, job_id, make_nil(), "cancel_timeout",
                              "NavigateToPose cancel timed out");
        }
        return result_map("cancel", request_id, "cancelled", true, job_id, make_nil());
    }

    mutable std::mutex mutex_{};
    std::shared_ptr<rclcpp::Node> node_{};
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_{};
    std::unordered_map<std::string, rclcpp_action::Client<NavigateToPose>::SharedPtr> clients_{};
    std::unordered_map<std::string, job_record> jobs_{};
};

}  // namespace

std::shared_ptr<cap_backend> make_nav2_navigation_capability() {
    return std::make_shared<nav2_navigation_capability>();
}

}  // namespace muslisp::integrations::ros2
