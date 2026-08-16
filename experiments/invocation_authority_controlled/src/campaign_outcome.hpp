#pragma once

#include "campaign_trial.hpp"

#include <string>

namespace muesli_bt::experiments::controlled_authority
{

[[nodiscard]] bool expected_outcome_holds(const trial_result& result);
[[nodiscard]] std::string decision_signature(const trial_result& result);

} // namespace muesli_bt::experiments::controlled_authority
