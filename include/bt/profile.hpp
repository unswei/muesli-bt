#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "bt/ast.hpp"

namespace bt {

struct duration_stats {
    std::uint64_t count = 0;
    std::chrono::nanoseconds last{0};
    std::chrono::nanoseconds max{0};
    std::chrono::nanoseconds total{0};
    std::uint64_t over_budget_count = 0;

    void observe(std::chrono::nanoseconds sample, std::chrono::nanoseconds budget = std::chrono::nanoseconds{0});
};

struct node_profile_stats {
    node_id id = 0;
    std::string name;
    duration_stats tick_duration;
    std::uint64_t running_returns = 0;
    std::uint64_t success_returns = 0;
    std::uint64_t failure_returns = 0;
};

struct tree_profile_stats {
    duration_stats tick_duration;
    std::uint64_t tick_count = 0;
    std::uint64_t tick_overrun_count = 0;
    std::chrono::nanoseconds configured_tick_budget{0};
};

struct scheduler_profile_stats {
    std::uint64_t submitted = 0;
    std::uint64_t started = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t queue_overflow = 0;

    duration_stats queue_delay;
    duration_stats run_time;
};

}  // namespace bt
