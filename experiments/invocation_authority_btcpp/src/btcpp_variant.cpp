#include "btcpp_variant.hpp"

#include "bt/event_log.hpp"
#include "muesli_bt/contract/events.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

const variant_descriptor kOrdinaryDescriptor{
    .variant_id = "btcpp-ordinary-asynchronous",
    .short_label = "BTCPP-ordinary",
    .reader_label = "BehaviorTree.CPP documented ordinary asynchronous lifecycle",
    .blocking = false,
};

const variant_descriptor kFullDescriptor{
    .variant_id = "btcpp-invocation-scoped",
    .short_label = "BTCPP-full",
    .reader_label = "BehaviorTree.CPP with the full invocation-scoped contract port",
    .blocking = false,
};

struct completed_call
{
  request_record request;
  provider_result result;
  logical_time completed_at{};
};

struct committed_proposal
{
  request_record request;
  task_proposal proposal;
};

provider_result provider_failure(std::string reason)
{
  return provider_result{
      .status = provider_status::failed,
      .proposal = {},
      .reason = std::move(reason),
  };
}

provider_result infer_safely(const std::shared_ptr<proposal_provider>& provider,
                             const request_record& request)
{
  try
  {
    return provider->infer(request);
  }
  catch (const std::exception& error)
  {
    return provider_failure(error.what());
  }
  catch (...)
  {
    return provider_failure("backend_terminal_failure");
  }
}

bool cancel_safely(const std::shared_ptr<proposal_provider>& provider,
                   const request_record& request) noexcept
{
  try
  {
    return provider->cancel(request);
  }
  catch (...)
  {
    return false;
  }
}

std::int64_t logical_ns(logical_time value) noexcept
{
  return value.count() * 1'000'000;
}

class variant_event_stream
{
public:
  explicit variant_event_stream(std::string run_id)
  {
    events_.set_run_id(std::move(run_id));
    events_.set_deterministic_time(0, 1);
    events_.set_host_info("BehaviorTree.CPP", "4.9.0", "controlled-authority");
    events_.set_tick_hz(50.0);
    events_.ensure_run_started("controlled-authority-btcpp");
  }

  void submit(const request_record& request, std::string_view acceptance_policy)
  {
    std::ostringstream data;
    data << "{\"job_id\":\"" << request.request_id << "\",\"generation\":"
         << request.generation
         << ",\"requesting_node_id\":3,\"authority_node_id\":1"
         << ",\"job_key\":\"approach-job\",\"context_key\":\"ball-context-id\""
         << ",\"captured_context_id\":\""
         << bt::event_log::json_escape(request.captured_context_id) << "\""
         << ",\"submitted_at_ns\":" << logical_ns(request.submitted_at)
         << ",\"deadline_at_ns\":" << logical_ns(request.deadline)
         << ",\"acceptance_policy\":\"" << acceptance_policy
         << "\",\"authority_state\":\"active\"}";
    (void)events_.emit("vla_submit", std::nullopt, data.str());
  }

  void provider_completed(const request_record& request, const provider_result& result)
  {
    std::ostringstream data;
    data << "{\"job_id\":\"" << request.request_id << "\",\"status\":\""
         << (result.status == provider_status::ok ? "done" : "failed")
         << "\",\"response_id\":\""
         << bt::event_log::json_escape(result.proposal.response_id) << "\"}";
    (void)events_.emit("vla_poll", std::nullopt, data.str());
  }

  void result(const request_record& request, const provider_result& provider_result,
              std::string_view current_context_id, bool accepted, std::string_view reason)
  {
    const std::string digest =
        bt::event_log::hash64_hex(provider_result.proposal.response_id + ":" +
                                   provider_result.proposal.frame_id);
    std::ostringstream data;
    data << "{\"job_id\":\"" << request.request_id << "\",\"generation\":"
         << request.generation
         << ",\"requesting_node_id\":3,\"authority_node_id\":1"
         << ",\"job_key\":\"approach-job\",\"captured_context_id\":\""
         << bt::event_log::json_escape(request.captured_context_id)
         << "\",\"current_context_id\":\""
         << bt::event_log::json_escape(current_context_id) << "\",\"authority_state\":\""
         << (accepted ? "accepted" : "rejected") << "\",\"decision\":\""
         << (accepted ? "accepted" : "rejected") << "\",\"reason\":\""
         << bt::event_log::json_escape(reason) << "\",\"digest\":\"" << digest << "\"}";
    (void)events_.emit("vla_result", std::nullopt, data.str());
  }

