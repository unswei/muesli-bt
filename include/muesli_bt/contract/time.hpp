#pragma once

#include <cstdint>
#include <optional>

namespace muesli_bt::contract {

using tick_index = std::uint64_t;
using unix_time_ms = std::int64_t;
using duration_ms = std::int64_t;

struct tick_timing {
    tick_index tick = 0;
    duration_ms tick_budget_ms = 0;
    std::optional<duration_ms> tick_elapsed_ms{};
    std::optional<duration_ms> tick_remaining_ms{};
    std::optional<unix_time_ms> tick_deadline_unix_ms{};
};

}  // namespace muesli_bt::contract
