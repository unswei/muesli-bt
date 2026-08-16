#include "timing_engine.hpp"

#include "effect_recorder.hpp"
#include "runtime_variant.hpp"
#include "task_runner.hpp"
#include "variant.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

using steady_clock = std::chrono::steady_clock;
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

std::string json_string(std::string_view value)
{
  std::ostringstream escaped;
  escaped << '"';
  for (const unsigned char character : value)
  {
    switch (character)
    {
      case '"':
        escaped << "\\\"";
        break;
      case '\\':
        escaped << "\\\\";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        if (character < 0x20)
        {
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                  << static_cast<int>(character) << std::dec;
        }
        else
        {
          escaped << static_cast<char>(character);
        }
    }
  }
  escaped << '"';
  return escaped.str();
}

std::vector<logical_time> realised_delays(const timing_run& run)
{
  std::mt19937_64 generator(run.seed);
  std::vector<logical_time> delays;
  delays.reserve(run.concurrent_jobs);
  const double base = static_cast<double>(run.service_delay.count());
  for (std::size_t index = 0; index < run.concurrent_jobs; ++index)
  {
    double multiplier = 1.0;
    if (run.distribution == "uniform")
    {
      multiplier = std::uniform_real_distribution<double>(0.0, 2.0)(generator);
    }
    else if (run.distribution == "bimodal")
    {
      multiplier = std::bernoulli_distribution(0.5)(generator) ? 0.5 : 1.5;
    }
    else if (run.distribution == "long_tail")
    {
      multiplier = std::bernoulli_distribution(0.1)(generator) ? 5.5 : 0.5;
    }
    delays.push_back(logical_time{static_cast<std::int64_t>(std::llround(base * multiplier))});
  }
  return delays;
}

class delayed_provider final : public proposal_provider
{
public:
  delayed_provider(logical_time delay, std::size_t instance_index, std::string frame_id)
      : delay_(delay), instance_index_(instance_index), frame_id_(std::move(frame_id))
  {
  }

  provider_result infer(const request_record& request) override
  {
    const steady_clock::time_point started = steady_clock::now();
    std::this_thread::sleep_for(delay_);
    const std::chrono::nanoseconds elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(steady_clock::now() - started);
    actual_service_ns_.store(elapsed.count());
    complete_.store(true);
    return provider_result{
        .status = provider_status::ok,
        .proposal = {.response_id = "timing-response-" + std::to_string(instance_index_) + "-" +
                                        std::to_string(request.request_id),
                     .frame_id = frame_id_,
                     .pose = {0.2, -0.1, 0.3},
                     .schema_valid = true},
    };
  }

  bool cancel(const request_record&) override { return false; }

  [[nodiscard]] bool complete() const noexcept { return complete_.load(); }

  [[nodiscard]] double actual_service_ms() const noexcept
  {
    return static_cast<double>(actual_service_ns_.load()) / 1'000'000.0;
  }

private:
  logical_time delay_;
  std::size_t instance_index_ = 0;
  std::string frame_id_;
  std::atomic<bool> complete_{false};
  std::atomic<std::int64_t> actual_service_ns_{0};
};

std::unique_ptr<authority_variant>
make_variant(std::string_view label, const std::shared_ptr<proposal_provider>& provider,
             effect_recorder& recorder, logical_now now, const timing_plan& plan)
{
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
  throw std::invalid_argument("unknown timing authority variant: " + std::string(label));
}

struct timing_unit
{
  std::unique_ptr<deterministic_coordinator> coordinator;
  std::unique_ptr<effect_recorder> recorder;
  std::shared_ptr<delayed_provider> provider;
  std::unique_ptr<shared_lisp_task_runner> runner;
};

struct timing_result
{
  double maximum_tick_ms = 0.0;
  double maximum_task_tick_ms = 0.0;
  double wall_duration_ms = 0.0;
  std::size_t tick_cycles = 0;
  std::size_t terminal_decisions = 0;
  std::size_t provider_completions = 0;
  std::size_t active_jobs_at_end = 0;
  std::vector<logical_time> requested_delays;
  std::vector<double> actual_service_ms;
};

