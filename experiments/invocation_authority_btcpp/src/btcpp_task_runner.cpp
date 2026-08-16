#include "btcpp_task_runner.hpp"

#include "bt/event_log.hpp"
#include "muesli_bt/contract/events.hpp"

#include <behaviortree_cpp/action_node.h>
#include <behaviortree_cpp/bt_factory.h>
#include <behaviortree_cpp/tree_node.h>

#include <chrono>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

constexpr std::string_view kTreeXml = R"xml(
<root BTCPP_format="4" main_tree_to_execute="MainTree">
  <BehaviorTree ID="MainTree">
    <ReactiveFallback name="authority-root">
      <ReactiveSequence name="emergency-branch">
        <EmergencyActive/>
        <SafeStand/>
      </ReactiveSequence>
      <ReactiveSequence name="model-branch">
        <ModelBranchActive/>
        <ModelStep/>
        <DispatchStep/>
      </ReactiveSequence>
      <SafeWait/>
    </ReactiveFallback>
  </BehaviorTree>
</root>
)xml";

std::string_view status_name(BT::NodeStatus status) noexcept
{
  switch (status)
  {
    case BT::NodeStatus::IDLE:
      return "idle";
    case BT::NodeStatus::RUNNING:
      return "running";
    case BT::NodeStatus::SUCCESS:
      return "success";
    case BT::NodeStatus::FAILURE:
      return "failure";
    case BT::NodeStatus::SKIPPED:
      return "skipped";
  }
  return "failure";
}

} // namespace

class btcpp_node_callbacks
{
public:
  virtual ~btcpp_node_callbacks() = default;
  virtual BT::NodeStatus model_step() = 0;
  virtual void halt_model() = 0;
  virtual BT::NodeStatus dispatch_start() = 0;
  virtual BT::NodeStatus dispatch_finish() = 0;
  virtual BT::NodeStatus running_branch(task_branch branch) = 0;
};

class model_step_node final : public BT::StatefulActionNode
{
public:
  model_step_node(const std::string& name, const BT::NodeConfig& config,
                  btcpp_node_callbacks* runner)
      : BT::StatefulActionNode(name, config), runner_(*runner)
  {
  }

  static BT::PortsList providedPorts() { return {}; }

  BT::NodeStatus onStart() override;
  BT::NodeStatus onRunning() override;
  void onHalted() override;

private:
  btcpp_node_callbacks& runner_;
};

class dispatch_step_node final : public BT::StatefulActionNode
{
public:
  dispatch_step_node(const std::string& name, const BT::NodeConfig& config,
                     btcpp_node_callbacks* runner)
      : BT::StatefulActionNode(name, config), runner_(*runner)
  {
  }

  static BT::PortsList providedPorts() { return {}; }

  BT::NodeStatus onStart() override;
  BT::NodeStatus onRunning() override;
  void onHalted() override {}

private:
  btcpp_node_callbacks& runner_;
};

class running_branch_node final : public BT::StatefulActionNode
{
public:
  running_branch_node(const std::string& name, const BT::NodeConfig& config,
                      btcpp_node_callbacks* runner, task_branch branch)
      : BT::StatefulActionNode(name, config), runner_(*runner), branch_(branch)
  {
  }

  static BT::PortsList providedPorts() { return {}; }

  BT::NodeStatus onStart() override { return runner_.running_branch(branch_); }
  BT::NodeStatus onRunning() override { return runner_.running_branch(branch_); }
  void onHalted() override {}

private:
  btcpp_node_callbacks& runner_;
  task_branch branch_;
};

class btcpp_task_runner::implementation : public btcpp_node_callbacks
{
public:
  implementation(deterministic_coordinator& coordinator, effect_recorder& recorder,
                 std::unique_ptr<authority_variant> variant, logical_time request_deadline)
      : coordinator_(coordinator), recorder_(recorder), variant_(std::move(variant)),
        request_deadline_(request_deadline)
  {
    if (!variant_)
    {
      throw std::invalid_argument("BehaviorTree.CPP task runner requires an authority variant");
    }
    if (request_deadline_.count() <= 0)
    {
      throw std::invalid_argument("BehaviorTree.CPP task runner deadline must be positive");
    }
    events_.set_run_id("btcpp-controlled-task");
    events_.set_deterministic_time(0, 1);
    events_.set_host_info("BehaviorTree.CPP", "4.9.0", "controlled-authority");
    events_.set_tick_hz(50.0);
    events_.ensure_run_started(bt::event_log::hash64_hex(kTreeXml));
    emit_tree_definition();
    register_nodes();
    factory_.registerBehaviorTreeFromText(std::string(kTreeXml));
    rebuild_tree();
  }

  ~implementation()
  {
    if (tree_)
    {
      tree_->haltTree();
    }
  }

  void request_submission(std::size_t count)
  {
    if (count == 0)
    {
      throw std::invalid_argument("BehaviorTree.CPP task runner submission count must be positive");
    }
    pending_submissions_ += count;
  }

