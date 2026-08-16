#include "variant.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

const variant_descriptor kBlockingDescriptor{
    .variant_id = "authority-b0-blocking",
    .short_label = "B0",
    .reader_label = "blocking service call",
    .blocking = true,
};

const variant_descriptor kAsynchronousDescriptor{
    .variant_id = "authority-b1-asynchronous",
    .short_label = "B1",
    .reader_label = "ordinary asynchronous completion",
    .blocking = false,
};

const variant_descriptor kTimeoutDescriptor{
    .variant_id = "authority-b2-timeout",
    .short_label = "B2",
    .reader_label = "timeout and best-effort cancellation",
    .blocking = false,
};

struct completed_provider_call
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

bool request_provider_cancellation(const std::shared_ptr<proposal_provider>& provider,
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

class shared_variant_state
{
public:
  shared_variant_state(variant_descriptor descriptor, effect_recorder& recorder,
                       proposal_validation_config validation)
      : descriptor_(std::move(descriptor)), recorder_(recorder), validation_(std::move(validation))
  {
  }

  [[nodiscard]] const variant_descriptor& descriptor() const noexcept { return descriptor_; }

  void record_request(const request_record& request)
  {
    recorder_.record_request(descriptor_.variant_id, request);
  }

  variant_update accept_completion(completed_provider_call completion, logical_time admission_at)
  {
    recorder_.record_provider_completion(descriptor_.variant_id, completion.request,
                                         completion.result.proposal.response_id,
                                         completion.completed_at);
    variant_update update = accept_completion_without_provider_record(std::move(completion),
                                                                       admission_at);
    update.provider_completions = 1;
    return update;
  }

  variant_update accept_completion_without_provider_record(completed_provider_call completion,
                                                            logical_time admission_at)
  {
    const std::optional<std::string> rejection =
        validate_provider_result(completion.result, validation_);
    if (rejection)
    {
      recorder_.record_rejection(descriptor_.variant_id, completion.request,
                                 completion.result.proposal.response_id, *rejection, admission_at);
      return variant_update{
          .provider_completions = 0,
          .commits = 0,
          .rejections = 1,
          .last_reason = *rejection,
      };
    }

    recorder_.record_commit(descriptor_.variant_id, completion.request,
                            completion.result.proposal.response_id, admission_at);
    std::lock_guard lock(mutex_);
    committed_ = committed_proposal{
        .request = std::move(completion.request),
        .proposal = std::move(completion.result.proposal),
    };
    return variant_update{
        .provider_completions = 0,
        .commits = 1,
        .rejections = 0,
        .last_reason = {},
    };
  }

  bool dispatch(logical_time dispatch_at)
  {
    std::optional<committed_proposal> proposal;
    {
      std::lock_guard lock(mutex_);
      proposal = std::move(committed_);
      committed_.reset();
    }
    if (!proposal)
    {
      return false;
    }
    recorder_.record_dispatch(descriptor_.variant_id, proposal->request,
                              proposal->proposal.response_id, dispatch_at);
    return true;
  }

private:
  variant_descriptor descriptor_;
  effect_recorder& recorder_;
  proposal_validation_config validation_;
  std::mutex mutex_;
  std::optional<committed_proposal> committed_;
};

}  // namespace

bool proposal_provider::cancel(const request_record&)
{
  return false;
}

void authority_variant::synchronise(const task_snapshot&)
{
}

void authority_variant::halt(logical_time, std::string_view)
{
}

void authority_variant::reset(logical_time reset_at)
{
  halt(reset_at, "runtime_reset");
}

std::vector<std::string> authority_variant::canonical_events() const
{
  return {};
}

std::optional<std::string>
validate_provider_result(const provider_result& result, const proposal_validation_config& config)
{
  if (result.status != provider_status::ok)
  {
    return "backend_terminal_failure";
  }
  if (!result.proposal.schema_valid || result.proposal.response_id.empty())
  {
    return "invalid_schema";
  }
  if (result.proposal.frame_id != config.frame_id)
  {
    return "invalid_frame";
  }
  for (std::size_t index = 0; index < result.proposal.pose.size(); ++index)
  {
    const double value = result.proposal.pose[index];
    if (!std::isfinite(value))
    {
      return "invalid_schema";
    }
    if (value < config.minimum[index] || value > config.maximum[index])
    {
      return "invalid_pose";
    }
  }
  return std::nullopt;
}

