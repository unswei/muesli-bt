#include "runtime_variant.hpp"

#include "bt/approach_pose_validator.hpp"
#include "bt/compiler.hpp"
#include "bt/runtime_host.hpp"
#include "bt/walking_target_dispatch.hpp"
#include "muslisp/reader.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

const variant_descriptor kInvocationScopedDescriptor{
    .variant_id = "authority-b3-invocation-scoped",
    .short_label = "B3",
    .reader_label = "invocation-scoped authority",
    .blocking = false,
};

class callback_clock final : public bt::clock_interface
{
public:
  explicit callback_clock(logical_now now) : now_(std::move(now)) {}

  [[nodiscard]] std::chrono::steady_clock::time_point now() const override
  {
    return std::chrono::steady_clock::time_point(now_());
  }

private:
  logical_now now_;
};

class accepting_walking_dispatcher final : public bt::walking_target_dispatcher
{
public:
  bt::walking_target_dispatch_result dispatch(const bt::walking_target_dispatch_context&,
                                               const bt::walking_target&) override
  {
    return {.accepted = true, .reason = {}};
  }
};

struct bridge_completion
{
  provider_result result;
  logical_time completed_at{};
};

class provider_backend final : public bt::vla_backend
{
public:
  provider_backend(std::shared_ptr<proposal_provider> provider, logical_now now)
      : provider_(std::move(provider)), now_(std::move(now))
  {
    if (!provider_ || !now_)
    {
      throw std::invalid_argument("production VLA bridge requires provider and clock");
    }
  }

  void bind(const request_record& request)
  {
    std::lock_guard lock(mutex_);
    requests_.insert_or_assign(request.request_id, request);
  }

  [[nodiscard]] std::optional<bridge_completion> completion(std::uint64_t request_id) const
  {
    std::lock_guard lock(mutex_);
    const auto found = completions_.find(request_id);
    return found == completions_.end() ? std::nullopt
                                       : std::optional<bridge_completion>{found->second};
  }

  bt::vla_response infer(const bt::vla_request& runtime_request,
                         std::function<bool(const bt::vla_partial&)>,
                         std::atomic<bool>& cancel_flag) override
  {
    std::uint64_t request_id = 0;
    try
    {
      request_id = std::stoull(runtime_request.task_id);
    }
    catch (...)
    {
      return failure(runtime_request, "missing controlled request binding");
    }

    request_record request;
    {
      std::lock_guard lock(mutex_);
      const auto found = requests_.find(request_id);
      if (found == requests_.end())
      {
        return failure(runtime_request, "unknown controlled request binding");
      }
      request = found->second;
    }

    std::jthread cancellation_watcher(
        [provider = provider_, request, &cancel_flag](std::stop_token stop)
        {
          while (!stop.stop_requested() && !cancel_flag.load())
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          if (cancel_flag.load())
          {
            try
            {
              (void)provider->cancel(request);
            }
            catch (...)
            {
              // Provider cancellation is best effort and must not terminate the worker.
            }
          }
        });

    provider_result result;
    try
    {
      result = provider_->infer(request);
    }
    catch (const std::exception& error)
    {
      result.status = provider_status::failed;
      result.reason = error.what();
    }
    catch (...)
    {
      result.status = provider_status::failed;
      result.reason = "backend_terminal_failure";
    }
    cancellation_watcher.request_stop();

    {
      std::lock_guard lock(mutex_);
      completions_.insert_or_assign(
          request_id, bridge_completion{.result = result, .completed_at = now_()});
    }
    return translate(runtime_request, result);
  }

private:
  static bt::vla_response failure(const bt::vla_request& request, std::string reason)
  {
    bt::vla_response response;
    response.status = bt::vla_status::error;
    response.model = request.model;
    response.explanation = std::move(reason);
    return response;
  }

  static bt::vla_response translate(const bt::vla_request& request,
                                    const provider_result& result)
  {
    if (result.status != provider_status::ok)
    {
      return failure(request, result.reason.empty() ? "backend_terminal_failure" : result.reason);
    }

    bt::vla_response response;
    response.status = bt::vla_status::ok;
    response.model = request.model;
    response.action.type = bt::vla_action_type::continuous;
    response.action.frame_id = result.proposal.frame_id;
    response.confidence = 1.0;
    response.explanation = result.proposal.response_id;

    const bool finite = std::all_of(result.proposal.pose.begin(), result.proposal.pose.end(),
                                    [](double value) { return std::isfinite(value); });
    if (!result.proposal.schema_valid || result.proposal.response_id.empty() || !finite)
    {
      response.action.u = {result.proposal.pose[0], result.proposal.pose[1]};
    }
    else
    {
      response.action.u.assign(result.proposal.pose.begin(), result.proposal.pose.end());
    }
    return response;
  }

