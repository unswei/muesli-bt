#include "effect_recorder.hpp"

#include <stdexcept>
#include <utility>

namespace muesli_bt::experiments::controlled_authority
{

std::string_view to_string(effect_kind kind) noexcept
{
  switch (kind)
  {
    case effect_kind::request_submitted:
      return "request_submitted";
    case effect_kind::provider_completed:
      return "provider_completed";
    case effect_kind::result_committed:
      return "result_committed";
    case effect_kind::result_rejected:
      return "result_rejected";
    case effect_kind::cancellation_requested:
      return "cancellation_requested";
    case effect_kind::capability_dispatched:
      return "capability_dispatched";
    case effect_kind::capability_dispatch_rejected:
      return "capability_dispatch_rejected";
    case effect_kind::fallback_activated:
      return "fallback_activated";
    case effect_kind::safe_stand_activated:
      return "safe_stand_activated";
  }
  return "result_rejected";
}

effect_recorder::effect_recorder(authority_assessor assessor) : assessor_(std::move(assessor))
{
  if (!assessor_)
  {
    throw std::invalid_argument("controlled effect recorder requires an authority oracle");
  }
}

void effect_recorder::record_request(std::string_view variant_id, const request_record& request)
{
  append(effect_record{.kind = effect_kind::request_submitted,
                       .variant_id = std::string(variant_id),
                       .at = request.submitted_at,
                       .request = request});
}

void effect_recorder::record_provider_completion(std::string_view variant_id,
                                                 const request_record& request,
                                                 std::string_view response_id,
                                                 logical_time completed_at)
{
  append(effect_record{.kind = effect_kind::provider_completed,
                       .variant_id = std::string(variant_id),
                       .at = completed_at,
                       .request = request,
                       .response_id = std::string(response_id)});
}

void effect_recorder::record_commit(std::string_view variant_id, const request_record& request,
                                    std::string_view response_id, logical_time committed_at)
{
  const authority_assessment authority = assess(request, committed_at);
  append(effect_record{.kind = effect_kind::result_committed,
                       .variant_id = std::string(variant_id),
                       .at = committed_at,
                       .request = request,
                       .response_id = std::string(response_id),
                       .reason = std::string(to_string(authority.reason)),
                       .authority_assessed = true,
                       .authority = authority});
}

void effect_recorder::record_rejection(std::string_view variant_id, const request_record& request,
                                       std::string_view response_id, std::string_view reason,
                                       logical_time rejected_at)
{
  const authority_assessment authority = assess(request, rejected_at);
  append(effect_record{.kind = effect_kind::result_rejected,
                       .variant_id = std::string(variant_id),
                       .at = rejected_at,
                       .request = request,
                       .response_id = std::string(response_id),
                       .reason = std::string(reason),
                       .authority_assessed = true,
                       .authority = authority});
}

void effect_recorder::record_cancellation(std::string_view variant_id,
                                          const request_record& request,
                                          logical_time requested_at, std::string_view reason)
{
  append(effect_record{.kind = effect_kind::cancellation_requested,
                       .variant_id = std::string(variant_id),
                       .at = requested_at,
                       .request = request,
                       .reason = std::string(reason)});
}

void effect_recorder::record_dispatch(std::string_view variant_id, const request_record& request,
                                      std::string_view response_id, logical_time dispatched_at)
{
  const authority_assessment authority = assess(request, dispatched_at);
  append(effect_record{.kind = effect_kind::capability_dispatched,
                       .variant_id = std::string(variant_id),
                       .at = dispatched_at,
                       .request = request,
                       .response_id = std::string(response_id),
                       .reason = std::string(to_string(authority.reason)),
                       .authority_assessed = true,
                       .authority = authority});
}

void effect_recorder::record_dispatch_rejection(std::string_view variant_id,
                                                const request_record& request,
                                                std::string_view response_id,
                                                std::string_view reason,
                                                logical_time rejected_at)
{
  const authority_assessment authority = assess(request, rejected_at);
  append(effect_record{.kind = effect_kind::capability_dispatch_rejected,
                       .variant_id = std::string(variant_id),
                       .at = rejected_at,
                       .request = request,
                       .response_id = std::string(response_id),
                       .reason = std::string(reason),
                       .authority_assessed = true,
                       .authority = authority});
}

void effect_recorder::record_fallback(std::string_view variant_id, logical_time activated_at,
                                      std::string_view reason)
{
  append(effect_record{.kind = effect_kind::fallback_activated,
                       .variant_id = std::string(variant_id),
                       .at = activated_at,
                       .reason = std::string(reason)});
}

void effect_recorder::record_safe_stand(std::string_view variant_id, logical_time activated_at,
                                        std::string_view reason)
{
  append(effect_record{.kind = effect_kind::safe_stand_activated,
                       .variant_id = std::string(variant_id),
                       .at = activated_at,
                       .reason = std::string(reason)});
}

std::vector<effect_record> effect_recorder::snapshot() const
{
  std::lock_guard lock(mutex_);
  return records_;
}

effect_summary effect_recorder::summary(std::string_view variant_id) const
{
  std::lock_guard lock(mutex_);
  effect_summary result;
  for (const effect_record& record : records_)
  {
    if (record.variant_id != variant_id)
    {
      continue;
    }
    switch (record.kind)
    {
      case effect_kind::request_submitted:
        ++result.requests_submitted;
        break;
      case effect_kind::provider_completed:
        ++result.provider_completions;
        break;
      case effect_kind::result_committed:
        ++result.terminal_decisions;
        if (record.authority.current)
        {
          ++result.current_commits;
        }
        else
        {
          ++result.obsolete_commits;
        }
        break;
      case effect_kind::result_rejected:
        ++result.result_rejections;
        ++result.terminal_decisions;
        break;
      case effect_kind::cancellation_requested:
        ++result.cancellation_requests;
        break;
      case effect_kind::capability_dispatched:
        if (record.authority.current)
        {
          ++result.current_dispatches;
        }
        else
        {
          ++result.obsolete_dispatches;
        }
        break;
      case effect_kind::capability_dispatch_rejected:
        ++result.dispatch_rejections;
        break;
      case effect_kind::fallback_activated:
        ++result.fallback_activations;
        break;
      case effect_kind::safe_stand_activated:
        ++result.safe_stand_activations;
        break;
    }
  }
  return result;
}

void effect_recorder::append(effect_record record)
{
  if (record.variant_id.empty())
  {
    throw std::invalid_argument("controlled effect record requires a variant ID");
  }
  std::lock_guard lock(mutex_);
  record.sequence = next_sequence_++;
  records_.push_back(std::move(record));
}

authority_assessment effect_recorder::assess(const request_record& request,
                                             logical_time effect_at) const
{
  return assessor_(request, effect_at);
}

}  // namespace muesli_bt::experiments::controlled_authority
