#include "bt/event_payload.hpp"

#include <sstream>

#include "bt/event_log.hpp"

namespace bt::event_payload {

std::string job_node_status(std::string_view job_id, node_id node, std::string_view status) {
    std::ostringstream data;
    data << "{\"job_id\":\"" << event_log::json_escape(job_id) << "\",\"node_id\":" << node
         << ",\"status\":\"" << event_log::json_escape(status) << "\"}";
    return data.str();
}

std::string job_node_reason(std::string_view job_id, node_id node, std::string_view reason) {
    std::ostringstream data;
    data << "{\"job_id\":\"" << event_log::json_escape(job_id) << "\",\"node_id\":" << node
         << ",\"reason\":\"" << event_log::json_escape(reason) << "\"}";
    return data.str();
}

std::string job_node_accepted(std::string_view job_id, node_id node, bool accepted) {
    std::ostringstream data;
    data << "{\"job_id\":\"" << event_log::json_escape(job_id) << "\",\"node_id\":" << node
         << ",\"accepted\":" << (accepted ? "true" : "false") << '}';
    return data.str();
}

std::string vla_result(std::string_view job_id, node_id node, std::string_view status, std::string_view digest) {
    std::ostringstream data;
    data << "{\"job_id\":\"" << event_log::json_escape(job_id) << "\",\"node_id\":" << node
         << ",\"status\":\"" << event_log::json_escape(status) << "\",\"digest\":\""
         << event_log::json_escape(digest) << "\"}";
    return data.str();
}

std::string planner_call_start(node_id node, std::string_view planner, std::int64_t budget_ms) {
    std::ostringstream data;
    data << "{\"node_id\":" << node << ",\"planner\":\"" << event_log::json_escape(planner)
         << "\",\"budget_ms\":" << budget_ms << '}';
    return data.str();
}

}  // namespace bt::event_payload
