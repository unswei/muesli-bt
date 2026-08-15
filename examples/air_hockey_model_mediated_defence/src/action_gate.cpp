#include "action_gate.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "muesli_bt/contract/events.hpp"

namespace air_hockey_demo {
namespace {

bt::vla_commit_validation reject(std::string reason) {
    return bt::vla_commit_validation{.accepted = false, .reason = std::move(reason)};
}

bool action_is_finite_pair(const std::vector<double>& action) {
    return action.size() == kActionDimension &&
           std::all_of(action.begin(), action.end(), [](double value) { return std::isfinite(value); });
}

std::string authority_state_name(bt::vla_authority_state state) {
    switch (state) {
        case bt::vla_authority_state::active:
            return "active";
        case bt::vla_authority_state::revoked:
            return "revoked";
        case bt::vla_authority_state::accepted:
            return "accepted";
        case bt::vla_authority_state::rejected:
            return "rejected";
    }
    return "rejected";
}

}  // namespace

air_hockey_action_validator::air_hockey_action_validator(action_validator_config configuration,
                                                         action_host_state_provider state_provider)
    : configuration_(std::move(configuration)), state_provider_(std::move(state_provider)) {
    if (configuration_.frame_id.empty() || !std::isfinite(configuration_.minimum) ||
        !std::isfinite(configuration_.maximum) || configuration_.minimum > configuration_.maximum ||
        configuration_.max_source_age_steps < 0) {
        throw std::invalid_argument("air-hockey action validator configuration is invalid");
    }
    if (!state_provider_) {
        throw std::invalid_argument("air-hockey action validator requires a host-state provider");
    }
}

void air_hockey_action_validator::record_source_step(std::uint64_t job_id,
                                                     std::int64_t observation_step) {
    if (job_id == 0 || observation_step < 0) {
        throw std::invalid_argument("air-hockey source-step record is invalid");
    }
    source_steps_[job_id] = observation_step;
}

std::optional<std::int64_t> air_hockey_action_validator::source_step(std::uint64_t job_id) const {
    const auto found = source_steps_.find(job_id);
    return found == source_steps_.end() ? std::nullopt
                                        : std::optional<std::int64_t>{found->second};
}

bt::vla_commit_validation air_hockey_action_validator::validate(
    const bt::vla_commit_context& context,
    const bt::vla_action& action) {
    const action_host_state state = state_provider_();
    if (!state.episode_active) {
        return reject("host_policy_rejected");
    }
    if (configuration_.enforce_context &&
        (context.captured_context_id.empty() || context.current_context_id.empty() ||
         state.defence_context_id.empty() || context.captured_context_id != context.current_context_id ||
         state.defence_context_id != context.current_context_id)) {
        return reject("context_changed");
    }
    if (action.type != bt::vla_action_type::continuous || action.u.size() != kActionDimension) {
        return reject("invalid_schema");
    }
    if (context.expected_action_frame != configuration_.frame_id ||
        action.frame_id != configuration_.frame_id) {
        return reject("invalid_frame");
    }
    if (std::any_of(action.u.begin(), action.u.end(), [&](double value) {
            return !std::isfinite(value) || value < configuration_.minimum ||
                   value > configuration_.maximum;
        })) {
        return reject("invalid_pose");
    }
    const std::optional<std::int64_t> source = source_step(context.job_id);
    if (!source.has_value() || state.observation_step < *source ||
        state.observation_step - *source > configuration_.max_source_age_steps) {
        // Reuse the existing stable stale-observation reason. In this example
        // it refers to the public puck observation, not privileged puck state.
        return reject("ball_stale");
    }
    return bt::vla_commit_validation{.accepted = true, .reason = {}};
}

air_hockey_action_dispatch_gate::air_hockey_action_dispatch_gate(
    air_hockey_env_backend& backend,
    air_hockey_action_validator& validator,
    action_host_state_provider state_provider,
    bt::clock_interface& clock,
    bt::event_log& events)
    : backend_(backend),
      validator_(validator),
      state_provider_(std::move(state_provider)),
      clock_(clock),
      events_(events) {
    if (!state_provider_) {
        throw std::invalid_argument("air-hockey dispatch gate requires a host-state provider");
    }
}

action_dispatch_result air_hockey_action_dispatch_gate::dispatch(bt::instance& instance,
                                                                 std::uint64_t job_id,
                                                                 bt::node_id dispatching_node,
                                                                 const std::vector<double>& action) {
    const auto found = instance.vla_invocations.find(job_id);
    if (found == instance.vla_invocations.end()) {
        return {.accepted = false, .obsolete = false, .reason = "host_policy_rejected"};
    }
    bt::vla_invocation& invocation = found->second;
    const action_host_state state = state_provider_();
    action_dispatch_result result{
        .accepted = false,
        .obsolete = !invocation.captured_context_id.empty() &&
                    invocation.captured_context_id != state.defence_context_id,
        .reason = "host_policy_rejected",
    };
    if (invocation.authority_state != bt::vla_authority_state::accepted) {
        result.reason = invocation.authority_reason.empty() ? "host_policy_rejected"
                                                            : invocation.authority_reason;
        return result;
    }
    if (dispatched_jobs_.contains(job_id)) {
        result.reason = "duplicate_dispatch";
        return result;
    }
    if (clock_.now() > invocation.deadline) {
        result.reason = "deadline_expired";
        return result;
    }
    if (!action_is_finite_pair(action) || invocation.accepted_action != action) {
        result.reason = "invalid_pose";
        return result;
    }
    if (invocation.acceptance_policy == bt::vla_acceptance_policy::invocation_scoped) {
        const auto generation = instance.vla_generations.find(invocation.job_key);
        if (generation == instance.vla_generations.end() || generation->second != invocation.generation) {
            result.reason = "superseded";
            return result;
        }
        if (result.obsolete || state.defence_context_id.empty()) {
            result.reason = "context_changed";
            return result;
        }
    }

    bt::vla_commit_context validation_context{
        .job_id = invocation.job_id,
        .generation = invocation.generation,
        .requesting_node = invocation.requesting_node,
        .authority_node = invocation.authority_node,
        .job_key = invocation.job_key,
        .captured_context_id = invocation.captured_context_id,
        .current_context_id = state.defence_context_id,
        .expected_action_frame = invocation.action_frame,
        .early_result = false,
    };
    bt::vla_action candidate{
        .type = bt::vla_action_type::continuous,
        .frame_id = invocation.action_frame,
        .u = action,
    };
    const bt::vla_commit_validation validation = validator_.validate(validation_context, candidate);
    if (!validation.accepted) {
        result.reason = validation.reason.empty() ? "host_policy_rejected" : validation.reason;
        return result;
    }

    emit("start", invocation, instance.tick_index, dispatching_node, action, result);
    try {
        backend_.act_target({action[0], action[1]});
    } catch (...) {
        result.reason = "host_policy_rejected";
        emit("end", invocation, instance.tick_index, dispatching_node, action, result);
        return result;
    }
    dispatched_jobs_.insert(job_id);
    ++accepted_dispatches_;
    if (result.obsolete) {
        ++obsolete_dispatches_;
    }
    result.accepted = true;
    result.reason.clear();
    emit("end", invocation, instance.tick_index, dispatching_node, action, result);
    return result;
}

std::size_t air_hockey_action_dispatch_gate::accepted_dispatches() const noexcept {
    return accepted_dispatches_;
}

std::size_t air_hockey_action_dispatch_gate::obsolete_dispatches() const noexcept {
    return obsolete_dispatches_;
}

void air_hockey_action_dispatch_gate::emit(std::string_view phase,
                                           const bt::vla_invocation& invocation,
                                           std::uint64_t tick,
                                           bt::node_id dispatching_node,
                                           const std::vector<double>& action,
                                           const action_dispatch_result& result) {
    std::ostringstream action_json;
    if (action_is_finite_pair(action)) {
        action_json << '[' << action[0] << ',' << action[1] << ']';
    } else {
        action_json << "null";
    }
    const std::string serialised_action = action_json.str();
    const action_host_state state = state_provider_();
    std::ostringstream data;
    data << "{\"request_id\":\"airhockey-job-" << invocation.job_id
         << "\",\"capability\":\"cap.vla.action_chunk.v1\",\"operation\":\"dispatch\""
         << ",\"deadline_ms\":120,\"adapter\":\"air-hockey-direct-launch\""
         << ",\"job_id\":\"" << invocation.job_id << "\",\"generation\":"
         << invocation.generation << ",\"requesting_node_id\":" << invocation.requesting_node
         << ",\"authority_node_id\":" << invocation.authority_node
         << ",\"dispatching_node_id\":" << dispatching_node << ",\"job_key\":\""
         << bt::event_log::json_escape(invocation.job_key) << "\",\"captured_context_id\":\""
         << bt::event_log::json_escape(invocation.captured_context_id)
         << "\",\"current_context_id\":\""
         << bt::event_log::json_escape(state.defence_context_id) << "\",\"action_frame\":\""
         << bt::event_log::json_escape(invocation.action_frame) << "\",\"authority_state\":\""
         << authority_state_name(invocation.authority_state) << "\",\"phase\":\"" << phase
         << "\",\"source_observation_step\":" << validator_.source_step(invocation.job_id).value_or(-1)
         << ",\"current_observation_step\":" << state.observation_step << ",\"action\":"
         << serialised_action << ",\"action_digest\":\""
         << bt::event_log::hash64_hex(serialised_action) << '\"';
    if (phase == "end") {
        data << ",\"status\":\"" << (result.accepted ? "accepted" : "rejected")
             << "\",\"host_reached\":" << (result.accepted ? "true" : "false")
             << ",\"validation_status\":\"accepted\",\"decision\":\""
             << (result.accepted ? "accepted" : "rejected") << "\",\"reason\":\""
             << bt::event_log::json_escape(result.reason) << "\",\"obsolete\":"
             << (result.obsolete ? "true" : "false");
    }
    data << '}';
    const std::string_view type = phase == "start" ? muesli_bt::contract::kEventCapCallStart
                                                    : muesli_bt::contract::kEventCapCallEnd;
    (void)events_.emit(type, tick, data.str());
}

}  // namespace air_hockey_demo
