#include "bt/planner.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace bt {
namespace {

std::uint64_t splitmix64_next(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30u)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27u)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31u);
}

double clamp_double(double value, double lo, double hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

bool vector_all_finite(const planner_vector& values) {
    for (double v : values) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    return true;
}

std::string json_escape(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string vector_to_json(const planner_vector& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << values[i];
    }
    out << ']';
    return out.str();
}

class toy_1d_model final : public planner_model {
public:
    planner_step_result step(const planner_vector& state, const planner_vector& action, planner_rng&) const override {
        if (state.empty()) {
            throw std::runtime_error("toy-1d.step: expected non-empty state");
        }
        if (action.empty()) {
            throw std::runtime_error("toy-1d.step: expected non-empty action");
        }

        const double x = state[0];
        const double a = clamp_double(action[0], -1.0, 1.0);
        const double x2 = x + 0.25 * a;
        const double goal = 1.0;
        const double err = goal - x2;

        planner_step_result out;
        out.next_state = {x2};
        out.reward = -std::fabs(err);
        out.done = std::fabs(err) < 0.05;
        return out;
    }

    planner_vector sample_action(const planner_vector&, planner_rng& rng) const override {
        return {rng.uniform(-1.0, 1.0)};
    }

    planner_vector rollout_action(const planner_vector& state, planner_rng& rng) const override {
        const double x = state.empty() ? 0.0 : state[0];
        const double dir = (x < 1.0) ? 1.0 : -1.0;
        const double noise = rng.normal(0.0, 0.25);
        return {clamp_double(0.8 * dir + noise, -1.0, 1.0)};
    }

    planner_vector clamp_action(const planner_vector& action) const override {
        if (action.empty()) {
            return {0.0};
        }
        return {clamp_double(action[0], -1.0, 1.0)};
    }

    planner_vector zero_action() const override {
        return {0.0};
    }

    bool validate_state(const planner_vector& state) const override {
        return !state.empty() && std::isfinite(state[0]);
    }

    std::size_t action_dims() const override {
        return 1;
    }
};

class ptz_track_model final : public planner_model {
public:
    planner_step_result step(const planner_vector& state, const planner_vector& action, planner_rng& rng) const override {
        if (state.size() < 4) {
            throw std::runtime_error("ptz-track.step: expected state [pan tilt ball_x ball_y ...]");
        }
        if (action.size() < 2) {
            throw std::runtime_error("ptz-track.step: expected action [dpan dtilt]");
        }

        const double pan = state[0];
        const double tilt = state[1];
        const double ball_x = state[2];
        const double ball_y = state[3];
        const double ball_vx = state.size() > 4 ? state[4] : 0.0;
        const double ball_vy = state.size() > 5 ? state[5] : 0.0;

        const double dpan = clamp_double(action[0], -0.25, 0.25);
        const double dtilt = clamp_double(action[1], -0.25, 0.25);

        const double pan2 = clamp_double(pan + dpan, -1.5, 1.5);
        const double tilt2 = clamp_double(tilt + dtilt, -1.0, 1.0);

        const double noise_x = rng.normal(0.0, 0.01);
        const double noise_y = rng.normal(0.0, 0.01);
        const double bx2 = clamp_double(ball_x + ball_vx - dpan * 1.15 + noise_x, -2.0, 2.0);
        const double by2 = clamp_double(ball_y + ball_vy - dtilt * 1.15 + noise_y, -2.0, 2.0);

        const double dist = std::sqrt(bx2 * bx2 + by2 * by2);
        const double effort = std::fabs(dpan) + std::fabs(dtilt);

        planner_step_result out;
        out.next_state = {pan2, tilt2, bx2, by2, ball_vx, ball_vy};
        out.reward = -dist - 0.05 * effort;
        out.done = dist < 0.03;
        return out;
    }

    planner_vector sample_action(const planner_vector&, planner_rng& rng) const override {
        return {rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.25)};
    }

    planner_vector rollout_action(const planner_vector& state, planner_rng& rng) const override {
        if (state.size() < 4) {
            return {0.0, 0.0};
        }
        const double bx = state[2];
        const double by = state[3];
        const double dpan = clamp_double(0.65 * bx + rng.normal(0.0, 0.03), -0.25, 0.25);
        const double dtilt = clamp_double(0.65 * by + rng.normal(0.0, 0.03), -0.25, 0.25);
        return {dpan, dtilt};
    }

    planner_vector clamp_action(const planner_vector& action) const override {
        const double a0 = action.empty() ? 0.0 : action[0];
        const double a1 = action.size() > 1 ? action[1] : 0.0;
        return {clamp_double(a0, -0.25, 0.25), clamp_double(a1, -0.25, 0.25)};
    }

    planner_vector zero_action() const override {
        return {0.0, 0.0};
    }

    bool validate_state(const planner_vector& state) const override {
        return state.size() >= 4 && vector_all_finite(state);
    }

    std::size_t action_dims() const override {
        return 2;
    }
};

