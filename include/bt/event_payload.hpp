#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "bt/ast.hpp"

namespace bt::event_payload {

std::string job_node_status(std::string_view job_id, node_id node, std::string_view status);
std::string job_node_reason(std::string_view job_id, node_id node, std::string_view reason);
std::string job_node_accepted(std::string_view job_id, node_id node, bool accepted);
std::string vla_result(std::string_view job_id, node_id node, std::string_view status, std::string_view digest);
std::string planner_call_start(node_id node, std::string_view planner, std::int64_t budget_ms);

}  // namespace bt::event_payload
