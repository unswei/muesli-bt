#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nav2_msgs/action/navigate_to_pose.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

namespace test_support {

class nav2_fake_action_server {
public:
    enum class mode {
        accept_succeed,
        reject_goal,
        accept_abort,
        accept_delay,
        slow_goal_accept
    };

    explicit nav2_fake_action_server(std::string action_name, mode scripted_mode = mode::accept_succeed)
        : action_name_(std::move(action_name)), mode_(scripted_mode) {
        if (!rclcpp::ok()) {
            int argc = 0;
            rclcpp::init(argc, nullptr);
        }
        static std::atomic<std::uint64_t> next_id{1};
        const std::uint64_t id = next_id.fetch_add(1, std::memory_order_relaxed);
        node_ = std::make_shared<rclcpp::Node>("muesli_bt_nav2_fake_action_server_" + std::to_string(id));
        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(node_);

        server_ = rclcpp_action::create_server<nav2_msgs::action::NavigateToPose>(
            node_,
            action_name_,
            [this](const rclcpp_action::GoalUUID&,
                   std::shared_ptr<const nav2_msgs::action::NavigateToPose::Goal> goal) {
                if (mode_ == mode::slow_goal_accept) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                }
                const std::lock_guard<std::mutex> lock(mutex_);
                last_goal_ = *goal;
                ++goal_count_;
                if (mode_ == mode::reject_goal) {
                    return rclcpp_action::GoalResponse::REJECT;
                }
                return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
            },
            [this](const std::shared_ptr<rclcpp_action::ServerGoalHandle<nav2_msgs::action::NavigateToPose>>) {
                const std::lock_guard<std::mutex> lock(mutex_);
                ++cancel_count_;
                return rclcpp_action::CancelResponse::ACCEPT;
            },
            [this](const std::shared_ptr<rclcpp_action::ServerGoalHandle<nav2_msgs::action::NavigateToPose>> goal_handle) {
                const std::lock_guard<std::mutex> lock(workers_mutex_);
                workers_.emplace_back([this, goal_handle]() { execute(goal_handle); });
            });

        spin_thread_ = std::thread([this]() { spin_loop(); });
    }

    ~nav2_fake_action_server() {
        running_.store(false, std::memory_order_release);
        if (spin_thread_.joinable()) {
            spin_thread_.join();
        }
        std::vector<std::thread> workers;
        {
            const std::lock_guard<std::mutex> lock(workers_mutex_);
            workers.swap(workers_);
        }
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        if (executor_ && node_) {
            executor_->remove_node(node_);
        }
    }

    [[nodiscard]] const std::string& action_name() const {
        return action_name_;
    }

    [[nodiscard]] bool wait_for_goal_count(std::size_t expected, std::chrono::milliseconds timeout) const {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (goal_count() >= expected) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return goal_count() >= expected;
    }

    [[nodiscard]] bool wait_for_cancel_count(std::size_t expected, std::chrono::milliseconds timeout) const {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancel_count() >= expected) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return cancel_count() >= expected;
    }

    [[nodiscard]] std::size_t goal_count() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return goal_count_;
    }

    [[nodiscard]] std::size_t cancel_count() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return cancel_count_;
    }

    [[nodiscard]] nav2_msgs::action::NavigateToPose::Goal last_goal() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return last_goal_;
    }

private:
    void spin_loop() {
        while (running_.load(std::memory_order_acquire)) {
            executor_->spin_some(std::chrono::milliseconds(0));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void execute(const std::shared_ptr<rclcpp_action::ServerGoalHandle<nav2_msgs::action::NavigateToPose>>& goal_handle) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto feedback = std::make_shared<nav2_msgs::action::NavigateToPose::Feedback>();
        feedback->current_pose.header.frame_id = "map";
        feedback->current_pose.pose.position.x = 0.5;
        feedback->current_pose.pose.position.y = 0.25;
        feedback->current_pose.pose.orientation.w = 1.0;
        feedback->navigation_time.sec = 1;
        feedback->estimated_time_remaining.sec = 2;
        feedback->number_of_recoveries = 1;
        feedback->distance_remaining = 0.75F;
        goal_handle->publish_feedback(feedback);

        const auto sleep_for = mode_ == mode::accept_delay ? std::chrono::milliseconds(500) : std::chrono::milliseconds(40);
        const auto deadline = std::chrono::steady_clock::now() + sleep_for;
        while (std::chrono::steady_clock::now() < deadline) {
            if (goal_handle->is_canceling()) {
                goal_handle->canceled(std::make_shared<nav2_msgs::action::NavigateToPose::Result>());
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (goal_handle->is_canceling()) {
            goal_handle->canceled(std::make_shared<nav2_msgs::action::NavigateToPose::Result>());
            return;
        }
        if (mode_ == mode::accept_abort) {
            goal_handle->abort(std::make_shared<nav2_msgs::action::NavigateToPose::Result>());
            return;
        }
        goal_handle->succeed(std::make_shared<nav2_msgs::action::NavigateToPose::Result>());
    }

    std::string action_name_;
    mode mode_;
    std::shared_ptr<rclcpp::Node> node_{};
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_{};
    rclcpp_action::Server<nav2_msgs::action::NavigateToPose>::SharedPtr server_{};
    std::atomic<bool> running_{true};
    std::thread spin_thread_{};
    mutable std::mutex mutex_{};
    mutable std::mutex workers_mutex_{};
    std::vector<std::thread> workers_{};
    nav2_msgs::action::NavigateToPose::Goal last_goal_{};
    std::size_t goal_count_ = 0;
    std::size_t cancel_count_ = 0;
};

}  // namespace test_support
