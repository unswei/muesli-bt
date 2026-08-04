#include "bt/approach_pose_validator.hpp"
#include "bt/compiler.hpp"
#include "bt/runtime.hpp"
#include "bt/runtime_host.hpp"
#include "muslisp/reader.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using namespace std::chrono_literals;

constexpr std::string_view kBaseTree =
    "(reactive-sel "
    "  (seq (vla-wait :name \"approach\" :job_key approach-job :action_key approach-action "
    "                 :meta_key approach-meta :clear_job #f) "
    "       (succeed)) "
    "  (vla-request :name \"approach\" :job_key approach-job :instruction \"approach the ball\" "
    "               :state_key state :model_name \"humanoid-scripted\" :deadline_ms 5000 :dims 3 "
    "               :action_frame ball_context :acceptance_policy invocation_scoped "
    "               :context_key ball-context))";

constexpr std::string_view kCancelTree =
    "(reactive-sel "
    "  (seq (cond bb-truthy cancel-now) (vla-cancel :name \"cancel-approach\" :job_key "
    "approach-job) "
    "       (succeed)) "
    "  (seq (vla-wait :name \"approach\" :job_key approach-job :action_key approach-action "
    "                 :meta_key approach-meta :clear_job #f) "
    "       (succeed)) "
    "  (vla-request :name \"approach\" :job_key approach-job :instruction \"approach the ball\" "
    "               :state_key state :model_name \"humanoid-scripted\" :deadline_ms 5000 :dims 3 "
    "               :action_frame ball_context :acceptance_policy invocation_scoped "
    "               :context_key ball-context))";

constexpr std::string_view kEmergencyTree =
    "(reactive-sel "
    "  (seq (cond bb-truthy emergency) (succeed)) "
    "  (seq (vla-wait :name \"approach\" :job_key approach-job :action_key approach-action "
    "                 :meta_key approach-meta :clear_job #f) "
    "       (succeed)) "
    "  (vla-request :name \"approach\" :job_key approach-job :instruction \"approach the ball\" "
    "               :state_key state :model_name \"humanoid-scripted\" :deadline_ms 5000 :dims 3 "
    "               :action_frame ball_context :acceptance_policy invocation_scoped "
    "               :context_key ball-context))";

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

bool event_has(const bt::event_log& events, std::string_view type,
               std::initializer_list<std::string_view> fields = {})
{
  const std::string type_field = "\"type\":\"" + std::string(type) + "\"";
  for (const std::string& line : events.snapshot())
  {
    if (line.find(type_field) == std::string::npos)
    {
      continue;
    }
    bool matches = true;
    for (const std::string_view field : fields)
    {
      if (line.find(field) == std::string::npos)
      {
        matches = false;
        break;
      }
    }
    if (matches)
    {
      return true;
    }
  }
  return false;
}

class manual_clock final : public bt::clock_interface
{
public:
  std::chrono::steady_clock::time_point now() const override { return now_; }

private:
  std::chrono::steady_clock::time_point now_{std::chrono::seconds(100)};
};

struct completion_gate
{
  double action_value = 0.25;
  bool ignore_cancel = false;

  void mark_started()
  {
    {
      std::lock_guard<std::mutex> lock(mutex);
      started = true;
    }
    cv.notify_all();
  }

  void release()
  {
    {
      std::lock_guard<std::mutex> lock(mutex);
      released = true;
    }
    cv.notify_all();
  }

  bool wait_until_released(std::atomic<bool>& cancel_flag)
  {
    std::unique_lock<std::mutex> lock(mutex);
    while (!released)
    {
      if (cancel_flag.load() && !ignore_cancel)
      {
        return false;
      }
      cv.wait_for(lock, 1ms);
    }
    return true;
  }

  void mark_finished()
  {
    {
      std::lock_guard<std::mutex> lock(mutex);
      finished = true;
    }
    cv.notify_all();
  }

  void wait_for_start()
  {
    std::unique_lock<std::mutex> lock(mutex);
    check(cv.wait_for(lock, 2000ms, [this] { return started; }),
          "scripted VLA backend did not start");
  }

