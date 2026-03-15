#pragma once

#include <cstddef>
#include <cstdint>

namespace muesli_bt::bench::allocation_tracker {

struct snapshot {
    std::uint64_t allocation_count = 0;
    std::uint64_t allocation_bytes = 0;
};

void reset() noexcept;
void set_enabled(bool enabled) noexcept;
bool enabled() noexcept;
snapshot read() noexcept;
void on_allocation(std::size_t size) noexcept;

}  // namespace muesli_bt::bench::allocation_tracker
