#include "btcpp_campaign_engine.hpp"

#include "btcpp_task_runner.hpp"
#include "btcpp_variant.hpp"
#include "campaign_outcome.hpp"
#include "campaign_trial.hpp"
#include "campaign_writer.hpp"
#include "runtime_variant.hpp"
#include "scripted_provider.hpp"
#include "task_runner.hpp"
#include "variant.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
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
                                                     std::string_view frame_id,
                                                     std::uint64_t seed)
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
        .result = result_for(completion_event, label, std::string(frame_id), seed, index,
                             duplicate),
    });
  }
  return jobs;
}

std::unique_ptr<authority_variant> make_variant(
    std::string_view label, const std::shared_ptr<proposal_provider>& provider,
    effect_recorder& recorder, deterministic_coordinator& coordinator, const campaign_plan& plan)
{
  const logical_now now = [&coordinator] { return coordinator.now(); };
  if (label == "MBT-ordinary")
  {
    return std::make_unique<asynchronous_variant>(provider, recorder, now, plan.validation);
  }
  if (label == "MBT-full")
  {
    return std::make_unique<invocation_scoped_variant>(provider, recorder, now,
                                                       plan.request_deadline, plan.validation);
  }
  if (label == "BTCPP-ordinary")
  {
    return std::make_unique<btcpp_asynchronous_variant>(provider, recorder, now, plan.validation);
  }
  if (label == "BTCPP-full")
  {
    return std::make_unique<btcpp_invocation_scoped_variant>(provider, recorder, now,
                                                             plan.validation);
  }
  throw std::invalid_argument("unknown external-comparison variant: " + std::string(label));
}

class matched_runner
{
public:
  matched_runner(std::string_view label, deterministic_coordinator& coordinator,
                 effect_recorder& recorder, std::unique_ptr<authority_variant> variant,
                 std::string common_task_source, logical_time request_deadline)
  {
    if (label == "MBT-ordinary" || label == "MBT-full")
    {
      runner_.emplace<std::unique_ptr<shared_lisp_task_runner>>(
          std::make_unique<shared_lisp_task_runner>(
              coordinator, recorder, std::move(variant), std::move(common_task_source),
              task_runner_config{.request_deadline = request_deadline}));
    }
    else
    {
      runner_.emplace<std::unique_ptr<btcpp_task_runner>>(std::make_unique<btcpp_task_runner>(
          coordinator, recorder, std::move(variant), request_deadline));
    }
  }

  void request_submission()
  {
    std::visit([](auto& runner) { runner->request_submission(); }, runner_);
  }

  void tick()
  {
    std::visit([](auto& runner) { (void)runner->tick(); }, runner_);
  }

  void reset()
  {
    std::visit([](auto& runner) { runner->reset(); }, runner_);
  }

  variant_update cancel_request(std::uint64_t request_id)
  {
    return std::visit([request_id](auto& runner) { return runner->cancel_request(request_id); },
                      runner_);
  }

  [[nodiscard]] const authority_variant& variant() const
  {
    return std::visit([](const auto& runner) -> const authority_variant&
                      { return runner->variant(); },
                      runner_);
  }

  [[nodiscard]] const std::vector<request_record>& submitted_requests() const
  {
    return std::visit([](const auto& runner) -> const std::vector<request_record>&
                      { return runner->submitted_requests(); },
                      runner_);
  }

  [[nodiscard]] std::vector<std::string> task_events() const
  {
    return std::visit([](const auto& runner) { return runner->task_events(); }, runner_);
  }

