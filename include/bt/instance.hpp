#pragma once

#include <any>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "bt/ast.hpp"
#include "bt/blackboard.hpp"
#include "bt/logging.hpp"
#include "bt/profile.hpp"
#include "bt/scheduler.hpp"
#include "bt/trace.hpp"

namespace bt {

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

struct services {
    scheduler* sched = nullptr;
    observability obs{};
    void* user = nullptr;
};

struct instance {
    explicit instance(const definition* definition_ptr = nullptr, std::size_t trace_capacity = 4096);

    const definition* def = nullptr;
    std::unordered_map<node_id, node_memory> memory;
    blackboard bb;
    std::uint64_t tick_index = 0;

    bool trace_enabled = true;
    bool read_trace_enabled = false;

    tree_profile_stats tree_stats{};
    std::unordered_map<node_id, node_profile_stats> node_stats;

    trace_buffer trace;
};

}  // namespace bt
