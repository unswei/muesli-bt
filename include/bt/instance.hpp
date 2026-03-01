#pragma once

#include <any>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "bt/ast.hpp"
#include "bt/blackboard.hpp"
#include "bt/logging.hpp"
#include "bt/profile.hpp"
#include "bt/scheduler.hpp"
#include "bt/status.hpp"
#include "bt/trace.hpp"

namespace bt {

struct tick_context;
class planner_service;
class vla_service;

struct node_memory {
    std::int64_t i0 = 0;
    std::int64_t i1 = 0;
    bool b0 = false;
    std::any payload;
};

struct observability {
    trace_buffer* trace = nullptr;
    log_sink* logger = nullptr;
};

class clock_interface {
public:
    virtual ~clock_interface() = default;
    virtual std::chrono::steady_clock::time_point now() const = 0;
};

class robot_interface {
public:
    virtual ~robot_interface() = default;

    virtual bool battery_ok(tick_context& ctx) = 0;
    virtual bool target_visible(tick_context& ctx) = 0;

    virtual status approach_target(tick_context& ctx, node_memory& mem) = 0;
    virtual status grasp(tick_context& ctx, node_memory& mem) = 0;
    virtual status search_target(tick_context& ctx, node_memory& mem) = 0;
};

struct services {
    scheduler* sched = nullptr;
    observability obs{};
    clock_interface* clock = nullptr;
    robot_interface* robot = nullptr;
    planner_service* planner = nullptr;
    vla_service* vla = nullptr;
};

struct instance {
    explicit instance(const definition* definition_ptr = nullptr, std::size_t trace_capacity = 4096);

    const definition* def = nullptr;
    std::int64_t instance_handle = 0;
    std::unordered_map<node_id, node_memory> memory;
    std::unordered_set<node_id> halt_warning_emitted;
    blackboard bb;
    std::uint64_t tick_index = 0;

    bool trace_enabled = true;
    bool read_trace_enabled = false;

    tree_profile_stats tree_stats{};
    std::unordered_map<node_id, node_profile_stats> node_stats;

    trace_buffer trace;
};

}  // namespace bt