  void wait_for_finish()
  {
    std::unique_lock<std::mutex> lock(mutex);
    check(cv.wait_for(lock, 2000ms, [this] { return finished; }),
          "scripted VLA backend did not finish");
  }

private:
  std::mutex mutex;
  std::condition_variable cv;
  bool started = false;
  bool released = false;
  bool finished = false;
};

class gated_vla_backend final : public bt::vla_backend
{
public:
  explicit gated_vla_backend(std::vector<std::shared_ptr<completion_gate>> gates)
      : gates_(std::move(gates))
  {
  }

  bt::vla_response infer(const bt::vla_request& request,
                         std::function<bool(const bt::vla_partial&)>,
                         std::atomic<bool>& cancel_flag) override
  {
    const std::size_t index = next_gate_.fetch_add(1);
    if (index >= gates_.size())
    {
      throw std::runtime_error("scripted VLA backend received an unexpected invocation");
    }
    const std::shared_ptr<completion_gate>& gate = gates_[index];
    gate->mark_started();

    bt::vla_response response;
    response.model = request.model;
    response.action.type = bt::vla_action_type::continuous;
    response.action.frame_id = request.action_space.frame_id;
    response.action.u.assign(static_cast<std::size_t>(request.action_space.dims),
                             gate->action_value);
    response.confidence = 1.0;
    response.explanation = "deterministic humanoid scenario";

    if (!gate->wait_until_released(cancel_flag))
    {
      response.status = bt::vla_status::cancelled;
      response.explanation = "cancelled before scripted completion";
      gate->mark_finished();
      return response;
    }

    response.status = bt::vla_status::ok;
    gate->mark_finished();
    return response;
  }

  void release_all()
  {
    for (const auto& gate : gates_)
    {
      gate->release();
    }
  }

private:
  std::vector<std::shared_ptr<completion_gate>> gates_;
  std::atomic<std::size_t> next_gate_{0};
};

class recording_dispatcher final : public bt::walking_target_dispatcher
{
public:
  bt::walking_target_dispatch_result dispatch(const bt::walking_target_dispatch_context& context,
                                              const bt::walking_target& target) override
  {
    ++calls;
    last_context = context;
    last_target = target;
    return {.accepted = true, .reason = {}};
  }

  std::size_t calls = 0;
  bt::walking_target_dispatch_context last_context;
  bt::walking_target last_target;
};

class scenario_rig
{
public:
  scenario_rig(std::string_view tree, std::vector<std::shared_ptr<completion_gate>> gates,
               std::string run_id)
      : validator_(bt::approach_pose_validator_config{.frame_id = "ball_context",
                                                      .bounds = {.min_x_m = -1.0,
                                                                 .max_x_m = 1.0,
                                                                 .min_y_m = -1.0,
                                                                 .max_y_m = 1.0,
                                                                 .min_yaw_rad = -3.141593,
                                                                 .max_yaw_rad = 3.141593}},
                   [this] { return host_state_; }),
        backend_(std::make_shared<gated_vla_backend>(std::move(gates)))
  {
    host_.enable_deterministic_test_mode(424242, std::move(run_id), 1735689605000, 1);
    host_.set_clock_interface(&clock_);
    host_.set_vla_commit_validator(&validator_);
    host_.set_walking_target_dispatcher(&dispatcher_);
    host_.vla_ref().set_cache_ttl_ms(0);
    host_.vla_ref().register_backend("humanoid-scripted", backend_);

    const std::int64_t definition_handle =
        host_.store_definition(bt::compile_definition(muslisp::read_one(tree)));
    instance_handle_ = host_.create_instance(definition_handle);
    instance_ = host_.find_instance(instance_handle_);
    check(instance_ != nullptr, "scenario instance was not created");
    set_ball_context("ball-A");
    put("state", bt::bb_value{std::vector<double>{0.0, 0.0, 0.0}});
  }

  ~scenario_rig()
  {
    backend_->release_all();
    host_.set_vla_commit_validator(nullptr);
    host_.set_walking_target_dispatcher(nullptr);
    host_.set_clock_interface(nullptr);
  }

