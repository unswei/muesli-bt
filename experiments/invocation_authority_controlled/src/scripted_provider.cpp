#include "scripted_provider.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

using namespace std::chrono_literals;

} // namespace

scripted_provider::scripted_provider(std::vector<scripted_provider_job> jobs)
{
  if (jobs.empty())
  {
    throw std::invalid_argument("scripted provider requires at least one job");
  }
  jobs_.reserve(jobs.size());
  std::unordered_map<std::string, bool> labels;
  for (scripted_provider_job& job : jobs)
  {
    if (job.request_label.empty() || !labels.emplace(job.request_label, true).second)
    {
      throw std::invalid_argument("scripted provider request labels must be non-empty and unique");
    }
    jobs_.push_back(std::make_unique<job_state>(job_state{.script = std::move(job)}));
  }
}

scripted_provider::~scripted_provider()
{
  release_all();
}

provider_result scripted_provider::infer(const request_record& request)
{
  std::unique_lock lock(mutex_);
  if (next_job_ >= jobs_.size())
  {
    throw std::runtime_error("scripted provider received an unexpected request");
  }
  job_state& job = *jobs_[next_job_++];
  job.request_id = request.request_id;
  job.started = true;
  condition_.notify_all();
  if (!condition_.wait_for(lock, 5s, [&job] { return job.released; }))
  {
    throw std::runtime_error("scripted provider release barrier timed out");
  }
  job.finished = true;
  condition_.notify_all();
  return job.script.result;
}

bool scripted_provider::cancel(const request_record& request)
{
  std::lock_guard lock(mutex_);
  job_state* const job = find_request(request.request_id);
  if (!job || job->finished)
  {
    return false;
  }
  job->cancellation_requested = true;
  condition_.notify_all();
  return true;
}

void scripted_provider::wait_until_started(std::string_view request_label)
{
  std::unique_lock lock(mutex_);
  job_state& job = find_label(request_label);
  if (!condition_.wait_for(lock, 5s, [&job] { return job.started; }))
  {
    throw std::runtime_error("scripted provider start barrier timed out for " +
                             std::string(request_label));
  }
}

void scripted_provider::release(std::string_view request_label)
{
  std::lock_guard lock(mutex_);
  job_state& job = find_label(request_label);
  job.released = true;
  condition_.notify_all();
}

void scripted_provider::wait_until_finished(std::string_view request_label)
{
  std::unique_lock lock(mutex_);
  job_state& job = find_label(request_label);
  if (!condition_.wait_for(lock, 5s, [&job] { return job.finished; }))
  {
    throw std::runtime_error("scripted provider finish barrier timed out for " +
                             std::string(request_label));
  }
}

void scripted_provider::release_all() noexcept
{
  std::lock_guard lock(mutex_);
  for (const std::unique_ptr<job_state>& job : jobs_)
  {
    job->released = true;
  }
  condition_.notify_all();
}

scripted_provider::job_state& scripted_provider::find_label(std::string_view request_label)
{
  for (const std::unique_ptr<job_state>& job : jobs_)
  {
    if (job->script.request_label == request_label)
    {
      return *job;
    }
  }
  throw std::invalid_argument("unknown scripted request label: " + std::string(request_label));
}

scripted_provider::job_state* scripted_provider::find_request(std::uint64_t request_id)
{
  for (const std::unique_ptr<job_state>& job : jobs_)
  {
    if (job->started && job->request_id == request_id)
    {
      return job.get();
    }
  }
  return nullptr;
}

} // namespace muesli_bt::experiments::controlled_authority