struct mcts_node;

struct mcts_child {
    planner_vector action;
    std::int64_t visits = 0;
    double value_sum = 0.0;
    std::unique_ptr<mcts_node> next;
};

struct mcts_node {
    std::int64_t visits = 0;
    double value_sum = 0.0;
    std::vector<mcts_child> children;

    [[nodiscard]] double q() const {
        if (visits <= 0) {
            return 0.0;
        }
        return value_sum / static_cast<double>(visits);
    }
};

double ucb_score(const mcts_child& child, std::int64_t parent_visits, double c_ucb) {
    if (child.visits <= 0) {
        return 1.0e30;
    }
    const double q = child.value_sum / static_cast<double>(child.visits);
    const double parent_n = static_cast<double>(std::max<std::int64_t>(1, parent_visits));
    const double child_n = static_cast<double>(child.visits);
    return q + c_ucb * std::sqrt(std::log(parent_n) / child_n);
}

std::optional<std::size_t> select_child_index(const mcts_node& node, double c_ucb) {
    if (node.children.empty()) {
        return std::nullopt;
    }

    std::size_t best_idx = 0;
    double best_score = -1.0e300;
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        const double score = ucb_score(node.children[i], node.visits, c_ucb);
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    return best_idx;
}

}  // namespace

planner_rng::planner_rng(std::uint64_t seed) : state_(seed == 0 ? 0x9e3779b97f4a7c15ull : seed) {}

double planner_rng::uniform(double lo, double hi) {
    if (!(lo <= hi)) {
        throw std::runtime_error("planner_rng.uniform: expected lo <= hi");
    }
    if (lo == hi) {
        return lo;
    }
    return lo + (hi - lo) * next_unit();
}

std::int64_t planner_rng::uniform_int(std::int64_t n) {
    if (n <= 0) {
        throw std::runtime_error("planner_rng.uniform_int: expected n > 0");
    }
    const std::uint64_t un = static_cast<std::uint64_t>(n);
    const std::uint64_t threshold = (~std::uint64_t{0} - un + 1u) % un;
    while (true) {
        const std::uint64_t x = next_u64();
        if (x >= threshold) {
            return static_cast<std::int64_t>(x % un);
        }
    }
}

double planner_rng::normal(double mu, double sigma) {
    if (sigma < 0.0) {
        throw std::runtime_error("planner_rng.normal: expected sigma >= 0");
    }
    if (sigma == 0.0) {
        return mu;
    }
    if (has_spare_normal_) {
        has_spare_normal_ = false;
        return mu + sigma * spare_normal_;
    }

    double u1 = 0.0;
    do {
        u1 = next_unit();
    } while (u1 <= std::numeric_limits<double>::min());
    const double u2 = next_unit();
    const double mag = std::sqrt(-2.0 * std::log(u1));
    const double theta = 6.28318530717958647692 * u2;
    spare_normal_ = mag * std::sin(theta);
    has_spare_normal_ = true;
    return mu + sigma * (mag * std::cos(theta));
}

std::uint64_t planner_rng::next_u64() {
    return splitmix64_next(state_);
}

double planner_rng::next_unit() {
    constexpr double kScale = 1.0 / 9007199254740992.0;
    const std::uint64_t bits = next_u64();
    return static_cast<double>(bits >> 11u) * kScale;
}

planner_vector planner_model::rollout_action(const planner_vector& state, planner_rng& rng) const {
    return sample_action(state, rng);
}

bool planner_model::validate_state(const planner_vector& state) const {
    return !state.empty() && vector_all_finite(state);
}

planner_service::planner_service() {
    records_.reserve(record_capacity_);
    register_model("toy-1d", std::make_shared<toy_1d_model>());
    register_model("ptz-track", std::make_shared<ptz_track_model>());
}

