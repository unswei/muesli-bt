#include "common_task.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

void require_context(std::string_view context_id)
{
  if (context_id.empty())
  {
    throw std::invalid_argument("controlled task context ID must not be empty");
  }
}

void require_event_order(const task_event& event, bool has_event,
                         std::uint64_t last_event_sequence, logical_time last_event_at)
{
  if (has_event && event.sequence <= last_event_sequence)
  {
    throw std::invalid_argument("controlled task event sequences must increase");
  }
  if (has_event && event.at < last_event_at)
  {
    throw std::invalid_argument("controlled task event times must not decrease");
  }
}

void apply_task_event(task_snapshot& state, const task_event& event)
{
  switch (event.kind)
  {
    case task_event_kind::enter_model_branch:
      if (state.model_branch_active || state.emergency)
      {
        throw std::logic_error("model branch cannot enter from the current task state");
      }
      ++state.branch_epoch;
      state.model_branch_active = true;
      state.branch = task_branch::model;
      break;

    case task_event_kind::leave_model_branch:
      if (!state.model_branch_active)
      {
        throw std::logic_error("inactive model branch cannot leave");
      }
      state.model_branch_active = false;
      state.branch = task_branch::fallback;
      break;

    case task_event_kind::reenter_model_branch:
      if (state.model_branch_active || state.emergency)
      {
        throw std::logic_error("model branch cannot re-enter from the current task state");
      }
      ++state.branch_epoch;
      state.model_branch_active = true;
      state.branch = task_branch::model;
      break;

    case task_event_kind::context_changed:
      require_context(event.context_id);
      if (event.context_id == state.context_id)
      {
        throw std::logic_error("context change must supply a new context ID");
      }
      state.context_id = event.context_id;
      break;

    case task_event_kind::emergency_activated:
      state.emergency = true;
      state.model_branch_active = false;
      state.branch = task_branch::safe_stand;
      break;

    case task_event_kind::emergency_cleared:
      if (!state.emergency)
      {
        throw std::logic_error("inactive emergency cannot be cleared");
      }
      state.emergency = false;
      state.branch = task_branch::fallback;
      break;

    case task_event_kind::runtime_reset:
      ++state.reset_epoch;
      state.generation = 0;
      state.model_branch_active = false;
      state.emergency = false;
      state.branch = task_branch::fallback;
      break;
  }
  state.last_event_at = event.at;
}

}  // namespace

common_task::common_task(std::string initial_context_id)
{
  require_context(initial_context_id);
  state_.context_id = std::move(initial_context_id);
}

void common_task::apply(const task_event& event)
{
  std::lock_guard lock(mutex_);
  require_event_order(event, has_event_, last_event_sequence_, state_.last_event_at);
  apply_task_event(state_, event);
  last_event_sequence_ = event.sequence;
  has_event_ = true;
}

request_record common_task::submit_request(logical_time now, logical_time deadline)
{
  std::lock_guard lock(mutex_);
  if (!state_.model_branch_active || state_.emergency)
  {
    throw std::logic_error("request submission requires an active model branch");
  }
  if (now < state_.last_event_at)
  {
    throw std::invalid_argument("request submission precedes the latest task event");
  }
  if (deadline < now)
  {
    throw std::invalid_argument("request deadline precedes submission");
  }

  ++state_.generation;
  return request_record{
      .request_id = next_request_id_++,
      .branch_epoch = state_.branch_epoch,
      .generation = state_.generation,
      .reset_epoch = state_.reset_epoch,
      .captured_context_id = state_.context_id,
      .submitted_at = now,
      .deadline = deadline,
  };
}

task_snapshot common_task::snapshot() const
{
  std::lock_guard lock(mutex_);
  return state_;
}

std::string_view to_string(authority_reason reason) noexcept
{
  switch (reason)
  {
    case authority_reason::current:
      return "current";
    case authority_reason::branch_revoked:
      return "branch_revoked";
    case authority_reason::superseded:
      return "superseded";
    case authority_reason::context_changed:
      return "context_changed";
    case authority_reason::deadline_expired:
      return "deadline_expired";
  }
  return "branch_revoked";
}

