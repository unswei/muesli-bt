#pragma once

#include "campaign_engine.hpp"
#include "campaign_plan.hpp"

#include <filesystem>

namespace muesli_bt::experiments::controlled_authority
{

[[nodiscard]] campaign_engine_result execute_btcpp_comparison_plan(
    const campaign_plan& plan, const std::filesystem::path& output_directory);

} // namespace muesli_bt::experiments::controlled_authority
