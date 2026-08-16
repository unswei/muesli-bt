#pragma once

#include <array>
#include <span>

#include "host_client.hpp"

namespace air_hockey_demo {

// Deterministic, public-observation-only recovery used after an authorised
// model result is rejected. A visible puck is the current target; during a
// blackout the mallet remains at its current public position.
[[nodiscard]] std::array<double, kActionDimension>
current_context_recovery_target(std::span<const double> observation);

}  // namespace air_hockey_demo
