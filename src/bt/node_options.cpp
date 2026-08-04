#include "bt/node_options.hpp"

#include <array>

namespace bt {
namespace {

using namespace std::string_view_literals;

constexpr std::array<std::string_view, 3> kPlannerValues{":mcts"sv, ":mppi"sv, ":ilqr"sv};
constexpr std::array<std::string_view, 1> kWorkMaxAliases{":iters_max"sv};
constexpr std::array<std::string_view, 1> kSafeActionAliases{":fallback_action"sv};
constexpr std::array<std::string_view, 1> kDeadlineAliases{":budget_ms"sv};
constexpr std::array<std::string_view, 2> kVlaAcceptancePolicyValues{"deadline_only"sv, "invocation_scoped"sv};

constexpr std::array<node_option_spec, 34> kPlanActionOptions{{
    {":name", option_value_kind::text, ""},
    {":planner", option_value_kind::text, ":mcts", false, {}, kPlannerValues},
    {":budget_ms", option_value_kind::integer, "20"},
    {":work_max", option_value_kind::integer, "0", false, kWorkMaxAliases},
    {":horizon", option_value_kind::integer, "0"},
    {":dt_ms", option_value_kind::integer, "0"},
    {":model_service", option_value_kind::text, "toy-1d"},
    {":state_key", option_value_kind::text, "state"},
    {":action_key", option_value_kind::text, "action"},
    {":meta_key", option_value_kind::text, ""},
    {":seed_key", option_value_kind::text, ""},
    {":safe_action_key", option_value_kind::text, ""},
    {":safe_action", option_value_kind::number, "", false, kSafeActionAliases},
    {":action_schema", option_value_kind::text, ""},
    {":top_k", option_value_kind::integer, "3"},
    {":gamma", option_value_kind::number, "0.95"},
    {":max_depth", option_value_kind::integer, "25"},
    {":c_ucb", option_value_kind::number, "1.2"},
    {":pw_k", option_value_kind::number, "2.0"},
    {":pw_alpha", option_value_kind::number, "0.5"},
    {":rollout_policy", option_value_kind::text, "model_default"},
    {":action_sampler", option_value_kind::text, "model_default"},
    {":lambda", option_value_kind::number, "1.0"},
    {":sigma", option_value_kind::number, ""},
    {":sigma_key", option_value_kind::text, ""},
    {":n_samples", option_value_kind::integer, "128"},
    {":n_elite", option_value_kind::integer, "0"},
    {":max_iters", option_value_kind::integer, "30"},
    {":reg_init", option_value_kind::number, "1.0"},
    {":reg_factor", option_value_kind::number, "10.0"},
    {":tol_cost", option_value_kind::number, "1.0e-4"},
    {":tol_grad", option_value_kind::number, "1.0e-4"},
    {":fd_eps", option_value_kind::number, "1.0e-4"},
    {":derivatives", option_value_kind::text, ":analytic"},
}};

constexpr std::array<node_option_spec, 5> kPlanActionConstraintOptions{{
    {":max_du", option_value_kind::number, ""},
    {":max_du_key", option_value_kind::text, ""},
    {":smoothness_weight", option_value_kind::number, ""},
    {":collision_weight", option_value_kind::number, ""},
    {":goal_tolerance", option_value_kind::number, ""},
}};

constexpr std::array<node_option_spec, 26> kVlaRequestOptions{{
    {":name", option_value_kind::text, ""},
    {":job_key", option_value_kind::text, "<name>.job_id"},
    {":instruction", option_value_kind::text, ""},
    {":instruction_key", option_value_kind::text, "instruction"},
    {":task_id", option_value_kind::text, "task"},
    {":task_key", option_value_kind::text, ""},
    {":state_key", option_value_kind::text, "state"},
    {":image_key", option_value_kind::text, ""},
    {":blob_key", option_value_kind::text, ""},
    {":capability", option_value_kind::text, "vla.rt2"},
    {":model_name", option_value_kind::text, "rt2-stub"},
    {":model_version", option_value_kind::text, "stub-1"},
    {":frame_id", option_value_kind::text, "base"},
    {":action_frame", option_value_kind::text, ""},
    {":deadline_ms", option_value_kind::integer, "20", false, kDeadlineAliases},
    {":acceptance_policy", option_value_kind::text, "deadline_only", false, {}, kVlaAcceptancePolicyValues},
    {":context_key", option_value_kind::text, ""},
    {":dims", option_value_kind::integer, "state dimension"},
    {":bound_lo", option_value_kind::number, "-1.0"},
    {":bound_hi", option_value_kind::number, "1.0"},
    {":max_abs", option_value_kind::number, "1.0"},
    {":max_delta", option_value_kind::number, "1.0"},
    {":forbidden_lo", option_value_kind::number, ""},
    {":forbidden_hi", option_value_kind::number, ""},
    {":seed_key", option_value_kind::text, ""},
    {":seed", option_value_kind::text, ""},
}};

constexpr std::array<node_option_spec, 7> kVlaWaitOptions{{
    {":name", option_value_kind::text, ""},
    {":job_key", option_value_kind::text, "<name>.job_id"},
    {":action_key", option_value_kind::text, "action"},
    {":meta_key", option_value_kind::text, ""},
    {":early_commit", option_value_kind::boolean, "false"},
    {":early_confidence", option_value_kind::number, "1.1"},
    {":cancel_on_early_commit", option_value_kind::boolean, "true"},
}};

constexpr std::array<node_option_spec, 1> kVlaWaitClearJobOption{{
    {":clear_job", option_value_kind::boolean, "true"},
}};

constexpr std::array<node_option_spec, 2> kVlaCancelOptions{{
    {":name", option_value_kind::text, ""},
    {":job_key", option_value_kind::text, "<name>.job_id"},
}};

template <std::size_t Left, std::size_t Right>
constexpr auto concat_options(const std::array<node_option_spec, Left>& left,
                              const std::array<node_option_spec, Right>& right) {
    std::array<node_option_spec, Left + Right> out{};
    for (std::size_t i = 0; i < Left; ++i) {
        out[i] = left[i];
    }
    for (std::size_t i = 0; i < Right; ++i) {
        out[Left + i] = right[i];
    }
    return out;
}

constexpr auto kPlanActionAllOptions = concat_options(kPlanActionOptions, kPlanActionConstraintOptions);
constexpr auto kVlaWaitAllOptions = concat_options(kVlaWaitOptions, kVlaWaitClearJobOption);

constexpr std::array<node_option_schema, 4> kSchemas{{
    {"plan-action", kPlanActionAllOptions},
    {"vla-request", kVlaRequestOptions},
    {"vla-wait", kVlaWaitAllOptions},
    {"vla-cancel", kVlaCancelOptions},
}};

bool option_name_matches(const node_option_spec& spec, std::string_view option_name) noexcept {
    if (spec.name == option_name) {
        return true;
    }
    for (std::string_view alias : spec.aliases) {
        if (alias == option_name) {
            return true;
        }
    }
    return false;
}

}  // namespace

const node_option_schema* find_node_option_schema(std::string_view node_name) noexcept {
    for (const node_option_schema& schema : kSchemas) {
        if (schema.node_name == node_name) {
            return &schema;
        }
    }
    return nullptr;
}

const node_option_spec* find_node_option_spec(const node_option_schema& schema, std::string_view option_name) noexcept {
    for (const node_option_spec& spec : schema.options) {
        if (option_name_matches(spec, option_name)) {
            return &spec;
        }
    }
    return nullptr;
}

std::string_view canonical_node_option_name(const node_option_schema& schema, std::string_view option_name) noexcept {
    if (const node_option_spec* spec = find_node_option_spec(schema, option_name); spec) {
        return spec->name;
    }
    return {};
}

}  // namespace bt
