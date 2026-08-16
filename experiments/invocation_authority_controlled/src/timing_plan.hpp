#pragma once

#include "common_task.hpp"
#include "variant.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{

struct timing_run
{
  std::string condition_id;
  std::string axis;
  std::string reader_label;
  std::string variant_label;
  std::uint64_t seed = 0;
  std::size_t repetition = 0;
  logical_time service_delay{};
  std::size_t tick_rate_hz = 0;
  std::size_t concurrent_jobs = 0;
  std::string distribution;
  bool recorded = true;
};

struct timing_plan
{
  std::string protocol_id;
  std::string timing_contract_id;
  std::string initial_context_id;
  logical_time request_deadline{500};
  proposal_validation_config validation;
  std::filesystem::path common_task_path;
  std::vector<timing_run> runs;
};

[[nodiscard]] timing_plan read_timing_plan(const std::filesystem::path& path);

} // namespace muesli_bt::experiments::controlled_authority
