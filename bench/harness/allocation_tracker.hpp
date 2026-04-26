#pragma once

#include <cstddef>
#include <cstdint>

namespace muesli_bt::bench::allocation_tracker {

struct snapshot {
    std::uint64_t allocation_count = 0;
    std::uint64_t allocation_bytes = 0;
    std::uint64_t allocation_failure_count = 0;
    std::uint64_t whitelisted_allocation_count = 0;
    std::uint64_t whitelisted_allocation_bytes = 0;
};

void reset() noexcept;
void set_enabled(bool enabled) noexcept;
bool enabled() noexcept;
void set_fail_on_unwhitelisted_allocation(bool enabled) noexcept;
bool fail_on_unwhitelisted_allocation() noexcept;
snapshot read() noexcept;
void on_allocation(std::size_t size) noexcept;
void enter_whitelisted_allocation_path() noexcept;
void leave_whitelisted_allocation_path() noexcept;

class whitelisted_allocation_scope final {
public:
    whitelisted_allocation_scope() noexcept;
    whitelisted_allocation_scope(const whitelisted_allocation_scope&) = delete;
    whitelisted_allocation_scope& operator=(const whitelisted_allocation_scope&) = delete;
    ~whitelisted_allocation_scope();
};

}  // namespace muesli_bt::bench::allocation_tracker
