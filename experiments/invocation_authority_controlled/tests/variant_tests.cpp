#include "effect_recorder.hpp"
#include "variant.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
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

provider_result valid_result(std::string response_id = "response-1")
{
  return provider_result{
      .status = provider_status::ok,
      .proposal = task_proposal{
          .response_id = std::move(response_id),
          .frame_id = "task_context",
          .pose = {0.2, -0.1, 0.3},
          .schema_valid = true,
      },
      .reason = {},
  };
}

class gated_provider final : public proposal_provider
{
public:
  explicit gated_provider(provider_result result) : result_(std::move(result)) {}

  provider_result infer(const request_record&) override
  {
    std::unique_lock lock(mutex_);
    started_ = true;
    condition_.notify_all();
    condition_.wait(lock, [this] { return released_; });
    finished_ = true;
    condition_.notify_all();
    return result_;
  }

  bool cancel(const request_record&) override
  {
    std::lock_guard lock(mutex_);
    ++cancellation_requests_;
    condition_.notify_all();
    return true;
  }

  void wait_for_start()
  {
    std::unique_lock lock(mutex_);
    if (!condition_.wait_for(lock, 2s, [this] { return started_; }))
    {
      throw std::runtime_error("provider did not start");
    }
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
    if (!condition_.wait_for(lock, 2s, [this] { return finished_; }))
    {
      throw std::runtime_error("provider did not finish");
    }
  }

  std::size_t cancellation_requests()
  {
    std::lock_guard lock(mutex_);
    return cancellation_requests_;
  }

private:
  provider_result result_;
  std::mutex mutex_;
  std::condition_variable condition_;
  bool started_ = false;
  bool released_ = false;
  bool finished_ = false;
  std::size_t cancellation_requests_ = 0;
};

class immediate_provider final : public proposal_provider
{
public:
  explicit immediate_provider(provider_result result) : result_(std::move(result)) {}

  provider_result infer(const request_record&) override { return result_; }

private:
  provider_result result_;
};

effect_recorder make_recorder(deterministic_coordinator& coordinator)
{
  return effect_recorder(
      [&coordinator](const request_record& request, logical_time effect_at)
      { return coordinator.assess(request, effect_at); });
}

request_record start_request(deterministic_coordinator& coordinator)
{
  coordinator.advance_to(0ms);
  return coordinator.submit_request(500ms);
}

void test_effect_recorder_reassesses_at_dispatch()
{
  deterministic_coordinator coordinator(
      "context-a",
      {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
       {.sequence = 2,
        .at = 80ms,
        .kind = task_event_kind::context_changed,
        .context_id = "context-b"}});
  const request_record request = start_request(coordinator);
  effect_recorder recorder = make_recorder(coordinator);

  recorder.record_request("test-variant", request);
  recorder.record_commit("test-variant", request, "response-1", 50ms);
  coordinator.advance_to(80ms);
  recorder.record_dispatch("test-variant", request, "response-1", 100ms);

  const effect_summary summary = recorder.summary("test-variant");
  check(summary.current_commits == 1 && summary.obsolete_commits == 0,
        "result should be current when committed before the context change");
  check(summary.obsolete_dispatches == 1 && summary.current_dispatches == 0,
        "recorder should independently reassess authority at dispatch");
  check(summary.has_obsolete_effect(), "obsolete dispatch should mark the run unsafe");
}

void test_blocking_variant_exposes_unobserved_context_change()
{
  deterministic_coordinator coordinator(
      "context-a",
      {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
       {.sequence = 2,
        .at = 80ms,
        .kind = task_event_kind::context_changed,
        .context_id = "context-b"}});
  const request_record request = start_request(coordinator);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>(valid_result());
  blocking_variant variant(provider, recorder, [&coordinator] { return coordinator.now(); });

  std::atomic<bool> returned{false};
  std::thread submitter(
      [&]
      {
        variant.submit(request);
        returned.store(true);
      });
  provider->wait_for_start();
  check(!returned.load(), "blocking variant should remain inside provider inference");

  coordinator.advance_to(80ms);
  provider->release();
  submitter.join();
  check(returned.load(), "blocking variant should return after provider completion");
  check(variant.dispatch(100ms), "blocking variant should expose its staged proposal");

  const effect_summary summary = recorder.summary(variant.descriptor().variant_id);
  check(summary.requests_submitted == 1 && summary.provider_completions == 1,
        "blocking variant should record one complete provider lifecycle");
  check(summary.obsolete_commits == 1 && summary.obsolete_dispatches == 1,
        "blocking variant should expose the context change at commit and dispatch");
  check(variant.descriptor().reader_label == "blocking service call",
        "blocking variant should provide a reader-facing label");
}

void test_asynchronous_variant_returns_before_completion_and_accepts_stale_result()
{
  deterministic_coordinator coordinator(
      "context-a",
      {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
       {.sequence = 2,
        .at = 80ms,
        .kind = task_event_kind::context_changed,
        .context_id = "context-b"}});
  const request_record request = start_request(coordinator);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>(valid_result());
  asynchronous_variant variant(provider, recorder, [&coordinator] { return coordinator.now(); });

  variant.submit(request);
  provider->wait_for_start();
  check(variant.active_jobs() == 1,
        "ordinary asynchronous submission should return while provider work is active");

  coordinator.advance_to(80ms);
  provider->release();
  provider->wait_for_finish();
  wait_for([&] { return variant.poll(200ms).provider_completions == 1; },
           "asynchronous variant did not publish its completion");
  check(variant.dispatch(220ms), "asynchronous variant should dispatch its accepted completion");

  const effect_summary summary = recorder.summary(variant.descriptor().variant_id);
  check(summary.obsolete_commits == 1 && summary.obsolete_dispatches == 1,
        "ordinary asynchronous completion should expose stale acceptance");
  check(variant.descriptor().reader_label == "ordinary asynchronous completion",
        "asynchronous variant should provide a reader-facing label");
}