  void cancellation(const request_record& request, std::string_view reason, bool accepted)
  {
    std::ostringstream requested;
    requested << "{\"job_id\":\"" << request.request_id
              << "\",\"node_id\":3,\"reason\":\""
              << bt::event_log::json_escape(reason) << "\"}";
    (void)events_.emit(muesli_bt::contract::kEventAsyncCancelRequested, std::nullopt,
                       requested.str());
    std::ostringstream acknowledged;
    acknowledged << "{\"job_id\":\"" << request.request_id
                 << "\",\"node_id\":3,\"accepted\":"
                 << (accepted ? "true" : "false") << '}';
    (void)events_.emit(muesli_bt::contract::kEventAsyncCancelAcknowledged, std::nullopt,
                       acknowledged.str());
  }

  void revoked(const request_record& request, std::string_view reason)
  {
    std::ostringstream data;
    data << "{\"job_id\":\"" << request.request_id << "\",\"generation\":"
         << request.generation
         << ",\"requesting_node_id\":3,\"authority_node_id\":1"
         << ",\"job_key\":\"approach-job\",\"captured_context_id\":\""
         << bt::event_log::json_escape(request.captured_context_id)
         << "\",\"authority_state\":\"revoked\",\"reason\":\""
         << bt::event_log::json_escape(reason)
         << "\",\"detail\":\"StatefulActionNode::onHalted\"}";
    (void)events_.emit(muesli_bt::contract::kEventAsyncAuthorityRevoked, std::nullopt,
                       data.str());
  }

  void completion_dropped(const request_record& request, std::string_view reason)
  {
    std::ostringstream data;
    data << "{\"job_id\":\"" << request.request_id
         << "\",\"node_id\":3,\"reason\":\""
         << bt::event_log::json_escape(reason) << "\"}";
    (void)events_.emit(muesli_bt::contract::kEventAsyncCompletionDropped, std::nullopt,
                       data.str());
  }

  void dispatch(const request_record& request, const task_proposal& proposal,
                std::string_view current_context_id, bool accepted, std::string_view reason)
  {
    std::ostringstream target;
    target << "{\"frame_id\":\"" << bt::event_log::json_escape(proposal.frame_id)
           << "\",\"x_m\":" << proposal.pose[0] << ",\"y_m\":" << proposal.pose[1]
           << ",\"yaw_rad\":" << proposal.pose[2] << '}';
    const std::string target_digest = bt::event_log::hash64_hex(target.str());
    std::ostringstream data;
    data << "{\"job_id\":\"" << request.request_id << "\",\"generation\":"
         << request.generation
         << ",\"requesting_node_id\":3,\"authority_node_id\":1,\"dispatching_node_id\":4"
         << ",\"job_key\":\"approach-job\",\"captured_context_id\":\""
         << bt::event_log::json_escape(request.captured_context_id)
         << "\",\"current_context_id\":\""
         << bt::event_log::json_escape(current_context_id) << "\",\"action_frame\":\""
         << bt::event_log::json_escape(proposal.frame_id) << "\",\"authority_state\":\""
         << (accepted ? "accepted" : "rejected") << "\",\"decision\":\""
         << (accepted ? "accepted" : "rejected") << "\",\"reason\":\""
         << bt::event_log::json_escape(reason)
         << "\",\"dispatch_source\":\"runtime_structural\",\"target\":" << target.str()
         << ",\"target_digest\":\"" << target_digest << "\"}";
    (void)events_.emit(muesli_bt::contract::kEventWalkingTargetDispatch, std::nullopt,
                       data.str());
  }