  scenario_rig(const scenario_rig&) = delete;
  scenario_rig& operator=(const scenario_rig&) = delete;

  void put(std::string key, bt::bb_value value)
  {
    instance_->bb.put(std::move(key), std::move(value), instance_->tick_index, clock_.now(), 0,
                      "humanoid-scenario-test");
  }

  void set_ball_context(std::string context_id)
  {
    host_state_.ball_context_id = context_id;
    put("ball-context", bt::bb_value{std::move(context_id)});
  }

  bt::status tick() { return host_.tick_instance(instance_handle_); }

  std::uint64_t only_job_id() const
  {
    check(instance_->vla_invocations.size() == 1, "scenario expected exactly one invocation");
    return instance_->vla_invocations.begin()->first;
  }

  bt::vla_invocation& invocation(std::uint64_t job_id)
  {
    return instance_->vla_invocations.at(job_id);
  }

  bt::services services()
  {
    return bt::services{
        .sched = &host_.scheduler_ref(),
        .obs = {.trace = &instance_->trace, .logger = &host_.logs(), .events = &host_.events()},
        .clock = &clock_,
        .robot = host_.robot_interface_ptr(),
        .planner = &host_.planner_ref(),
        .vla = &host_.vla_ref(),
        .vla_commit = &validator_,
    };
  }

  bt::runtime_host& host() { return host_; }
  bt::instance& instance() { return *instance_; }
  recording_dispatcher& dispatcher() { return dispatcher_; }

private:
  bt::runtime_host host_;
  manual_clock clock_;
  bt::approach_pose_host_state host_state_{.ball_context_id = "ball-A", .robot_stable = true};
  bt::approach_pose_validator validator_;
  recording_dispatcher dispatcher_;
  std::shared_ptr<gated_vla_backend> backend_;
  std::int64_t instance_handle_ = 0;
  bt::instance* instance_ = nullptr;
};

void adopt_running_invocation(scenario_rig& rig, std::uint64_t job_id)
{
  check(rig.tick() == bt::status::running, "wait branch should remain running before completion");
  check(rig.invocation(job_id).authority_node != rig.invocation(job_id).requesting_node,
        "vla-wait should own authority before scripted completion");
}

void wait_for_authority(scenario_rig& rig, std::uint64_t job_id, bt::vla_authority_state state)
{
  wait_for(
      [&]
      {
        (void)rig.tick();
        return rig.invocation(job_id).authority_state == state;
      },
      "invocation did not reach the expected authority state");
}

void wait_for_completion_drop(scenario_rig& rig)
{
  wait_for(
      [&]
      {
        return event_has(rig.host().events(), "async_completion_dropped",
                         {"\"reason\":\"completion_after_cancel\""});
      },
      "late completion was not recorded as dropped");
}

bt::walking_target accepted_target(double value)
{
  return bt::walking_target{
      .frame_id = "ball_context",
      .x_m = value,
      .y_m = value,
      .yaw_rad = value,
  };
}

void test_normal_acceptance()
{
  auto gate = std::make_shared<completion_gate>();
  scenario_rig rig(kBaseTree, {gate}, "humanoid-normal-acceptance");

  check(rig.tick() == bt::status::running, "normal scenario should submit a request");
  const std::uint64_t job_id = rig.only_job_id();
  adopt_running_invocation(rig, job_id);
  gate->wait_for_start();
  gate->release();
  wait_for_authority(rig, job_id, bt::vla_authority_state::accepted);

  const bt::bb_entry* action = rig.instance().bb.get("approach-action");
  check(action && std::holds_alternative<std::vector<double>>(action->value),
        "normal scenario should commit the three-dimensional approach pose");
  const bt::walking_target_dispatch_result dispatch = rig.host().dispatch_walking_target(
      rig.instance().instance_handle, job_id, 900, accepted_target(0.25));
  check(dispatch.accepted && rig.dispatcher().calls == 1,
        "normal scenario should dispatch the accepted walking target exactly once");
  check(event_has(rig.host().events(), "vla_result",
                  {"\"generation\":1", "\"decision\":\"accepted\"",
                   "\"captured_context_id\":\"ball-A\""}),
        "normal scenario should emit accepted invocation evidence");
  check(event_has(rig.host().events(), "walking_target_dispatch",
                  {"\"decision\":\"accepted\"", "\"generation\":1"}),
        "normal scenario should emit accepted walking-target evidence");
}