void planner_service::register_model(std::string name, std::shared_ptr<planner_model> model) {
    if (name.empty()) {
        throw std::invalid_argument("register_model: model name must not be empty");
    }
    if (!model) {
        throw std::invalid_argument("register_model: model pointer is null");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    models_[std::move(name)] = std::move(model);
}

bool planner_service::has_model(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return models_.find(std::string(name)) != models_.end();
}

planner_result planner_service::plan(const planner_request& request) {
    planner_result result;
    result.status = planner_status::error;
    result.stats.seed = request.seed;

    std::shared_ptr<planner_model> model;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = models_.find(request.model_service);
        if (it != models_.end()) {
            model = it->second;
        }
    }

    if (!model) {
        result.error = "planner model not found: " + request.model_service;
        planner_record rec;
        rec.ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                        .count();
        rec.run_id = request.run_id;
        rec.tick_index = request.tick_index;
        rec.node_name = request.node_name;
        rec.budget_ms = request.config.budget_ms;
        rec.status = planner_status::error;
        rec.seed = request.seed;
        rec.state_key = request.state_key;
        append_record(std::move(rec));
        return result;
    }

    if (!model->validate_state(request.state)) {
        result.error = "planner state validation failed";
        planner_record rec;
        rec.ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                        .count();
        rec.run_id = request.run_id;
        rec.tick_index = request.tick_index;
        rec.node_name = request.node_name;
        rec.budget_ms = request.config.budget_ms;
        rec.status = planner_status::error;
        rec.seed = request.seed;
        rec.state_key = request.state_key;
        append_record(std::move(rec));
        return result;
    }

    planner_config cfg = request.config;
    if (cfg.budget_ms < 0) {
        cfg.budget_ms = 0;
    }
    if (cfg.iters_max < 1) {
        cfg.iters_max = 1;
    }
    if (cfg.max_depth < 1) {
        cfg.max_depth = 1;
    }
    if (cfg.time_check_interval < 1) {
        cfg.time_check_interval = 1;
    }
    if (cfg.gamma < 0.0) {
        cfg.gamma = 0.0;
    }
    if (cfg.gamma > 1.0) {
        cfg.gamma = 1.0;
    }
    if (cfg.c_ucb < 0.0) {
        cfg.c_ucb = 0.0;
    }
    if (cfg.pw_k < 0.0) {
        cfg.pw_k = 0.0;
    }
    if (cfg.pw_alpha < 0.0) {
        cfg.pw_alpha = 0.0;
    }
    if (cfg.top_k < 0) {
        cfg.top_k = 0;
    }
    if (cfg.action_prior_sigma < 0.0) {
        cfg.action_prior_sigma = 0.0;
    }
    cfg.action_prior_mix = clamp_double(cfg.action_prior_mix, 0.0, 1.0);

    planner_rng rng(request.seed);
    mcts_node root;
    std::int64_t widen_added = 0;
    std::int64_t depth_sum = 0;
    std::int64_t depth_max = 0;
    std::int64_t iter_depth_max = 0;
    std::int64_t* depth_tracker = &iter_depth_max;

    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(cfg.budget_ms);
    bool timed_out = false;

    std::function<double(const planner_vector&, std::int64_t)> rollout = [&](const planner_vector& state,
                                                                             std::int64_t depth) -> double {
        if (depth >= cfg.max_depth) {
            *depth_tracker = std::max(*depth_tracker, depth);
            return 0.0;
        }

        planner_vector action = model->clamp_action(model->rollout_action(state, rng));
        const planner_step_result step_out = model->step(state, action, rng);
        *depth_tracker = std::max(*depth_tracker, depth + 1);

        if (step_out.done) {
            return step_out.reward;
        }
        return step_out.reward + cfg.gamma * rollout(step_out.next_state, depth + 1);
    };

    std::function<double(mcts_node&, const planner_vector&, std::int64_t)> simulate =
        [&](mcts_node& node, const planner_vector& state, std::int64_t depth) -> double {
        if (depth >= cfg.max_depth) {
            *depth_tracker = std::max(*depth_tracker, depth);
            return 0.0;
        }

        const double child_cap =
            cfg.pw_k * std::pow(static_cast<double>(std::max<std::int64_t>(1, node.visits)), cfg.pw_alpha);
        const bool allow_expand = static_cast<double>(node.children.size()) < child_cap;

        if (allow_expand) {
            planner_vector sampled = model->sample_action(state, rng);
            if (cfg.action_sampler == "vla_mixture" && !cfg.action_prior_mean.empty() &&
                cfg.action_prior_mean.size() == model->action_dims() &&
                rng.uniform(0.0, 1.0) < cfg.action_prior_mix) {
                sampled = cfg.action_prior_mean;
                for (double& dim : sampled) {
                    dim = rng.normal(dim, cfg.action_prior_sigma);
                }
            }

            planner_vector action = model->clamp_action(sampled);
            mcts_child child;
            child.action = action;

            const planner_step_result step_out = model->step(state, action, rng);
            double value = step_out.reward;
            if (!step_out.done) {
                value += cfg.gamma * rollout(step_out.next_state, depth + 1);
            }

            child.visits = 1;
            child.value_sum = value;
            node.children.push_back(std::move(child));
            ++widen_added;

            ++node.visits;
            node.value_sum += value;
            return value;
        }

        const std::optional<std::size_t> choice = select_child_index(node, cfg.c_ucb);
        if (!choice.has_value()) {
            ++node.visits;
            return 0.0;
        }

        mcts_child& child = node.children[*choice];
        const planner_step_result step_out = model->step(state, child.action, rng);

        double value = step_out.reward;
        if (!step_out.done) {
            if (!child.next) {
                child.next = std::make_unique<mcts_node>();
            }
            value += cfg.gamma * simulate(*child.next, step_out.next_state, depth + 1);
        } else {
            *depth_tracker = std::max(*depth_tracker, depth + 1);
        }

        ++child.visits;
        child.value_sum += value;
        ++node.visits;
        node.value_sum += value;
        return value;
    };

    std::int64_t completed_iters = 0;
    for (std::int64_t i = 0; i < cfg.iters_max; ++i) {
        if ((i % cfg.time_check_interval) == 0) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                timed_out = true;
                break;
            }
        }
        iter_depth_max = 0;
        const double value = simulate(root, request.state, 0);
        (void)value;
        ++completed_iters;
        depth_sum += iter_depth_max;
        depth_max = std::max(depth_max, iter_depth_max);
    }

    const auto end = std::chrono::steady_clock::now();
    const double time_used_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

    result.stats.iters = completed_iters;
    result.stats.root_visits = root.visits;
    result.stats.root_children = static_cast<std::int64_t>(root.children.size());
    result.stats.widen_added = widen_added;
    result.stats.depth_max = depth_max;
    result.stats.depth_mean = completed_iters > 0 ? static_cast<double>(depth_sum) / completed_iters : 0.0;
    result.stats.time_used_ms = time_used_ms;
    result.stats.value_est = root.q();

    std::vector<const mcts_child*> sorted_children;
    sorted_children.reserve(root.children.size());
    for (const mcts_child& child : root.children) {
        sorted_children.push_back(&child);
    }
    std::sort(sorted_children.begin(), sorted_children.end(), [](const mcts_child* lhs, const mcts_child* rhs) {
        return lhs->visits > rhs->visits;
    });

    const std::size_t top_k = static_cast<std::size_t>(cfg.top_k);
    for (std::size_t i = 0; i < sorted_children.size() && i < top_k; ++i) {
        const mcts_child* child = sorted_children[i];
        planner_top_choice top;
        top.action = child->action;
        top.visits = child->visits;
        top.q = child->visits > 0 ? child->value_sum / static_cast<double>(child->visits) : 0.0;
        result.stats.top_k.push_back(std::move(top));
    }

    if (!sorted_children.empty()) {
        const mcts_child* best = sorted_children.front();
        result.action = model->clamp_action(best->action);
        result.confidence =
            root.visits > 0 ? static_cast<double>(best->visits) / static_cast<double>(std::max<std::int64_t>(1, root.visits))
                            : 0.0;
        result.status = timed_out ? planner_status::timeout : planner_status::ok;
    } else {
        if (!cfg.fallback_action.empty()) {
            result.action = model->clamp_action(cfg.fallback_action);
        } else {
            result.action = model->zero_action();
        }
        result.confidence = 0.0;
        result.status = planner_status::noaction;
    }

    if (result.action.size() != model->action_dims()) {
        // Force a safe action shape on model mismatch.
        result.action = model->zero_action();
        result.status = planner_status::error;
        result.error = "planner action dimensionality mismatch";
    }

    planner_record rec;
    rec.ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end.time_since_epoch()).count();
    rec.run_id = request.run_id;
    rec.tick_index = request.tick_index;
    rec.node_name = request.node_name;
    rec.budget_ms = cfg.budget_ms;
    rec.time_used_ms = result.stats.time_used_ms;
    rec.iters = result.stats.iters;
    rec.root_visits = result.stats.root_visits;
    rec.root_children = result.stats.root_children;
    rec.widen_added = result.stats.widen_added;
    rec.action = result.action;
    rec.confidence = result.confidence;
    rec.value_est = result.stats.value_est;
    rec.status = result.status;
    rec.depth_max = result.stats.depth_max;
    rec.depth_mean = result.stats.depth_mean;
    rec.seed = request.seed;
    rec.state_key = request.state_key;
    rec.top_k = result.stats.top_k;
    append_record(std::move(rec));

    return result;
}