  std::shared_ptr<proposal_provider> provider_;
  logical_now now_;
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, request_record> requests_;
  std::unordered_map<std::uint64_t, bridge_completion> completions_;
};

std::string lisp_string(std::string_view value)
{
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value)
  {
    if (character == '\\' || character == '"')
    {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  return escaped;
}

std::string runtime_tree(logical_time deadline, std::string_view action_frame)
{
  std::ostringstream source;
  source << "(reactive-sel "
            "  (seq (cond bb-truthy controlled-revoke-now) (succeed)) "
            "  (seq (vla-wait :name \"controlled-approach\" :job_key controlled-job "
            "                 :action_key controlled-action :meta_key controlled-meta "
            "                 :clear_job #f) "
            "       (succeed)) "
            "  (vla-request :name \"controlled-approach\" :job_key controlled-job "
            "               :instruction \"choose an approach pose\" :state_key controlled-state "
            "               :task_key controlled-task-id :model_name \"controlled-provider\" "
            "               :deadline_ms "
         << deadline.count()
         << " :dims 3 :bound_lo -1000000 :bound_hi 1000000 "
            "               :max_abs 1000000 :max_delta 1000000 "
            "               :action_frame \""
         << lisp_string(action_frame)
         << "\" :acceptance_policy invocation_scoped "
            "               :context_key controlled-context))";
  return source.str();
}

struct invocation_binding
{
  request_record request;
  bool provider_recorded = false;
  bool terminal_recorded = false;
};

}  // namespace

class invocation_scoped_variant::implementation
{
public:
  implementation(std::shared_ptr<proposal_provider> provider, effect_recorder& recorder,
                 logical_now now, logical_time request_deadline,
                 proposal_validation_config validation)
      : recorder_(recorder), now_(std::move(now)), clock_(now_),
        backend_(std::make_shared<provider_backend>(std::move(provider), now_)),
        validator_(bt::approach_pose_validator_config{
                       .frame_id = validation.frame_id,
                       .bounds = {.min_x_m = validation.minimum[0],
                                  .max_x_m = validation.maximum[0],
                                  .min_y_m = validation.minimum[1],
                                  .max_y_m = validation.maximum[1],
                                  .min_yaw_rad = validation.minimum[2],
                                  .max_yaw_rad = validation.maximum[2]}},
                   [this] { return host_state_; })
  {
    if (!now_ || !backend_)
    {
      throw std::invalid_argument("invocation-scoped variant requires provider and clock");
    }
    if (request_deadline.count() <= 0)
    {
      throw std::invalid_argument("invocation-scoped variant deadline must be positive");
    }

    host_.enable_deterministic_test_mode(0x4233494e564f4b45ull,
                                         "controlled-authority-b3");
    host_.set_clock_interface(&clock_);
    host_.set_vla_commit_validator(&validator_);
    host_.set_walking_target_dispatcher(&dispatcher_);
    host_.vla_ref().set_cache_ttl_ms(0);
    host_.vla_ref().register_backend("controlled-provider", backend_);
    const std::int64_t definition = host_.store_definition(
        bt::compile_definition(
            muslisp::read_one(runtime_tree(request_deadline, validation.frame_id))));
    instance_handle_ = host_.create_instance(definition);
    instance_ = host_.find_instance(instance_handle_);
    if (!instance_)
    {
      throw std::runtime_error("invocation-scoped runtime instance was not created");
    }
    put("controlled-state", bt::bb_value{std::vector<double>{0.0, 0.0, 0.0}});
    put("controlled-revoke-now", bt::bb_value{false});
  }

  ~implementation()
  {
    try
    {
      if (instance_)
      {
        put("controlled-revoke-now", bt::bb_value{true});
        (void)host_.tick_instance(instance_handle_);
      }
    }
    catch (...)
    {
      // Destruction still disconnects the non-owning runtime hooks below.
    }
    host_.set_walking_target_dispatcher(nullptr);
    host_.set_vla_commit_validator(nullptr);
    host_.set_clock_interface(nullptr);
  }

  [[nodiscard]] const variant_descriptor& descriptor() const noexcept
  {
    return kInvocationScopedDescriptor;
  }