  [[nodiscard]] std::vector<std::string> snapshot() const { return events_.snapshot(); }

private:
  bt::event_log events_;
};

} // namespace

class btcpp_asynchronous_variant::implementation
{
public:
  implementation(std::shared_ptr<proposal_provider> provider, effect_recorder& recorder,
                 logical_now now, proposal_validation_config validation)
      : provider_(std::move(provider)), recorder_(recorder), now_(std::move(now)),
        validation_(std::move(validation)), events_("btcpp-ordinary-authority")
  {
    if (!provider_ || !now_)
    {
      throw std::invalid_argument("BehaviorTree.CPP asynchronous variant requires provider and clock");
    }
  }

  ~implementation()
  {
    halt(now_(), "runner_destroyed");
    std::vector<std::unique_ptr<worker_job>> jobs;
    {
      std::lock_guard lock(jobs_mutex_);
      jobs.swap(jobs_);
    }
  }

  variant_update submit(const request_record& request)
  {
    recorder_.record_request(kOrdinaryDescriptor.variant_id, request);
    events_.submit(request, "ordinary_asynchronous");
    auto job = std::make_unique<worker_job>();
    job->request = request;
    worker_job* const job_ptr = job.get();
    {
      std::lock_guard lock(jobs_mutex_);
      jobs_.push_back(std::move(job));
    }
    job_ptr->thread = std::jthread(
        [this, job_ptr, request]
        {
          provider_result result = infer_safely(provider_, request);
          const logical_time completed_at = now_();
          {
            std::lock_guard lock(completions_mutex_);
            const std::size_t copies = std::max<std::size_t>(1, result.completion_copies);
            for (std::size_t copy = 0; copy < copies; ++copy)
            {
              recorder_.record_provider_completion(kOrdinaryDescriptor.variant_id, request,
                                                   result.proposal.response_id, completed_at);
              events_.provider_completed(request, result);
              completions_.push_back(completed_call{request, result, completed_at});
            }
          }
          job_ptr->finished.store(true);
        });
    return {};
  }

  variant_update poll(logical_time admission_at)
  {
    std::deque<completed_call> completions;
    {
      std::lock_guard lock(completions_mutex_);
      completions.swap(completions_);
    }
    variant_update update;
    while (!completions.empty())
    {
      completed_call completion = std::move(completions.front());
      completions.pop_front();
      ++update.provider_completions;
      const std::optional<std::string> rejection =
          validate_provider_result(completion.result, validation_);
      if (rejection)
      {
        recorder_.record_rejection(kOrdinaryDescriptor.variant_id, completion.request,
                                   completion.result.proposal.response_id, *rejection,
                                   admission_at);
        events_.result(completion.request, completion.result, snapshot().context_id, false,
                       *rejection);
        ++update.rejections;
        update.last_reason = *rejection;
        continue;
      }
      recorder_.record_commit(kOrdinaryDescriptor.variant_id, completion.request,
                              completion.result.proposal.response_id, admission_at);
      events_.result(completion.request, completion.result, snapshot().context_id, true, "");
      {
        std::lock_guard lock(committed_mutex_);
        committed_ = committed_proposal{completion.request, completion.result.proposal};
      }
      ++update.commits;
    }
    reap_finished_jobs();
    return update;
  }

  bool dispatch(logical_time dispatch_at)
  {
    std::optional<committed_proposal> proposal;
    {
      std::lock_guard lock(committed_mutex_);
      proposal = std::move(committed_);
      committed_.reset();
    }
    if (!proposal)
    {
      return false;
    }
    recorder_.record_dispatch(kOrdinaryDescriptor.variant_id, proposal->request,
                              proposal->proposal.response_id, dispatch_at);
    events_.dispatch(proposal->request, proposal->proposal, snapshot().context_id, true, "");
    return true;
  }

