#include "common_task.hpp"

#include "bt/compiler.hpp"
#include "bt/runtime_host.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/reader.hpp"
#include "muslisp/value.hpp"

#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
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

void check_reason(const authority_assessment& assessment, authority_reason expected,
                  const std::string& message)
{
  check(assessment.reason == expected && assessment.current == (expected == authority_reason::current),
        message);
}

void test_current_and_deadline_boundary()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
  coordinator.advance_to(0ms);
  const request_record request = coordinator.submit_request(500ms);

  check_reason(coordinator.assess(request, 500ms), authority_reason::current,
               "result at the exact deadline should remain current");
  check_reason(coordinator.assess(request, 501ms), authority_reason::deadline_expired,
               "result after the deadline should be expired");
}

void test_context_change()
{
  deterministic_coordinator coordinator(
      "context-a",
      {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
       {.sequence = 2,
        .at = 80ms,
        .kind = task_event_kind::context_changed,
        .context_id = "context-b"}});
  coordinator.advance_to(0ms);
  const request_record request = coordinator.submit_request(500ms);
  coordinator.advance_to(80ms);

  check_reason(coordinator.assess(request, 200ms), authority_reason::context_changed,
               "captured context should become obsolete after a context change");
  check(coordinator.task_state().model_branch_active,
        "context change alone should not pre-empt the model branch");
}

void test_branch_exit_and_reentry()
{
  deterministic_coordinator coordinator(
      "context-a",
      {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
       {.sequence = 2, .at = 80ms, .kind = task_event_kind::leave_model_branch},
       {.sequence = 3, .at = 120ms, .kind = task_event_kind::reenter_model_branch}});
  coordinator.advance_to(0ms);
  const request_record old_request = coordinator.submit_request(500ms);
  coordinator.advance_to(120ms);

  check_reason(coordinator.assess(old_request, 200ms), authority_reason::branch_revoked,
               "old epoch should not satisfy a re-entered model branch");
  check(coordinator.task_state().branch_epoch == old_request.branch_epoch + 1,
        "re-entry should advance the branch epoch");
}

void test_supersession()
{
  deterministic_coordinator coordinator(
      "context-a", {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch}});
  coordinator.advance_to(0ms);
  const request_record old_request = coordinator.submit_request(500ms);
  coordinator.advance_to(120ms);
  const request_record current_request = coordinator.submit_request(500ms);

  check_reason(coordinator.assess(old_request, 200ms), authority_reason::superseded,
               "older generation should be superseded");
  check_reason(coordinator.assess(current_request, 200ms), authority_reason::current,
               "newest generation should remain current");
}

void test_emergency_and_reset()
{
  deterministic_coordinator emergency(
      "context-a",
      {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
       {.sequence = 2, .at = 80ms, .kind = task_event_kind::emergency_activated}});
  emergency.advance_to(0ms);
  const request_record emergency_request = emergency.submit_request(500ms);
  emergency.advance_to(80ms);
  check_reason(emergency.assess(emergency_request, 200ms), authority_reason::branch_revoked,
               "emergency should revoke model-branch authority");
  check(emergency.task_state().branch == task_branch::safe_stand,
        "emergency should select safe stand");

  deterministic_coordinator reset(
      "context-a",
      {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
       {.sequence = 2, .at = 80ms, .kind = task_event_kind::runtime_reset}});
  reset.advance_to(0ms);
  const request_record reset_request = reset.submit_request(500ms);
  reset.advance_to(80ms);
  check_reason(reset.assess(reset_request, 200ms), authority_reason::branch_revoked,
               "reset should revoke work captured before the reset epoch");
}