class blocking_variant::implementation
{
public:
  implementation(std::shared_ptr<proposal_provider> provider, effect_recorder& recorder,
                 logical_now now, proposal_validation_config validation)
      : provider_(std::move(provider)), now_(std::move(now)),
        state_(kBlockingDescriptor, recorder, std::move(validation))
  {
    if (!provider_ || !now_)
    {
      throw std::invalid_argument("blocking authority variant requires provider and clock");
    }
  }

  variant_update submit(const request_record& request)
  {
    state_.record_request(request);
    provider_result result;
    try
    {
      result = provider_->infer(request);
    }
    catch (const std::exception& error)
    {
      result = provider_failure(error.what());
    }
    catch (...)
    {
      result = provider_failure("backend_terminal_failure");
    }
    const logical_time completed_at = now_();
    return state_.accept_completion(
        completed_provider_call{.request = request,
                                .result = std::move(result),
                                .completed_at = completed_at},
        completed_at);
  }

  [[nodiscard]] const variant_descriptor& descriptor() const noexcept
  {
    return state_.descriptor();
  }

  bool dispatch(logical_time dispatch_at) { return state_.dispatch(dispatch_at); }

private:
  std::shared_ptr<proposal_provider> provider_;
  logical_now now_;
  shared_variant_state state_;
};

blocking_variant::blocking_variant(std::shared_ptr<proposal_provider> provider,
                                   effect_recorder& recorder, logical_now now,
                                   proposal_validation_config validation)
    : implementation_(std::make_unique<implementation>(std::move(provider), recorder,
                                                       std::move(now), std::move(validation)))
{
}

blocking_variant::~blocking_variant() = default;

const variant_descriptor& blocking_variant::descriptor() const noexcept
{
  return implementation_->descriptor();
}

variant_update blocking_variant::submit(const request_record& request)
{
  return implementation_->submit(request);
}

variant_update blocking_variant::poll(logical_time)
{
  return {};
}

bool blocking_variant::dispatch(logical_time dispatch_at)
{
  return implementation_->dispatch(dispatch_at);
}

std::size_t blocking_variant::active_jobs() const
{
  return 0;
}

class asynchronous_variant::implementation
{
public:
  implementation(std::shared_ptr<proposal_provider> provider, effect_recorder& recorder,
                 logical_now now, proposal_validation_config validation)
      : provider_(std::move(provider)), now_(std::move(now)),
        state_(kAsynchronousDescriptor, recorder, std::move(validation))
  {
    if (!provider_ || !now_)
    {
      throw std::invalid_argument("asynchronous authority variant requires provider and clock");
    }
  }

  ~implementation()
  {
    std::vector<std::unique_ptr<worker_job>> jobs;
    {
      std::lock_guard lock(mutex_);
      jobs.swap(jobs_);
    }
  }

  variant_update submit(const request_record& request)
  {
    state_.record_request(request);
    auto job = std::make_unique<worker_job>();
    worker_job* const job_ptr = job.get();
    {
      std::lock_guard lock(mutex_);
      jobs_.push_back(std::move(job));
    }
    job_ptr->thread = std::jthread(
        [this, job_ptr, request]
        {
          provider_result result;
          try
          {
            result = provider_->infer(request);
          }
          catch (const std::exception& error)
          {
            result = provider_failure(error.what());
          }
          catch (...)
          {
            result = provider_failure("backend_terminal_failure");
          }
          const logical_time completed_at = now_();
          {
            std::lock_guard lock(completion_mutex_);
            completions_.push_back(completed_provider_call{
                .request = request,
                .result = std::move(result),
                .completed_at = completed_at,
            });
          }
          job_ptr->finished.store(true);
        });
    return {};
  }

  variant_update poll(logical_time admission_at)
  {
    std::deque<completed_provider_call> completions;
    {
      std::lock_guard lock(completion_mutex_);
      completions.swap(completions_);
    }
    variant_update update;
    while (!completions.empty())
    {
      const variant_update completion =
          state_.accept_completion(std::move(completions.front()), admission_at);
      update.provider_completions += completion.provider_completions;
      update.commits += completion.commits;
      update.rejections += completion.rejections;
      if (!completion.last_reason.empty())
      {
        update.last_reason = completion.last_reason;
      }
      completions.pop_front();
    }
    reap_finished_jobs();
    return update;
  }

