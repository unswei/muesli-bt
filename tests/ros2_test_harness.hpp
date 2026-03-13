#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <geometry_msgs/msg/twist.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>

namespace test_support {

class ros2_test_harness {
public:
    explicit ros2_test_harness(std::string topic_ns)
        : topic_ns_(std::move(topic_ns)) {
        if (!rclcpp::ok()) {
            int argc = 0;
            rclcpp::init(argc, nullptr);
        }

        static std::atomic<std::uint64_t> next_id{1};
        const std::uint64_t id = next_id.fetch_add(1, std::memory_order_relaxed);
        node_ = std::make_shared<rclcpp::Node>("muesli_bt_ros2_test_harness_" + std::to_string(id));
        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(node_);

        odom_pub_ = node_->create_publisher<nav_msgs::msg::Odometry>(resolve_topic("odom"), rclcpp::SystemDefaultsQoS());
        cmd_sub_ = node_->create_subscription<geometry_msgs::msg::Twist>(
            resolve_topic("cmd_vel"),
            rclcpp::SystemDefaultsQoS(),
            [this](const geometry_msgs::msg::Twist::SharedPtr msg) {
                const std::lock_guard<std::mutex> lock(mutex_);
                last_command_ = *msg;
                ++command_count_;
            });
    }

    ~ros2_test_harness() {
        if (executor_ && node_) {
            executor_->remove_node(node_);
        }
    }

    [[nodiscard]] const std::string& topic_ns() const {
        return topic_ns_;
    }

    void publish_odom(double x, double y, double yaw, double vx, double vy, double wz) {
        nav_msgs::msg::Odometry msg;
        msg.header.stamp = node_->get_clock()->now();
        msg.header.frame_id = "map";
        msg.child_frame_id = "base_link";
        msg.pose.pose.position.x = x;
        msg.pose.pose.position.y = y;
        msg.pose.pose.position.z = 0.0;
        msg.pose.pose.orientation.x = 0.0;
        msg.pose.pose.orientation.y = 0.0;
        msg.pose.pose.orientation.z = std::sin(yaw * 0.5);
        msg.pose.pose.orientation.w = std::cos(yaw * 0.5);
        msg.twist.twist.linear.x = vx;
        msg.twist.twist.linear.y = vy;
        msg.twist.twist.angular.z = wz;
        odom_pub_->publish(msg);
        spin_for(std::chrono::milliseconds(25));
    }

    void spin_for(std::chrono::milliseconds duration) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        do {
            executor_->spin_some(std::chrono::milliseconds(0));
            if (duration.count() <= 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
    }

    [[nodiscard]] bool wait_for_command_count(std::size_t expected, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            spin_for(std::chrono::milliseconds(2));
            if (command_count() >= expected) {
                return true;
            }
        }
        spin_for(std::chrono::milliseconds(0));
        return command_count() >= expected;
    }

    [[nodiscard]] bool wait_for_transport_ready(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            spin_for(std::chrono::milliseconds(2));
            if (odom_pub_->get_subscription_count() > 0 && cmd_sub_->get_publisher_count() > 0) {
                return true;
            }
        }
        spin_for(std::chrono::milliseconds(0));
        return odom_pub_->get_subscription_count() > 0 && cmd_sub_->get_publisher_count() > 0;
    }

    [[nodiscard]] std::size_t command_count() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return command_count_;
    }

    [[nodiscard]] geometry_msgs::msg::Twist last_command() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return last_command_;
    }

private:
    [[nodiscard]] std::string resolve_topic(const std::string& leaf) const {
        if (topic_ns_.empty() || topic_ns_ == "/") {
            return "/" + leaf;
        }
        if (topic_ns_.front() == '/') {
            return topic_ns_ + "/" + leaf;
        }
        return "/" + topic_ns_ + "/" + leaf;
    }

    std::string topic_ns_;
    std::shared_ptr<rclcpp::Node> node_{};
    mutable std::mutex mutex_{};
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_{};
    rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_{};
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_{};
    geometry_msgs::msg::Twist last_command_{};
    std::size_t command_count_ = 0;
};

}  // namespace test_support