std::uint64_t planner_service::base_seed() const noexcept {
    return base_seed_;
}

void planner_service::set_base_seed(std::uint64_t seed) noexcept {
    base_seed_ = seed;
}

std::uint64_t planner_service::derive_seed(std::string_view node_name, std::uint64_t tick_index) const {
    std::uint64_t seed = base_seed_;
    const std::uint64_t node_hash = hash64(node_name);
    seed ^= node_hash + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
    seed ^= tick_index + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
    return seed == 0 ? 0x9e3779b97f4a7c15ull : seed;
}

std::uint64_t planner_service::hash64(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

void planner_service::set_jsonl_path(std::string path) {
    if (path.empty()) {
        throw std::invalid_argument("set_jsonl_path: path must not be empty");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    jsonl_path_ = std::move(path);
}

const std::string& planner_service::jsonl_path() const noexcept {
    return jsonl_path_;
}

void planner_service::set_jsonl_enabled(bool enabled) noexcept {
    jsonl_enabled_ = enabled;
}

bool planner_service::jsonl_enabled() const noexcept {
    return jsonl_enabled_;
}

std::vector<planner_record> planner_service::recent_records(std::size_t max_count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (max_count >= records_.size()) {
        return records_;
    }
    return std::vector<planner_record>(records_.end() - static_cast<std::ptrdiff_t>(max_count), records_.end());
}

std::string planner_service::dump_recent_records(std::size_t max_count) const {
    const std::vector<planner_record> recs = recent_records(max_count);
    std::ostringstream out;
    for (const planner_record& rec : recs) {
        out << record_to_json(rec) << '\n';
    }
    return out.str();
}

void planner_service::clear_records() {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
}

void planner_service::append_record(planner_record record) {
    std::string json_line;
    bool write_jsonl = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (record_capacity_ > 0 && records_.size() == record_capacity_) {
            records_.erase(records_.begin());
        }
        records_.push_back(record);
        write_jsonl = jsonl_enabled_;
        if (write_jsonl) {
            json_line = record_to_json(record);
        }
    }

    if (write_jsonl) {
        append_jsonl_line(json_line);
    }
}

std::string planner_service::record_to_json(const planner_record& record) const {
    std::ostringstream out;
    out << '{'
        << "\"ts_ms\":" << record.ts_ms << ','
        << "\"run_id\":\"" << json_escape(record.run_id) << "\","
        << "\"tick_index\":" << record.tick_index << ','
        << "\"node_name\":\"" << json_escape(record.node_name) << "\","
        << "\"budget_ms\":" << record.budget_ms << ','
        << "\"time_used_ms\":" << record.time_used_ms << ','
        << "\"iters\":" << record.iters << ','
        << "\"root_visits\":" << record.root_visits << ','
        << "\"root_children\":" << record.root_children << ','
        << "\"widen_added\":" << record.widen_added << ','
        << "\"action\":" << vector_to_json(record.action) << ','
        << "\"confidence\":" << record.confidence << ','
        << "\"value_est\":" << record.value_est << ','
        << "\"status\":\"" << planner_status_name(record.status) << "\","
        << "\"depth_max\":" << record.depth_max << ','
        << "\"depth_mean\":" << record.depth_mean << ','
        << "\"seed\":" << record.seed;
    if (!record.state_key.empty()) {
        out << ",\"state_key\":\"" << json_escape(record.state_key) << "\"";
    }
    if (!record.top_k.empty()) {
        out << ",\"top_k\":[";
        for (std::size_t i = 0; i < record.top_k.size(); ++i) {
            if (i != 0) {
                out << ',';
            }
            out << '{' << "\"action\":" << vector_to_json(record.top_k[i].action) << ",\"visits\":" << record.top_k[i].visits
                << ",\"q\":" << record.top_k[i].q << '}';
        }
        out << ']';
    }
    out << '}';
    return out.str();
}

void planner_service::append_jsonl_line(const std::string& line) {
    const std::string path = jsonl_path_;
    std::lock_guard<std::mutex> lock(file_mutex_);
    std::ofstream out(path, std::ios::app);
    if (!out) {
        return;
    }
    out << line << '\n';
}

const char* planner_status_name(planner_status status) noexcept {
    switch (status) {
        case planner_status::ok:
            return "ok";
        case planner_status::timeout:
            return "timeout";
        case planner_status::noaction:
            return "noaction";
        case planner_status::error:
            return "error";
    }
    return "error";
}

}  // namespace bt