void test_asynchronous_variant_accepts_current_result()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
  const request_record request = start_request(coordinator);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<immediate_provider>(valid_result());
  asynchronous_variant variant(provider, recorder, [&coordinator] { return coordinator.now(); });

  variant.submit(request);
  wait_for([&] { return variant.poll(200ms).provider_completions == 1; },
           "asynchronous current completion did not become available");
  check(variant.dispatch(220ms), "current asynchronous completion should dispatch once");
  check(!variant.dispatch(240ms), "staged proposal should not dispatch twice");

  const effect_summary summary = recorder.summary(variant.descriptor().variant_id);
  check(summary.current_commits == 1 && summary.current_dispatches == 1,
        "ordinary asynchronous variant should accept a valid current result");
  check(!summary.has_obsolete_effect(), "current positive control should have no obsolete effect");
}

void test_shared_validation_rejects_invalid_result()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
  const request_record request = start_request(coordinator);
  effect_recorder recorder = make_recorder(coordinator);
  provider_result invalid = valid_result();
  invalid.proposal.pose[0] = 2.0;
  auto provider = std::make_shared<immediate_provider>(std::move(invalid));
  blocking_variant variant(provider, recorder, [&coordinator] { return coordinator.now(); });

  const variant_update update = variant.submit(request);
  check(update.rejections == 1 && update.last_reason == "invalid_pose",
        "blocking submission should expose its stable rejection outcome to the task");
  check(!variant.dispatch(20ms), "invalid proposal must not reach the dispatch stage");
  const effect_summary summary = recorder.summary(variant.descriptor().variant_id);
  check(summary.result_rejections == 1 && summary.terminal_decisions == 1 &&
            summary.current_commits == 0 && summary.obsolete_commits == 0,
        "common validation should reject an out-of-bounds proposal before commit");
  const std::vector<effect_record> records = recorder.snapshot();
  check(records.back().kind == effect_kind::result_rejected &&
            records.back().reason == "invalid_pose",
        "shared validation should record the stable invalid_pose reason");
}

void test_timeout_variant_claims_deadline_once_and_requests_cancellation()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
  const request_record request = start_request(coordinator);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>(valid_result());
  timeout_variant variant(provider, recorder, [&coordinator] { return coordinator.now(); });

  variant.submit(request);
  provider->wait_for_start();
  coordinator.advance_to(501ms);
  const variant_update timeout = variant.poll(501ms);
  check(timeout.rejections == 1 && timeout.last_reason == "deadline_expired",
        "timeout-only adapter should reject after its deadline");
  check(provider->cancellation_requests() == 1,
        "timeout-only adapter should request best-effort cancellation");

  provider->release();
  provider->wait_for_finish();
  wait_for([&] { return variant.poll(520ms).provider_completions == 1; },
           "late provider completion was not observed");
  check(!variant.dispatch(530ms), "timed-out result must not be staged for dispatch");

  const effect_summary summary = recorder.summary(variant.descriptor().variant_id);
  check(summary.terminal_decisions == 1 && summary.result_rejections == 1,
        "timeout and late completion should produce one terminal decision");
  check(summary.cancellation_requests == 1,
        "timeout evidence should contain one cancellation request");
}

void test_timeout_variant_does_not_gain_context_authority()
{
  deterministic_coordinator coordinator(
      "context-a",
      {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
       {.sequence = 2,
        .at = 80ms,
        .kind = task_event_kind::context_changed,
        .context_id = "context-b"}});
  const request_record request = start_request(coordinator);
  effect_recorder recorder = make_recorder(coordinator);
  auto provider = std::make_shared<gated_provider>(valid_result());
  timeout_variant variant(provider, recorder, [&coordinator] { return coordinator.now(); });

  variant.submit(request);
  provider->wait_for_start();
  coordinator.advance_to(80ms);
  provider->release();
  provider->wait_for_finish();
  wait_for([&] { return variant.poll(200ms).provider_completions == 1; },
           "timeout-only completion did not become available");
  check(variant.dispatch(220ms), "pre-deadline timeout-only result should remain dispatchable");

  const effect_summary summary = recorder.summary(variant.descriptor().variant_id);
  check(summary.obsolete_commits == 1 && summary.obsolete_dispatches == 1,
        "B2 must remain blind to context identity before the deadline");
}

}  // namespace

int main()
{
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
      {"effect recorder dispatch reassessment", test_effect_recorder_reassesses_at_dispatch},
      {"B0 blocking context change", test_blocking_variant_exposes_unobserved_context_change},
      {"B1 asynchronous stale completion",
       test_asynchronous_variant_returns_before_completion_and_accepts_stale_result},
      {"B1 asynchronous current completion", test_asynchronous_variant_accepts_current_result},
      {"shared proposal validation", test_shared_validation_rejects_invalid_result},
      {"B2 deadline terminal claim",
       test_timeout_variant_claims_deadline_once_and_requests_cancellation},
      {"B2 context limitation", test_timeout_variant_does_not_gain_context_authority},
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

  std::cout << "All controlled-authority variant tests passed (" << passed << "/"
            << tests.size() << ").\n";
  return 0;
}