authority_oracle::authority_oracle(std::string initial_context_id)
{
  require_context(initial_context_id);
  state_.context_id = std::move(initial_context_id);
}

void authority_oracle::apply(const task_event& event)
{
  std::lock_guard lock(mutex_);
  require_event_order(event, has_event_, last_event_sequence_, state_.last_event_at);
  apply_task_event(state_, event);
  last_event_sequence_ = event.sequence;
  has_event_ = true;
}

void authority_oracle::register_request(const request_record& request)
{
  std::lock_guard lock(mutex_);
  if (!state_.model_branch_active || state_.emergency)
  {
    throw std::logic_error("oracle cannot register a request outside the model branch");
  }
  if (request.branch_epoch != state_.branch_epoch || request.reset_epoch != state_.reset_epoch ||
      request.captured_context_id != state_.context_id)
  {
    throw std::logic_error("request capture does not match oracle task state");
  }
  if (request.generation <= state_.generation)
  {
    throw std::logic_error("request generation must increase");
  }
  state_.generation = request.generation;
}

authority_assessment authority_oracle::assess(const request_record& request,
                                               logical_time effect_at) const
{
  std::lock_guard lock(mutex_);

  if (request.reset_epoch != state_.reset_epoch || !state_.model_branch_active ||
      request.branch_epoch != state_.branch_epoch)
  {
    return {.current = false, .reason = authority_reason::branch_revoked};
  }
  if (request.generation != state_.generation)
  {
    return {.current = false, .reason = authority_reason::superseded};
  }
  if (request.captured_context_id != state_.context_id)
  {
    return {.current = false, .reason = authority_reason::context_changed};
  }
  if (effect_at > request.deadline)
  {
    return {.current = false, .reason = authority_reason::deadline_expired};
  }
  return {.current = true, .reason = authority_reason::current};
}

task_snapshot authority_oracle::snapshot() const
{
  std::lock_guard lock(mutex_);
  return state_;
}

deterministic_coordinator::deterministic_coordinator(std::string initial_context_id,
                                                     std::vector<task_event> events)
    : task_(initial_context_id), oracle_(std::move(initial_context_id)), events_(std::move(events))
{
  validate_schedule(events_);
}

void deterministic_coordinator::validate_schedule(const std::vector<task_event>& events)
{
  for (std::size_t index = 1; index < events.size(); ++index)
  {
    const task_event& previous = events[index - 1];
    const task_event& current = events[index];
    if (current.at < previous.at || current.sequence <= previous.sequence)
    {
      throw std::invalid_argument(
          "controlled task schedule must be ordered by time and increasing sequence");
    }
  }
}

void deterministic_coordinator::advance_to(logical_time target)
{
  std::lock_guard lock(mutex_);
  if (target < now_)
  {
    throw std::invalid_argument("controlled task clock cannot move backwards");
  }

  while (next_event_ < events_.size() && events_[next_event_].at <= target)
  {
    const task_event& event = events_[next_event_];
    task_.apply(event);
    oracle_.apply(event);
    ++next_event_;
  }
  now_ = target;
}

request_record deterministic_coordinator::submit_request(logical_time deadline_after_submission)
{
  std::lock_guard lock(mutex_);
  if (deadline_after_submission.count() < 0)
  {
    throw std::invalid_argument("request deadline offset must not be negative");
  }
  request_record request = task_.submit_request(now_, now_ + deadline_after_submission);
  oracle_.register_request(request);
  return request;
}

authority_assessment deterministic_coordinator::assess(const request_record& request,
                                                        logical_time effect_at) const
{
  std::lock_guard lock(mutex_);
  return oracle_.assess(request, effect_at);
}

task_snapshot deterministic_coordinator::task_state() const
{
  std::lock_guard lock(mutex_);
  return task_.snapshot();
}

task_snapshot deterministic_coordinator::oracle_state() const
{
  std::lock_guard lock(mutex_);
  return oracle_.snapshot();
}

logical_time deterministic_coordinator::now() const
{
  std::lock_guard lock(mutex_);
  return now_;
}

std::size_t deterministic_coordinator::remaining_events() const
{
  std::lock_guard lock(mutex_);
  return events_.size() - next_event_;
}

}  // namespace muesli_bt::experiments::controlled_authority