  void synchronise(const task_snapshot& task)
  {
    std::lock_guard lock(task_mutex_);
    task_ = task;
  }

  void halt(logical_time halted_at, std::string_view reason)
  {
    std::vector<request_record> requests;
    {
      std::lock_guard lock(jobs_mutex_);
      for (const std::unique_ptr<worker_job>& job : jobs_)
      {
        if (!job->finished.load() && !job->cancellation_requested)
        {
          job->cancellation_requested = true;
          requests.push_back(job->request);
        }
      }
    }
    for (const request_record& request : requests)
    {
      recorder_.record_cancellation(kOrdinaryDescriptor.variant_id, request, halted_at, reason);
      const bool accepted = cancel_safely(provider_, request);
      events_.cancellation(request, reason, accepted);
    }
  }

  void reset(logical_time reset_at)
  {
    halt(reset_at, "runtime_reset");
    std::lock_guard lock(committed_mutex_);
    committed_.reset();
  }

  [[nodiscard]] std::size_t active_jobs() const
  {
    std::lock_guard lock(jobs_mutex_);
    return static_cast<std::size_t>(
        std::count_if(jobs_.begin(), jobs_.end(),
                      [](const std::unique_ptr<worker_job>& job)
                      { return !job->finished.load(); }));
  }

  [[nodiscard]] std::vector<std::string> canonical_events() const
  {
    return events_.snapshot();
  }

  [[nodiscard]] task_snapshot snapshot() const
  {
    std::lock_guard lock(task_mutex_);
    return task_;
  }

private:
  struct worker_job
  {
    request_record request;
    bool cancellation_requested = false;
    std::atomic<bool> finished{false};
    std::jthread thread;
  };

  void reap_finished_jobs()
  {
    std::vector<std::unique_ptr<worker_job>> finished;
    {
      std::lock_guard lock(jobs_mutex_);
      auto first = std::stable_partition(
          jobs_.begin(), jobs_.end(),
          [](const std::unique_ptr<worker_job>& job) { return !job->finished.load(); });
      std::move(first, jobs_.end(), std::back_inserter(finished));
      jobs_.erase(first, jobs_.end());
    }
  }

  std::shared_ptr<proposal_provider> provider_;
  effect_recorder& recorder_;
  logical_now now_;
  proposal_validation_config validation_;
  variant_event_stream events_;
  mutable std::mutex task_mutex_;
  task_snapshot task_;
  mutable std::mutex jobs_mutex_;
  std::vector<std::unique_ptr<worker_job>> jobs_;
  std::mutex completions_mutex_;
  std::deque<completed_call> completions_;
  std::mutex committed_mutex_;
  std::optional<committed_proposal> committed_;
};

btcpp_asynchronous_variant::btcpp_asynchronous_variant(
    std::shared_ptr<proposal_provider> provider, effect_recorder& recorder, logical_now now,
    proposal_validation_config validation)
    : implementation_(std::make_unique<implementation>(std::move(provider), recorder,
                                                       std::move(now), std::move(validation)))
{
}

btcpp_asynchronous_variant::~btcpp_asynchronous_variant() = default;

const variant_descriptor& btcpp_asynchronous_variant::descriptor() const noexcept
{
  return kOrdinaryDescriptor;
}

variant_update btcpp_asynchronous_variant::submit(const request_record& request)
{
  return implementation_->submit(request);
}

variant_update btcpp_asynchronous_variant::poll(logical_time admission_at)
{
  return implementation_->poll(admission_at);
}

bool btcpp_asynchronous_variant::dispatch(logical_time dispatch_at)
{
  return implementation_->dispatch(dispatch_at);
}

void btcpp_asynchronous_variant::synchronise(const task_snapshot& task)
{
  implementation_->synchronise(task);
}

void btcpp_asynchronous_variant::halt(logical_time halted_at, std::string_view reason)
{
  implementation_->halt(halted_at, reason);
}

void btcpp_asynchronous_variant::reset(logical_time reset_at)
{
  implementation_->reset(reset_at);
}

