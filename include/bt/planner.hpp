#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
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
};

struct planner_config {
    std::int64_t budget_ms = 20;
    std::int64_t iters_max = 2000;
    double gamma = 0.95;
    std::int64_t max_depth = 25;
    double c_ucb = 1.2;
    double pw_k = 2.0;
    double pw_alpha = 0.5;
    std::int64_t time_check_interval = 8;
    std::int64_t top_k = 3;
    planner_vector fallback_action{};
    std::string rollout_policy = "model_default";
    std::string action_sampler = "model_default";
    planner_vector action_prior_mean{};
    double action_prior_sigma = 0.2;
    double action_prior_mix = 0.5;
};

enum class planner_status {
    ok,
    timeout,
    noaction,
    error
};

struct planner_top_choice {
    planner_vector action;
    std::int64_t visits = 0;
    double q = 0.0;
};

struct planner_stats {
    std::int64_t iters = 0;
    std::int64_t root_visits = 0;
    std::int64_t root_children = 0;
    std::int64_t widen_added = 0;
    std::int64_t depth_max = 0;
    double depth_mean = 0.0;
    double time_used_ms = 0.0;
    double value_est = 0.0;
    std::uint64_t seed = 0;
    std::vector<planner_top_choice> top_k{};
};

struct planner_request {
    std::string model_service = "toy-1d";
    planner_vector state;
    planner_config config;
    std::uint64_t seed = 0;
    std::string run_id = "default";
    std::uint64_t tick_index = 0;
    std::string node_name = "planner";
    std::string state_key;
};

struct planner_result {
    planner_status status = planner_status::error;
    planner_vector action;
    double confidence = 0.0;
    planner_stats stats{};
    std::string error;
};

struct planner_record {
    std::int64_t ts_ms = 0;
    std::string run_id;
    std::uint64_t tick_index = 0;
    std::string node_name;
    std::int64_t budget_ms = 0;
    double time_used_ms = 0.0;
    std::int64_t iters = 0;
    std::int64_t root_visits = 0;
    std::int64_t root_children = 0;
    std::int64_t widen_added = 0;
    planner_vector action;
    double confidence = 0.0;
    double value_est = 0.0;
    planner_status status = planner_status::error;
    std::int64_t depth_max = 0;
    double depth_mean = 0.0;
    std::uint64_t seed = 0;
    std::string state_key;
    std::vector<planner_top_choice> top_k{};
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
    std::string jsonl_path_ = "planner-records.jsonl";

    mutable std::mutex mutex_;
    mutable std::mutex file_mutex_;
};

[[nodiscard]] const char* planner_status_name(planner_status status) noexcept;

}  // namespace bt
