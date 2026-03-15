#pragma once

#include <cstdint>

#include "harness/scenario.hpp"

namespace muesli_bt::bench {

bool schedule_active(schedule_kind kind, std::uint64_t tick_index, std::uint64_t seed) noexcept;

}  // namespace muesli_bt::bench