std::vector<std::string> btcpp_asynchronous_variant::canonical_events() const
{
  return implementation_->canonical_events();
}

std::size_t btcpp_asynchronous_variant::active_jobs() const
{
  return implementation_->active_jobs();
}

class btcpp_invocation_scoped_variant::implementation
{
public:
  implementation(std::shared_ptr<proposal_provider> provider, effect_recorder& recorder,
                 logical_now now, proposal_validation_config validation)
      : provider_(std::move(provider)), recorder_(recorder), now_(std::move(now)),
        validation_(std::move(validation)), events_("btcpp-full-authority")
  {
    if (!provider_ || !now_)
    {
      throw std::invalid_argument("BehaviorTree.CPP invocation-scoped variant requires provider and clock");
    }
  }

  ~implementation()
  {
    halt(now_(), "runner_destroyed");
    std::vector<std::unique_ptr<worker_job>> jobs;
    {
      std::lock_guard lock(jobs_mutex_);
      jobs.swap(jobs_);
    }
  }

  void synchronise(const task_snapshot& task)
  {
    std::lock_guard lock(task_mutex_);
    task_ = task;
  }

  variant_update submit(const request_record& request)
  {
    recorder_.record_request(kFullDescriptor.variant_id, request);
    events_.submit(request, "invocation_scoped");
    auto job = std::make_unique<worker_job>();
    job->request = request;
    worker_job* const job_ptr = job.get();
    {
      std::lock_guard lock(jobs_mutex_);
      jobs_.push_back(std::move(job));
    }
    job_ptr->thread = std::jthread(
        [this, job_ptr, request]
        {
          provider_result result = infer_safely(provider_, request);
          const logical_time completed_at = now_();
          {
            std::lock_guard lock(completions_mutex_);
            const std::size_t copies = std::max<std::size_t>(1, result.completion_copies);
            for (std::size_t copy = 0; copy < copies; ++copy)
            {
              recorder_.record_provider_completion(kFullDescriptor.variant_id, request,
                                                   result.proposal.response_id, completed_at);
              events_.provider_completed(request, result);
              completions_.push_back(completed_call{request, result, completed_at});
            }
          }
          job_ptr->finished.store(true);
        });
    return {};
  }

  variant_update poll(logical_time admission_at)
  {
    std::deque<completed_call> completions;
    {
      std::lock_guard lock(completions_mutex_);
      completions.swap(completions_);
    }
    variant_update update;
    while (!completions.empty())
    {
      completed_call completion = std::move(completions.front());
      completions.pop_front();
      ++update.provider_completions;
      if (!claim_terminal(completion.request.request_id))
      {
        events_.completion_dropped(completion.request, "terminal_already_claimed");
        continue;
      }

      const authority_assessment authority = assess(completion.request, admission_at);
      std::optional<std::string> rejection;
      if (!authority.current)
      {
        rejection = std::string(to_string(authority.reason));
      }
      else
      {
        rejection = validate_provider_result(completion.result, validation_);
      }
      const task_snapshot task = snapshot();
      if (rejection)
      {
        recorder_.record_rejection(kFullDescriptor.variant_id, completion.request,
                                   completion.result.proposal.response_id, *rejection,
                                   admission_at);
        events_.result(completion.request, completion.result, task.context_id, false, *rejection);
        ++update.rejections;
        update.last_reason = *rejection;
        continue;
      }

      recorder_.record_commit(kFullDescriptor.variant_id, completion.request,
                              completion.result.proposal.response_id, admission_at);
      events_.result(completion.request, completion.result, task.context_id, true, "");
      {
        std::lock_guard lock(committed_mutex_);
        committed_ = committed_proposal{completion.request, completion.result.proposal};
      }
      ++update.commits;
    }
    reap_finished_terminal_jobs();
    return update;
  }

