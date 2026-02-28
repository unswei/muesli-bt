#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bt {

using planner_vector = std::vector<double>;

class planner_rng {
public:
    explicit planner_rng(std::uint64_t seed);

    [[nodiscard]] double uniform(double lo, double hi);
    [[nodiscard]] std::int64_t uniform_int(std::int64_t n);
    [[nodiscard]] double normal(double mu, double sigma);

private:
    [[nodiscard]] std::uint64_t next_u64();
    [[nodiscard]] double next_unit();

    std::uint64_t state_ = 0;
    bool has_spare_normal_ = false;
    double spare_normal_ = 0.0;
};

struct planner_step_result {
    planner_vector next_state;
    double reward = 0.0;
    bool done = false;
};

struct planner_bound {
    double lo = -1.0;
    double hi = 1.0;
};

struct planner_action {
    std::string action_schema;
    planner_vector u;
};

struct planner_constraints {
    planner_vector max_du;
    double smoothness_weight = 0.0;
    double collision_weight = 0.0;
    double goal_tolerance = 0.0;
    bool has_smoothness_weight = false;
    bool has_collision_weight = false;
    bool has_goal_tolerance = false;
};

enum class planner_backend {
    mcts,
    mppi,
    ilqr
};

enum class planner_ilqr_derivatives_mode {
    analytic,
    finite_diff
};

struct planner_mcts_config {
    double c_ucb = 1.2;
    double pw_k = 2.0;
    double pw_alpha = 0.5;
    std::int64_t max_depth = 25;
    double gamma = 0.95;
    std::string rollout_policy = "model_default";
    std::string action_sampler = "model_default";
    std::int64_t default_iters = 2000;
    std::int64_t time_check_interval = 8;
};

struct planner_mppi_config {
    double lambda = 1.0;
    planner_vector sigma{};
    std::int64_t n_samples = 128;
    std::int64_t n_elite = 0;
    planner_vector u_init{};
    planner_vector u_nominal{};
};

struct planner_ilqr_config {
    std::int64_t max_iters = 30;
    double reg_init = 1.0;
    double reg_factor = 10.0;
    double tol_cost = 1.0e-4;
    double tol_grad = 1.0e-4;
    planner_vector u_init{};
    planner_ilqr_derivatives_mode derivatives = planner_ilqr_derivatives_mode::analytic;
    double fd_eps = 1.0e-4;
};

struct planner_linearisation {
    std::size_t state_dim = 0;
    std::size_t action_dim = 0;
    std::vector<double> A;
    std::vector<double> B;
};

struct planner_quadratic_cost {
    double l0 = 0.0;
    planner_vector l_x;
    planner_vector l_u;
    std::vector<double> l_xx;
    std::vector<double> l_uu;
    std::vector<double> l_xu;
};

struct planner_terminal_quadratic_cost {
    double l0 = 0.0;
    planner_vector l_x;
    std::vector<double> l_xx;
};

class planner_model {
public:
    virtual ~planner_model() = default;

    [[nodiscard]] virtual planner_step_result step(const planner_vector& state,
                                                   const planner_vector& action,
                                                   planner_rng& rng) const = 0;
    [[nodiscard]] virtual planner_vector sample_action(const planner_vector& state, planner_rng& rng) const = 0;
    [[nodiscard]] virtual planner_vector rollout_action(const planner_vector& state, planner_rng& rng) const;
    [[nodiscard]] virtual planner_vector clamp_action(const planner_vector& action) const = 0;
    [[nodiscard]] virtual planner_vector zero_action() const = 0;
    [[nodiscard]] virtual bool validate_state(const planner_vector& state) const;
    [[nodiscard]] virtual std::size_t action_dims() const = 0;

    [[nodiscard]] virtual std::vector<planner_bound> action_bounds() const;
    [[nodiscard]] virtual std::int64_t default_horizon() const;
    [[nodiscard]] virtual std::int64_t default_dt_ms() const;

    [[nodiscard]] virtual bool rollout_cost(const planner_vector& state,
                                            const std::vector<planner_vector>& action_sequence,
                                            double& cost) const;

    [[nodiscard]] virtual bool deterministic_step(const planner_vector& state,
                                                  const planner_vector& action,
                                                  planner_step_result& out) const;

    [[nodiscard]] virtual bool linearise_dynamics(const planner_vector& state,
                                                  const planner_vector& action,
                                                  planner_linearisation& out) const;

    [[nodiscard]] virtual bool quadraticise_cost(const planner_vector& state,
                                                 const planner_vector& action,
                                                 planner_quadratic_cost& out) const;

    [[nodiscard]] virtual bool quadraticise_terminal_cost(const planner_vector& state,
                                                          planner_terminal_quadratic_cost& out) const;
};

enum class planner_status {
    ok,
    timeout,
    noaction,
    error
};

struct planner_top_choice_mcts {
    planner_action action;
    std::int64_t visits = 0;
    double q = 0.0;
};

struct planner_top_choice_mppi {
    planner_action action;
    double weight = 0.0;
    double cost = 0.0;
};