  variant_update submit(const request_record& request)
  {
    variant_update update = collect_transitions(now_());
    backend_->bind(request);
    recorder_.record_request(kInvocationScopedDescriptor.variant_id, request);

    std::vector<std::uint64_t> prior_active;
    for (const auto& [job_id, invocation] : instance_->vla_invocations)
    {
      if (invocation.authority_state == bt::vla_authority_state::active)
      {
        prior_active.push_back(job_id);
      }
    }

    put("controlled-revoke-now", bt::bb_value{false});
    put("controlled-task-id", bt::bb_value{static_cast<std::int64_t>(request.request_id)});
    put("controlled-context", bt::bb_value{request.captured_context_id});
    put("controlled-job", bt::bb_value{std::monostate{}});
    const bt::status status = host_.tick_instance(instance_handle_);
    if (status == bt::status::failure || instance_->vla_invocations.empty())
    {
      recorder_.record_rejection(kInvocationScopedDescriptor.variant_id, request, {},
                                 "backend_terminal_failure", now_());
      ++update.rejections;
      update.last_reason = "backend_terminal_failure";
      return update;
    }

    for (const std::uint64_t old_job : prior_active)
    {
      const auto binding = bindings_.find(old_job);
      if (binding != bindings_.end() && !binding->second.terminal_recorded)
      {
        recorder_.record_rejection(kInvocationScopedDescriptor.variant_id,
                                   binding->second.request, {}, "superseded", now_());
        binding->second.terminal_recorded = true;
        ++update.rejections;
        update.last_reason = "superseded";
      }
    }

    const auto newest = std::max_element(
        instance_->vla_invocations.begin(), instance_->vla_invocations.end(),
        [](const auto& left, const auto& right)
        { return left.second.generation < right.second.generation; });
    bindings_.insert_or_assign(newest->first, invocation_binding{.request = request});
    return update;
  }

  variant_update poll(logical_time admission_at)
  {
    (void)host_.tick_instance(instance_handle_);
    return collect_transitions(admission_at);
  }

  bool dispatch(logical_time dispatch_at)
  {
    if (!accepted_job_)
    {
      return false;
    }
    const std::uint64_t job_id = *accepted_job_;
    const auto invocation = instance_->vla_invocations.find(job_id);
    const auto binding = bindings_.find(job_id);
    if (invocation == instance_->vla_invocations.end() || binding == bindings_.end() ||
        invocation->second.accepted_action.size() != 3)
    {
      accepted_job_.reset();
      return false;
    }

    const std::optional<bridge_completion> completion =
        backend_->completion(binding->second.request.request_id);
    const std::string response_id = completion ? completion->result.proposal.response_id : "";
    const bt::walking_target target{
        .frame_id = invocation->second.action_frame,
        .x_m = invocation->second.accepted_action[0],
        .y_m = invocation->second.accepted_action[1],
        .yaw_rad = invocation->second.accepted_action[2],
    };
    const bt::walking_target_dispatch_result result =
        host_.dispatch_walking_target(instance_handle_, job_id, 900, target);
    accepted_job_.reset();
    if (result.accepted)
    {
      recorder_.record_dispatch(kInvocationScopedDescriptor.variant_id, binding->second.request,
                                response_id, dispatch_at);
      return true;
    }
    recorder_.record_dispatch_rejection(kInvocationScopedDescriptor.variant_id,
                                        binding->second.request, response_id, result.reason,
                                        dispatch_at);
    return false;
  }

  void synchronise(const task_snapshot& task)
  {
    task_ = task;
    host_state_.ball_context_id = task.context_id;
    host_state_.robot_stable = !task.emergency;
    put("controlled-context", bt::bb_value{task.context_id});
    put("controlled-revoke-now",
        bt::bb_value{task.emergency || !task.model_branch_active});
  }

  void halt(logical_time halted_at, std::string_view)
  {
    put("controlled-revoke-now", bt::bb_value{true});
    (void)host_.tick_instance(instance_handle_);
    (void)collect_transitions(halted_at);
    accepted_job_.reset();
  }

  void reset(logical_time reset_at)
  {
    halt(reset_at, "runtime_reset");
    host_.reset_instance(instance_handle_);
    put("controlled-state", bt::bb_value{std::vector<double>{0.0, 0.0, 0.0}});
    put("controlled-context", bt::bb_value{task_.context_id});
    put("controlled-revoke-now", bt::bb_value{true});
  }