  variant_update cancel_request(std::uint64_t request_id)
  {
    variant_->synchronise(coordinator_.task_state());
    return variant_->cancel(request_id, coordinator_.now());
  }

  BT::NodeStatus tick()
  {
    variant_->synchronise(coordinator_.task_state());
    ++tick_index_;
    std::ostringstream begin;
    begin << "{\"runtime\":\"behaviortree-cpp\",\"variant_id\":\""
          << bt::event_log::json_escape(variant_->descriptor().variant_id) << "\"}";
    (void)events_.emit(muesli_bt::contract::kEventTickBegin, tick_index_, begin.str());
    const auto started = std::chrono::steady_clock::now();
    const BT::NodeStatus result = tree_->tickExactlyOnce();
    const double elapsed_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();
    std::ostringstream end;
    end << "{\"root_status\":\"" << status_name(result)
        << "\",\"tick_ms\":" << elapsed_ms << ",\"tick_budget_ms\":20.0}";
    (void)events_.emit(muesli_bt::contract::kEventTickEnd, tick_index_, end.str());
    return result;
  }

  void reset()
  {
    tree_->haltTree();
    variant_->synchronise(coordinator_.task_state());
    variant_->reset(coordinator_.now());
    pending_submissions_ = 0;
    model_ready_ = false;
    last_failure_reason_.clear();
    active_recorded_branch_ = task_branch::model;
    has_recorded_branch_ = false;
    rebuild_tree();
  }

  BT::NodeStatus model_step() override
  {
    variant_->synchronise(coordinator_.task_state());
    record_branch(task_branch::model);
    if (model_ready_)
    {
      return BT::NodeStatus::SUCCESS;
    }

    variant_update update = variant_->poll(coordinator_.now());
    while (pending_submissions_ > 0)
    {
      --pending_submissions_;
      const request_record request = coordinator_.submit_request(request_deadline_);
      submitted_requests_.push_back(request);
      variant_->synchronise(coordinator_.task_state());
      merge(update, variant_->submit(request));
    }
    if (update.commits > 0)
    {
      model_ready_ = true;
      return BT::NodeStatus::SUCCESS;
    }
    if (update.rejections > 0 && variant_->active_jobs() == 0)
    {
      last_failure_reason_ = update.last_reason;
      return BT::NodeStatus::FAILURE;
    }
    return BT::NodeStatus::RUNNING;
  }

  void halt_model() override
  {
    variant_->synchronise(coordinator_.task_state());
    variant_->halt(coordinator_.now(), "branch_revoked");
    model_ready_ = false;
  }

  BT::NodeStatus dispatch_start() override
  {
    record_branch(task_branch::model);
    return BT::NodeStatus::RUNNING;
  }

  BT::NodeStatus dispatch_finish() override
  {
    variant_->synchronise(coordinator_.task_state());
    const bool dispatched = variant_->dispatch(coordinator_.now());
    model_ready_ = false;
    if (!dispatched)
    {
      last_failure_reason_ = "dispatch_rejected";
    }
    return dispatched ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
  }

  BT::NodeStatus running_branch(task_branch branch) override
  {
    record_branch(branch);
    return BT::NodeStatus::RUNNING;
  }

  [[nodiscard]] const authority_variant& variant() const noexcept { return *variant_; }
  [[nodiscard]] authority_variant& variant() noexcept { return *variant_; }

  [[nodiscard]] const std::vector<request_record>& submitted_requests() const noexcept
  {
    return submitted_requests_;
  }

  [[nodiscard]] std::vector<std::string> task_events() const { return events_.snapshot(); }
  [[nodiscard]] std::vector<std::string> variant_events() const
  {
    return variant_->canonical_events();
  }

private:
  static void merge(variant_update& target, const variant_update& source)
  {
    target.provider_completions += source.provider_completions;
    target.commits += source.commits;
    target.rejections += source.rejections;
    if (!source.last_reason.empty())
    {
      target.last_reason = source.last_reason;
    }
  }

  void emit_tree_definition()
  {
    const std::string tree_hash = bt::event_log::hash64_hex(kTreeXml);
    std::ostringstream data;
    data << "{\"tree_name\":\"MainTree\",\"dsl\":\""
         << bt::event_log::json_escape(kTreeXml) << "\",\"tree_hash\":\"" << tree_hash
         << "\",\"nodes\":["
         << "{\"id\":0,\"kind\":\"reactive_fallback\",\"name\":\"authority-root\"},"
         << "{\"id\":1,\"kind\":\"sequence\",\"name\":\"emergency-branch\"},"
         << "{\"id\":2,\"kind\":\"sequence\",\"name\":\"model-branch\"},"
         << "{\"id\":3,\"kind\":\"action\",\"name\":\"ModelStep\"},"
         << "{\"id\":4,\"kind\":\"action\",\"name\":\"DispatchStep\"}],"
         << "\"edges\":["
         << "{\"parent\":0,\"child\":1,\"index\":0},"
         << "{\"parent\":0,\"child\":2,\"index\":1},"
         << "{\"parent\":2,\"child\":3,\"index\":1},"
         << "{\"parent\":2,\"child\":4,\"index\":2}]}";
    (void)events_.emit(muesli_bt::contract::kEventBtDef, std::nullopt, data.str());
  }