timing_result execute_trial(const timing_plan& plan, const timing_run& run,
                            const std::string& common_task_source)
{
  const steady_clock::time_point clock_origin = steady_clock::now();
  const logical_now now = [clock_origin]
  {
    return std::chrono::duration_cast<logical_time>(steady_clock::now() - clock_origin);
  };
  const std::vector<logical_time> delays = realised_delays(run);
  std::vector<timing_unit> units;
  units.reserve(run.concurrent_jobs);
  for (std::size_t index = 0; index < run.concurrent_jobs; ++index)
  {
    timing_unit unit;
    unit.coordinator = std::make_unique<deterministic_coordinator>(
        plan.initial_context_id,
        std::vector<task_event>{{.sequence = 1,
                                 .at = 0ms,
                                 .kind = task_event_kind::enter_model_branch}});
    deterministic_coordinator* const coordinator = unit.coordinator.get();
    unit.recorder = std::make_unique<effect_recorder>(
        [coordinator](const request_record& request, logical_time at)
        { return coordinator->assess(request, at); });
    unit.provider =
        std::make_shared<delayed_provider>(delays[index], index, plan.validation.frame_id);
    std::unique_ptr<authority_variant> variant =
        make_variant(run.variant_label, unit.provider, *unit.recorder, now, plan);
    unit.runner = std::make_unique<shared_lisp_task_runner>(
        *unit.coordinator, *unit.recorder, std::move(variant), common_task_source,
        task_runner_config{.request_deadline = plan.request_deadline});
    units.push_back(std::move(unit));
  }

  for (timing_unit& unit : units)
  {
    unit.coordinator->advance_to(now());
    unit.runner->request_submission();
  }

  timing_result result;
  result.requested_delays = delays;
  const steady_clock::time_point trial_started = steady_clock::now();
  steady_clock::time_point next_cycle = trial_started;
  const std::chrono::nanoseconds tick_period{
      static_cast<std::int64_t>(1'000'000'000ull / run.tick_rate_hz)};
  const logical_time maximum_delay = *std::max_element(delays.begin(), delays.end());
  logical_time blocking_budget{};
  for (const logical_time delay : delays)
  {
    blocking_budget += delay;
  }
  const logical_time service_budget =
      run.variant_label == "B0" ? blocking_budget : maximum_delay;
  const steady_clock::time_point trial_timeout =
      trial_started + service_budget + plan.request_deadline + 10s;

  while (true)
  {
    if (steady_clock::now() < next_cycle)
    {
      std::this_thread::sleep_until(next_cycle);
    }
    const steady_clock::time_point batch_started = steady_clock::now();
    for (timing_unit& unit : units)
    {
      unit.coordinator->advance_to(now());
      const steady_clock::time_point task_started = steady_clock::now();
      (void)unit.runner->tick();
      const double task_ms = static_cast<double>(
                                 std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     steady_clock::now() - task_started)
                                     .count()) /
                             1'000'000.0;
      result.maximum_task_tick_ms = std::max(result.maximum_task_tick_ms, task_ms);
    }
    const double batch_ms =
        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                steady_clock::now() - batch_started)
                                .count()) /
        1'000'000.0;
    result.maximum_tick_ms = std::max(result.maximum_tick_ms, batch_ms);
    ++result.tick_cycles;

    bool providers_complete = true;
    std::size_t active_jobs = 0;
    std::size_t terminal_decisions = 0;
    std::size_t provider_completions = 0;
    for (const timing_unit& unit : units)
    {
      providers_complete = providers_complete && unit.provider->complete();
      active_jobs += unit.runner->variant().active_jobs();
      const effect_summary summary =
          unit.recorder->summary(unit.runner->variant().descriptor().variant_id);
      terminal_decisions += summary.terminal_decisions;
      provider_completions += summary.provider_completions;
    }
    if (providers_complete && active_jobs == 0 && terminal_decisions >= units.size())
    {
      result.terminal_decisions = terminal_decisions;
      result.provider_completions = provider_completions;
      result.active_jobs_at_end = active_jobs;
      break;
    }
    if (steady_clock::now() >= trial_timeout)
    {
      throw std::runtime_error("timing trial exceeded its service and deadline budget");
    }
    next_cycle += tick_period;
    if (next_cycle < steady_clock::now())
    {
      next_cycle = steady_clock::now();
    }
  }

  result.wall_duration_ms =
      static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                              steady_clock::now() - trial_started)
                              .count()) /
      1'000'000.0;
  for (const timing_unit& unit : units)
  {
    result.actual_service_ms.push_back(unit.provider->actual_service_ms());
  }
  return result;
}