  bool dispatch(logical_time dispatch_at)
  {
    std::optional<committed_proposal> proposal;
    {
      std::lock_guard lock(committed_mutex_);
      proposal = std::move(committed_);
      committed_.reset();
    }
    if (!proposal)
    {
      return false;
    }
    const authority_assessment authority = assess(proposal->request, dispatch_at);
    const task_snapshot task = snapshot();
    if (!authority.current)
    {
      const std::string reason(to_string(authority.reason));
      recorder_.record_dispatch_rejection(kFullDescriptor.variant_id, proposal->request,
                                          proposal->proposal.response_id, reason, dispatch_at);
      events_.dispatch(proposal->request, proposal->proposal, task.context_id, false, reason);
      return false;
    }
    recorder_.record_dispatch(kFullDescriptor.variant_id, proposal->request,
                              proposal->proposal.response_id, dispatch_at);
    events_.dispatch(proposal->request, proposal->proposal, task.context_id, true, "");
    return true;
  }

  variant_update cancel(std::uint64_t request_id, logical_time cancelled_at)
  {
    std::optional<request_record> request = claim_request(request_id);
    if (!request)
    {
      return {};
    }
    recorder_.record_rejection(kFullDescriptor.variant_id, *request, {}, "cancelled", cancelled_at);
    recorder_.record_cancellation(kFullDescriptor.variant_id, *request, cancelled_at,
                                  "explicit_cancel");
    const bool accepted = cancel_safely(provider_, *request);
    events_.cancellation(*request, "explicit_cancel", accepted);
    provider_result result;
    events_.result(*request, result, snapshot().context_id, false, "cancelled");
    return {.provider_completions = 0,
            .commits = 0,
            .rejections = 1,
            .last_reason = "cancelled"};
  }

  void halt(logical_time halted_at, std::string_view reason)
  {
    std::vector<request_record> requests;
    {
      std::lock_guard lock(jobs_mutex_);
      for (const std::unique_ptr<worker_job>& job : jobs_)
      {
        if (!job->terminal_claimed)
        {
          job->terminal_claimed = true;
          requests.push_back(job->request);
        }
      }
    }
    for (const request_record& request : requests)
    {
      recorder_.record_rejection(kFullDescriptor.variant_id, request, {}, reason, halted_at);
      recorder_.record_cancellation(kFullDescriptor.variant_id, request, halted_at, reason);
      events_.revoked(request, reason);
      const bool accepted = cancel_safely(provider_, request);
      events_.cancellation(request, reason, accepted);
      provider_result result;
      events_.result(request, result, snapshot().context_id, false, reason);
    }
    std::lock_guard lock(committed_mutex_);
    committed_.reset();
  }

  void reset(logical_time reset_at)
  {
    halt(reset_at, "branch_revoked");
  }

  [[nodiscard]] std::size_t active_jobs() const
  {
    std::lock_guard lock(jobs_mutex_);
    return static_cast<std::size_t>(
        std::count_if(jobs_.begin(), jobs_.end(),
                      [](const std::unique_ptr<worker_job>& job)
                      { return !job->terminal_claimed; }));
  }

  [[nodiscard]] std::vector<std::string> canonical_events() const
  {
    return events_.snapshot();
  }

private:
  struct worker_job
  {
    request_record request;
    bool terminal_claimed = false;
    std::atomic<bool> finished{false};
    std::jthread thread;
  };

  [[nodiscard]] task_snapshot snapshot() const
  {
    std::lock_guard lock(task_mutex_);
    return task_;
  }

  [[nodiscard]] authority_assessment assess(const request_record& request,
                                             logical_time at) const
  {
    const task_snapshot task = snapshot();
    if (request.reset_epoch != task.reset_epoch || !task.model_branch_active ||
        request.branch_epoch != task.branch_epoch)
    {
      return {.current = false, .reason = authority_reason::branch_revoked};
    }
    if (request.generation != task.generation)
    {
      return {.current = false, .reason = authority_reason::superseded};
    }
    if (request.captured_context_id != task.context_id)
    {
      return {.current = false, .reason = authority_reason::context_changed};
    }
    if (at > request.deadline)
    {
      return {.current = false, .reason = authority_reason::deadline_expired};
    }
    return {.current = true, .reason = authority_reason::current};
  }