  bool dispatch(logical_time dispatch_at) { return state_.dispatch(dispatch_at); }

  [[nodiscard]] std::size_t active_jobs() const
  {
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
        jobs_.begin(), jobs_.end(),
        [](const std::unique_ptr<worker_job>& job) { return !job->finished.load(); }));
  }

  [[nodiscard]] const variant_descriptor& descriptor() const noexcept
  {
    return state_.descriptor();
  }

private:
  struct worker_job
  {
    std::atomic<bool> finished{false};
    std::jthread thread;
  };

  void reap_finished_jobs()
  {
    std::vector<std::unique_ptr<worker_job>> finished;
    {
      std::lock_guard lock(mutex_);
      auto first_finished = std::stable_partition(
          jobs_.begin(), jobs_.end(),
          [](const std::unique_ptr<worker_job>& job) { return !job->finished.load(); });
      std::move(first_finished, jobs_.end(), std::back_inserter(finished));
      jobs_.erase(first_finished, jobs_.end());
    }
  }

  std::shared_ptr<proposal_provider> provider_;
  logical_now now_;
  shared_variant_state state_;
  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<worker_job>> jobs_;
  std::mutex completion_mutex_;
  std::deque<completed_provider_call> completions_;
};

asynchronous_variant::asynchronous_variant(std::shared_ptr<proposal_provider> provider,
                                           effect_recorder& recorder, logical_now now,
                                           proposal_validation_config validation)
    : implementation_(std::make_unique<implementation>(std::move(provider), recorder,
                                                       std::move(now), std::move(validation)))
{
}

asynchronous_variant::~asynchronous_variant() = default;

const variant_descriptor& asynchronous_variant::descriptor() const noexcept
{
  return implementation_->descriptor();
}

variant_update asynchronous_variant::submit(const request_record& request)
{
  return implementation_->submit(request);
}

variant_update asynchronous_variant::poll(logical_time admission_at)
{
  return implementation_->poll(admission_at);
}

bool asynchronous_variant::dispatch(logical_time dispatch_at)
{
  return implementation_->dispatch(dispatch_at);
}

std::size_t asynchronous_variant::active_jobs() const
{
  return implementation_->active_jobs();
}

class timeout_variant::implementation
{
public:
  implementation(std::shared_ptr<proposal_provider> provider, effect_recorder& recorder,
                 logical_now now, proposal_validation_config validation)
      : provider_(std::move(provider)), recorder_(recorder), now_(std::move(now)),
        state_(kTimeoutDescriptor, recorder, std::move(validation))
  {
    if (!provider_ || !now_)
    {
      throw std::invalid_argument("timeout authority variant requires provider and clock");
    }
  }

  ~implementation()
  {
    std::vector<std::unique_ptr<worker_job>> jobs;
    {
      std::lock_guard lock(mutex_);
      for (const auto& job : jobs_)
      {
        if (!job->terminal_claimed && !job->finished.load())
        {
          (void)request_provider_cancellation(provider_, job->request);
        }
      }
      jobs.swap(jobs_);
    }
  }

  variant_update submit(const request_record& request)
  {
    state_.record_request(request);
    auto job = std::make_unique<worker_job>();
    job->request = request;
    worker_job* const job_ptr = job.get();
    {
      std::lock_guard lock(mutex_);
      jobs_.push_back(std::move(job));
    }
    job_ptr->thread = std::jthread(
        [this, job_ptr, request]
        {
          provider_result result;
          try
          {
            result = provider_->infer(request);
          }
          catch (const std::exception& error)
          {
            result = provider_failure(error.what());
          }
          catch (...)
          {
            result = provider_failure("backend_terminal_failure");
          }
          const logical_time completed_at = now_();
          {
            std::lock_guard lock(completion_mutex_);
            completions_.push_back(completed_provider_call{
                .request = request,
                .result = std::move(result),
                .completed_at = completed_at,
            });
          }
          job_ptr->finished.store(true);
        });
    return {};
  }

