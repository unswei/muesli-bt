#pragma once

#include "timing_plan.hpp"

#include <cstddef>
#include <filesystem>

namespace muesli_bt::experiments::controlled_authority
{

struct timing_engine_result
{
  std::size_t warmups_executed = 0;
  std::size_t trials_written = 0;
  std::filesystem::path raw_results_path;
};

[[nodiscard]] timing_engine_result
execute_timing_plan(const timing_plan& plan, const std::filesystem::path& output_directory);

} // namespace muesli_bt::experiments::controlled_authority
