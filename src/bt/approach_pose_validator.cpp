#include "bt/approach_pose_validator.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace bt {
namespace {

bool valid_interval(double lo, double hi) {
    return std::isfinite(lo) && std::isfinite(hi) && lo <= hi;
}

bool within(double value, double lo, double hi) {
    return value >= lo && value <= hi;
}

vla_commit_validation reject(std::string reason) {
    return vla_commit_validation{.accepted = false, .reason = std::move(reason)};
}

}  // namespace

approach_pose_validator::approach_pose_validator(approach_pose_validator_config config,
                                                 approach_pose_host_state_provider host_state_provider)
    : config_(std::move(config)), host_state_provider_(std::move(host_state_provider)) {
    if (config_.frame_id.empty()) {
        throw std::invalid_argument("approach pose validator frame_id must not be empty");
    }
    if (!valid_interval(config_.bounds.min_x_m, config_.bounds.max_x_m) ||
        !valid_interval(config_.bounds.min_y_m, config_.bounds.max_y_m) ||
        !valid_interval(config_.bounds.min_yaw_rad, config_.bounds.max_yaw_rad)) {
        throw std::invalid_argument("approach pose validator bounds must be finite and ordered");
    }
    if (!host_state_provider_) {
        throw std::invalid_argument("approach pose validator host-state provider must be set");
    }
}

vla_commit_validation approach_pose_validator::validate(const vla_commit_context& context,
                                                        const vla_action& action) {
    const approach_pose_host_state host_state = host_state_provider_();
    if (context.captured_context_id.empty() || context.current_context_id.empty() ||
        host_state.ball_context_id.empty()) {
        return reject("ball_stale");
    }
    if (context.captured_context_id != context.current_context_id ||
        host_state.ball_context_id != context.current_context_id) {
        return reject("context_changed");
    }
    if (!host_state.robot_stable) {
        return reject("robot_unstable");
    }
    if (action.type != vla_action_type::continuous || action.u.size() != 3) {
        return reject("invalid_schema");
    }
    if (context.expected_action_frame != config_.frame_id || action.frame_id != config_.frame_id) {
        return reject("invalid_frame");
    }

    const double x_m = action.u[0];
    const double y_m = action.u[1];
    const double yaw_rad = action.u[2];
    if (!std::isfinite(x_m) || !std::isfinite(y_m) || !std::isfinite(yaw_rad) ||
        !within(x_m, config_.bounds.min_x_m, config_.bounds.max_x_m) ||
        !within(y_m, config_.bounds.min_y_m, config_.bounds.max_y_m) ||
        !within(yaw_rad, config_.bounds.min_yaw_rad, config_.bounds.max_yaw_rad)) {
        return reject("invalid_pose");
    }

    return vla_commit_validation{.accepted = true, .reason = {}};
}

}  // namespace bt