void test_moved_ball()
{
  auto gate = std::make_shared<completion_gate>();
  scenario_rig rig(kBaseTree, {gate}, "humanoid-moved-ball");

  check(rig.tick() == bt::status::running, "moved-ball scenario should submit a request");
  const std::uint64_t job_id = rig.only_job_id();
  adopt_running_invocation(rig, job_id);
  gate->wait_for_start();
  rig.set_ball_context("ball-B");
  gate->release();
  wait_for_authority(rig, job_id, bt::vla_authority_state::rejected);

  check(rig.invocation(job_id).authority_reason == "context_changed",
        "moved-ball result should be rejected with context_changed");
  check(rig.instance().bb.get("approach-action") == nullptr,
        "moved-ball result must not write an approach action");
  check(rig.dispatcher().calls == 0, "moved-ball result must not reach the walking controller");
  check(event_has(rig.host().events(), "vla_result",
                  {"\"decision\":\"rejected\"", "\"reason\":\"context_changed\"",
                   "\"captured_context_id\":\"ball-A\"", "\"current_context_id\":\"ball-B\""}),
        "moved-ball scenario should emit both captured and current context evidence");
}

void test_supersession()
{
  auto obsolete = std::make_shared<completion_gate>();
  obsolete->action_value = 0.1;
  obsolete->ignore_cancel = true;
  auto current = std::make_shared<completion_gate>();
  current->action_value = 0.4;
  scenario_rig rig(kBaseTree, {obsolete, current}, "humanoid-supersession");

  check(rig.tick() == bt::status::running, "supersession scenario should submit generation one");
  const std::uint64_t obsolete_job = rig.only_job_id();
  adopt_running_invocation(rig, obsolete_job);
  obsolete->wait_for_start();

  rig.put("approach-job", bt::bb_value{std::monostate{}});
  check(rig.tick() == bt::status::running,
        "cleared job key should submit the replacement generation");
  const std::uint64_t current_job = rig.only_job_id();
  check(current_job != obsolete_job, "supersession should allocate a new backend job");
  check(rig.invocation(current_job).generation == 2, "supersession should advance the generation");
  check(event_has(
            rig.host().events(), "async_authority_revoked",
            {"\"job_id\":\"" + std::to_string(obsolete_job) + "\"", "\"reason\":\"superseded\""}),
        "supersession should canonically revoke generation one");

  adopt_running_invocation(rig, current_job);
  current->wait_for_start();
  obsolete->release();
  obsolete->wait_for_finish();
  wait_for_completion_drop(rig);
  check(rig.instance().bb.get("approach-action") == nullptr,
        "late completion from the superseded generation must not write an action");

  current->release();
  wait_for_authority(rig, current_job, bt::vla_authority_state::accepted);
  const bt::bb_entry* action = rig.instance().bb.get("approach-action");
  check(action && std::holds_alternative<std::vector<double>>(action->value) &&
            std::get<std::vector<double>>(action->value).front() == 0.4,
        "only the replacement generation should commit its target");
  check(event_has(rig.host().events(), "vla_result",
                  {"\"generation\":2", "\"decision\":\"accepted\""}),
        "supersession scenario should accept generation two");
}

void test_late_completion()
{
  auto gate = std::make_shared<completion_gate>();
  gate->ignore_cancel = true;
  scenario_rig rig(kCancelTree, {gate}, "humanoid-late-completion");

  check(rig.tick() == bt::status::running, "late-completion scenario should submit a request");
  const std::uint64_t job_id = rig.only_job_id();
  adopt_running_invocation(rig, job_id);
  gate->wait_for_start();

  rig.put("cancel-now", bt::bb_value{true});
  check(rig.tick() == bt::status::success, "explicit cancellation branch should complete safely");
  check(rig.invocation(job_id).authority_state == bt::vla_authority_state::revoked,
        "explicit cancellation should revoke invocation authority");
  gate->release();
  gate->wait_for_finish();
  wait_for_completion_drop(rig);

  check(rig.instance().bb.get("approach-action") == nullptr,
        "completion after cancellation must not write an action");
  check(rig.dispatcher().calls == 0, "completion after cancellation must not dispatch walking");
  check(rig.host().vla_ref().poll(job_id).status == bt::vla_job_status::cancelled,
        "late successful backend output should resolve as cancelled");
}

