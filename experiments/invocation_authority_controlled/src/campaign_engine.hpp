#pragma once

#include "campaign_plan.hpp"

#include <filesystem>

namespace muesli_bt::experiments::controlled_authority
{

struct campaign_engine_result
{
  std::size_t trials_written = 0;
  std::filesystem::path raw_results_path;
};

[[nodiscard]] campaign_engine_result
execute_campaign_plan(const campaign_plan& plan, const std::filesystem::path& output_directory);

} // namespace muesli_bt::experiments::controlled_authority
