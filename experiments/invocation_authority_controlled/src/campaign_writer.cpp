#include "campaign_writer.hpp"

#include "bt/event_log.hpp"

#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

std::string json_string(std::string_view value)
{
  return "\"" + bt::event_log::json_escape(value) + "\"";
}

const char* json_boolean(bool value) noexcept
{
  return value ? "true" : "false";
}

std::string run_stem(const trial_result& result)
{
  return result.schedule_id + "_" + result.variant_label + "_" + std::to_string(result.seed);
}

std::vector<std::string> write_streams(const std::filesystem::path& output_directory,
                                       const trial_result& result,
                                       const std::vector<std::vector<std::string>>& streams,
                                       std::string_view stream_kind)
{
  std::vector<std::string> paths;
  for (std::size_t index = 0; index < streams.size(); ++index)
  {
    std::ostringstream name;
    name << run_stem(result) << '.' << stream_kind;
    if (streams.size() > 1)
    {
      name << '.' << std::setw(2) << std::setfill('0') << index;
    }
    name << ".mbt.evt.v1.jsonl";
    const std::filesystem::path relative = std::filesystem::path("events") / name.str();
    std::ofstream output(output_directory / relative);
    if (!output)
    {
      throw std::runtime_error("could not write campaign event stream");
    }
    for (const std::string& line : streams[index])
    {
      output << line << '\n';
    }
    paths.push_back(relative.generic_string());
  }
  return paths;
}

void write_string_array(std::ostream& output, const std::vector<std::string>& values)
{
  output << '[';
  for (std::size_t index = 0; index < values.size(); ++index)
  {
    if (index > 0)
    {
      output << ',';
    }
    output << json_string(values[index]);
  }
  output << ']';
}

void write_effect(std::ostream& output, const effect_record& effect)
{
  output << "{\"sequence\":" << effect.sequence
         << ",\"kind\":" << json_string(to_string(effect.kind))
         << ",\"at_ms\":" << effect.at.count()
         << ",\"response_id\":" << json_string(effect.response_id)
         << ",\"reason\":" << json_string(effect.reason)
         << ",\"authority_assessed\":" << json_boolean(effect.authority_assessed);
  if (effect.request)
  {
    output << ",\"request_id\":" << effect.request->request_id
           << ",\"branch_epoch\":" << effect.request->branch_epoch
           << ",\"generation\":" << effect.request->generation
           << ",\"reset_epoch\":" << effect.request->reset_epoch
           << ",\"captured_context_id\":" << json_string(effect.request->captured_context_id)
           << ",\"submitted_at_ms\":" << effect.request->submitted_at.count()
           << ",\"deadline_ms\":" << effect.request->deadline.count();
  }
  if (effect.authority_assessed)
  {
    output << ",\"authority_current\":" << json_boolean(effect.authority.current)
           << ",\"authority_reason\":" << json_string(to_string(effect.authority.reason));
  }
  output << '}';
}

bool false_rejection(const trial_result& result)
{
  return std::any_of(result.effects.begin(), result.effects.end(),
                     [](const effect_record& effect)
                     {
                       return effect.kind == effect_kind::result_rejected &&
                              effect.authority_assessed && effect.authority.current &&
                              effect.reason != "invalid_schema" &&
                              effect.reason != "invalid_pose" && effect.reason != "invalid_frame" &&
                              effect.reason != "robot_unstable" && effect.reason != "ball_stale" &&
                              effect.reason != "backend_terminal_failure" &&
                              effect.reason != "cancelled";
                     });
}

void write_raw_result(std::ostream& output, const trial_result& result,
                      const std::vector<std::string>& task_paths,
                      const std::vector<std::string>& variant_paths)
{
  const effect_summary& summary = result.summary;
  output << "{\"schema_version\":\"controlled-authority.raw-trial.v1\""
         << ",\"schedule_id\":" << json_string(result.schedule_id)
         << ",\"variant_label\":" << json_string(result.variant_label)
         << ",\"variant_id\":" << json_string(result.variant_id) << ",\"seed\":" << result.seed
         << ",\"expected_outcome\":" << json_string(result.expected_outcome)
         << ",\"observed_outcome\":" << json_string(result.observed_outcome)
         << ",\"expected_outcome_met\":" << json_boolean(result.expected_outcome_met)
         << ",\"metrics\":{"
         << "\"obsolete_effect\":" << json_boolean(summary.has_obsolete_effect())
         << ",\"current_result_accepted_exactly_once\":"
         << json_boolean(summary.current_commits == 1)
         << ",\"valid_current_result_rejected\":" << json_boolean(false_rejection(result))
         << ",\"intervention_to_fallback_ms\":";
  if (result.intervention_to_fallback_ms)
  {
    output << *result.intervention_to_fallback_ms;
  }
  else
  {
    output << "null";
  }
  output << ",\"maximum_tick_ms\":" << result.maximum_tick_ms
         << ",\"terminal_outcome_count\":" << summary.terminal_decisions
         << ",\"task_replay_equal\":" << json_boolean(result.replay_equal) << '}'
         << ",\"counts\":{\"requests_expected\":" << result.expected_requests
         << ",\"requests_submitted\":" << result.submitted_requests
         << ",\"blocked_submissions\":" << result.blocked_submissions
         << ",\"provider_completions\":" << summary.provider_completions
         << ",\"current_commits\":" << summary.current_commits
         << ",\"obsolete_commits\":" << summary.obsolete_commits
         << ",\"result_rejections\":" << summary.result_rejections
         << ",\"cancellation_requests\":" << summary.cancellation_requests
         << ",\"current_dispatches\":" << summary.current_dispatches
         << ",\"obsolete_dispatches\":" << summary.obsolete_dispatches
         << ",\"dispatch_rejections\":" << summary.dispatch_rejections
         << ",\"fallback_activations\":" << summary.fallback_activations
         << ",\"safe_stand_activations\":" << summary.safe_stand_activations
         << ",\"active_jobs_at_end\":" << result.active_jobs_at_end << '}'
         << ",\"task_event_streams\":";
  write_string_array(output, task_paths);
  output << ",\"variant_event_streams\":";
  write_string_array(output, variant_paths);
  output << ",\"effects\":[";
  for (std::size_t index = 0; index < result.effects.size(); ++index)
  {
    if (index > 0)
    {
      output << ',';
    }
    write_effect(output, result.effects[index]);
  }
  output << "]}\n";
}

} // namespace

void write_trial_result(const std::filesystem::path& output_directory, std::ostream& raw_results,
                        const trial_result& result)
{
  const std::vector<std::string> task_paths =
      write_streams(output_directory, result, result.task_streams, "task");
  const std::vector<std::string> variant_paths =
      write_streams(output_directory, result, result.variant_streams, "variant");
  write_raw_result(raw_results, result, task_paths, variant_paths);
}

} // namespace muesli_bt::experiments::controlled_authority
