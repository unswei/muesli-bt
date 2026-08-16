#pragma once

#include "effect_recorder.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{

struct trial_result
{
  std::string schedule_id;
  std::string variant_label;
  std::string variant_id;
  std::uint64_t seed = 0;
  std::string expected_outcome;
  std::string observed_outcome;
  bool expected_outcome_met = false;
  effect_summary summary;
  std::vector<effect_record> effects;
  std::size_t expected_requests = 0;
  std::size_t submitted_requests = 0;
  std::size_t blocked_submissions = 0;
  std::size_t active_jobs_at_end = 0;
  double maximum_tick_ms = 0.0;
  std::optional<double> intervention_to_fallback_ms;
  bool replay_equal = true;
  std::vector<std::string> replay_mismatch_schedule_ids;
  std::vector<std::vector<std::string>> task_streams;
  std::vector<std::vector<std::string>> variant_streams;
};

} // namespace muesli_bt::experiments::controlled_authority
