#include "fixtures/schedules.hpp"

namespace muesli_bt::bench {
namespace {

std::uint64_t splitmix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

}  // namespace

bool schedule_active(schedule_kind kind, std::uint64_t tick_index, std::uint64_t seed) noexcept {
    if (tick_index == 0) {
        return false;
    }

    switch (kind) {
        case schedule_kind::none:
            return false;
        case schedule_kind::flip_every_100:
            return tick_index % 100u == 0u;
        case schedule_kind::flip_every_20:
            return tick_index % 20u == 0u;
        case schedule_kind::flip_every_5:
            return tick_index % 5u == 0u;
        case schedule_kind::bursty: {
            const std::uint64_t zero_based = tick_index - 1u;
            const std::uint64_t window = zero_based / 16u;
            const std::uint64_t offset = zero_based % 16u;
            const std::uint64_t mixed = splitmix64(seed + window);
            const std::uint64_t start = mixed % 10u;
            const std::uint64_t width = 1u + ((mixed >> 8u) % 4u);
            return offset >= start && offset < start + width;
        }
    }

    return false;
}

}  // namespace muesli_bt::bench