  void register_nodes()
  {
    factory_.registerSimpleCondition(
        "EmergencyActive", [this](BT::TreeNode&)
        {
          return coordinator_.task_state().emergency ? BT::NodeStatus::SUCCESS
                                                     : BT::NodeStatus::FAILURE;
        });
    factory_.registerSimpleCondition(
        "ModelBranchActive", [this](BT::TreeNode&)
        {
          const task_snapshot task = coordinator_.task_state();
          return task.model_branch_active && !task.emergency ? BT::NodeStatus::SUCCESS
                                                             : BT::NodeStatus::FAILURE;
        });
    factory_.registerBuilder<running_branch_node>(
        "SafeStand", BT::CreateBuilder<running_branch_node>(
                         static_cast<btcpp_node_callbacks*>(this), task_branch::safe_stand));
    factory_.registerBuilder<running_branch_node>(
        "SafeWait", BT::CreateBuilder<running_branch_node>(
                        static_cast<btcpp_node_callbacks*>(this), task_branch::fallback));
    factory_.registerBuilder<model_step_node>(
        "ModelStep", BT::CreateBuilder<model_step_node>(static_cast<btcpp_node_callbacks*>(this)));
    factory_.registerBuilder<dispatch_step_node>(
        "DispatchStep",
        BT::CreateBuilder<dispatch_step_node>(static_cast<btcpp_node_callbacks*>(this)));
  }

  void record_branch(task_branch branch)
  {
    if (has_recorded_branch_ && active_recorded_branch_ == branch)
    {
      return;
    }
    active_recorded_branch_ = branch;
    has_recorded_branch_ = true;
    if (branch == task_branch::safe_stand)
    {
      recorder_.record_safe_stand(variant_->descriptor().variant_id, coordinator_.now(),
                                  "emergency_activated");
    }
    else if (branch == task_branch::fallback)
    {
      const std::string reason =
          last_failure_reason_.empty() ? "model_branch_unavailable" : last_failure_reason_;
      recorder_.record_fallback(variant_->descriptor().variant_id, coordinator_.now(), reason);
    }
  }

  void rebuild_tree()
  {
    tree_.emplace(factory_.createTree("MainTree"));
  }

  deterministic_coordinator& coordinator_;
  effect_recorder& recorder_;
  std::unique_ptr<authority_variant> variant_;
  logical_time request_deadline_;
  BT::BehaviorTreeFactory factory_;
  std::optional<BT::Tree> tree_;
  bt::event_log events_;
  std::uint64_t tick_index_ = 0;
  std::size_t pending_submissions_ = 0;
  bool model_ready_ = false;
  std::string last_failure_reason_;
  std::vector<request_record> submitted_requests_;
  task_branch active_recorded_branch_ = task_branch::model;
  bool has_recorded_branch_ = false;
};

BT::NodeStatus model_step_node::onStart()
{
  return runner_.model_step();
}

BT::NodeStatus model_step_node::onRunning()
{
  return runner_.model_step();
}

void model_step_node::onHalted()
{
  runner_.halt_model();
}

BT::NodeStatus dispatch_step_node::onStart()
{
  return runner_.dispatch_start();
}

BT::NodeStatus dispatch_step_node::onRunning()
{
  return runner_.dispatch_finish();
}

btcpp_task_runner::btcpp_task_runner(deterministic_coordinator& coordinator,
                                     effect_recorder& recorder,
                                     std::unique_ptr<authority_variant> variant,
                                     logical_time request_deadline)
    : implementation_(std::make_unique<implementation>(coordinator, recorder, std::move(variant),
                                                       request_deadline))
{
}

btcpp_task_runner::~btcpp_task_runner() = default;

void btcpp_task_runner::request_submission(std::size_t count)
{
  implementation_->request_submission(count);
}

variant_update btcpp_task_runner::cancel_request(std::uint64_t request_id)
{
  return implementation_->cancel_request(request_id);
}

BT::NodeStatus btcpp_task_runner::tick()
{
  return implementation_->tick();
}

void btcpp_task_runner::reset()
{
  implementation_->reset();
}

const authority_variant& btcpp_task_runner::variant() const noexcept
{
  return implementation_->variant();
}

authority_variant& btcpp_task_runner::variant() noexcept
{
  return implementation_->variant();
}

const std::vector<request_record>& btcpp_task_runner::submitted_requests() const noexcept
{
  return implementation_->submitted_requests();
}

std::vector<std::string> btcpp_task_runner::task_events() const
{
  return implementation_->task_events();
}

std::vector<std::string> btcpp_task_runner::variant_events() const
{
  return implementation_->variant_events();
}

} // namespace muesli_bt::experiments::controlled_authority
