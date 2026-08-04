#pragma once

#include <functional>
#include <string>

#include "bt/vla.hpp"

namespace bt {

struct approach_pose_bounds {
    double min_x_m = 0.0;
    double max_x_m = 0.0;
    double min_y_m = 0.0;
    double max_y_m = 0.0;
    double min_yaw_rad = 0.0;
    double max_yaw_rad = 0.0;
};

struct approach_pose_host_state {
    std::string ball_context_id;
    bool robot_stable = false;
};

struct approach_pose_validator_config {
    std::string frame_id;
    approach_pose_bounds bounds{};
};

using approach_pose_host_state_provider = std::function<approach_pose_host_state()>;

class approach_pose_validator final : public vla_commit_validator {
public:
    approach_pose_validator(approach_pose_validator_config config,
                            approach_pose_host_state_provider host_state_provider);

    vla_commit_validation validate(const vla_commit_context& context,
                                   const vla_action& action) override;

private:
    approach_pose_validator_config config_;
    approach_pose_host_state_provider host_state_provider_;
};

}  // namespace bt