  variant_update poll(logical_time admission_at)
  {
    std::deque<completed_provider_call> completions;
    {
      std::lock_guard lock(completion_mutex_);
      completions.swap(completions_);
    }

    variant_update update;
    while (!completions.empty())
    {
      completed_provider_call completion = std::move(completions.front());
      completions.pop_front();
      recorder_.record_provider_completion(kTimeoutDescriptor.variant_id, completion.request,
                                           completion.result.proposal.response_id,
                                           completion.completed_at);
      if (claim_terminal(completion.request.request_id))
      {
        if (admission_at > completion.request.deadline)
        {
          record_timeout(completion.request, admission_at, update);
        }
        else
        {
          const variant_update accepted =
              state_.accept_completion_without_provider_record(std::move(completion), admission_at);
          merge(update, accepted);
        }
      }
      ++update.provider_completions;
    }

    std::vector<request_record> expired;
    {
      std::lock_guard lock(mutex_);
      for (const auto& job : jobs_)
      {
        if (!job->terminal_claimed && admission_at > job->request.deadline)
        {
          job->terminal_claimed = true;
          expired.push_back(job->request);
        }
      }
    }
    for (const request_record& request : expired)
    {
      record_timeout(request, admission_at, update);
    }
    reap_finished_jobs();
    return update;
  }

  bool dispatch(logical_time dispatch_at) { return state_.dispatch(dispatch_at); }

  [[nodiscard]] std::size_t active_jobs() const
  {
    std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
        jobs_.begin(), jobs_.end(),
        [](const std::unique_ptr<worker_job>& job) { return !job->terminal_claimed; }));
  }

  [[nodiscard]] const variant_descriptor& descriptor() const noexcept
  {
    return state_.descriptor();
  }

private:
  struct worker_job
  {
    request_record request;
    bool terminal_claimed = false;
    std::atomic<bool> finished{false};
    std::jthread thread;
  };

  static void merge(variant_update& target, const variant_update& source)
  {
    target.commits += source.commits;
    target.rejections += source.rejections;
    if (!source.last_reason.empty())
    {
      target.last_reason = source.last_reason;
    }
  }

  bool claim_terminal(std::uint64_t request_id)
  {
    std::lock_guard lock(mutex_);
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

  void record_timeout(const request_record& request, logical_time admission_at,
                      variant_update& update)
  {
    recorder_.record_rejection(kTimeoutDescriptor.variant_id, request, {}, "deadline_expired",
                               admission_at);
    recorder_.record_cancellation(kTimeoutDescriptor.variant_id, request, admission_at,
                                  "deadline_expired");
    (void)request_provider_cancellation(provider_, request);
    ++update.rejections;
    update.last_reason = "deadline_expired";
  }

  void reap_finished_jobs()
  {
    std::vector<std::unique_ptr<worker_job>> finished;
    {
      std::lock_guard lock(mutex_);
      auto first_finished = std::stable_partition(
          jobs_.begin(), jobs_.end(),
          [](const std::unique_ptr<worker_job>& job) { return !job->finished.load(); });
      std::move(first_finished, jobs_.end(), std::back_inserter(finished));
      jobs_.erase(first_finished, jobs_.end());
    }
  }

  std::shared_ptr<proposal_provider> provider_;
  effect_recorder& recorder_;
  logical_now now_;
  shared_variant_state state_;
  mutable std::mutex mutex_;
  std::vector<std::unique_ptr<worker_job>> jobs_;
  std::mutex completion_mutex_;
  std::deque<completed_provider_call> completions_;
};

timeout_variant::timeout_variant(std::shared_ptr<proposal_provider> provider,
                                 effect_recorder& recorder, logical_now now,
                                 proposal_validation_config validation)
    : implementation_(std::make_unique<implementation>(std::move(provider), recorder,
                                                       std::move(now), std::move(validation)))
{
}

timeout_variant::~timeout_variant() = default;

const variant_descriptor& timeout_variant::descriptor() const noexcept
{
  return implementation_->descriptor();
}

variant_update timeout_variant::submit(const request_record& request)
{
  return implementation_->submit(request);
}

variant_update timeout_variant::poll(logical_time admission_at)
{
  return implementation_->poll(admission_at);
}

bool timeout_variant::dispatch(logical_time dispatch_at)
{
  return implementation_->dispatch(dispatch_at);
}

std::size_t timeout_variant::active_jobs() const
{
  return implementation_->active_jobs();
}

}  // namespace muesli_bt::experiments::controlled_authority
