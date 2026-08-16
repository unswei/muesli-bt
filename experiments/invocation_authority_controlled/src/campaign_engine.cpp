#include "campaign_engine.hpp"

#include "campaign_outcome.hpp"
#include "campaign_trial.hpp"
#include "campaign_writer.hpp"
#include "effect_recorder.hpp"
#include "runtime_variant.hpp"
#include "scripted_provider.hpp"
#include "task_runner.hpp"
#include "variant.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

using namespace std::chrono_literals;

std::string read_text(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input)
  {
    throw std::invalid_argument("could not open common Lisp task: " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<task_event> task_events_for(const schedule_definition& schedule)
{
  std::vector<task_event> events;
  std::uint64_t sequence = 1;
  for (const planned_event& planned : schedule.events)
  {
    std::optional<task_event_kind> kind;
    if (planned.event == "enter_model_branch")
    {
      kind = task_event_kind::enter_model_branch;
    }
    else if (planned.event == "leave_model_branch")
    {
      kind = task_event_kind::leave_model_branch;
    }
    else if (planned.event == "reenter_model_branch")
    {
      kind = task_event_kind::reenter_model_branch;
    }
    else if (planned.event == "change_context")
    {
      kind = task_event_kind::context_changed;
    }
    else if (planned.event == "activate_emergency")
    {
      kind = task_event_kind::emergency_activated;
    }
    else if (planned.event == "reset_runtime")
    {
      kind = task_event_kind::runtime_reset;
    }
    if (kind)
    {
      events.push_back(task_event{.sequence = sequence++,
                                  .at = planned.at,
                                  .kind = *kind,
                                  .context_id = planned.context_id});
    }
  }
  return events;
}

provider_result result_for(std::string_view completion_event, std::string request_label,
                           std::string frame_id, std::uint64_t seed, std::size_t request_index,
                           bool duplicate)
{
  const double seed_offset = static_cast<double>((seed + request_index * 17) % 31) / 10000.0;
  provider_result result{
      .status = provider_status::ok,
      .proposal = {.response_id = "response-" + request_label + "-" + std::to_string(seed),
                   .frame_id = std::move(frame_id),
                   .pose = {0.2 + seed_offset, -0.1, 0.3},
                   .schema_valid = true},
      .reason = {},
      .completion_copies = duplicate ? 2u : 1u,
  };
  if (completion_event == "receive_malformed_result")
  {
    result.proposal.schema_valid = false;
  }
  else if (completion_event == "receive_host_invalid_result")
  {
    result.proposal.pose[0] = 2.0;
  }
  else if (completion_event == "service_disconnect")
  {
    result.status = provider_status::disconnected;
    result.reason = "service_disconnect";
  }
  return result;
}

std::vector<scripted_provider_job> provider_jobs_for(const schedule_definition& schedule,
                                                     std::string_view frame_id, std::uint64_t seed)
{
  std::vector<std::string> request_labels;
  for (const planned_event& event : schedule.events)
  {
    if (event.event == "submit_request")
    {
      request_labels.push_back(event.request);
    }
  }

  std::vector<scripted_provider_job> jobs;
  jobs.reserve(request_labels.size());
  for (std::size_t index = 0; index < request_labels.size(); ++index)
  {
    const std::string& label = request_labels[index];
    std::string completion_event = "receive_result";
    bool duplicate = false;
    for (const planned_event& event : schedule.events)
    {
      if (event.request == label &&
          (event.event == "receive_result" || event.event == "receive_malformed_result" ||
           event.event == "receive_host_invalid_result" || event.event == "service_disconnect"))
      {
        completion_event = event.event;
        duplicate = duplicate || event.duplicate;
      }
    }
    jobs.push_back(scripted_provider_job{
        .request_label = label,
        .result =
            result_for(completion_event, label, std::string(frame_id), seed, index, duplicate),
    });
  }
  return jobs;
}

std::unique_ptr<authority_variant> make_variant(std::string_view label,
                                                const std::shared_ptr<proposal_provider>& provider,
                                                effect_recorder& recorder,
                                                deterministic_coordinator& coordinator,
                                                const campaign_plan& plan)
{
  const logical_now now = [&coordinator] { return coordinator.now(); };
  if (label == "B0")
  {
    return std::make_unique<blocking_variant>(provider, recorder, now, plan.validation);
  }
  if (label == "B1")
  {
    return std::make_unique<asynchronous_variant>(provider, recorder, now, plan.validation);
  }
  if (label == "B2")
  {
    return std::make_unique<timeout_variant>(provider, recorder, now, plan.validation);
  }
  if (label == "B3")
  {
    return std::make_unique<invocation_scoped_variant>(provider, recorder, now,
                                                       plan.request_deadline, plan.validation);
  }
  throw std::invalid_argument("unknown campaign authority variant: " + std::string(label));
}

void add_summary(effect_summary& target, const effect_summary& source)
{
  target.requests_submitted += source.requests_submitted;
  target.provider_completions += source.provider_completions;
  target.current_commits += source.current_commits;
  target.obsolete_commits += source.obsolete_commits;
  target.result_rejections += source.result_rejections;
  target.cancellation_requests += source.cancellation_requests;
  target.terminal_decisions += source.terminal_decisions;
  target.current_dispatches += source.current_dispatches;
  target.obsolete_dispatches += source.obsolete_dispatches;
  target.dispatch_rejections += source.dispatch_rejections;
  target.fallback_activations += source.fallback_activations;
  target.safe_stand_activations += source.safe_stand_activations;
}

class trial_unit
{
public:
  trial_unit(const campaign_plan& plan, const schedule_definition& schedule,
             std::string variant_label, std::uint64_t seed, std::string common_task_source,
             std::vector<scripted_provider_job> jobs)
      : plan_(plan), schedule_(schedule), variant_label_(std::move(variant_label)),
        coordinator_(std::make_unique<deterministic_coordinator>(plan.initial_context_id,
                                                                 task_events_for(schedule))),
        recorder_(
            std::make_unique<effect_recorder>([this](const request_record& request, logical_time at)
                                              { return coordinator_->assess(request, at); })),
        provider_(std::make_shared<scripted_provider>(std::move(jobs)))
  {
    std::unique_ptr<authority_variant> variant =
        make_variant(variant_label_, provider_, *recorder_, *coordinator_, plan_);
    variant_id_ = variant->descriptor().variant_id;
    runner_ = std::make_unique<shared_lisp_task_runner>(
        *coordinator_, *recorder_, std::move(variant), std::move(common_task_source),
        task_runner_config{.request_deadline = plan.request_deadline});
    (void)seed;
  }

  ~trial_unit()
  {
    provider_->release_all();
    join_blocked_tick();
  }

  void advance_to(logical_time at) { coordinator_->advance_to(at); }

  void submit(std::string_view request_label)
  {
    ++expected_requests_;
    if (blocking_tick_)
    {
      ++blocked_submissions_;
      return;
    }
    runner_->request_submission();
    if (variant_label_ == "B0")
    {
      blocking_error_ = nullptr;
      blocking_tick_.emplace(
          [this]
          {
            try
            {
              measure([this] { (void)runner_->tick(); });
            }
            catch (...)
            {
              blocking_error_ = std::current_exception();
            }
          });
      provider_->wait_until_started(request_label);
    }
    else
    {
      measure([this] { (void)runner_->tick(); });
      provider_->wait_until_started(request_label);
    }
    const std::vector<request_record>& requests = runner_->submitted_requests();
    if (requests.empty())
    {
      throw std::runtime_error("campaign runner did not expose its submitted request");
    }
    request_ids_.insert_or_assign(std::string(request_label), requests.back().request_id);
  }

  void release(std::string_view request_label)
  {
    const auto request = request_ids_.find(std::string(request_label));
    if (request == request_ids_.end())
    {
      return;
    }
    provider_->release(request_label);
    provider_->wait_until_finished(request_label);
    if (variant_label_ == "B0")
    {
      join_blocked_tick();
      if (deferred_reset_)
      {
        runner_->reset();
        deferred_reset_ = false;
      }
      if (deferred_emergency_)
      {
        measure([this] { (void)runner_->tick(); });
        deferred_emergency_ = false;
      }
      return;
    }
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    bool provider_completion_observed = false;
    do
    {
      variant_update update;
      measure([this, &update] { update = runner_->pump(); });
      provider_completion_observed =
          provider_completion_observed || update.provider_completions > 0;
      if (!provider_completion_observed)
      {
        std::this_thread::yield();
        continue;
      }
      measure([this] { (void)runner_->tick(); });
      if (terminal_recorded(request->second))
      {
        return;
      }
      std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("campaign did not observe a terminal decision after completion");
  }

  void cancel(std::string_view request_label)
  {
    const auto request = request_ids_.find(std::string(request_label));
    if (request == request_ids_.end() || variant_label_ == "B0")
    {
      return;
    }
    measure([this, request] { (void)runner_->cancel_request(request->second); });
    measure([this] { (void)runner_->tick(); });
  }

  void duplicate_completion(std::string_view request_label)
  {
    if (variant_label_ == "B3" &&
        request_ids_.find(std::string(request_label)) != request_ids_.end())
    {
      measure([this] { (void)runner_->pump(); });
    }
  }

  void task_transition(std::string_view event)
  {
    if (variant_label_ == "B0" && blocking_tick_)
    {
      deferred_emergency_ = deferred_emergency_ || event == "activate_emergency";
      deferred_reset_ = deferred_reset_ || event == "reset_runtime";
      return;
    }
    if (event == "reset_runtime")
    {
      runner_->reset();
      return;
    }
    if (event == "leave_model_branch" || event == "reenter_model_branch" ||
        event == "activate_emergency")
    {
      measure([this] { (void)runner_->tick(); });
    }
  }

  void dispatch()
  {
    measure([this] { (void)runner_->tick(); });
  }

  trial_result finish(std::string schedule_id, std::uint64_t seed, std::string expected_outcome)
  {
    provider_->release_all();
    join_blocked_tick();
    trial_result result;
    result.schedule_id = std::move(schedule_id);
    result.variant_label = variant_label_;
    result.variant_id = variant_id_;
    result.seed = seed;
    result.expected_outcome = std::move(expected_outcome);
    result.summary = recorder_->summary(variant_id_);
    result.effects = recorder_->snapshot();
    result.expected_requests = expected_requests_;
    result.submitted_requests = runner_->submitted_requests().size();
    result.blocked_submissions = blocked_submissions_;
    result.active_jobs_at_end = runner_->variant().active_jobs();
    result.maximum_tick_ms = static_cast<double>(maximum_tick_ns_.load()) / 1'000'000.0;
    result.task_streams.push_back(runner_->task_events());
    const std::vector<std::string> variant_events = runner_->variant_events();
    if (!variant_events.empty())
    {
      result.variant_streams.push_back(variant_events);
    }

    std::optional<logical_time> intervention;
    for (const planned_event& event : schedule_.events)
    {
      if (event.event == "leave_model_branch" || event.event == "activate_emergency" ||
          event.event == "reset_runtime" || event.event == "service_disconnect")
      {
        intervention = event.at;
        break;
      }
    }
    if (intervention)
    {
      for (const effect_record& effect : result.effects)
      {
        if (effect.kind == effect_kind::fallback_activated && effect.at >= *intervention)
        {
          result.intervention_to_fallback_ms =
              static_cast<double>((effect.at - *intervention).count());
          break;
        }
      }
    }
    return result;
  }

private:
  [[nodiscard]] bool terminal_recorded(std::uint64_t request_id) const
  {
    const std::vector<effect_record> effects = recorder_->snapshot();
    return std::any_of(effects.begin(), effects.end(),
                       [request_id](const effect_record& effect)
                       {
                         return effect.request && effect.request->request_id == request_id &&
                                (effect.kind == effect_kind::result_committed ||
                                 effect.kind == effect_kind::result_rejected);
                       });
  }

  template <typename Callable> void measure(Callable&& callable)
  {
    const auto started = std::chrono::steady_clock::now();
    callable();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started);
    std::int64_t observed = maximum_tick_ns_.load();
    while (observed < elapsed.count() &&
           !maximum_tick_ns_.compare_exchange_weak(observed, elapsed.count()))
    {
    }
  }

  void join_blocked_tick()
  {
    if (!blocking_tick_)
    {
      return;
    }
    blocking_tick_->join();
    blocking_tick_.reset();
    if (blocking_error_)
    {
      std::rethrow_exception(blocking_error_);
    }
  }

  const campaign_plan& plan_;
  const schedule_definition& schedule_;
  std::string variant_label_;
  std::string variant_id_;
  std::unique_ptr<deterministic_coordinator> coordinator_;
  std::unique_ptr<effect_recorder> recorder_;
  std::shared_ptr<scripted_provider> provider_;
  std::unique_ptr<shared_lisp_task_runner> runner_;
  std::optional<std::jthread> blocking_tick_;
  std::exception_ptr blocking_error_;
  std::unordered_map<std::string, std::uint64_t> request_ids_;
  std::size_t expected_requests_ = 0;
  std::size_t blocked_submissions_ = 0;
  bool deferred_emergency_ = false;
  bool deferred_reset_ = false;
  std::atomic<std::int64_t> maximum_tick_ns_{0};
};

trial_result execute_single(const campaign_plan& plan, const schedule_definition& schedule,
                            const planned_run& run, const std::string& common_task_source)
{
  trial_unit unit(plan, schedule, run.variant_label, run.seed, common_task_source,
                  provider_jobs_for(schedule, plan.validation.frame_id, run.seed));
  for (const planned_event& event : schedule.events)
  {
    unit.advance_to(event.at);
    if (event.event == "submit_request")
    {
      unit.submit(event.request);
    }
    else if (event.event == "receive_result" || event.event == "receive_malformed_result" ||
             event.event == "receive_host_invalid_result" || event.event == "service_disconnect")
    {
      if (!event.duplicate)
      {
        unit.release(event.request);
      }
      else
      {
        unit.duplicate_completion(event.request);
      }
    }
    else if (event.event == "cancel_request")
    {
      unit.cancel(event.request);
    }
    else if (event.event == "dispatch_result")
    {
      unit.dispatch();
    }
    else if (event.event == "leave_model_branch" || event.event == "reenter_model_branch" ||
             event.event == "activate_emergency" || event.event == "reset_runtime")
    {
      unit.task_transition(event.event);
    }
  }
  trial_result result = unit.finish(schedule.schedule_id, run.seed, run.expected_outcome);
  result.expected_outcome_met = expected_outcome_holds(result);
  result.observed_outcome =
      result.expected_outcome_met ? result.expected_outcome : "unexpected_outcome";
  return result;
}

trial_result execute_burst(const campaign_plan& plan, const schedule_definition& schedule,
                           const planned_run& run, const std::string& common_task_source)
{
  constexpr std::size_t kBurstCount = 16;
  schedule_definition unit_schedule{
      .schedule_id = schedule.schedule_id,
      .events = {{.at = 0ms, .event = "enter_model_branch"},
                 {.at = 0ms, .event = "submit_request", .request = "r1"},
                 {.at = 200ms, .event = "receive_result", .request = "r1"}},
  };
  trial_result aggregate;
  aggregate.schedule_id = schedule.schedule_id;
  aggregate.variant_label = run.variant_label;
  aggregate.seed = run.seed;
  aggregate.expected_outcome = run.expected_outcome;

  std::vector<std::unique_ptr<trial_unit>> units;
  units.reserve(kBurstCount);
  for (std::size_t index = 0; index < kBurstCount; ++index)
  {
    const std::uint64_t instance_seed = run.seed + index * 1'000'003;
    units.push_back(std::make_unique<trial_unit>(
        plan, unit_schedule, run.variant_label, instance_seed, common_task_source,
        provider_jobs_for(unit_schedule, plan.validation.frame_id, instance_seed)));
    units.back()->advance_to(0ms);
    units.back()->submit("r1");
  }

  for (const std::unique_ptr<trial_unit>& unit : units)
  {
    unit->advance_to(200ms);
    unit->release("r1");
  }

  for (std::size_t index = 0; index < units.size(); ++index)
  {
    trial_result result = units[index]->finish(schedule.schedule_id, run.seed + index * 1'000'003,
                                               "accept_current_exactly_once");
    if (aggregate.variant_id.empty())
    {
      aggregate.variant_id = result.variant_id;
    }
    add_summary(aggregate.summary, result.summary);
    aggregate.effects.insert(aggregate.effects.end(), result.effects.begin(), result.effects.end());
    aggregate.expected_requests += result.expected_requests;
    aggregate.submitted_requests += result.submitted_requests;
    aggregate.blocked_submissions += result.blocked_submissions;
    aggregate.active_jobs_at_end += result.active_jobs_at_end;
    aggregate.maximum_tick_ms = std::max(aggregate.maximum_tick_ms, result.maximum_tick_ms);
    aggregate.task_streams.insert(aggregate.task_streams.end(), result.task_streams.begin(),
                                  result.task_streams.end());
    aggregate.variant_streams.insert(aggregate.variant_streams.end(),
                                     result.variant_streams.begin(), result.variant_streams.end());
  }
  aggregate.expected_outcome_met = expected_outcome_holds(aggregate);
  aggregate.observed_outcome =
      aggregate.expected_outcome_met ? aggregate.expected_outcome : "unexpected_outcome";
  return aggregate;
}

trial_result execute_trial(const campaign_plan& plan, const schedule_definition& schedule,
                           const planned_run& run, const std::string& common_task_source)
{
  if (schedule.schedule_id == "F13")
  {
    return execute_burst(plan, schedule, run, common_task_source);
  }
  return execute_single(plan, schedule, run, common_task_source);
}

std::string trial_key(std::string_view variant, std::uint64_t seed, std::string_view schedule)
{
  return std::string(variant) + ":" + std::to_string(seed) + ":" + std::string(schedule);
}

} // namespace

campaign_engine_result execute_campaign_plan(const campaign_plan& plan,
                                             const std::filesystem::path& output_directory)
{
  const std::filesystem::path raw_results_path = output_directory / "raw_trials.jsonl";
  const std::filesystem::path events_directory = output_directory / "events";
  if (std::filesystem::exists(raw_results_path) ||
      (std::filesystem::exists(events_directory) && !std::filesystem::is_empty(events_directory)))
  {
    throw std::runtime_error("campaign output already contains engine artefacts");
  }
  std::filesystem::create_directories(output_directory / "events");
  std::ofstream raw_results(raw_results_path);
  if (!raw_results)
  {
    throw std::runtime_error("could not create raw campaign results");
  }

  const std::string common_task_source = read_text(plan.common_task_path);
  std::unordered_map<std::string, trial_result> live_results;
  std::size_t written = 0;
  for (const planned_run& run : plan.runs)
  {
    const schedule_definition& schedule = plan.schedules.at(run.schedule_id);
    trial_result result;
    if (run.schedule_id != "F16")
    {
      result = execute_trial(plan, schedule, run, common_task_source);
      live_results.insert_or_assign(trial_key(run.variant_label, run.seed, run.schedule_id),
                                    result);
    }
    else
    {
      result.schedule_id = run.schedule_id;
      result.variant_label = run.variant_label;
      result.seed = run.seed;
      result.expected_outcome = run.expected_outcome;
      result.replay_equal = true;
      for (std::size_t schedule_index = 1; schedule_index <= 15; ++schedule_index)
      {
        std::ostringstream schedule_id;
        schedule_id << 'F' << std::setw(2) << std::setfill('0') << schedule_index;
        const std::string key = trial_key(run.variant_label, run.seed, schedule_id.str());
        const auto live = live_results.find(key);
        if (live == live_results.end())
        {
          throw std::runtime_error("replay schedule requires its preceding live trial: " + key);
        }
        planned_run replay_run{.schedule_id = schedule_id.str(),
                               .variant_label = run.variant_label,
                               .seed = run.seed,
                               .expected_outcome = live->second.expected_outcome};
        trial_result replay = execute_trial(plan, plan.schedules.at(schedule_id.str()), replay_run,
                                            common_task_source);
        const std::string live_signature = decision_signature(live->second);
        const std::string replay_signature = decision_signature(replay);
        if (live_signature != replay_signature)
        {
          result.replay_equal = false;
          result.replay_mismatch_schedule_ids.push_back(schedule_id.str());
          result.replay_mismatch_details.push_back(schedule_id.str() + "|live=" + live_signature +
                                                   "|replay=" + replay_signature);
        }
        result.maximum_tick_ms = std::max(result.maximum_tick_ms, replay.maximum_tick_ms);
        result.task_streams.insert(result.task_streams.end(), replay.task_streams.begin(),
                                   replay.task_streams.end());
        result.variant_streams.insert(result.variant_streams.end(), replay.variant_streams.begin(),
                                      replay.variant_streams.end());
      }
      result.variant_id = live_results.at(trial_key(run.variant_label, run.seed, "F01")).variant_id;
      result.expected_outcome_met = expected_outcome_holds(result);
      result.observed_outcome =
          result.expected_outcome_met ? result.expected_outcome : "replay_mismatch";
    }

    write_trial_result(output_directory, raw_results, result);
    ++written;
  }
  return {.trials_written = written, .raw_results_path = raw_results_path};
}

} // namespace muesli_bt::experiments::controlled_authority