  [[nodiscard]] std::vector<std::string> variant_events() const
  {
    return std::visit([](const auto& runner) { return runner->variant_events(); }, runner_);
  }

private:
  std::variant<std::unique_ptr<shared_lisp_task_runner>, std::unique_ptr<btcpp_task_runner>>
      runner_;
};

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
    implementation_id_ = variant->descriptor().variant_id;
    runner_ = std::make_unique<matched_runner>(variant_label_, *coordinator_, *recorder_,
                                               std::move(variant), std::move(common_task_source),
                                               plan.request_deadline);
    (void)seed;
  }

  ~trial_unit() { provider_->release_all(); }

  void advance_to(logical_time at) { coordinator_->advance_to(at); }

  void submit(std::string_view request_label)
  {
    ++expected_requests_;
    runner_->request_submission();
    measure([this] { runner_->tick(); });
    provider_->wait_until_started(request_label);
    const std::vector<request_record>& requests = runner_->submitted_requests();
    if (requests.empty())
    {
      throw std::runtime_error("external comparison did not expose its submitted request");
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

    const task_snapshot task = coordinator_->task_state();
    if (!task.model_branch_active || task.emergency)
    {
      measure([this] { runner_->tick(); });
      return;
    }

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    do
    {
      measure([this] { runner_->tick(); });
      if (terminal_recorded(request->second))
      {
        return;
      }
      std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("external comparison did not observe a terminal decision");
  }

  void cancel(std::string_view request_label)
  {
    const auto request = request_ids_.find(std::string(request_label));
    if (request == request_ids_.end())
    {
      return;
    }
    measure([this, request] { (void)runner_->cancel_request(request->second); });
    measure([this] { runner_->tick(); });
  }

  void duplicate_completion() { measure([this] { runner_->tick(); }); }

  void task_transition(std::string_view event)
  {
    if (event == "reset_runtime")
    {
      runner_->reset();
      return;
    }
    measure([this] { runner_->tick(); });
  }

  void dispatch() { measure([this] { runner_->tick(); }); }

  trial_result finish(std::string schedule_id, std::uint64_t seed, std::string reference_outcome)
  {
    provider_->release_all();
    trial_result result;
    result.schedule_id = std::move(schedule_id);
    result.variant_label = variant_label_;
    result.variant_id = implementation_id_;
    result.seed = seed;
    result.expected_outcome = std::move(reference_outcome);
    result.summary = recorder_->summary(implementation_id_);
    result.effects = recorder_->snapshot();
    result.expected_requests = expected_requests_;
    result.submitted_requests = runner_->submitted_requests().size();
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
    return std::any_of(effects.begin(), effects.end(), [request_id](const effect_record& effect)
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

  const campaign_plan& plan_;
  const schedule_definition& schedule_;
  std::string variant_label_;
  std::string implementation_id_;
  std::unique_ptr<deterministic_coordinator> coordinator_;
  std::unique_ptr<effect_recorder> recorder_;
  std::shared_ptr<scripted_provider> provider_;
  std::unique_ptr<matched_runner> runner_;
  std::unordered_map<std::string, std::uint64_t> request_ids_;
  std::size_t expected_requests_ = 0;
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
      if (event.duplicate)
      {
        unit.duplicate_completion();
      }
      else
      {
        unit.release(event.request);
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
      result.expected_outcome_met ? "matches_c0_profile_reference" : "differs_from_c0_profile_reference";
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
    trial_result result = units[index]->finish(schedule.schedule_id,
                                               run.seed + index * 1'000'003,
                                               "accept_current_exactly_once");
    if (aggregate.variant_id.empty())
    {
      aggregate.variant_id = result.variant_id;
    }
    add_summary(aggregate.summary, result.summary);
    aggregate.effects.insert(aggregate.effects.end(), result.effects.begin(), result.effects.end());
    aggregate.expected_requests += result.expected_requests;
    aggregate.submitted_requests += result.submitted_requests;
    aggregate.active_jobs_at_end += result.active_jobs_at_end;
    aggregate.maximum_tick_ms = std::max(aggregate.maximum_tick_ms, result.maximum_tick_ms);
    aggregate.task_streams.insert(aggregate.task_streams.end(), result.task_streams.begin(),
                                  result.task_streams.end());
    aggregate.variant_streams.insert(aggregate.variant_streams.end(),
                                     result.variant_streams.begin(), result.variant_streams.end());
  }
  aggregate.expected_outcome_met = expected_outcome_holds(aggregate);
  aggregate.observed_outcome = aggregate.expected_outcome_met
                                   ? "matches_c0_profile_reference"
                                   : "differs_from_c0_profile_reference";
  return aggregate;
}

trial_result execute_trial(const campaign_plan& plan, const schedule_definition& schedule,
                           const planned_run& run, const std::string& common_task_source)
{
  return schedule.schedule_id == "F13"
             ? execute_burst(plan, schedule, run, common_task_source)
             : execute_single(plan, schedule, run, common_task_source);
}

std::string trial_key(std::string_view variant, std::uint64_t seed, std::string_view schedule)
{
  return std::string(variant) + ':' + std::to_string(seed) + ':' + std::string(schedule);
}

} // namespace

campaign_engine_result execute_btcpp_comparison_plan(
    const campaign_plan& plan, const std::filesystem::path& output_directory)
{
  if (plan.plan_version != "controlled-authority.btcpp-plan.v1")
  {
    throw std::invalid_argument("external comparison requires a btcpp-plan.v1 input");
  }
  const std::filesystem::path raw_results_path = output_directory / "raw_trials.jsonl";
  const std::filesystem::path events_directory = output_directory / "events";
  if (std::filesystem::exists(raw_results_path) ||
      (std::filesystem::exists(events_directory) && !std::filesystem::is_empty(events_directory)))
  {
    throw std::runtime_error("external-comparison output already contains engine artefacts");
  }
  std::filesystem::create_directories(events_directory);
  std::ofstream raw_results(raw_results_path);
  if (!raw_results)
  {
    throw std::runtime_error("could not create external-comparison raw results");
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
      live_results.insert_or_assign(trial_key(run.variant_label, run.seed, run.schedule_id), result);
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
          throw std::runtime_error("replay requires its preceding live trial: " + key);
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
      result.variant_id =
          live_results.at(trial_key(run.variant_label, run.seed, "F01")).variant_id;
      result.expected_outcome_met = expected_outcome_holds(result);
      result.observed_outcome = result.expected_outcome_met
                                    ? "matches_c0_profile_reference"
                                    : "differs_from_c0_profile_reference";
    }
    write_trial_result(output_directory, raw_results, result);
    ++written;
  }
  return {.trials_written = written, .raw_results_path = raw_results_path};
}

} // namespace muesli_bt::experiments::controlled_authority