void test_duplicate_completion()
{
  auto gate = std::make_shared<completion_gate>();
  scenario_rig rig(kBaseTree, {gate}, "humanoid-duplicate-completion");

  check(rig.tick() == bt::status::running, "duplicate-completion scenario should submit a request");
  const std::uint64_t job_id = rig.only_job_id();
  adopt_running_invocation(rig, job_id);
  gate->wait_for_start();
  gate->release();
  wait_for_authority(rig, job_id, bt::vla_authority_state::accepted);

  const bt::bb_entry* action = rig.instance().bb.get("approach-action");
  check(action != nullptr, "first completion should write the action");
  const std::uint64_t accepted_write_tick = action->last_write_tick;
  (void)rig.tick();

  check(rig.instance().bb.get("approach-action")->last_write_tick == accepted_write_tick,
        "duplicate terminal polling must not write the action twice");
  check(event_has(rig.host().events(), "vla_result",
                  {"\"job_id\":\"" + std::to_string(job_id) + "\"", "\"decision\":\"rejected\"",
                   "\"reason\":\"duplicate_terminal_result\""}),
        "duplicate completion should emit a deterministic rejection reason");
}

void test_branch_halt()
{
  auto gate = std::make_shared<completion_gate>();
  scenario_rig rig(kBaseTree, {gate}, "humanoid-branch-halt");

  check(rig.tick() == bt::status::running, "branch-halt scenario should submit a request");
  const std::uint64_t job_id = rig.only_job_id();
  adopt_running_invocation(rig, job_id);
  gate->wait_for_start();
  rig.put("approach-action", bt::bb_value{std::vector<double>{0.75, 0.75, 0.75}});

  const bt::node_id authority_node = rig.invocation(job_id).authority_node;
  bt::services services = rig.services();
  bt::halt_subtree(rig.instance(), rig.host().callbacks(), services, authority_node,
                   "deterministic branch halt");

  check(rig.invocation(job_id).authority_state == bt::vla_authority_state::revoked,
        "branch halt should revoke the running invocation");
  check(rig.invocation(job_id).authority_reason == "branch_revoked",
        "branch halt should use branch_revoked");
  const bt::bb_entry* job = rig.instance().bb.get("approach-job");
  const bt::bb_entry* action = rig.instance().bb.get("approach-action");
  const bt::bb_entry* meta = rig.instance().bb.get("approach-meta");
  check(job && action && meta && std::holds_alternative<std::monostate>(job->value) &&
            std::holds_alternative<std::monostate>(action->value) &&
            std::holds_alternative<std::monostate>(meta->value),
        "branch halt should clear all invocation-owned blackboard keys");
  check(event_has(rig.host().events(), "async_authority_revoked",
                  {"\"reason\":\"branch_revoked\"", "\"detail\":\"deterministic branch halt\""}),
        "branch halt should emit canonical revocation evidence");
  gate->wait_for_finish();
}