void test_task_and_oracle_are_kept_in_step()
{
  deterministic_coordinator coordinator(
      "context-a",
      {{.sequence = 1, .at = 0ms, .kind = task_event_kind::enter_model_branch},
       {.sequence = 2,
        .at = 40ms,
        .kind = task_event_kind::context_changed,
        .context_id = "context-b"},
       {.sequence = 3, .at = 80ms, .kind = task_event_kind::emergency_activated}});
  coordinator.advance_to(80ms);
  const task_snapshot task = coordinator.task_state();
  const task_snapshot oracle = coordinator.oracle_state();

  check(task.branch == oracle.branch && task.model_branch_active == oracle.model_branch_active &&
            task.emergency == oracle.emergency && task.branch_epoch == oracle.branch_epoch &&
            task.context_id == oracle.context_id,
        "coordinator should deliver the same ordered world events to task and oracle");
  check(coordinator.remaining_events() == 0, "coordinator should consume all eligible events");
}

void test_common_lisp_task_preempts_model_branch()
{
  std::ifstream input(MUESLI_BT_CONTROLLED_AUTHORITY_COMMON_TREE);
  check(input.good(), "common task Lisp source should be readable");
  const std::string source((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());

  bool emergency = false;
  int model_ticks = 0;
  int model_halts = 0;
  int safe_stand_ticks = 0;
  bt::runtime_host host;
  host.callbacks().register_condition(
      "controlled-emergency?",
      [&emergency](bt::tick_context&, std::span<const muslisp::value>) { return emergency; });
  host.callbacks().register_action(
      "controlled-safe-stand",
      [&safe_stand_ticks](bt::tick_context&, bt::node_id, bt::node_memory&,
                          std::span<const muslisp::value>)
      {
        ++safe_stand_ticks;
        return bt::status::running;
      });
  host.callbacks().register_action(
      "controlled-model-step",
      [&model_ticks](bt::tick_context&, bt::node_id, bt::node_memory&,
                     std::span<const muslisp::value>)
      {
        ++model_ticks;
        return bt::status::running;
      },
      [&model_halts](bt::tick_context&, bt::node_id, bt::node_memory&) { ++model_halts; });
  host.callbacks().register_action(
      "controlled-dispatch-step",
      [](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>)
      { return bt::status::success; });
  host.callbacks().register_action(
      "controlled-fallback",
      [](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>)
      { return bt::status::running; });

  std::vector<muslisp::value> expressions = muslisp::read_all(source);
  muslisp::gc_root_scope roots(muslisp::default_gc());
  for (muslisp::value& expression : expressions)
  {
    roots.add(&expression);
  }
  check(expressions.size() == 1 && muslisp::is_proper_list(expressions.front()),
        "common task should contain one defbt form");
  const std::vector<muslisp::value> form = muslisp::vector_from_list(expressions.front());
  check(form.size() == 3 && muslisp::is_symbol(form[0]) &&
            muslisp::symbol_name(form[0]) == "defbt" && muslisp::is_symbol(form[1]),
        "common task should have the form (defbt name tree)");

  const auto definition = host.store_definition(bt::compile_definition(form[2]));
  const auto instance = host.create_instance(definition);
  check(host.tick_instance(instance) == bt::status::running && model_ticks == 1,
        "common task should tick the model branch when no emergency is active");

  emergency = true;
  check(host.tick_instance(instance) == bt::status::running && safe_stand_ticks == 1,
        "common task should select safe stand when emergency becomes active");
  check(model_halts == 1, "reactive pre-emption should halt the running model branch");
}

}  // namespace

int main()
{
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
      {"current result and deadline boundary", test_current_and_deadline_boundary},
      {"context change", test_context_change},
      {"branch exit and re-entry", test_branch_exit_and_reentry},
      {"request supersession", test_supersession},
      {"emergency and reset", test_emergency_and_reset},
      {"task and oracle event parity", test_task_and_oracle_are_kept_in_step},
      {"common Lisp task pre-emption", test_common_lisp_task_preempts_model_branch},
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

  std::cout << "All controlled-authority common-task tests passed (" << passed << "/"
            << tests.size() << ").\n";
  return 0;
}
