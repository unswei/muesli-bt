#include "campaign_outcome.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace muesli_bt::experiments::controlled_authority
{
namespace
{

bool has_rejection_reason(const trial_result& result, std::string_view reason)
{
  return std::any_of(
      result.effects.begin(), result.effects.end(), [reason](const effect_record& effect)
      { return effect.kind == effect_kind::result_rejected && effect.reason == reason; });
}

bool has_host_rejection(const trial_result& result)
{
  return has_rejection_reason(result, "invalid_pose") ||
         has_rejection_reason(result, "invalid_frame") ||
         has_rejection_reason(result, "robot_unstable") ||
         has_rejection_reason(result, "ball_stale") ||
         has_rejection_reason(result, "host_policy_rejected");
}

} // namespace

bool expected_outcome_holds(const trial_result& result)
{
  const std::string& expected = result.expected_outcome;
  if (expected == "accept_current_exactly_once")
  {
    return result.summary.current_commits == 1 && result.summary.terminal_decisions == 1 &&
           !result.summary.has_obsolete_effect();
  }
  if (expected.find("obsolete_dispatch") != std::string::npos)
  {
    return result.summary.obsolete_dispatches > 0;
  }
  if (expected.find("obsolete_effect") != std::string::npos)
  {
    return result.summary.has_obsolete_effect();
  }
  if (expected == "reject_deadline_expired")
  {
    return has_rejection_reason(result, "deadline_expired");
  }
  if (expected == "reject_branch_revoked" || expected == "reject_branch_revoked_after_reset")
  {
    return has_rejection_reason(result, "branch_revoked") && !result.summary.has_obsolete_effect();
  }
  if (expected == "reject_branch_revoked_and_activate_safe_stand")
  {
    return has_rejection_reason(result, "branch_revoked") &&
           result.summary.safe_stand_activations == 1 && !result.summary.has_obsolete_effect();
  }
  if (expected == "reject_superseded_then_accept_current")
  {
    return has_rejection_reason(result, "superseded") && result.summary.current_commits == 1 &&
           !result.summary.has_obsolete_effect();
  }
  if (expected == "reject_context_changed")
  {
    return has_rejection_reason(result, "context_changed") && result.summary.current_commits == 0;
  }
  if (expected == "reject_dispatch_context_changed")
  {
    return result.summary.dispatch_rejections == 1 && result.summary.current_dispatches == 0 &&
           result.summary.obsolete_dispatches == 0;
  }
  if (expected == "duplicate_effect_exposed")
  {
    return result.summary.terminal_decisions > 1;
  }
  if (expected == "one_terminal_decision" || expected == "one_deterministic_terminal_decision")
  {
    return result.summary.terminal_decisions == 1;
  }
  if (expected == "reject_invalid_schema")
  {
    return has_rejection_reason(result, "invalid_schema");
  }
  if (expected == "reject_host_validation")
  {
    return has_host_rejection(result);
  }
  if (expected == "one_failure_and_delayed_fallback" || expected == "one_failure_and_fallback")
  {
    return result.summary.result_rejections == 1 && result.summary.fallback_activations == 1;
  }
  if (expected == "no_cross_job_effect" ||
      expected == "no_cross_job_effect_across_parallel_instances")
  {
    return result.expected_requests == 16 && result.submitted_requests == 16 &&
           result.summary.terminal_decisions == 16 && !result.summary.has_obsolete_effect();
  }
  if (expected == "new_request_cannot_start_while_blocked" ||
      expected == "request_key_reuse_cannot_start_while_blocked")
  {
    return result.blocked_submissions == 1;
  }
  if (expected == "obsolete_effect_or_old_key_write")
  {
    return result.summary.has_obsolete_effect();
  }
  if (expected == "reject_superseded_without_clearing_new_request")
  {
    return has_rejection_reason(result, "superseded") && result.active_jobs_at_end == 1 &&
           !result.summary.has_obsolete_effect();
  }
  if (expected == "task_decisions_equal")
  {
    return result.replay_equal;
  }
  if (expected.rfind("not_applicable", 0) == 0)
  {
    return true;
  }
  return false;
}

std::string decision_signature(const trial_result& result)
{
  std::ostringstream signature;
  for (const effect_record& effect : result.effects)
  {
    if (effect.kind != effect_kind::result_committed &&
        effect.kind != effect_kind::result_rejected &&
        effect.kind != effect_kind::capability_dispatched &&
        effect.kind != effect_kind::capability_dispatch_rejected &&
        effect.kind != effect_kind::fallback_activated &&
        effect.kind != effect_kind::safe_stand_activated)
    {
      continue;
    }
    signature << to_string(effect.kind) << ':' << effect.reason << ':' << effect.at.count();
    if (effect.request)
    {
      signature << ':' << effect.request->generation;
    }
    if (effect.authority_assessed)
    {
      signature << ':' << (effect.authority.current ? '1' : '0') << ':'
                << to_string(effect.authority.reason);
    }
    signature << ';';
  }
  return signature.str();
}

} // namespace muesli_bt::experiments::controlled_authority