  bool claim_terminal(std::uint64_t request_id)
  {
    std::lock_guard lock(jobs_mutex_);
    const auto found = std::find_if(
        jobs_.begin(), jobs_.end(), [request_id](const std::unique_ptr<worker_job>& job)
        { return job->request.request_id == request_id; });
    if (found == jobs_.end() || (*found)->terminal_claimed)
    {
      return false;
    }
    (*found)->terminal_claimed = true;
    return true;
  }

  std::optional<request_record> claim_request(std::uint64_t request_id)
  {
    std::lock_guard lock(jobs_mutex_);
    const auto found = std::find_if(
        jobs_.begin(), jobs_.end(), [request_id](const std::unique_ptr<worker_job>& job)
        { return job->request.request_id == request_id; });
    if (found == jobs_.end() || (*found)->terminal_claimed)
    {
      return std::nullopt;
    }
    (*found)->terminal_claimed = true;
    return (*found)->request;
  }

  void reap_finished_terminal_jobs()
  {
    std::vector<std::unique_ptr<worker_job>> finished;
    {
      std::lock_guard lock(jobs_mutex_);
      auto first = std::stable_partition(
          jobs_.begin(), jobs_.end(), [](const std::unique_ptr<worker_job>& job)
          { return !(job->finished.load() && job->terminal_claimed); });
      std::move(first, jobs_.end(), std::back_inserter(finished));
      jobs_.erase(first, jobs_.end());
    }
  }

  std::shared_ptr<proposal_provider> provider_;
  effect_recorder& recorder_;
  logical_now now_;
  proposal_validation_config validation_;
  variant_event_stream events_;
  mutable std::mutex task_mutex_;
  task_snapshot task_;
  mutable std::mutex jobs_mutex_;
  std::vector<std::unique_ptr<worker_job>> jobs_;
  std::mutex completions_mutex_;
  std::deque<completed_call> completions_;
  std::mutex committed_mutex_;
  std::optional<committed_proposal> committed_;
};

btcpp_invocation_scoped_variant::btcpp_invocation_scoped_variant(
    std::shared_ptr<proposal_provider> provider, effect_recorder& recorder, logical_now now,
    proposal_validation_config validation)
    : implementation_(std::make_unique<implementation>(std::move(provider), recorder,
                                                       std::move(now), std::move(validation)))
{
}

btcpp_invocation_scoped_variant::~btcpp_invocation_scoped_variant() = default;

const variant_descriptor& btcpp_invocation_scoped_variant::descriptor() const noexcept
{
  return kFullDescriptor;
}

variant_update btcpp_invocation_scoped_variant::submit(const request_record& request)
{
  return implementation_->submit(request);
}

variant_update btcpp_invocation_scoped_variant::poll(logical_time admission_at)
{
  return implementation_->poll(admission_at);
}

bool btcpp_invocation_scoped_variant::dispatch(logical_time dispatch_at)
{
  return implementation_->dispatch(dispatch_at);
}

variant_update btcpp_invocation_scoped_variant::cancel(std::uint64_t request_id,
                                                       logical_time cancelled_at)
{
  return implementation_->cancel(request_id, cancelled_at);
}

void btcpp_invocation_scoped_variant::synchronise(const task_snapshot& task)
{
  implementation_->synchronise(task);
}

void btcpp_invocation_scoped_variant::halt(logical_time halted_at, std::string_view reason)
{
  implementation_->halt(halted_at, reason);
}

void btcpp_invocation_scoped_variant::reset(logical_time reset_at)
{
  implementation_->reset(reset_at);
}

std::vector<std::string> btcpp_invocation_scoped_variant::canonical_events() const
{
  return implementation_->canonical_events();
}

std::size_t btcpp_invocation_scoped_variant::active_jobs() const
{
  return implementation_->active_jobs();
}

} // namespace muesli_bt::experiments::controlled_authority
