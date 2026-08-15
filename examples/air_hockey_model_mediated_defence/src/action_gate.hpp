#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bt/event_log.hpp"
#include "bt/instance.hpp"
#include "bt/vla.hpp"
#include "env_backend.hpp"

namespace air_hockey_demo {

struct action_host_state {
    std::string defence_context_id;
    std::int64_t observation_step = 0;
    bool episode_active = false;
};

struct action_validator_config {
    std::string frame_id = "airhockey.normalised_mallet_target.v1";
    double minimum = -1.0;
    double maximum = 1.0;
    std::int64_t max_source_age_steps = 6;
    bool enforce_context = true;
};

using action_host_state_provider = std::function<action_host_state()>;

class air_hockey_action_validator final : public bt::vla_commit_validator {
public:
    air_hockey_action_validator(action_validator_config configuration,
                                action_host_state_provider state_provider);

    void record_source_step(std::uint64_t job_id, std::int64_t observation_step);
    [[nodiscard]] std::optional<std::int64_t> source_step(std::uint64_t job_id) const;

    bt::vla_commit_validation validate(const bt::vla_commit_context& context,
                                       const bt::vla_action& action) override;

private:
    action_validator_config configuration_;
    action_host_state_provider state_provider_;
    std::unordered_map<std::uint64_t, std::int64_t> source_steps_;
};

struct action_dispatch_result {
    bool accepted = false;
    bool obsolete = false;
    std::string reason = "host_policy_rejected";
};

class air_hockey_action_dispatch_gate {
public:
    air_hockey_action_dispatch_gate(air_hockey_env_backend& backend,
                                    air_hockey_action_validator& validator,
                                    action_host_state_provider state_provider,
                                    bt::clock_interface& clock,
                                    bt::event_log& events);

    action_dispatch_result dispatch(bt::instance& instance,
                                    std::uint64_t job_id,
                                    bt::node_id dispatching_node,
                                    const std::vector<double>& action);

    [[nodiscard]] std::size_t accepted_dispatches() const noexcept;
    [[nodiscard]] std::size_t obsolete_dispatches() const noexcept;

private:
    void emit(std::string_view phase,
              const bt::vla_invocation& invocation,
              std::uint64_t tick,
              bt::node_id dispatching_node,
              const std::vector<double>& action,
              const action_dispatch_result& result);

    air_hockey_env_backend& backend_;
    air_hockey_action_validator& validator_;
    action_host_state_provider state_provider_;
    bt::clock_interface& clock_;
    bt::event_log& events_;
    std::unordered_set<std::uint64_t> dispatched_jobs_;
    std::size_t accepted_dispatches_ = 0;
    std::size_t obsolete_dispatches_ = 0;
};

}  // namespace air_hockey_demo
