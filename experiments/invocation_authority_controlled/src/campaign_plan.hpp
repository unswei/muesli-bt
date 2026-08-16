#pragma once

#include "common_task.hpp"
#include "variant.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{

struct planned_event
{
  logical_time at{};
  std::string event;
  std::string request;
  std::string context_id;
  std::size_t count = 0;
  bool duplicate = false;
  std::int64_t ordering = -1;
};

struct schedule_definition
{
  std::string schedule_id;
  std::vector<planned_event> events;
};

struct planned_run
{
  std::string schedule_id;
  std::string variant_label;
  std::uint64_t seed = 0;
  std::string expected_outcome;
};

struct campaign_plan
{
  std::string protocol_id;
  std::string catalogue_id;
  std::string matrix_id;
  std::string initial_context_id;
  logical_time request_deadline{500};
  proposal_validation_config validation;
  std::filesystem::path common_task_path;
  std::unordered_map<std::string, schedule_definition> schedules;
  std::vector<planned_run> runs;
};

[[nodiscard]] campaign_plan read_campaign_plan(const std::filesystem::path& path);

} // namespace muesli_bt::experiments::controlled_authority