struct planner_trace_mcts {
    bool available = false;
    std::int64_t root_visits = 0;
    std::int64_t root_children = 0;
    std::int64_t widen_added = 0;
    std::vector<planner_top_choice_mcts> top_k{};
};

struct planner_trace_mppi {
    bool available = false;
    std::int64_t n_samples = 0;
    std::int64_t horizon = 0;
    std::vector<planner_top_choice_mppi> top_k{};
};

struct planner_trace_ilqr {
    bool available = false;
    std::int64_t iters = 0;
    double cost_init = 0.0;
    double cost_final = 0.0;
    double reg_final = 0.0;
    std::vector<planner_top_choice_mppi> top_k{};
};

struct planner_trace {
    planner_trace_mcts mcts;
    planner_trace_mppi mppi;
    planner_trace_ilqr ilqr;
};

struct planner_stats {
    std::int64_t budget_ms = 0;
    std::int64_t time_used_ms = 0;
    std::int64_t work_done = 0;
    std::uint64_t seed = 0;
    bool overrun = false;
    std::string note;
};

struct planner_request {
    std::string schema_version = "planner.request.v1";
    planner_backend planner = planner_backend::mcts;
    std::string model_service = "toy-1d";
    planner_vector state;

    std::int64_t budget_ms = 20;
    std::int64_t work_max = 0;
    std::int64_t horizon = 0;
    std::int64_t dt_ms = 0;

    std::vector<planner_bound> bounds{};
    planner_constraints constraints{};
    std::int64_t top_k = 3;

    std::uint64_t seed = 0;
    planner_action safe_action{};
    std::string action_schema;

    planner_mcts_config mcts{};
    planner_mppi_config mppi{};
    planner_ilqr_config ilqr{};

    std::string run_id = "default";
    std::uint64_t tick_index = 0;
    std::string node_name = "planner";
    std::string state_key;
};

struct planner_result {
    std::string schema_version = "planner.result.v1";
    planner_backend planner = planner_backend::mcts;
    planner_status status = planner_status::error;
    planner_action action;
    double confidence = 0.0;
    planner_stats stats{};
    planner_trace trace{};
    std::string error;
};

struct planner_record {
    std::string schema_version = "planner.v1";
    std::int64_t ts_ms = 0;
    std::string run_id;
    std::uint64_t tick_index = 0;
    std::string node_name;
    planner_backend planner = planner_backend::mcts;
    planner_status status = planner_status::error;
    std::int64_t budget_ms = 0;
    std::int64_t time_used_ms = 0;
    std::int64_t work_done = 0;
    planner_action action;
    double confidence = 0.0;
    std::uint64_t seed = 0;
    bool overrun = false;
    std::string note;
    planner_trace trace{};
    std::string state_key;
};

class planner_service {
public:
    planner_service();

    void register_model(std::string name, std::shared_ptr<planner_model> model);
    [[nodiscard]] bool has_model(std::string_view name) const;

    [[nodiscard]] planner_result plan(const planner_request& request);

    [[nodiscard]] std::uint64_t base_seed() const noexcept;
    void set_base_seed(std::uint64_t seed) noexcept;

    [[nodiscard]] std::uint64_t derive_seed(std::string_view node_name, std::uint64_t tick_index) const;

    [[nodiscard]] static std::uint64_t hash64(std::string_view text) noexcept;

    void set_jsonl_path(std::string path);
    [[nodiscard]] const std::string& jsonl_path() const noexcept;
    void set_jsonl_enabled(bool enabled) noexcept;
    [[nodiscard]] bool jsonl_enabled() const noexcept;

    [[nodiscard]] std::vector<planner_record> recent_records(std::size_t max_count) const;
    [[nodiscard]] std::string dump_recent_records(std::size_t max_count) const;
    void clear_records();

private:
    void append_record(planner_record record);
    [[nodiscard]] std::string record_to_json(const planner_record& record) const;
    void append_jsonl_line(const std::string& line);

    std::unordered_map<std::string, std::shared_ptr<planner_model>> models_;

    std::uint64_t base_seed_ = 0x4d6f6f736c694254ull;

    std::vector<planner_record> records_;
    std::size_t record_capacity_ = 4096;

    bool jsonl_enabled_ = true;
    std::string jsonl_path_ = "logs/planner-records.jsonl";

    mutable std::mutex mutex_;
    mutable std::mutex file_mutex_;
};

[[nodiscard]] const char* planner_status_name(planner_status status) noexcept;
[[nodiscard]] const char* planner_backend_name(planner_backend planner) noexcept;
[[nodiscard]] const char* planner_ilqr_derivatives_mode_name(planner_ilqr_derivatives_mode mode) noexcept;
[[nodiscard]] bool planner_backend_from_string(std::string_view text, planner_backend& out) noexcept;
[[nodiscard]] bool planner_ilqr_derivatives_mode_from_string(std::string_view text,
                                                             planner_ilqr_derivatives_mode& out) noexcept;

}  // namespace bt