void test_re_entry()
{
  auto first = std::make_shared<completion_gate>();
  auto second = std::make_shared<completion_gate>();
  second->action_value = 0.35;
  scenario_rig rig(kBaseTree, {first, second}, "humanoid-re-entry");

  check(rig.tick() == bt::status::running, "re-entry scenario should submit generation one");
  const std::uint64_t first_job = rig.only_job_id();
  adopt_running_invocation(rig, first_job);
  first->wait_for_start();

  const bt::node_id authority_node = rig.invocation(first_job).authority_node;
  bt::services services = rig.services();
  bt::halt_subtree(rig.instance(), rig.host().callbacks(), services, authority_node,
                   "leave approach branch");
  first->wait_for_finish();

  rig.set_ball_context("ball-B");
  check(rig.tick() == bt::status::running, "re-entry should submit a fresh invocation");
  const std::uint64_t second_job = rig.only_job_id();
  check(second_job != first_job, "re-entry should use a fresh backend job");
  check(rig.invocation(second_job).generation == 2,
        "re-entry should advance the job-key generation");
  check(rig.invocation(second_job).captured_context_id == "ball-B",
        "re-entry should capture the new ball context");

  adopt_running_invocation(rig, second_job);
  second->wait_for_start();
  second->release();
  wait_for_authority(rig, second_job, bt::vla_authority_state::accepted);
  const bt::walking_target_dispatch_result dispatch = rig.host().dispatch_walking_target(
      rig.instance().instance_handle, second_job, 901, accepted_target(0.35));
  check(dispatch.accepted && rig.dispatcher().last_context.generation == 2 &&
            rig.dispatcher().last_context.current_context_id == "ball-B",
        "re-entry should dispatch only the fresh generation and context");
}

void test_emergency_interruption()
{
  auto gate = std::make_shared<completion_gate>();
  gate->ignore_cancel = true;
  scenario_rig rig(kEmergencyTree, {gate}, "humanoid-emergency-interruption");
  rig.put("emergency", bt::bb_value{false});

  check(rig.tick() == bt::status::running, "emergency scenario should submit a request");
  const std::uint64_t job_id = rig.only_job_id();
  adopt_running_invocation(rig, job_id);
  gate->wait_for_start();

  rig.put("emergency", bt::bb_value{true});
  check(rig.tick() == bt::status::success, "emergency branch should interrupt immediately");
  check(rig.invocation(job_id).authority_state == bt::vla_authority_state::revoked,
        "emergency interruption should revoke model authority");
  check(rig.instance().active_vla_jobs.empty(),
        "emergency interruption should remove active VLA tracking");
  check(rig.instance().bb.get("approach-action") == nullptr,
        "emergency interruption must not create a walking target");

  gate->release();
  gate->wait_for_finish();
  wait_for_completion_drop(rig);
  const bt::walking_target_dispatch_result dispatch = rig.host().dispatch_walking_target(
      rig.instance().instance_handle, job_id, 902, accepted_target(0.25));
  check(!dispatch.accepted && dispatch.reason == "branch_revoked" && rig.dispatcher().calls == 0,
        "old emergency-interrupted result must not reach the walking controller");
  check(event_has(rig.host().events(), "async_authority_revoked",
                  {"\"reason\":\"branch_revoked\"",
                   "\"detail\":\"reactive-sel switched to higher priority\""}),
        "emergency interruption should emit pre-emption evidence");
  check(event_has(rig.host().events(), "walking_target_dispatch",
                  {"\"decision\":\"rejected\"", "\"reason\":\"branch_revoked\""}),
        "emergency interruption should emit rejected dispatch evidence");
}

using test_fn = void (*)();

const std::vector<std::pair<std::string_view, test_fn>> kTests = {
    {"normal_acceptance", test_normal_acceptance},
    {"moved_ball", test_moved_ball},
    {"supersession", test_supersession},
    {"late_completion", test_late_completion},
    {"duplicate_completion", test_duplicate_completion},
    {"branch_halt", test_branch_halt},
    {"re_entry", test_re_entry},
    {"emergency_interruption", test_emergency_interruption},
};

int run_test(std::string_view requested)
{
  for (const auto& [name, fn] : kTests)
  {
    if (!requested.empty() && requested != name)
    {
      continue;
    }
    try
    {
      fn();
      std::cout << "[PASS] " << name << '\n';
    }
    catch (const std::exception& error)
    {
      std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
      return 1;
    }
    if (!requested.empty())
    {
      return 0;
    }
  }
  if (!requested.empty())
  {
    std::cerr << "unknown scenario: " << requested << '\n';
    return 2;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv)
{
  if (argc > 2)
  {
    std::cerr << "usage: muesli_bt_humanoid_vla_scenario_tests [scenario]\n";
    return 2;
  }
  return run_test(argc == 2 ? std::string_view(argv[1]) : std::string_view{});
}
