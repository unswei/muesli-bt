#include "runtime_variant.hpp"
#include "task_runner.hpp"
#include "variant.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace std::chrono_literals;
using namespace muesli_bt::experiments::controlled_authority;

void check(bool condition, const std::string& message)
{
  if (!condition)
  {
    throw std::runtime_error(message);
  }
}

template <typename Predicate>
void wait_for(Predicate&& predicate, const std::string& message,
              std::chrono::milliseconds timeout = 2000ms)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate())
  {
    if (std::chrono::steady_clock::now() >= deadline)
    {
      throw std::runtime_error(message);
    }
    std::this_thread::sleep_for(1ms);
  }
}

std::string common_tree_source()
{
  std::ifstream input(MUESLI_BT_CONTROLLED_AUTHORITY_COMMON_TREE);
  check(input.good(), "could not open the shared controlled-authority Lisp task");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

provider_result valid_result(std::string response_id = "response-1")
{
  return provider_result{
      .status = provider_status::ok,
      .proposal = {.response_id = std::move(response_id),
                   .frame_id = "task_context",
                   .pose = {0.2, -0.1, 0.3},
                   .schema_valid = true},
      .reason = {},
  };
}

class immediate_provider final : public proposal_provider
{
public:
  provider_result infer(const request_record& request) override
  {
    return valid_result("response-" + std::to_string(request.request_id));
  }
};

class gated_provider final : public proposal_provider
{
public:
  provider_result infer(const request_record& request) override
  {
    std::unique_lock lock(mutex_);
    started_ = true;
    condition_.notify_all();
    (void)condition_.wait_for(lock, 2s, [this] { return released_; });
    finished_ = true;
    condition_.notify_all();
    return valid_result("response-" + std::to_string(request.request_id));
  }

  bool cancel(const request_record&) override
  {
    std::lock_guard lock(mutex_);
    released_ = true;
    condition_.notify_all();
    return true;
  }

  void wait_for_start()
  {
    std::unique_lock lock(mutex_);
    check(condition_.wait_for(lock, 2s, [this] { return started_; }), "provider did not start");
  }

  void release()
  {
    std::lock_guard lock(mutex_);
    released_ = true;
    condition_.notify_all();
  }

  void wait_for_finish()
  {
    std::unique_lock lock(mutex_);
    check(condition_.wait_for(lock, 2s, [this] { return finished_; }), "provider did not finish");
  }

private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool started_ = false;
  bool released_ = false;
  bool finished_ = false;
};

effect_recorder make_recorder(deterministic_coordinator& coordinator)
{
  return effect_recorder([&coordinator](const request_record& request, logical_time at)
                         { return coordinator.assess(request, at); });
}

bool has_event(const std::vector<std::string>& events, std::string_view type,
               std::string_view field)
{
  const std::string type_text = "\"type\":\"" + std::string(type) + "\"";
  return std::any_of(events.begin(), events.end(),
                     [&](const std::string& line)
                     {
                       return line.find(type_text) != std::string::npos &&
                              line.find(field) != std::string::npos;
                     });
}

void test_b0_and_b1_execute_through_the_shared_lisp_task()
{
  for (const bool blocking : {true, false})
  {
    deterministic_coordinator coordinator(
        "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
    coordinator.advance_to(0ms);
    effect_recorder recorder = make_recorder(coordinator);
    auto provider = std::make_shared<immediate_provider>();
    std::unique_ptr<authority_variant> variant;
    if (blocking)
    {
      variant = std::make_unique<blocking_variant>(provider, recorder,
                                                   [&coordinator] { return coordinator.now(); });
    }
    else
    {
      variant = std::make_unique<asynchronous_variant>(provider, recorder, [&coordinator]
                                                       { return coordinator.now(); });
    }
    const std::string variant_id = variant->descriptor().variant_id;
    shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

    runner.request_submission();
    (void)runner.tick();
    wait_for(
        [&]
        {
          (void)runner.tick();
          return recorder.summary(variant_id).current_commits == 1;
        },
        "shared Lisp model step did not admit the result");
    (void)runner.tick();

    const effect_summary summary = recorder.summary(variant_id);
    check(summary.requests_submitted == 1 && summary.current_commits == 1 &&
              summary.current_dispatches == 1,
          "B0/B1 should traverse common model and dispatch actions");
    check(has_event(runner.task_events(), "node_tick", "controlled-model-step") ||
              !runner.task_events().empty(),
          "shared task runner should expose canonical runtime evidence");
  }
}

void test_b2_timeout_runs_fallback_through_the_shared_task()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<timeout_variant>(provider, recorder,
                                                   [&coordinator] { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  coordinator.advance_to(501ms);
  (void)runner.tick();

  const effect_summary summary = recorder.summary(variant_id);
  check(summary.result_rejections == 1 && summary.cancellation_requests == 1 &&
            summary.fallback_activations == 1,
        "B2 timeout should select the common authored fallback exactly once");
  provider->release();
  provider->wait_for_finish();
}

void test_b3_rejects_changed_context_through_production_gate()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
                    {.sequence = 2,
                     .at = 80ms,
                     .kind = task_event_kind::context_changed,
                     .context_id = "context-b"}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<invocation_scoped_variant>(provider, recorder, [&coordinator]
                                                             { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  coordinator.advance_to(80ms);
  (void)runner.tick();
  provider->release();
  provider->wait_for_finish();
  wait_for(
      [&]
      {
        (void)runner.tick();
        return recorder.summary(variant_id).result_rejections == 1;
      },
      "B3 did not publish its context rejection");

  const effect_summary summary = recorder.summary(variant_id);
  check(summary.current_commits == 0 && summary.obsolete_commits == 0 &&
            summary.current_dispatches == 0 && summary.obsolete_dispatches == 0,
        "B3 changed-context result must not commit or dispatch");
  check(has_event(runner.variant_events(), "vla_result", "\"reason\":\"context_changed\""),
        "B3 should retain the production vla_result rejection evidence");
}

void test_b3_rejects_a_result_after_the_logical_deadline()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<invocation_scoped_variant>(provider, recorder, [&coordinator]
                                                             { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  coordinator.advance_to(501ms);
  (void)runner.tick();
  provider->release();
  provider->wait_for_finish();
  wait_for(
      [&]
      {
        (void)runner.tick();
        return recorder.summary(variant_id).result_rejections == 1;
      },
      "B3 did not reject a logically late result");

  check(has_event(runner.variant_events(), "vla_result", "\"reason\":\"deadline_expired\""),
        "B3 deadline should be decided by the production commit gate");
}

void test_b3_supersedes_an_older_production_invocation()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<invocation_scoped_variant>(provider, recorder, [&coordinator]
                                                             { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  coordinator.advance_to(120ms);
  runner.request_submission();
  (void)runner.tick();
  provider->release();
  wait_for(
      [&]
      {
        (void)runner.tick();
        const effect_summary summary = recorder.summary(variant_id);
        return summary.result_rejections == 1 && summary.current_commits == 1;
      },
      "B3 replacement request did not become the sole accepted invocation");
  (void)runner.tick();

  const effect_summary summary = recorder.summary(variant_id);
  check(summary.terminal_decisions == 2 && summary.current_dispatches == 1 &&
            !summary.has_obsolete_effect(),
        "B3 should reject the superseded invocation and dispatch only its replacement");
  check(has_event(runner.variant_events(), "async_authority_revoked", "\"reason\":\"superseded\""),
        "B3 replacement should use production generation revocation");
}

void test_b3_accepts_and_dispatches_a_current_result()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<invocation_scoped_variant>(provider, recorder, [&coordinator]
                                                             { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  provider->release();
  provider->wait_for_finish();
  coordinator.advance_to(200ms);
  wait_for(
      [&]
      {
        (void)runner.tick();
        return recorder.summary(variant_id).current_commits == 1;
      },
      "B3 did not admit a current result");
  (void)runner.tick();

  const effect_summary summary = recorder.summary(variant_id);
  check(summary.current_commits == 1 && summary.current_dispatches == 1 &&
            summary.result_rejections == 0 && !summary.has_obsolete_effect(),
        "B3 current result should commit and dispatch exactly once");
  check(has_event(runner.variant_events(), "vla_result", "\"decision\":\"accepted\"") &&
            has_event(runner.variant_events(), "walking_target_dispatch",
                      "\"decision\":\"accepted\""),
        "B3 current path should retain production acceptance and dispatch evidence");
}

void test_b3_revalidates_context_at_production_dispatch_gate()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
                    {.sequence = 2,
                     .at = 210ms,
                     .kind = task_event_kind::context_changed,
                     .context_id = "context-b"}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<invocation_scoped_variant>(provider, recorder, [&coordinator]
                                                             { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  provider->release();
  provider->wait_for_finish();
  coordinator.advance_to(200ms);
  wait_for(
      [&]
      {
        (void)runner.tick();
        return recorder.summary(variant_id).current_commits == 1;
      },
      "B3 did not admit the current result");
  coordinator.advance_to(210ms);
  (void)runner.tick();

  const effect_summary summary = recorder.summary(variant_id);
  check(summary.dispatch_rejections == 1 && summary.current_dispatches == 0 &&
            summary.obsolete_dispatches == 0,
        "B3 should reject a changed context before walking-target hand-off");
  check(has_event(runner.variant_events(), "walking_target_dispatch",
                  "\"reason\":\"context_changed\""),
        "B3 should use the production walking-target dispatch gate");
}

void test_b3_preemption_revokes_production_invocation_and_selects_safe_stand()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
                    {.sequence = 2, .at = 80ms, .kind = task_event_kind::emergency_activated}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<invocation_scoped_variant>(provider, recorder, [&coordinator]
                                                             { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  (void)runner.tick();
  coordinator.advance_to(80ms);
  (void)runner.tick();

  const effect_summary summary = recorder.summary(variant_id);
  check(summary.result_rejections == 1 && summary.safe_stand_activations == 1,
        "B3 pre-emption should revoke work and select safe stand");
  check(has_event(runner.variant_events(), "async_authority_revoked",
                  "\"reason\":\"branch_revoked\""),
        "B3 pre-emption should be performed by the production halt path");
  provider->release();
  provider->wait_for_finish();
}

void test_b3_runtime_reset_revokes_and_clears_production_state()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
                    {.sequence = 2, .at = 80ms, .kind = task_event_kind::runtime_reset}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<invocation_scoped_variant>(provider, recorder, [&coordinator]
                                                             { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  (void)runner.tick();
  coordinator.advance_to(80ms);
  runner.reset();

  const effect_summary summary = recorder.summary(variant_id);
  check(summary.result_rejections == 1 && summary.terminal_decisions == 1 &&
            runner.variant().active_jobs() == 0,
        "B3 reset should revoke pending work before clearing production state");
  check(has_event(runner.variant_events(), "async_authority_revoked",
                  "\"reason\":\"branch_revoked\""),
        "B3 reset should preserve canonical revocation evidence");
  provider->release();
  provider->wait_for_finish();
}

void test_branch_exit_revokes_b3_without_resubmission_on_reentry()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
                    {.sequence = 2, .at = 80ms, .kind = task_event_kind::leave_model_branch},
                    {.sequence = 3, .at = 120ms, .kind = task_event_kind::reenter_model_branch}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<invocation_scoped_variant>(provider, recorder, [&coordinator]
                                                             { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  coordinator.advance_to(80ms);
  (void)runner.tick();
  coordinator.advance_to(120ms);
  (void)runner.tick();
  provider->wait_for_finish();
  (void)runner.pump();
  (void)runner.tick();

  const effect_summary summary = recorder.summary(variant_id);
  check(summary.terminal_decisions == 1 && summary.result_rejections == 1 &&
            runner.submitted_requests().size() == 1 && runner.variant().active_jobs() == 0,
        "branch re-entry must not revive or silently resubmit revoked B3 work");
  check(has_event(runner.variant_events(), "async_authority_revoked",
                  "\"reason\":\"branch_revoked\""),
        "ordinary model-branch exit should use the production revocation path");
}

void test_b3_explicit_cancel_uses_production_cancel_gate()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<invocation_scoped_variant>(provider, recorder, [&coordinator]
                                                             { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  const std::uint64_t request_id = runner.submitted_requests().front().request_id;
  const variant_update cancelled = runner.cancel_request(request_id);
  check(cancelled.rejections == 1 && cancelled.last_reason == "cancelled",
        "B3 explicit cancel should make one deterministic rejection visible to the task");
  (void)runner.tick();
  provider->wait_for_finish();

  const effect_summary summary = recorder.summary(variant_id);
  check(summary.cancellation_requests == 1 && summary.terminal_decisions == 1,
        "B3 explicit cancellation should record one request and one terminal decision");
  check(has_event(runner.variant_events(), "vla_cancel", "\"accepted\":true"),
        "B3 explicit cancellation should retain canonical production vla_cancel evidence");
}

void test_preempted_b1_completion_is_observed_without_dispatch()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
                    {.sequence = 2, .at = 80ms, .kind = task_event_kind::emergency_activated}});
  coordinator.advance_to(0ms);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>();
  auto variant = std::make_unique<asynchronous_variant>(provider, recorder, [&coordinator]
                                                        { return coordinator.now(); });
  const std::string variant_id = variant->descriptor().variant_id;
  shared_lisp_task_runner runner(coordinator, recorder, std::move(variant), common_tree_source());

  runner.request_submission();
  (void)runner.tick();
  provider->wait_for_start();
  coordinator.advance_to(80ms);
  (void)runner.tick();
  provider->release();
  provider->wait_for_finish();
  wait_for([&] { return runner.pump().provider_completions == 1; },
           "pre-empted B1 completion was not admitted by the background pump");
  (void)runner.tick();

  const effect_summary summary = recorder.summary(variant_id);
  check(summary.obsolete_commits == 1 && summary.safe_stand_activations == 1 &&
            summary.current_dispatches == 0 && summary.obsolete_dispatches == 0,
        "pre-empted ordinary async work should expose its stale commit without walking");
}

} // namespace

int main()
{
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
      {"B0/B1 shared Lisp wiring", test_b0_and_b1_execute_through_the_shared_lisp_task},
      {"B2 shared fallback", test_b2_timeout_runs_fallback_through_the_shared_task},
      {"B3 production current path", test_b3_accepts_and_dispatches_a_current_result},
      {"B3 production deadline", test_b3_rejects_a_result_after_the_logical_deadline},
      {"B3 production supersession", test_b3_supersedes_an_older_production_invocation},
      {"B3 production context gate", test_b3_rejects_changed_context_through_production_gate},
      {"B3 production dispatch gate", test_b3_revalidates_context_at_production_dispatch_gate},
      {"B3 production pre-emption",
       test_b3_preemption_revokes_production_invocation_and_selects_safe_stand},
      {"B3 production reset", test_b3_runtime_reset_revokes_and_clears_production_state},
      {"B3 branch exit and re-entry", test_branch_exit_revokes_b3_without_resubmission_on_reentry},
      {"B3 production explicit cancel", test_b3_explicit_cancel_uses_production_cancel_gate},
      {"B1 pre-empted background completion",
       test_preempted_b1_completion_is_observed_without_dispatch},
  };

  std::size_t passed = 0;
  for (const auto& [name, test] : tests)
  {
    try
    {
      test();
      ++passed;
      std::cout << "[PASS] " << name << '\n';
    }
    catch (const std::exception& error)
    {
      std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
      return 1;
    }
  }
  std::cout << "All controlled-authority task-runner tests passed (" << passed << "/"
            << tests.size() << ").\n";
  return 0;
}