  [[nodiscard]] std::vector<std::string> canonical_events() const
  {
    return host_.events().snapshot();
  }

  [[nodiscard]] std::size_t active_jobs() const
  {
    return static_cast<std::size_t>(std::count_if(
        instance_->vla_invocations.begin(), instance_->vla_invocations.end(),
        [](const auto& entry)
        { return entry.second.authority_state == bt::vla_authority_state::active; }));
  }

private:
  void put(std::string key, bt::bb_value value)
  {
    instance_->bb.put(std::move(key), std::move(value), instance_->tick_index, clock_.now(), 0,
                      "controlled-authority-b3-adapter");
  }

  variant_update collect_transitions(logical_time admission_at)
  {
    variant_update update;
    for (auto& [job_id, binding] : bindings_)
    {
      const std::optional<bridge_completion> completion =
          backend_->completion(binding.request.request_id);
      if (completion && !binding.provider_recorded)
      {
        recorder_.record_provider_completion(kInvocationScopedDescriptor.variant_id,
                                             binding.request,
                                             completion->result.proposal.response_id,
                                             completion->completed_at);
        binding.provider_recorded = true;
        ++update.provider_completions;
      }
      if (binding.terminal_recorded)
      {
        continue;
      }
      const auto invocation = instance_->vla_invocations.find(job_id);
      if (invocation == instance_->vla_invocations.end())
      {
        continue;
      }
      const std::string response_id = completion ? completion->result.proposal.response_id : "";
      if (invocation->second.authority_state == bt::vla_authority_state::accepted)
      {
        recorder_.record_commit(kInvocationScopedDescriptor.variant_id, binding.request,
                                response_id, admission_at);
        binding.terminal_recorded = true;
        accepted_job_ = job_id;
        ++update.commits;
      }
      else if (invocation->second.authority_state == bt::vla_authority_state::rejected ||
               invocation->second.authority_state == bt::vla_authority_state::revoked)
      {
        const std::string reason = invocation->second.authority_reason.empty()
                                       ? "branch_revoked"
                                       : invocation->second.authority_reason;
        recorder_.record_rejection(kInvocationScopedDescriptor.variant_id, binding.request,
                                   response_id, reason, admission_at);
        binding.terminal_recorded = true;
        ++update.rejections;
        update.last_reason = reason;
      }
    }
    return update;
  }

  effect_recorder& recorder_;
  logical_now now_;
  callback_clock clock_;
  bt::runtime_host host_;
  std::shared_ptr<provider_backend> backend_;
  bt::approach_pose_host_state host_state_;
  bt::approach_pose_validator validator_;
  accepting_walking_dispatcher dispatcher_;
  std::int64_t instance_handle_ = 0;
  bt::instance* instance_ = nullptr;
  std::unordered_map<std::uint64_t, invocation_binding> bindings_;
  std::optional<std::uint64_t> accepted_job_;
  task_snapshot task_;
};

invocation_scoped_variant::invocation_scoped_variant(
    std::shared_ptr<proposal_provider> provider, effect_recorder& recorder, logical_now now,
    logical_time request_deadline, proposal_validation_config validation)
    : implementation_(std::make_unique<implementation>(std::move(provider), recorder,
                                                       std::move(now), request_deadline,
                                                       std::move(validation)))
{
}

invocation_scoped_variant::~invocation_scoped_variant() = default;

const variant_descriptor& invocation_scoped_variant::descriptor() const noexcept
{
  return implementation_->descriptor();
}

variant_update invocation_scoped_variant::submit(const request_record& request)
{
  return implementation_->submit(request);
}

variant_update invocation_scoped_variant::poll(logical_time admission_at)
{
  return implementation_->poll(admission_at);
}

bool invocation_scoped_variant::dispatch(logical_time dispatch_at)
{
  return implementation_->dispatch(dispatch_at);
}

void invocation_scoped_variant::synchronise(const task_snapshot& task)
{
  implementation_->synchronise(task);
}

void invocation_scoped_variant::halt(logical_time halted_at, std::string_view reason)
{
  implementation_->halt(halted_at, reason);
}

void invocation_scoped_variant::reset(logical_time reset_at)
{
  implementation_->reset(reset_at);
}

std::vector<std::string> invocation_scoped_variant::canonical_events() const
{
  return implementation_->canonical_events();
}

std::size_t invocation_scoped_variant::active_jobs() const
{
  return implementation_->active_jobs();
}

}  // namespace muesli_bt::experiments::controlled_authority
