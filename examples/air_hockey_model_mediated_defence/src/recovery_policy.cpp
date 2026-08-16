#include "recovery_policy.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace air_hockey_demo {
namespace {

constexpr std::size_t kMalletX = 14;
constexpr std::size_t kMalletY = 15;
constexpr std::size_t kPuckX = 16;
constexpr std::size_t kPuckY = 17;
constexpr std::size_t kPuckVisible = 18;

double bounded(double value) {
    return std::clamp(value, -1.0, 1.0);
}

}  // namespace

std::array<double, kActionDimension>
current_context_recovery_target(std::span<const double> observation) {
    if (observation.size() != kObservationDimension) {
        throw std::invalid_argument("air-hockey recovery requires a 19-value public observation");
    }
    if (std::any_of(observation.begin(), observation.end(),
                    [](double value) { return !std::isfinite(value); })) {
        throw std::invalid_argument("air-hockey recovery requires finite public observations");
    }
    const bool puck_visible = observation[kPuckVisible] >= 0.5;
    const std::size_t x = puck_visible ? kPuckX : kMalletX;
    const std::size_t y = puck_visible ? kPuckY : kMalletY;
    return {bounded(observation[x]), bounded(observation[y])};
}

}  // namespace air_hockey_demo
