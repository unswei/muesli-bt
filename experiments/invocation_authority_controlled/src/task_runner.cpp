#include "task_runner.hpp"

#include "bt/compiler.hpp"
#include "bt/runtime.hpp"
#include "bt/runtime_host.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/reader.hpp"
#include "muslisp/value.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

bt::definition compile_common_task(const std::string& source)
{
  std::vector<muslisp::value> expressions = muslisp::read_all(source);
  muslisp::gc_root_scope roots(muslisp::default_gc());
  for (muslisp::value& expression : expressions)
  {
    roots.add(&expression);
  }
  if (expressions.size() != 1 || !muslisp::is_proper_list(expressions.front()))
  {
    throw std::invalid_argument("shared task source must contain exactly one defbt form");
  }
  const std::vector<muslisp::value> form = muslisp::vector_from_list(expressions.front());
  if (form.size() != 3 || !muslisp::is_symbol(form[0]) || muslisp::symbol_name(form[0]) != "defbt")
  {
    throw std::invalid_argument("shared task source must have the form (defbt name tree)");
  }
  return bt::compile_definition(form[2]);
}

class coordinator_clock final : public bt::clock_interface
{
public:
  explicit coordinator_clock(deterministic_coordinator& coordinator) : coordinator_(coordinator) {}

  [[nodiscard]] std::chrono::steady_clock::time_point now() const override
  {
    return std::chrono::steady_clock::time_point(coordinator_.now());
  }

private:
  deterministic_coordinator& coordinator_;
};

} // namespace

class shared_lisp_task_runner::implementation
{
public:
  implementation(deterministic_coordinator& coordinator, effect_recorder& recorder,
                 std::unique_ptr<authority_variant> variant, std::string common_task_source,
                 task_runner_config config)
      : coordinator_(coordinator), recorder_(recorder), variant_(std::move(variant)),
        config_(config), clock_(coordinator)
  {
    if (!variant_)
    {
      throw std::invalid_argument("shared task runner requires an authority variant");
    }
    if (config_.request_deadline.count() <= 0)
    {
      throw std::invalid_argument("shared task runner deadline must be positive");
    }

    host_.enable_deterministic_test_mode(0x43415554484f5249ull, "controlled-authority-task");
    host_.set_clock_interface(&clock_);
    register_callbacks();
    const std::int64_t definition_handle =
        host_.store_definition(compile_common_task(common_task_source));
    instance_handle_ = host_.create_instance(definition_handle);
  }

  ~implementation() { host_.set_clock_interface(nullptr); }

  void request_submission(std::size_t count)
  {
    if (count == 0)
    {
      throw std::invalid_argument("shared task runner submission count must be positive");
    }
    pending_submissions_ += count;
  }

  variant_update pump()
  {
    variant_->synchronise(coordinator_.task_state());
    variant_update update = variant_->poll(coordinator_.now());
    merge_update(pending_update_, update);
    return update;
  }

  variant_update cancel_request(std::uint64_t request_id)
  {
    variant_->synchronise(coordinator_.task_state());
    variant_update update = variant_->cancel(request_id, coordinator_.now());
    merge_update(pending_update_, update);
    return update;
  }

  bt::status tick()
  {
    const task_snapshot task = coordinator_.task_state();
    variant_->synchronise(task);
    if (has_last_task_ && last_task_.model_branch_active && !task.model_branch_active)
    {
      variant_->halt(coordinator_.now(), "branch_revoked");
    }
    const bt::status result = host_.tick_instance(instance_handle_);
    last_task_ = task;
    has_last_task_ = true;
    return result;
  }

  void reset()
  {
    host_.reset_instance(instance_handle_);
    variant_->reset(coordinator_.now());
    pending_submissions_ = 0;
    model_ready_ = false;
    pending_update_ = {};
    last_task_ = coordinator_.task_state();
    has_last_task_ = true;
  }

  [[nodiscard]] const authority_variant& variant() const noexcept { return *variant_; }
  [[nodiscard]] authority_variant& variant() noexcept { return *variant_; }
  [[nodiscard]] const std::vector<request_record>& submitted_requests() const noexcept
  {
    return submitted_requests_;
  }

  [[nodiscard]] std::vector<std::string> task_events() const { return host_.events().snapshot(); }

  [[nodiscard]] std::vector<std::string> variant_events() const
  {
    return variant_->canonical_events();
  }

private:
  static void merge_update(variant_update& target, const variant_update& source)
  {
    target.provider_completions += source.provider_completions;
    target.commits += source.commits;
    target.rejections += source.rejections;
    if (!source.last_reason.empty())
    {
      target.last_reason = source.last_reason;
    }
  }

