#pragma once

#include <any>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bt/ast.hpp"
#include "bt/blackboard.hpp"
#include "bt/event_log.hpp"
#include "bt/logging.hpp"
#include "bt/profile.hpp"
#include "bt/scheduler.hpp"
#include "bt/status.hpp"
#include "bt/trace.hpp"

namespace bt {

struct tick_context;
class planner_service;
class vla_service;
class vla_commit_validator;

struct node_memory {
    std::int64_t i0 = 0;
    std::int64_t i1 = 0;
    bool b0 = false;
    std::any payload;
};

enum class vla_acceptance_policy {
    deadline_only,
    invocation_scoped,
};

enum class vla_authority_state {
    active,
    revoked,
    accepted,
    rejected,
};

struct vla_invocation {
    std::uint64_t job_id = 0;
    std::uint64_t generation = 0;
    node_id requesting_node = 0;
    node_id authority_node = 0;
    std::string job_key;
    std::string action_key;
    std::string meta_key;
    std::string context_key;
    std::string captured_context_id;
    std::string action_frame;
    std::vector<double> accepted_action;
    std::int64_t action_dims = 0;
    std::chrono::steady_clock::time_point submitted_at{};
    std::chrono::steady_clock::time_point deadline{};
    vla_acceptance_policy acceptance_policy = vla_acceptance_policy::deadline_only;
    vla_authority_state authority_state = vla_authority_state::active;
    std::string authority_reason;
    bool dispatch_authority_active = true;
    std::string dispatch_authority_reason;
    bool cancel_requested = false;
    bool walking_target_dispatched = false;
};

struct observability {
    trace_buffer* trace = nullptr;
    log_sink* logger = nullptr;
    event_log* events = nullptr;
};

class clock_interface {
public:
    virtual ~clock_interface() = default;
    virtual std::chrono::steady_clock::time_point now() const = 0;
};

class robot_interface {
public:
    virtual ~robot_interface() = default;

    virtual bool battery_ok(tick_context& ctx) = 0;
    virtual bool target_visible(tick_context& ctx) = 0;

    virtual status approach_target(tick_context& ctx, node_memory& mem) = 0;
    virtual status grasp(tick_context& ctx, node_memory& mem) = 0;
    virtual status search_target(tick_context& ctx, node_memory& mem) = 0;
};

struct services {
    scheduler* sched = nullptr;
    observability obs{};
    clock_interface* clock = nullptr;
    robot_interface* robot = nullptr;
    planner_service* planner = nullptr;
    vla_service* vla = nullptr;
    vla_commit_validator* vla_commit = nullptr;
};

struct subtree_install_request {
    std::string proposal_id;
    std::string source;
    std::string slot;
    std::string fragment_contract;
    std::string install_mode = "at-tick-boundary";
    std::string validation_status;
    std::string source_hash;
    std::string canonical_dsl_hash;
    std::string validation_result_hash;
    definition fragment;
};

struct subtree_rollback_request {
    std::string rollback_id;
    std::string slot;
    std::string installed_subtree_hash;
    std::string previous_subtree_hash;
};

struct subtree_install_result {
    bool queued = false;
    std::string reason_code;
    std::string slot;
    std::string proposal_id;
    std::string source_hash;
    std::string canonical_dsl_hash;
    std::string validation_result_hash;
    std::string old_subtree_hash;
    std::string new_subtree_hash;
    std::string rollback_id;
};

struct subtree_slot_rollback_state {
    std::string rollback_id;
    std::string proposal_id;
    std::string slot;
    std::string previous_subtree_hash;
    std::string installed_subtree_hash;
    definition previous_definition;
};

struct instance {
    explicit instance(const definition* definition_ptr = nullptr, std::size_t trace_capacity = 4096);

    const definition* def = nullptr;
    std::unique_ptr<definition> owned_definition;
    std::int64_t instance_handle = 0;
    std::unordered_map<node_id, node_memory> memory;
    std::unordered_map<node_id, std::uint64_t> active_vla_jobs;
    std::unordered_map<std::uint64_t, vla_invocation> vla_invocations;
    std::unordered_map<std::string, std::uint64_t> vla_generations;
    std::unordered_set<node_id> halt_warning_emitted;
    blackboard bb;
    std::uint64_t tick_index = 0;

    bool trace_enabled = true;
    bool read_trace_enabled = false;

    tree_profile_stats tree_stats{};
    std::unordered_map<node_id, node_profile_stats> node_stats;
    std::vector<node_id> halt_stack;
    std::optional<subtree_install_request> pending_subtree_install;
    std::optional<subtree_rollback_request> pending_subtree_rollback;
    std::unordered_map<std::string, subtree_slot_rollback_state> subtree_rollbacks;

    trace_buffer trace;
};

}  // namespace bt
