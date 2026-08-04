#pragma once

#include <cstdint>
#include <string>

#include "bt/ast.hpp"

namespace bt {

struct walking_target {
    std::string frame_id;
    double x_m = 0.0;
    double y_m = 0.0;
    double yaw_rad = 0.0;
};

struct walking_target_dispatch_context {
    std::int64_t instance_handle = 0;
    std::uint64_t job_id = 0;
    std::uint64_t generation = 0;
    node_id requesting_node = 0;
    node_id authority_node = 0;
    node_id dispatching_node = 0;
    std::string job_key;
    std::string captured_context_id;
    std::string current_context_id;
};

struct walking_target_dispatch_result {
    bool accepted = false;
    std::string reason = "walking_controller_rejected";
};

class walking_target_dispatcher {
public:
    virtual ~walking_target_dispatcher() = default;

    virtual walking_target_dispatch_result dispatch(const walking_target_dispatch_context& context,
                                                     const walking_target& target) = 0;
};

}  // namespace bt
