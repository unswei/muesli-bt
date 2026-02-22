#pragma once

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>

#include "bt/instance.hpp"
#include "bt/registry.hpp"
#include "bt/status.hpp"

namespace bt {

class bt_runtime_error : public std::runtime_error {
public:
    explicit bt_runtime_error(const std::string& message) : std::runtime_error(message) {}
};

struct tick_context {
    instance& inst;
    registry& reg;
    services& svc;
    std::uint64_t tick_index = 0;
    std::chrono::steady_clock::time_point now{};
    node_id current_node = 0;

    void bb_put(std::string key, bb_value value, std::string writer_name = "");
    const bb_entry* bb_get(std::string_view key);
    void scheduler_event(trace_event_kind kind, job_id job, job_status st, std::string message = "");
};

status tick(instance& inst, registry& reg, services& svc);
void reset(instance& inst);

std::string dump_stats(const instance& inst);
std::string dump_trace(const instance& inst);
std::string dump_blackboard(const instance& inst);

void set_tick_budget_ms(instance& inst, std::int64_t budget_ms);

}  // namespace bt
