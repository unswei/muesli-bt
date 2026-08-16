#include "btcpp_task_runner.hpp"
#include "btcpp_variant.hpp"
#include "scripted_provider.hpp"

#include <behaviortree_cpp/basic_types.h>

#include <chrono>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace std::chrono_literals;
using namespace muesli_bt::experiments::controlled_authority;

void check(bool condition, std::string_view message)
{
  if (!condition)
  {
    throw std::runtime_error(std::string(message));
  }
}

provider_result valid_result(std::string id, std::size_t completion_copies = 1)
{
  return provider_result{
      .status = provider_status::ok,
      .proposal = {.response_id = std::move(id),
                   .frame_id = "task_context",
                   .pose = {0.2, -0.1, 0.3},
                   .schema_valid = true},
      .reason = {},
      .completion_copies = completion_copies,
  };
}

std::vector<scripted_provider_job> jobs(std::size_t count = 1,
                                        std::size_t first_completion_copies = 1)
{
  std::vector<scripted_provider_job> result;
  for (std::size_t index = 0; index < count; ++index)
  {
    result.push_back(scripted_provider_job{
        .request_label = "r" + std::to_string(index + 1),
        .result = valid_result("response-r" + std::to_string(index + 1),
                               index == 0 ? first_completion_copies : 1),
    });
  }
  return result;
}

enum class profile
{
  ordinary,
  full,
};

class fixture
{
public:
  fixture(profile selected, std::vector<task_event> events,
          std::vector<scripted_provider_job> provider_jobs)
      : coordinator("context-a", std::move(events)),
        recorder([this](const request_record& request, logical_time at)
                 { return coordinator.assess(request, at); }),
        provider(std::make_shared<scripted_provider>(std::move(provider_jobs)))
  {
    const logical_now now = [this] { return coordinator.now(); };
    std::unique_ptr<authority_variant> variant;
    if (selected == profile::ordinary)
    {
      variant = std::make_unique<btcpp_asynchronous_variant>(provider, recorder, now);
    }
    else
    {
      variant = std::make_unique<btcpp_invocation_scoped_variant>(provider, recorder, now);
    }
    runner = std::make_unique<btcpp_task_runner>(coordinator, recorder, std::move(variant));
  }

  ~fixture() { provider->release_all(); }

  void enter_and_submit(std::string_view request = "r1")
  {
    coordinator.advance_to(0ms);
    runner->request_submission();
    check(runner->tick() == BT::NodeStatus::RUNNING, "submission tick should remain running");
    provider->wait_until_started(request);
  }

  void release(std::string_view request, logical_time at)
  {
    coordinator.advance_to(at);
    provider->release(request);
    provider->wait_until_finished(request);
  }

  [[nodiscard]] effect_summary summary() const
  {
    return recorder.summary(runner->variant().descriptor().variant_id);
  }

  template <typename Predicate> BT::NodeStatus tick_until(Predicate&& predicate)
  {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    BT::NodeStatus status = BT::NodeStatus::IDLE;
    do
    {
      status = runner->tick();
      if (predicate())
      {
        return status;
      }
      std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("timed out waiting for the BehaviorTree.CPP completion");
  }

  [[nodiscard]] bool has_rejection(std::string_view reason) const
  {
    for (const effect_record& effect : recorder.snapshot())
    {
      if (effect.kind == effect_kind::result_rejected && effect.reason == reason)
      {
        return true;
      }
    }
    return false;
  }

  deterministic_coordinator coordinator;
  effect_recorder recorder;
  std::shared_ptr<scripted_provider> provider;
  std::unique_ptr<btcpp_task_runner> runner;
};

std::vector<task_event> enter_only()
{
  return {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}};
}

void test_current_result_accepts_and_dispatches()
{
  fixture test(profile::full, enter_only(), jobs());
  test.enter_and_submit();
  test.release("r1", 200ms);
  check(test.tick_until([&test] { return test.summary().current_commits == 1; }) ==
            BT::NodeStatus::RUNNING,
        "accepted result should enter the delayed dispatch node");
  check(test.summary().current_commits == 1, "current result should commit exactly once");
  test.coordinator.advance_to(220ms);
  check(test.runner->tick() == BT::NodeStatus::SUCCESS,
        "current proposal should dispatch successfully");
  check(test.summary().current_dispatches == 1, "current target should dispatch once");
}