  void register_callbacks()
  {
    host_.callbacks().register_condition("controlled-emergency?",
                                         [this](bt::tick_context&, std::span<const muslisp::value>)
                                         { return coordinator_.task_state().emergency; });

    host_.callbacks().register_action(
        "controlled-safe-stand",
        [this](bt::tick_context&, bt::node_id, bt::node_memory& memory,
               std::span<const muslisp::value>)
        {
          if (!memory.b0)
          {
            recorder_.record_safe_stand(variant_->descriptor().variant_id, coordinator_.now(),
                                        "emergency_activated");
            memory.b0 = true;
          }
          return bt::status::running;
        });

    host_.callbacks().register_action(
        "controlled-model-step",
        [this](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>)
        {
          const task_snapshot task = coordinator_.task_state();
          variant_->synchronise(task);
          if (!task.model_branch_active || task.emergency)
          {
            return bt::status::failure;
          }
          if (model_ready_)
          {
            return bt::status::success;
          }

          variant_update update = pending_update_;
          pending_update_ = {};
          merge_update(update, variant_->poll(coordinator_.now()));
          while (pending_submissions_ > 0)
          {
            --pending_submissions_;
            const request_record request = coordinator_.submit_request(config_.request_deadline);
            submitted_requests_.push_back(request);
            const variant_update submitted = variant_->submit(request);
            merge_update(update, submitted);
          }

          if (update.commits > 0)
          {
            model_ready_ = true;
            return bt::status::success;
          }
          if (update.rejections > 0 && variant_->active_jobs() == 0)
          {
            last_failure_reason_ = update.last_reason;
            return bt::status::failure;
          }
          return bt::status::running;
        },
        [this](bt::tick_context&, bt::node_id, bt::node_memory&)
        {
          const task_snapshot task = coordinator_.task_state();
          variant_->synchronise(task);
          variant_->halt(coordinator_.now(), "branch_revoked");
          model_ready_ = false;
        });

    host_.callbacks().register_action(
        "controlled-dispatch-step",
        [this](bt::tick_context&, bt::node_id, bt::node_memory& memory,
               std::span<const muslisp::value>)
        {
          if (!memory.b0)
          {
            memory.b0 = true;
            return bt::status::running;
          }
          variant_->synchronise(coordinator_.task_state());
          const bool dispatched = variant_->dispatch(coordinator_.now());
          model_ready_ = false;
          return dispatched ? bt::status::success : bt::status::failure;
        });

    host_.callbacks().register_action(
        "controlled-fallback",
        [this](bt::tick_context&, bt::node_id, bt::node_memory& memory,
               std::span<const muslisp::value>)
        {
          if (!memory.b0)
          {
            const std::string reason =
                last_failure_reason_.empty() ? "model_branch_unavailable" : last_failure_reason_;
            recorder_.record_fallback(variant_->descriptor().variant_id, coordinator_.now(),
                                      reason);
            memory.b0 = true;
          }
          return bt::status::running;
        });
  }

  deterministic_coordinator& coordinator_;
  effect_recorder& recorder_;
  std::unique_ptr<authority_variant> variant_;
  task_runner_config config_;
  coordinator_clock clock_;
  bt::runtime_host host_;
  std::int64_t instance_handle_ = 0;
  std::size_t pending_submissions_ = 0;
  bool model_ready_ = false;
  std::string last_failure_reason_;
  variant_update pending_update_;
  std::vector<request_record> submitted_requests_;
  task_snapshot last_task_;
  bool has_last_task_ = false;
};

shared_lisp_task_runner::shared_lisp_task_runner(deterministic_coordinator& coordinator,
                                                 effect_recorder& recorder,
                                                 std::unique_ptr<authority_variant> variant,
                                                 std::string common_task_source,
                                                 task_runner_config config)
    : implementation_(std::make_unique<implementation>(coordinator, recorder, std::move(variant),
                                                       std::move(common_task_source), config))
{
}

shared_lisp_task_runner::~shared_lisp_task_runner() = default;

void shared_lisp_task_runner::request_submission(std::size_t count)
{
  implementation_->request_submission(count);
}

variant_update shared_lisp_task_runner::pump()
{
  return implementation_->pump();
}

variant_update shared_lisp_task_runner::cancel_request(std::uint64_t request_id)
{
  return implementation_->cancel_request(request_id);
}

bt::status shared_lisp_task_runner::tick()
{
  return implementation_->tick();
}

void shared_lisp_task_runner::reset()
{
  implementation_->reset();
}

const authority_variant& shared_lisp_task_runner::variant() const noexcept
{
  return implementation_->variant();
}

authority_variant& shared_lisp_task_runner::variant() noexcept
{
  return implementation_->variant();
}

const std::vector<request_record>& shared_lisp_task_runner::submitted_requests() const noexcept
{
  return implementation_->submitted_requests();
}

std::vector<std::string> shared_lisp_task_runner::task_events() const
{
  return implementation_->task_events();
}

std::vector<std::string> shared_lisp_task_runner::variant_events() const
{
  return implementation_->variant_events();
}

} // namespace muesli_bt::experiments::controlled_authority