template <typename Value>
void write_array(std::ostream& output, const std::vector<Value>& values)
{
  output << '[';
  for (std::size_t index = 0; index < values.size(); ++index)
  {
    if (index > 0)
    {
      output << ',';
    }
    output << values[index];
  }
  output << ']';
}

void write_result(std::ostream& output, const timing_run& run, const timing_result& result)
{
  output << std::setprecision(12);
  output << "{\"schema_version\":\"controlled-authority.timing-raw-trial.v1\""
         << ",\"condition_id\":" << json_string(run.condition_id)
         << ",\"axis\":" << json_string(run.axis)
         << ",\"reader_label\":" << json_string(run.reader_label)
         << ",\"variant_label\":" << json_string(run.variant_label)
         << ",\"seed\":" << run.seed << ",\"repetition\":" << run.repetition
         << ",\"service_delay_ms\":" << run.service_delay.count()
         << ",\"tick_rate_hz\":" << run.tick_rate_hz
         << ",\"concurrent_jobs\":" << run.concurrent_jobs
         << ",\"distribution\":" << json_string(run.distribution)
         << ",\"maximum_tick_ms\":" << result.maximum_tick_ms
         << ",\"maximum_task_tick_ms\":" << result.maximum_task_tick_ms
         << ",\"wall_duration_ms\":" << result.wall_duration_ms
         << ",\"tick_cycles\":" << result.tick_cycles
         << ",\"terminal_decisions\":" << result.terminal_decisions
         << ",\"provider_completions\":" << result.provider_completions
         << ",\"active_jobs_at_end\":" << result.active_jobs_at_end
         << ",\"requested_delays_ms\":";
  std::vector<std::int64_t> requested;
  requested.reserve(result.requested_delays.size());
  for (const logical_time delay : result.requested_delays)
  {
    requested.push_back(delay.count());
  }
  write_array(output, requested);
  output << ",\"actual_service_ms\":";
  write_array(output, result.actual_service_ms);
  output << "}\n";
}

} // namespace

timing_engine_result execute_timing_plan(const timing_plan& plan,
                                         const std::filesystem::path& output_directory)
{
  const std::filesystem::path raw_results_path = output_directory / "raw_timing_trials.jsonl";
  if (std::filesystem::exists(raw_results_path))
  {
    throw std::runtime_error("timing output already contains raw trial artefacts");
  }
  std::filesystem::create_directories(output_directory);
  std::ofstream raw_results(raw_results_path);
  if (!raw_results)
  {
    throw std::runtime_error("could not create raw timing results");
  }

  const std::string common_task_source = read_text(plan.common_task_path);
  timing_engine_result engine_result{.raw_results_path = raw_results_path};
  for (const timing_run& run : plan.runs)
  {
    const timing_result result = execute_trial(plan, run, common_task_source);
    if (!run.recorded)
    {
      ++engine_result.warmups_executed;
      continue;
    }
    write_result(raw_results, run, result);
    raw_results.flush();
    ++engine_result.trials_written;
    if (engine_result.trials_written % 25 == 0)
    {
      std::cout << "timing progress: " << engine_result.trials_written << " trials\n";
    }
  }
  return engine_result;
}

} // namespace muesli_bt::experiments::controlled_authority
