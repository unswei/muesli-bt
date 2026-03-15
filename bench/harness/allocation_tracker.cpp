#include "harness/allocation_tracker.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace muesli_bt::bench::allocation_tracker {
namespace detail {

std::atomic<bool> g_enabled{false};
std::atomic<std::uint64_t> g_allocation_count{0};
std::atomic<std::uint64_t> g_allocation_bytes{0};
thread_local bool g_reentrant = false;

void* allocate_raw(std::size_t size, std::size_t alignment) {
    const std::size_t actual_size = size == 0u ? 1u : size;
    if (alignment <= alignof(std::max_align_t)) {
        return std::malloc(actual_size);
    }

    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, actual_size) != 0) {
        return nullptr;
    }
    return ptr;
}

void deallocate_raw(void* ptr) noexcept {
    std::free(ptr);
}

void record_allocation(std::size_t size) noexcept {
    if (g_reentrant || !g_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    g_reentrant = true;
    g_allocation_count.fetch_add(1u, std::memory_order_relaxed);
    g_allocation_bytes.fetch_add(static_cast<std::uint64_t>(size), std::memory_order_relaxed);
    g_reentrant = false;
}

void* checked_allocate(std::size_t size, std::size_t alignment) {
    if (void* ptr = allocate_raw(size, alignment)) {
        record_allocation(size);
        return ptr;
    }
    throw std::bad_alloc();
}

}  // namespace detail

void reset() noexcept {
    detail::g_allocation_count.store(0u, std::memory_order_relaxed);
    detail::g_allocation_bytes.store(0u, std::memory_order_relaxed);
}

void set_enabled(bool enabled_value) noexcept {
    detail::g_enabled.store(enabled_value, std::memory_order_relaxed);
}

bool enabled() noexcept {
    return detail::g_enabled.load(std::memory_order_relaxed);
}

snapshot read() noexcept {
    return snapshot{
        .allocation_count = detail::g_allocation_count.load(std::memory_order_relaxed),
        .allocation_bytes = detail::g_allocation_bytes.load(std::memory_order_relaxed),
    };
}

void on_allocation(std::size_t size) noexcept {
    detail::record_allocation(size);
}

}  // namespace muesli_bt::bench::allocation_tracker

void* operator new(std::size_t size) {
    return muesli_bt::bench::allocation_tracker::detail::checked_allocate(size, alignof(std::max_align_t));
}

void* operator new[](std::size_t size) {
    return operator new(size);
}

void operator delete(void* ptr) noexcept {
    muesli_bt::bench::allocation_tracker::detail::deallocate_raw(ptr);
}

void operator delete[](void* ptr) noexcept {
    operator delete(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    operator delete(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    operator delete[](ptr);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return muesli_bt::bench::allocation_tracker::detail::checked_allocate(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return operator new(size, alignment);
}

void operator delete(void* ptr, std::align_val_t) noexcept {
    muesli_bt::bench::allocation_tracker::detail::deallocate_raw(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
    operator delete(ptr, std::align_val_t{alignof(std::max_align_t)});
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    operator delete(ptr, std::align_val_t{alignof(std::max_align_t)});
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    operator delete[](ptr, std::align_val_t{alignof(std::max_align_t)});
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return operator new[](size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return operator new(size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return operator new[](size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    operator delete(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    operator delete[](ptr);
}

void operator delete(void* ptr, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    operator delete(ptr, alignment);
}

void operator delete[](void* ptr, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    operator delete[](ptr, alignment);
}