void test_context_change_distinguishes_profiles()
{
  const std::vector<task_event> events{
      {.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
      {.sequence = 2,
       .at = 80ms,
       .kind = task_event_kind::context_changed,
       .context_id = "context-b"},
  };
  fixture ordinary(profile::ordinary, events, jobs());
  ordinary.enter_and_submit();
  ordinary.release("r1", 200ms);
  (void)ordinary.tick_until([&ordinary] { return ordinary.summary().obsolete_commits == 1; });
  check(ordinary.summary().obsolete_commits == 1,
        "ordinary async should expose a stale context commit");

  fixture full(profile::full, events, jobs());
  full.enter_and_submit();
  full.release("r1", 200ms);
  (void)full.tick_until([&full] { return full.summary().terminal_decisions == 1; });
  check(full.has_rejection("context_changed"), "full port should reject the changed context");
  check(!full.summary().has_obsolete_effect(), "full port must not produce a stale context effect");
}

void test_late_completion_is_rejected()
{
  fixture full(profile::full, enter_only(), jobs());
  full.enter_and_submit();
  full.release("r1", 501ms);
  (void)full.tick_until([&full] { return full.summary().terminal_decisions == 1; });
  check(full.has_rejection("deadline_expired"), "late completion should fail the deadline gate");
  check(!full.summary().has_obsolete_effect(), "late completion must not commit");
}

void test_branch_halt_uses_stateful_lifecycle_and_revocation()
{
  const std::vector<task_event> events{
      {.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
      {.sequence = 2, .at = 80ms, .kind = task_event_kind::leave_model_branch},
      {.sequence = 3, .at = 120ms, .kind = task_event_kind::reenter_model_branch},
  };
  fixture ordinary(profile::ordinary, events, jobs());
  ordinary.enter_and_submit();
  ordinary.coordinator.advance_to(80ms);
  (void)ordinary.runner->tick();
  check(ordinary.summary().cancellation_requests == 1,
        "ordinary StatefulActionNode halt should request provider cancellation");
  ordinary.coordinator.advance_to(120ms);
  (void)ordinary.runner->tick();
  ordinary.release("r1", 200ms);
  (void)ordinary.tick_until(
      [&ordinary] { return ordinary.summary().obsolete_commits == 1; });
  check(ordinary.summary().obsolete_commits == 1,
        "physical cancellation alone should not authorise a late re-entry result");

  fixture full(profile::full, events, jobs());
  full.enter_and_submit();
  full.coordinator.advance_to(80ms);
  (void)full.runner->tick();
  check(full.has_rejection("branch_revoked"), "full halt should logically revoke the request");
  full.coordinator.advance_to(120ms);
  (void)full.runner->tick();
  full.release("r1", 200ms);
  (void)full.runner->tick();
  check(!full.summary().has_obsolete_effect(), "revoked completion must not commit on re-entry");
  check(full.summary().terminal_decisions == 1, "revoked request needs one terminal outcome");
}

void test_emergency_pre_empts_and_activates_safe_stand()
{
  const std::vector<task_event> events{
      {.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
      {.sequence = 2, .at = 80ms, .kind = task_event_kind::emergency_activated},
  };
  fixture full(profile::full, events, jobs());
  full.enter_and_submit();
  full.coordinator.advance_to(80ms);
  check(full.runner->tick() == BT::NodeStatus::RUNNING,
        "safe-stand branch should remain running");
  check(full.summary().safe_stand_activations == 1, "emergency should activate safe stand once");
  check(full.has_rejection("branch_revoked"), "emergency halt should revoke the request");
  full.release("r1", 200ms);
  (void)full.runner->tick();
  check(!full.summary().has_obsolete_effect(), "emergency completion must not create an effect");
}

void test_supersession_rejects_old_then_accepts_current()
{
  fixture full(profile::full, enter_only(), jobs(2));
  full.enter_and_submit("r1");
  full.coordinator.advance_to(120ms);
  full.runner->request_submission();
  (void)full.runner->tick();
  full.provider->wait_until_started("r2");

  full.release("r1", 200ms);
  (void)full.tick_until([&full] { return full.has_rejection("superseded"); });
  check(full.has_rejection("superseded"), "older generation should be rejected");
  full.release("r2", 240ms);
  (void)full.tick_until([&full] { return full.summary().current_commits == 1; });
  check(full.summary().current_commits == 1, "newest generation should be accepted");
  check(!full.summary().has_obsolete_effect(), "supersession must not produce obsolete effects");
}

void test_dispatch_revalidates_context()
{
  const std::vector<task_event> events{
      {.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
      {.sequence = 2,
       .at = 210ms,
       .kind = task_event_kind::context_changed,
       .context_id = "context-b"},
  };
  fixture full(profile::full, events, jobs());
  full.enter_and_submit();
  full.release("r1", 200ms);
  (void)full.tick_until([&full] { return full.summary().current_commits == 1; });
  check(full.summary().current_commits == 1, "proposal should be current at admission");
  full.coordinator.advance_to(220ms);
  (void)full.runner->tick();
  check(full.summary().dispatch_rejections == 1,
        "full port should reject a target made stale before dispatch");
  check(full.summary().current_dispatches == 0 && full.summary().obsolete_dispatches == 0,
        "rejected stale target must not dispatch");
}

void test_duplicate_completion_has_one_terminal_decision()
{
  fixture full(profile::full, enter_only(), jobs(1, 2));
  full.enter_and_submit();
  full.release("r1", 200ms);
  (void)full.tick_until([&full] { return full.summary().terminal_decisions == 1; });
  check(full.summary().terminal_decisions == 1,
        "full terminal claim should drop a duplicate completion");
  const std::vector<std::string> events = full.runner->variant_events();
  check(!events.empty() && events.front().find("\"type\":\"run_start\"") != std::string::npos,
        "variant evidence should begin with a canonical run_start");
  bool dropped = false;
  for (const std::string& event : events)
  {
    dropped = dropped || event.find("\"type\":\"async_completion_dropped\"") !=
                           std::string::npos;
  }
  check(dropped, "duplicate completion should be visible in canonical evidence");
}

void test_cancel_completion_race_has_one_terminal_decision()
{
  fixture full(profile::full, enter_only(), jobs());
  full.enter_and_submit();
  full.coordinator.advance_to(200ms);
  const std::uint64_t request_id = full.runner->submitted_requests().front().request_id;
  const variant_update cancelled = full.runner->cancel_request(request_id);
  check(cancelled.rejections == 1, "explicit cancellation should claim the terminal outcome");
  full.provider->release("r1");
  full.provider->wait_until_finished("r1");
  (void)full.runner->tick();
  check(full.summary().terminal_decisions == 1,
        "completion after cancellation must not create a second terminal outcome");
  check(!full.summary().has_obsolete_effect(), "cancelled completion must not commit");
}

void test_host_validation_rejects_invalid_pose()
{
  std::vector<scripted_provider_job> provider_jobs = jobs();
  provider_jobs.front().result.proposal.pose[0] = 2.0;
  fixture full(profile::full, enter_only(), std::move(provider_jobs));
  full.enter_and_submit();
  full.release("r1", 200ms);
  (void)full.tick_until([&full] { return full.summary().terminal_decisions == 1; });
  check(full.has_rejection("invalid_pose"), "shared host bounds should reject the proposal");
  check(full.summary().current_commits == 0, "host-invalid proposal must not commit");
}

void test_ordinary_rejection_completes_lifecycle_deterministically()
{
  std::vector<scripted_provider_job> provider_jobs = jobs();
  provider_jobs.front().result.proposal.pose[0] = 2.0;
  fixture ordinary(profile::ordinary, enter_only(), std::move(provider_jobs));
  ordinary.enter_and_submit();
  ordinary.release("r1", 200ms);
  (void)ordinary.tick_until(
      [&ordinary] { return ordinary.summary().terminal_decisions == 1; });
  check(ordinary.has_rejection("invalid_pose"),
        "ordinary lifecycle should expose the host rejection");
  check(ordinary.summary().fallback_activations == 1,
        "ordinary lifecycle should activate fallback in the rejecting tick");
  check(ordinary.runner->variant().active_jobs() == 0,
        "an observed ordinary completion should no longer be logically active");
}

void test_reset_revokes_pending_work()
{
  const std::vector<task_event> events{
      {.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
      {.sequence = 2, .at = 80ms, .kind = task_event_kind::runtime_reset},
  };
  fixture full(profile::full, events, jobs());
  full.enter_and_submit();
  full.coordinator.advance_to(80ms);
  full.runner->reset();
  check(full.has_rejection("branch_revoked"), "runtime reset should revoke pending work");
  full.release("r1", 200ms);
  (void)full.runner->tick();
  check(!full.summary().has_obsolete_effect(), "completion after reset must not commit");
  check(full.runner->variant().active_jobs() == 0, "reset should leave no authoritative job");
}

void write_lines(const std::filesystem::path& path, const std::vector<std::string>& lines)
{
  std::ofstream output(path);
  if (!output)
  {
    throw std::runtime_error("could not create BehaviorTree.CPP evidence fixture");
  }
  for (const std::string& line : lines)
  {
    output << line << '\n';
  }
}

void dump_canonical_evidence(const std::filesystem::path& output_directory)
{
  std::filesystem::create_directories(output_directory);
  fixture full(profile::full, enter_only(), jobs());
  full.enter_and_submit();
  full.release("r1", 200ms);
  (void)full.tick_until([&full] { return full.summary().current_commits == 1; });
  full.coordinator.advance_to(220ms);
  (void)full.runner->tick();
  write_lines(output_directory / "task.mbt.evt.v1.jsonl", full.runner->task_events());
  write_lines(output_directory / "variant.mbt.evt.v1.jsonl", full.runner->variant_events());
}

} // namespace

int main(int argc, char** argv)
{
  if (argc == 3 && std::string_view(argv[1]) == "--dump-events")
  {
    try
    {
      dump_canonical_evidence(argv[2]);
      return 0;
    }
    catch (const std::exception& error)
    {
      std::cerr << "FAIL evidence dump: " << error.what() << '\n';
      return 1;
    }
  }
  if (argc != 1)
  {
    std::cerr << "usage: muesli_bt_controlled_authority_btcpp_tests [--dump-events DIRECTORY]\n";
    return 2;
  }
  const std::vector<std::pair<std::string, std::function<void()>>> tests{
      {"current acceptance", test_current_result_accepts_and_dispatches},
      {"context identity", test_context_change_distinguishes_profiles},
      {"late completion", test_late_completion_is_rejected},
      {"branch halt and re-entry", test_branch_halt_uses_stateful_lifecycle_and_revocation},
      {"emergency interruption", test_emergency_pre_empts_and_activates_safe_stand},
      {"supersession", test_supersession_rejects_old_then_accepts_current},
      {"dispatch revalidation", test_dispatch_revalidates_context},
      {"duplicate completion", test_duplicate_completion_has_one_terminal_decision},
      {"cancel completion race", test_cancel_completion_race_has_one_terminal_decision},
      {"host validation", test_host_validation_rejects_invalid_pose},
      {"ordinary rejection lifecycle",
       test_ordinary_rejection_completes_lifecycle_deterministically},
      {"runtime reset", test_reset_revokes_pending_work},
  };

  std::size_t passed = 0;
  for (const auto& [name, test] : tests)
  {
    try
    {
      test();
      ++passed;
    }
    catch (const std::exception& error)
    {
      std::cerr << "FAIL " << name << ": " << error.what() << '\n';
      return 1;
    }
  }
  std::cout << "All BehaviorTree.CPP authority tests passed (" << passed << '/' << tests.size()
            << ")\n";
  return 0;
}
