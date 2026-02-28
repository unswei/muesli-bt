#include "bt/planner.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace bt {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

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

bool vectors_close(const planner_vector& a, const planner_vector& b, double eps = 1.0e-9) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i] - b[i]) > eps) {
            return false;
        }
    }
    return true;
}

double clamp_confidence(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::clamp(value, 0.0, 1.0);
}

std::string normalized_token(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == ':') {
            continue;
        }
        if (c == '-') {
            out.push_back('_');
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
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

std::string action_to_json(const planner_action& action) {
    std::ostringstream out;
    out << '{' << "\"action_schema\":\"" << json_escape(action.action_schema) << "\",";
    out << "\"u\":" << vector_to_json(action.u) << '}';
    return out.str();
}

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::int64_t elapsed_ms(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

double wrap_angle(double angle) {
    while (angle > kPi) {
        angle -= (2.0 * kPi);
    }
    while (angle < -kPi) {
        angle += (2.0 * kPi);
    }
    return angle;
}

planner_vector clamp_action_with_bounds(const planner_vector& action,
                                        const std::vector<planner_bound>& bounds,
                                        const planner_model& model) {
    if (!bounds.empty()) {
        const std::size_t dims = model.action_dims();
        if (bounds.size() == dims) {
            planner_vector out(dims, 0.0);
            for (std::size_t i = 0; i < dims; ++i) {
                const double v = i < action.size() ? action[i] : 0.0;
                out[i] = clamp_double(v, bounds[i].lo, bounds[i].hi);
            }
            return model.clamp_action(out);
        }
    }
    return model.clamp_action(action);
}

std::vector<planner_bound> sanitise_bounds(const std::vector<planner_bound>& bounds, std::size_t dims) {
    if (bounds.size() != dims) {
        return {};
    }
    std::vector<planner_bound> out = bounds;
    for (planner_bound& b : out) {
        if (!std::isfinite(b.lo) || !std::isfinite(b.hi)) {
            return {};
        }
        if (b.lo > b.hi) {
            std::swap(b.lo, b.hi);
        }
    }
    return out;
}

std::vector<planner_bound> resolve_bounds(const planner_request& request, const planner_model& model) {
    const std::size_t dims = model.action_dims();
    const std::vector<planner_bound> req_bounds = sanitise_bounds(request.bounds, dims);
    if (!req_bounds.empty()) {
        return req_bounds;
    }
    return sanitise_bounds(model.action_bounds(), dims);
}

std::string resolve_action_schema(const planner_request& request) {
    if (!request.action_schema.empty()) {
        return request.action_schema;
    }
    if (!request.safe_action.action_schema.empty()) {
        return request.safe_action.action_schema;
    }
    return "action.u.v1";
}

planner_action resolve_safe_action(const planner_request& request,
                                   const planner_model& model,
                                   const std::vector<planner_bound>& bounds,
                                   std::string action_schema) {
    planner_action out;
    out.action_schema = std::move(action_schema);
    if (!request.safe_action.u.empty()) {
        out.u = clamp_action_with_bounds(request.safe_action.u, bounds, model);
    } else {
        out.u = model.zero_action();
    }
    if (out.u.size() != model.action_dims()) {
        out.u = model.zero_action();
    }
    if (!vector_all_finite(out.u)) {
        out.u = model.zero_action();
    }
    return out;
}

planner_action make_action(std::string action_schema, planner_vector u) {
    planner_action out;
    out.action_schema = std::move(action_schema);
    out.u = std::move(u);
    return out;
}

void apply_rate_limits(std::vector<planner_vector>& actions, const planner_vector& max_du) {
    if (actions.empty() || max_du.empty()) {
        return;
    }
    for (std::size_t t = 1; t < actions.size(); ++t) {
        planner_vector& cur = actions[t];
        const planner_vector& prev = actions[t - 1];
        const std::size_t dims = std::min({cur.size(), prev.size(), max_du.size()});
        for (std::size_t d = 0; d < dims; ++d) {
            const double lim = std::fabs(max_du[d]);
            if (!std::isfinite(lim) || lim <= 0.0) {
                continue;
            }
            const double delta = cur[d] - prev[d];
            if (delta > lim) {
                cur[d] = prev[d] + lim;
            } else if (delta < -lim) {
                cur[d] = prev[d] - lim;
            }
        }
    }
}

std::vector<planner_vector> unpack_sequence(const planner_vector& flat,
                                            std::size_t action_dim,
                                            std::size_t horizon) {
    std::vector<planner_vector> seq(horizon, planner_vector(action_dim, 0.0));
    if (horizon == 0 || action_dim == 0 || flat.empty()) {
        return seq;
    }

    if (flat.size() == action_dim * horizon) {
        for (std::size_t t = 0; t < horizon; ++t) {
            for (std::size_t d = 0; d < action_dim; ++d) {
                seq[t][d] = flat[t * action_dim + d];
            }
        }
        return seq;
    }

    if (flat.size() >= action_dim) {
        planner_vector first(action_dim, 0.0);
        for (std::size_t d = 0; d < action_dim; ++d) {
            first[d] = flat[d];
        }
        for (std::size_t t = 0; t < horizon; ++t) {
            seq[t] = first;
        }
    }
    return seq;
}

bool deterministic_step_eval(const planner_model& model,
                             const planner_vector& state,
                             const planner_vector& action,
                             planner_step_result& out) {
    if (model.deterministic_step(state, action, out)) {
        return true;
    }
    try {
        planner_rng rng(0x9e3779b97f4a7c15ull);
        out = model.step(state, action, rng);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

double smoothness_penalty(const std::vector<planner_vector>& actions, double smoothness_weight) {
    if (smoothness_weight <= 0.0 || actions.size() <= 1) {
        return 0.0;
    }
    double penalty = 0.0;
    for (std::size_t t = 1; t < actions.size(); ++t) {
        const planner_vector& prev = actions[t - 1];
        const planner_vector& cur = actions[t];
        const std::size_t dims = std::min(prev.size(), cur.size());
        for (std::size_t d = 0; d < dims; ++d) {
            const double delta = cur[d] - prev[d];
            penalty += delta * delta;
        }
    }
    return smoothness_weight * penalty;
}

std::optional<double> rollout_cost_with_step(const planner_model& model,
                                             const planner_vector& state,
                                             const std::vector<planner_vector>& actions,
                                             planner_rng& rng,
                                             const planner_constraints& constraints) {
    if (actions.empty()) {
        return 0.0;
    }

    double cost = 0.0;
    if (model.rollout_cost(state, actions, cost)) {
        if (!std::isfinite(cost)) {
            return std::nullopt;
        }
        cost += smoothness_penalty(actions, constraints.smoothness_weight);
        return cost;
    }

    planner_vector cur = state;
    for (const planner_vector& u : actions) {
        const planner_step_result step_out = model.step(cur, u, rng);
        if (!vector_all_finite(step_out.next_state) || !std::isfinite(step_out.reward)) {
            return std::nullopt;
        }
        cost += -step_out.reward;
        cur = step_out.next_state;
        if (step_out.done) {
            break;
        }
    }
    cost += smoothness_penalty(actions, constraints.smoothness_weight);
    return std::isfinite(cost) ? std::optional<double>(cost) : std::nullopt;
}

std::optional<double> stage_cost_deterministic(const planner_model& model,
                                               const planner_vector& state,
                                               const planner_vector& action,
                                               planner_vector* next_state = nullptr) {
    planner_step_result out;
    if (!deterministic_step_eval(model, state, action, out)) {
        return std::nullopt;
    }
    if (!std::isfinite(out.reward) || !vector_all_finite(out.next_state)) {
        return std::nullopt;
    }
    if (next_state) {
        *next_state = out.next_state;
    }
    const double cost = -out.reward;
    return std::isfinite(cost) ? std::optional<double>(cost) : std::nullopt;
}

bool solve_linear_system(std::vector<double>& a, std::vector<double>& b, std::size_t n, std::size_t rhs_cols) {
    if (a.size() != n * n || b.size() != n * rhs_cols) {
        return false;
    }

    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        double pivot_abs = std::fabs(a[col * n + col]);
        for (std::size_t r = col + 1; r < n; ++r) {
            const double v = std::fabs(a[r * n + col]);
            if (v > pivot_abs) {
                pivot = r;
                pivot_abs = v;
            }
        }
        if (pivot_abs < 1.0e-12 || !std::isfinite(pivot_abs)) {
            return false;
        }

        if (pivot != col) {
            for (std::size_t c = 0; c < n; ++c) {
                std::swap(a[pivot * n + c], a[col * n + c]);
            }
            for (std::size_t c = 0; c < rhs_cols; ++c) {
                std::swap(b[pivot * rhs_cols + c], b[col * rhs_cols + c]);
            }
        }

        const double diag = a[col * n + col];
        for (std::size_t c = 0; c < n; ++c) {
            a[col * n + c] /= diag;
        }
        for (std::size_t c = 0; c < rhs_cols; ++c) {
            b[col * rhs_cols + c] /= diag;
        }

        for (std::size_t r = 0; r < n; ++r) {
            if (r == col) {
                continue;
            }
            const double factor = a[r * n + col];
            if (factor == 0.0) {
                continue;
            }
            for (std::size_t c = 0; c < n; ++c) {
                a[r * n + c] -= factor * a[col * n + c];
            }
            for (std::size_t c = 0; c < rhs_cols; ++c) {
                b[r * rhs_cols + c] -= factor * b[col * rhs_cols + c];
            }
        }
    }

    for (double v : b) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    return true;
}

planner_result make_noaction_result(planner_backend planner, planner_action safe_action) {
    planner_result result;
    result.planner = planner;
    result.status = planner_status::noaction;
    result.action = std::move(safe_action);
    result.confidence = 0.0;
    return result;
}

planner_result make_error_result(planner_backend planner, planner_action safe_action, std::string error) {
    planner_result result;
    result.planner = planner;
    result.status = planner_status::error;
    result.action = std::move(safe_action);
    result.confidence = 0.0;
    result.error = std::move(error);
    return result;
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
        out.reward = -(err * err);
        out.done = std::fabs(err) < 0.05;
        return out;
    }

    planner_vector sample_action(const planner_vector&, planner_rng& rng) const override {
        return {rng.uniform(-1.0, 1.0)};
    }

    planner_vector rollout_action(const planner_vector& state, planner_rng& rng) const override {
        const double x = state.empty() ? 0.0 : state[0];
        const double dir = (x < 1.0) ? 1.0 : -1.0;
        const double noise = rng.normal(0.0, 0.2);
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

    std::vector<planner_bound> action_bounds() const override {
        return {{-1.0, 1.0}};
    }

    std::int64_t default_horizon() const override {
        return 20;
    }

    bool deterministic_step(const planner_vector& state,
                            const planner_vector& action,
                            planner_step_result& out) const override {
        planner_rng rng(0x1234abcdULL);
        out = step(state, action, rng);
        return true;
    }

    bool linearise_dynamics(const planner_vector& state,
                            const planner_vector& action,
                            planner_linearisation& out) const override {
        if (state.empty() || action.empty()) {
            return false;
        }
        out.state_dim = 1;
        out.action_dim = 1;
        out.A = {1.0};
        const double raw_u = action[0];
        const double d = (raw_u > -1.0 && raw_u < 1.0) ? 0.25 : 0.0;
        out.B = {d};
        return true;
    }

    bool quadraticise_cost(const planner_vector& state,
                           const planner_vector& action,
                           planner_quadratic_cost& out) const override {
        if (state.empty() || action.empty()) {
            return false;
        }
        const double x = state[0];
        const double u = clamp_double(action[0], -1.0, 1.0);
        const double z = x + 0.25 * u;
        const double err = 1.0 - z;

        out.l0 = err * err;
        out.l_x = {-2.0 * err};
        out.l_u = {-0.5 * err};
        out.l_xx = {2.0};
        out.l_uu = {0.125};
        out.l_xu = {0.5};
        return true;
    }

    bool quadraticise_terminal_cost(const planner_vector& state, planner_terminal_quadratic_cost& out) const override {
        if (state.empty()) {
            return false;
        }
        const double err = 1.0 - state[0];
        out.l0 = err * err;
        out.l_x = {-2.0 * err};
        out.l_xx = {2.0};
        return true;
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

    std::vector<planner_bound> action_bounds() const override {
        return {{-0.25, 0.25}, {-0.25, 0.25}};
    }

    std::int64_t default_horizon() const override {
        return 15;
    }
};

class toy_unicycle_goal_model final : public planner_model {
public:
    planner_step_result step(const planner_vector& state, const planner_vector& action, planner_rng&) const override {
        if (state.size() < 5) {
            throw std::runtime_error("toy-unicycle.step: expected [x y yaw gx gy]");
        }
        if (action.size() < 2) {
            throw std::runtime_error("toy-unicycle.step: expected [v omega]");
        }

        const double x = state[0];
        const double y = state[1];
        const double yaw = state[2];
        const double gx = state[3];
        const double gy = state[4];

        const double v = clamp_double(action[0], -1.0, 1.0);
        const double w = clamp_double(action[1], -1.5, 1.5);

        constexpr double kDt = 0.15;
        const double yaw2 = wrap_angle(yaw + w * kDt);
        const double x2 = x + v * std::cos(yaw2) * kDt;
        const double y2 = y + v * std::sin(yaw2) * kDt;

        const double dx = gx - x2;
        const double dy = gy - y2;
        const double dist = std::sqrt(dx * dx + dy * dy);

        planner_step_result out;
        out.next_state = {x2, y2, yaw2, gx, gy};
        out.reward = -(dist + 0.05 * (v * v + w * w));
        out.done = dist < 0.10;
        return out;
    }

    planner_vector sample_action(const planner_vector&, planner_rng& rng) const override {
        return {rng.uniform(-1.0, 1.0), rng.uniform(-1.5, 1.5)};
    }

    planner_vector rollout_action(const planner_vector& state, planner_rng& rng) const override {
        if (state.size() < 5) {
            return {0.0, 0.0};
        }
        const double x = state[0];
        const double y = state[1];
        const double yaw = state[2];
        const double gx = state[3];
        const double gy = state[4];
        const double heading = std::atan2(gy - y, gx - x);
        const double heading_err = wrap_angle(heading - yaw);
        const double v = clamp_double(0.8 + rng.normal(0.0, 0.15), -1.0, 1.0);
        const double w = clamp_double(2.0 * heading_err + rng.normal(0.0, 0.2), -1.5, 1.5);
        return {v, w};
    }

    planner_vector clamp_action(const planner_vector& action) const override {
        const double v = action.empty() ? 0.0 : action[0];
        const double w = action.size() > 1 ? action[1] : 0.0;
        return {clamp_double(v, -1.0, 1.0), clamp_double(w, -1.5, 1.5)};
    }

    planner_vector zero_action() const override {
        return {0.0, 0.0};
    }

    bool validate_state(const planner_vector& state) const override {
        return state.size() >= 5 && vector_all_finite(state);
    }

    std::size_t action_dims() const override {
        return 2;
    }

    std::vector<planner_bound> action_bounds() const override {
        return {{-1.0, 1.0}, {-1.5, 1.5}};
    }

    std::int64_t default_horizon() const override {
        return 20;
    }

    bool deterministic_step(const planner_vector& state,
                            const planner_vector& action,
                            planner_step_result& out) const override {
        planner_rng rng(0xabcdu);
        out = step(state, action, rng);
        return true;
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

planner_result run_mcts_backend(const planner_request& request,
                                const planner_model& model,
                                const std::vector<planner_bound>& bounds,
                                const planner_action& safe_action,
                                const std::string& action_schema,
                                std::chrono::steady_clock::time_point deadline) {
    planner_result result;
    result.planner = planner_backend::mcts;
    result.action = safe_action;

    planner_mcts_config cfg = request.mcts;
    cfg.gamma = clamp_double(cfg.gamma, 0.0, 1.0);
    cfg.c_ucb = std::max(0.0, cfg.c_ucb);
    cfg.pw_k = std::max(0.0, cfg.pw_k);
    cfg.pw_alpha = std::max(0.0, cfg.pw_alpha);
    cfg.max_depth = std::max<std::int64_t>(1, cfg.max_depth);
    cfg.time_check_interval = std::max<std::int64_t>(1, cfg.time_check_interval);

    const std::int64_t iter_cap = std::max<std::int64_t>(
        1,
        request.work_max > 0 ? request.work_max : std::max<std::int64_t>(1, cfg.default_iters));

    planner_rng rng(request.seed);

    mcts_node root;
    std::int64_t widen_added = 0;
    bool timed_out = false;

    std::function<double(const planner_vector&, std::int64_t)> rollout = [&](const planner_vector& state,
                                                                             std::int64_t depth) -> double {
        if (depth >= cfg.max_depth) {
            return 0.0;
        }

        planner_vector action;
        if (normalized_token(cfg.rollout_policy) == "random") {
            action = model.sample_action(state, rng);
        } else {
            action = model.rollout_action(state, rng);
        }
        action = clamp_action_with_bounds(action, bounds, model);

        const planner_step_result step_out = model.step(state, action, rng);
        if (step_out.done) {
            return step_out.reward;
        }
        return step_out.reward + cfg.gamma * rollout(step_out.next_state, depth + 1);
    };

    std::function<double(mcts_node&, const planner_vector&, std::int64_t)> simulate =
        [&](mcts_node& node, const planner_vector& state, std::int64_t depth) -> double {
        if (depth >= cfg.max_depth) {
            return 0.0;
        }

        const double child_cap =
            cfg.pw_k * std::pow(static_cast<double>(std::max<std::int64_t>(1, node.visits)), cfg.pw_alpha);
        const bool allow_expand = static_cast<double>(node.children.size()) < child_cap;

        if (allow_expand) {
            planner_vector sampled;
            if (normalized_token(cfg.action_sampler) == "safe_action" && safe_action.u.size() == model.action_dims()) {
                sampled = safe_action.u;
            } else {
                sampled = model.sample_action(state, rng);
            }

            planner_vector action = clamp_action_with_bounds(sampled, bounds, model);
            mcts_child child;
            child.action = action;

            const planner_step_result step_out = model.step(state, action, rng);
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
        const planner_step_result step_out = model.step(state, child.action, rng);

        double value = step_out.reward;
        if (!step_out.done) {
            if (!child.next) {
                child.next = std::make_unique<mcts_node>();
            }
            value += cfg.gamma * simulate(*child.next, step_out.next_state, depth + 1);
        }

        ++child.visits;
        child.value_sum += value;
        ++node.visits;
        node.value_sum += value;
        return value;
    };

    std::int64_t completed_iters = 0;
    for (std::int64_t i = 0; i < iter_cap; ++i) {
        if ((i % cfg.time_check_interval) == 0) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                timed_out = true;
                break;
            }
        }
        (void)simulate(root, request.state, 0);
        ++completed_iters;
    }

    std::vector<const mcts_child*> sorted_children;
    sorted_children.reserve(root.children.size());
    for (const mcts_child& child : root.children) {
        sorted_children.push_back(&child);
    }
    std::sort(sorted_children.begin(), sorted_children.end(), [](const mcts_child* lhs, const mcts_child* rhs) {
        return lhs->visits > rhs->visits;
    });

    result.trace.mcts.available = true;
    result.trace.mcts.root_visits = root.visits;
    result.trace.mcts.root_children = static_cast<std::int64_t>(root.children.size());
    result.trace.mcts.widen_added = widen_added;

    const std::size_t top_k = static_cast<std::size_t>(std::max<std::int64_t>(0, request.top_k));
    for (std::size_t i = 0; i < sorted_children.size() && i < top_k; ++i) {
        const mcts_child* child = sorted_children[i];
        planner_top_choice_mcts top;
        top.action = make_action(action_schema, clamp_action_with_bounds(child->action, bounds, model));
        top.visits = child->visits;
        top.q = child->visits > 0 ? child->value_sum / static_cast<double>(child->visits) : 0.0;
        result.trace.mcts.top_k.push_back(std::move(top));
    }

    if (!sorted_children.empty()) {
        const mcts_child* best = sorted_children.front();
        result.action = make_action(action_schema, clamp_action_with_bounds(best->action, bounds, model));
        result.confidence = root.visits > 0
                                ? static_cast<double>(best->visits) /
                                      static_cast<double>(std::max<std::int64_t>(1, root.visits))
                                : 0.0;
        result.status = timed_out ? planner_status::timeout : planner_status::ok;
    } else {
        result.status = planner_status::noaction;
        result.confidence = 0.0;
    }

    result.stats.work_done = completed_iters;
    return result;
}

planner_result run_mppi_backend(const planner_request& request,
                                const planner_model& model,
                                const std::vector<planner_bound>& bounds,
                                const planner_action& safe_action,
                                const std::string& action_schema,
                                std::chrono::steady_clock::time_point deadline) {
    planner_result result;
    result.planner = planner_backend::mppi;
    result.action = safe_action;

    const std::size_t action_dim = model.action_dims();
    if (action_dim == 0) {
        return make_error_result(planner_backend::mppi, safe_action, "mppi: model action dimension is zero");
    }

    std::int64_t horizon_i = request.horizon > 0 ? request.horizon : model.default_horizon();
    if (horizon_i <= 0) {
        horizon_i = 20;
    }
    const std::size_t horizon = static_cast<std::size_t>(horizon_i);

    planner_mppi_config cfg = request.mppi;
    if (cfg.lambda <= 1.0e-9 || !std::isfinite(cfg.lambda)) {
        cfg.lambda = 1.0;
    }
    if (cfg.n_samples <= 0) {
        cfg.n_samples = 64;
    }

    std::int64_t sample_cap = cfg.n_samples;
    if (request.work_max > 0) {
        sample_cap = std::min(sample_cap, request.work_max);
    }
    sample_cap = std::max<std::int64_t>(1, sample_cap);

    planner_vector sigma = cfg.sigma;
    if (sigma.empty()) {
        sigma.assign(action_dim, 0.25);
    } else if (sigma.size() == 1 && action_dim > 1) {
        sigma.assign(action_dim, sigma[0]);
    } else if (sigma.size() != action_dim) {
        sigma.assign(action_dim, 0.25);
    }
    for (double& s : sigma) {
        if (!std::isfinite(s) || s < 0.0) {
            s = 0.0;
        }
    }

    std::vector<planner_vector> u_seq;
    if (!cfg.u_nominal.empty()) {
        u_seq = unpack_sequence(cfg.u_nominal, action_dim, horizon);
    } else if (!cfg.u_init.empty()) {
        u_seq = unpack_sequence(cfg.u_init, action_dim, horizon);
    } else {
        u_seq.assign(horizon, planner_vector(action_dim, 0.0));
    }

    for (planner_vector& u : u_seq) {
        u = clamp_action_with_bounds(u, bounds, model);
    }
    apply_rate_limits(u_seq, request.constraints.max_du);

    struct sample_rollout {
        std::vector<planner_vector> noise;
        planner_vector first_action;
        double cost = std::numeric_limits<double>::infinity();
    };

    std::vector<sample_rollout> samples;
    samples.reserve(static_cast<std::size_t>(sample_cap));

    planner_rng rng(request.seed);
    bool timed_out = false;
    for (std::int64_t i = 0; i < sample_cap; ++i) {
        if ((i % 8) == 0) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                timed_out = true;
                break;
            }
        }

        sample_rollout sample;
        sample.noise.assign(horizon, planner_vector(action_dim, 0.0));

        std::vector<planner_vector> actions = u_seq;
        for (std::size_t t = 0; t < horizon; ++t) {
            for (std::size_t d = 0; d < action_dim; ++d) {
                const double e = rng.normal(0.0, sigma[d]);
                sample.noise[t][d] = e;
                actions[t][d] += e;
            }
            actions[t] = clamp_action_with_bounds(actions[t], bounds, model);
        }
        apply_rate_limits(actions, request.constraints.max_du);

        const std::optional<double> cost = rollout_cost_with_step(model, request.state, actions, rng, request.constraints);
        if (!cost.has_value()) {
            continue;
        }

        sample.cost = *cost;
        sample.first_action = actions.front();
        samples.push_back(std::move(sample));
    }

    if (samples.empty()) {
        planner_result empty = make_noaction_result(planner_backend::mppi, safe_action);
        empty.stats.work_done = 0;
        return empty;
    }

    std::vector<std::size_t> order(samples.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
        return samples[lhs].cost < samples[rhs].cost;
    });

    const double cost_min = samples[order.front()].cost;
    std::vector<double> weights(samples.size(), 0.0);

    std::size_t selected = samples.size();
    if (cfg.n_elite > 0) {
        selected = std::min<std::size_t>(samples.size(), static_cast<std::size_t>(cfg.n_elite));
    }

    double weight_sum = 0.0;
    for (std::size_t rank = 0; rank < selected; ++rank) {
        const std::size_t idx = order[rank];
        const double exponent = -(samples[idx].cost - cost_min) / cfg.lambda;
        const double w = std::exp(std::max(-80.0, std::min(80.0, exponent)));
        weights[idx] = std::isfinite(w) ? w : 0.0;
        weight_sum += weights[idx];
    }

    if (weight_sum <= 0.0 || !std::isfinite(weight_sum)) {
        weights.assign(samples.size(), 0.0);
        weights[order.front()] = 1.0;
        weight_sum = 1.0;
    }

    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double w = weights[i] / weight_sum;
        if (w == 0.0) {
            continue;
        }
        for (std::size_t t = 0; t < horizon; ++t) {
            for (std::size_t d = 0; d < action_dim; ++d) {
                u_seq[t][d] += w * samples[i].noise[t][d];
            }
        }
    }

    for (planner_vector& u : u_seq) {
        u = clamp_action_with_bounds(u, bounds, model);
    }
    apply_rate_limits(u_seq, request.constraints.max_du);

    const planner_vector action = u_seq.front();
    result.action = make_action(action_schema, action);
    result.status = timed_out ? planner_status::timeout : planner_status::ok;
    result.stats.work_done = static_cast<std::int64_t>(samples.size());

    double sum_w = 0.0;
    double sum_w_sq = 0.0;
    for (double w : weights) {
        if (w <= 0.0) {
            continue;
        }
        sum_w += w;
        sum_w_sq += w * w;
    }
    double ess_ratio = 0.0;
    if (sum_w > 0.0 && sum_w_sq > 0.0) {
        const double ess = (sum_w * sum_w) / sum_w_sq;
        ess_ratio = ess / static_cast<double>(samples.size());
    }
    result.confidence = clamp_confidence(ess_ratio);

    result.trace.mppi.available = true;
    result.trace.mppi.n_samples = static_cast<std::int64_t>(samples.size());
    result.trace.mppi.horizon = static_cast<std::int64_t>(horizon);

    const std::size_t top_k = static_cast<std::size_t>(std::max<std::int64_t>(0, request.top_k));
    for (std::size_t rank = 0; rank < order.size() && rank < top_k; ++rank) {
        const std::size_t idx = order[rank];
        planner_top_choice_mppi entry;
        entry.action = make_action(action_schema, samples[idx].first_action);
        entry.weight = weights[idx] / weight_sum;
        entry.cost = samples[idx].cost;
        result.trace.mppi.top_k.push_back(std::move(entry));
    }

    return result;
}

bool finite_diff_linearise_dynamics(const planner_model& model,
                                    const planner_vector& x,
                                    const planner_vector& u,
                                    const std::vector<planner_bound>& bounds,
                                    double eps,
                                    planner_linearisation& out) {
    if (x.empty() || u.empty()) {
        return false;
    }

    const std::size_t n = x.size();
    const std::size_t m = u.size();
    out.state_dim = n;
    out.action_dim = m;
    out.A.assign(n * n, 0.0);
    out.B.assign(n * m, 0.0);

    for (std::size_t i = 0; i < n; ++i) {
        planner_vector xp = x;
        planner_vector xm = x;
        xp[i] += eps;
        xm[i] -= eps;

        planner_step_result sp;
        planner_step_result sm;
        if (!deterministic_step_eval(model, xp, u, sp) || !deterministic_step_eval(model, xm, u, sm)) {
            return false;
        }
        if (sp.next_state.size() != n || sm.next_state.size() != n) {
            return false;
        }
        for (std::size_t r = 0; r < n; ++r) {
            out.A[r * n + i] = (sp.next_state[r] - sm.next_state[r]) / (2.0 * eps);
        }
    }

    for (std::size_t j = 0; j < m; ++j) {
        planner_vector up = u;
        planner_vector um = u;
        up[j] += eps;
        um[j] -= eps;
        up = clamp_action_with_bounds(up, bounds, model);
        um = clamp_action_with_bounds(um, bounds, model);

        planner_step_result sp;
        planner_step_result sm;
        if (!deterministic_step_eval(model, x, up, sp) || !deterministic_step_eval(model, x, um, sm)) {
            return false;
        }
        if (sp.next_state.size() != n || sm.next_state.size() != n) {
            return false;
        }
        for (std::size_t r = 0; r < n; ++r) {
            out.B[r * m + j] = (sp.next_state[r] - sm.next_state[r]) / (2.0 * eps);
        }
    }

    return true;
}

bool finite_diff_quadraticise_cost(const planner_model& model,
                                   const planner_vector& x,
                                   const planner_vector& u,
                                   const std::vector<planner_bound>& bounds,
                                   double eps,
                                   planner_quadratic_cost& out) {
    if (x.empty() || u.empty()) {
        return false;
    }

    const std::size_t n = x.size();
    const std::size_t m = u.size();

    out.l_x.assign(n, 0.0);
    out.l_u.assign(m, 0.0);
    out.l_xx.assign(n * n, 0.0);
    out.l_uu.assign(m * m, 0.0);
    out.l_xu.assign(n * m, 0.0);

    const std::optional<double> c0_opt = stage_cost_deterministic(model, x, u);
    if (!c0_opt.has_value()) {
        return false;
    }
    const double c0 = *c0_opt;
    out.l0 = c0;

    for (std::size_t i = 0; i < n; ++i) {
        planner_vector xp = x;
        planner_vector xm = x;
        xp[i] += eps;
        xm[i] -= eps;

        const std::optional<double> cp = stage_cost_deterministic(model, xp, u);
        const std::optional<double> cm = stage_cost_deterministic(model, xm, u);
        if (!cp.has_value() || !cm.has_value()) {
            return false;
        }
        out.l_x[i] = (*cp - *cm) / (2.0 * eps);
        out.l_xx[i * n + i] = (*cp - 2.0 * c0 + *cm) / (eps * eps);
    }

    for (std::size_t j = 0; j < m; ++j) {
        planner_vector up = u;
        planner_vector um = u;
        up[j] += eps;
        um[j] -= eps;
        up = clamp_action_with_bounds(up, bounds, model);
        um = clamp_action_with_bounds(um, bounds, model);

        const std::optional<double> cp = stage_cost_deterministic(model, x, up);
        const std::optional<double> cm = stage_cost_deterministic(model, x, um);
        if (!cp.has_value() || !cm.has_value()) {
            return false;
        }
        out.l_u[j] = (*cp - *cm) / (2.0 * eps);
        out.l_uu[j * m + j] = (*cp - 2.0 * c0 + *cm) / (eps * eps);
    }

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < m; ++j) {
            planner_vector x_p = x;
            planner_vector x_m = x;
            x_p[i] += eps;
            x_m[i] -= eps;

            planner_vector u_p = u;
            planner_vector u_m = u;
            u_p[j] += eps;
            u_m[j] -= eps;
            u_p = clamp_action_with_bounds(u_p, bounds, model);
            u_m = clamp_action_with_bounds(u_m, bounds, model);

            const std::optional<double> cpp = stage_cost_deterministic(model, x_p, u_p);
            const std::optional<double> cpm = stage_cost_deterministic(model, x_p, u_m);
            const std::optional<double> cmp = stage_cost_deterministic(model, x_m, u_p);
            const std::optional<double> cmm = stage_cost_deterministic(model, x_m, u_m);
            if (!cpp.has_value() || !cpm.has_value() || !cmp.has_value() || !cmm.has_value()) {
                return false;
            }
            out.l_xu[i * m + j] = (*cpp - *cpm - *cmp + *cmm) / (4.0 * eps * eps);
        }
    }

    return true;
}

planner_result run_ilqr_backend(const planner_request& request,
                                const planner_model& model,
                                const std::vector<planner_bound>& bounds,
                                const planner_action& safe_action,
                                const std::string& action_schema,
                                std::chrono::steady_clock::time_point deadline) {
    planner_result result;
    result.planner = planner_backend::ilqr;
    result.action = safe_action;

    const std::size_t action_dim = model.action_dims();
    const std::size_t state_dim = request.state.size();
    if (action_dim == 0 || state_dim == 0) {
        return make_error_result(planner_backend::ilqr, safe_action, "ilqr: invalid model dimensions");
    }

    std::int64_t horizon_i = request.horizon > 0 ? request.horizon : model.default_horizon();
    if (horizon_i <= 0) {
        horizon_i = 20;
    }
    const std::size_t horizon = static_cast<std::size_t>(horizon_i);

    planner_ilqr_config cfg = request.ilqr;
    cfg.max_iters = std::max<std::int64_t>(1, cfg.max_iters);
    cfg.reg_init = std::max(1.0e-8, cfg.reg_init);
    cfg.reg_factor = std::max(1.2, cfg.reg_factor);
    cfg.tol_cost = std::max(0.0, cfg.tol_cost);
    cfg.tol_grad = std::max(0.0, cfg.tol_grad);
    cfg.fd_eps = std::max(1.0e-8, cfg.fd_eps);

    std::int64_t max_iters = cfg.max_iters;
    if (request.work_max > 0) {
        max_iters = std::min(max_iters, request.work_max);
    }
    max_iters = std::max<std::int64_t>(1, max_iters);

    std::vector<planner_vector> u_seq;
    if (!cfg.u_init.empty()) {
        u_seq = unpack_sequence(cfg.u_init, action_dim, horizon);
    } else {
        u_seq.assign(horizon, planner_vector(action_dim, 0.0));
    }
    for (planner_vector& u : u_seq) {
        u = clamp_action_with_bounds(u, bounds, model);
    }
    apply_rate_limits(u_seq, request.constraints.max_du);

    auto rollout = [&](const std::vector<planner_vector>& controls,
                       std::vector<planner_vector>& x_seq,
                       double& total_cost) -> bool {
        x_seq.assign(horizon + 1, planner_vector{});
        x_seq[0] = request.state;
        total_cost = 0.0;
        for (std::size_t t = 0; t < horizon; ++t) {
            const planner_vector u = clamp_action_with_bounds(controls[t], bounds, model);
            planner_vector next;
            const std::optional<double> c = stage_cost_deterministic(model, x_seq[t], u, &next);
            if (!c.has_value()) {
                return false;
            }
            total_cost += *c;
            x_seq[t + 1] = std::move(next);
        }
        return std::isfinite(total_cost);
    };

    if (cfg.derivatives == planner_ilqr_derivatives_mode::finite_diff) {
        const planner_vector probe = clamp_action_with_bounds(u_seq.front(), bounds, model);
        planner_step_result s1;
        planner_step_result s2;
        if (!deterministic_step_eval(model, request.state, probe, s1) ||
            !deterministic_step_eval(model, request.state, probe, s2) ||
            !vectors_close(s1.next_state, s2.next_state, 1.0e-9) ||
            std::fabs(s1.reward - s2.reward) > 1.0e-9 || s1.done != s2.done) {
            return make_error_result(planner_backend::ilqr,
                                     safe_action,
                                     "ilqr: finite_diff requires deterministic step(state, action)");
        }
    }

    std::vector<planner_vector> x_seq;
    double cost = 0.0;
    if (!rollout(u_seq, x_seq, cost)) {
        return make_error_result(planner_backend::ilqr, safe_action, "ilqr: initial rollout failed");
    }

    const double cost_init = cost;
    double reg = cfg.reg_init;
    bool timed_out = false;
    bool converged = false;
    std::int64_t completed_iters = 0;

    std::vector<planner_vector> k_seq(horizon, planner_vector(action_dim, 0.0));
    std::vector<std::vector<double>> k_gain(horizon, std::vector<double>(action_dim * state_dim, 0.0));

    for (std::int64_t iter = 0; iter < max_iters; ++iter) {
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            break;
        }

        std::vector<planner_linearisation> dyn(horizon);
        std::vector<planner_quadratic_cost> stage(horizon);
        planner_terminal_quadratic_cost terminal;

        bool derivative_ok = true;
        for (std::size_t t = 0; t < horizon; ++t) {
            const planner_vector u = clamp_action_with_bounds(u_seq[t], bounds, model);
            if (cfg.derivatives == planner_ilqr_derivatives_mode::analytic) {
                if (!model.linearise_dynamics(x_seq[t], u, dyn[t])) {
                    result.error = "ilqr: analytic derivatives unavailable (linearise_dynamics)";
                    derivative_ok = false;
                    break;
                }
                if (!model.quadraticise_cost(x_seq[t], u, stage[t])) {
                    result.error = "ilqr: analytic derivatives unavailable (quadraticise_cost)";
                    derivative_ok = false;
                    break;
                }
            } else {
                if (!finite_diff_linearise_dynamics(model, x_seq[t], u, bounds, cfg.fd_eps, dyn[t])) {
                    result.error = "ilqr: finite-diff dynamics linearisation failed";
                    derivative_ok = false;
                    break;
                }
                if (!finite_diff_quadraticise_cost(model, x_seq[t], u, bounds, cfg.fd_eps, stage[t])) {
                    result.error = "ilqr: finite-diff cost quadraticisation failed";
                    derivative_ok = false;
                    break;
                }
            }

            if (dyn[t].state_dim != state_dim || dyn[t].action_dim != action_dim ||
                dyn[t].A.size() != state_dim * state_dim || dyn[t].B.size() != state_dim * action_dim ||
                stage[t].l_x.size() != state_dim || stage[t].l_u.size() != action_dim ||
                stage[t].l_xx.size() != state_dim * state_dim ||
                stage[t].l_uu.size() != action_dim * action_dim ||
                stage[t].l_xu.size() != state_dim * action_dim) {
                result.error = "ilqr: derivative dimensions mismatch";
                derivative_ok = false;
                break;
            }
        }

        if (!derivative_ok) {
            planner_result err = make_error_result(planner_backend::ilqr, safe_action, result.error);
            err.trace.ilqr.available = true;
            err.trace.ilqr.iters = completed_iters;
            err.trace.ilqr.cost_init = cost_init;
            err.trace.ilqr.cost_final = cost;
            err.trace.ilqr.reg_final = reg;
            err.stats.work_done = completed_iters;
            return err;
        }

        if (!model.quadraticise_terminal_cost(x_seq.back(), terminal)) {
            terminal.l0 = 0.0;
            terminal.l_x.assign(state_dim, 0.0);
            terminal.l_xx.assign(state_dim * state_dim, 0.0);
        }
        if (terminal.l_x.size() != state_dim || terminal.l_xx.size() != state_dim * state_dim) {
            terminal.l_x.assign(state_dim, 0.0);
            terminal.l_xx.assign(state_dim * state_dim, 0.0);
        }

        planner_vector v_x = terminal.l_x;
        std::vector<double> v_xx = terminal.l_xx;

        double grad_inf_norm = 0.0;
        bool backward_ok = true;

        for (std::size_t t = horizon; t-- > 0;) {
            const std::vector<double>& a = dyn[t].A;
            const std::vector<double>& b = dyn[t].B;
            const planner_quadratic_cost& l = stage[t];

            planner_vector q_x(state_dim, 0.0);
            planner_vector q_u(action_dim, 0.0);
            std::vector<double> q_xx(state_dim * state_dim, 0.0);
            std::vector<double> q_uu(action_dim * action_dim, 0.0);
            std::vector<double> q_ux(action_dim * state_dim, 0.0);

            for (std::size_t i = 0; i < state_dim; ++i) {
                double acc = l.l_x[i];
                for (std::size_t r = 0; r < state_dim; ++r) {
                    acc += a[r * state_dim + i] * v_x[r];
                }
                q_x[i] = acc;
            }

            for (std::size_t j = 0; j < action_dim; ++j) {
                double acc = l.l_u[j];
                for (std::size_t r = 0; r < state_dim; ++r) {
                    acc += b[r * action_dim + j] * v_x[r];
                }
                q_u[j] = acc;
                grad_inf_norm = std::max(grad_inf_norm, std::fabs(acc));
            }

            for (std::size_t i = 0; i < state_dim; ++i) {
                for (std::size_t j = 0; j < state_dim; ++j) {
                    double acc = l.l_xx[i * state_dim + j];
                    for (std::size_t r = 0; r < state_dim; ++r) {
                        for (std::size_t c = 0; c < state_dim; ++c) {
                            acc += a[r * state_dim + i] * v_xx[r * state_dim + c] * a[c * state_dim + j];
                        }
                    }
                    q_xx[i * state_dim + j] = acc;
                }
            }

            for (std::size_t i = 0; i < action_dim; ++i) {
                for (std::size_t j = 0; j < action_dim; ++j) {
                    double acc = l.l_uu[i * action_dim + j];
                    for (std::size_t r = 0; r < state_dim; ++r) {
                        for (std::size_t c = 0; c < state_dim; ++c) {
                            acc += b[r * action_dim + i] * v_xx[r * state_dim + c] * b[c * action_dim + j];
                        }
                    }
                    if (i == j) {
                        acc += reg;
                    }
                    q_uu[i * action_dim + j] = acc;
                }
            }

            for (std::size_t i = 0; i < action_dim; ++i) {
                for (std::size_t j = 0; j < state_dim; ++j) {
                    double acc = l.l_xu[j * action_dim + i];
                    for (std::size_t r = 0; r < state_dim; ++r) {
                        for (std::size_t c = 0; c < state_dim; ++c) {
                            acc += b[r * action_dim + i] * v_xx[r * state_dim + c] * a[c * state_dim + j];
                        }
                    }
                    q_ux[i * state_dim + j] = acc;
                }
            }

            std::vector<double> a_sys = q_uu;
            std::vector<double> rhs(action_dim * (1 + state_dim), 0.0);
            for (std::size_t i = 0; i < action_dim; ++i) {
                rhs[i * (1 + state_dim)] = -q_u[i];
                for (std::size_t j = 0; j < state_dim; ++j) {
                    rhs[i * (1 + state_dim) + 1 + j] = -q_ux[i * state_dim + j];
                }
            }

            if (!solve_linear_system(a_sys, rhs, action_dim, 1 + state_dim)) {
                backward_ok = false;
                break;
            }

            planner_vector k(action_dim, 0.0);
            std::vector<double> k_mat(action_dim * state_dim, 0.0);
            for (std::size_t i = 0; i < action_dim; ++i) {
                k[i] = rhs[i * (1 + state_dim)];
                for (std::size_t j = 0; j < state_dim; ++j) {
                    k_mat[i * state_dim + j] = rhs[i * (1 + state_dim) + 1 + j];
                }
            }
            k_seq[t] = k;
            k_gain[t] = k_mat;

            planner_vector next_v_x(state_dim, 0.0);
            for (std::size_t i = 0; i < state_dim; ++i) {
                double term1 = 0.0;
                double term2 = 0.0;
                double term3 = 0.0;
                for (std::size_t a_i = 0; a_i < action_dim; ++a_i) {
                    term1 += k_mat[a_i * state_dim + i] * q_u[a_i];
                    term2 += q_ux[a_i * state_dim + i] * k[a_i];
                    for (std::size_t b_i = 0; b_i < action_dim; ++b_i) {
                        term3 += k_mat[a_i * state_dim + i] * q_uu[a_i * action_dim + b_i] * k[b_i];
                    }
                }
                next_v_x[i] = q_x[i] + term1 + term2 + term3;
            }

            std::vector<double> next_v_xx(state_dim * state_dim, 0.0);
            for (std::size_t i = 0; i < state_dim; ++i) {
                for (std::size_t j = 0; j < state_dim; ++j) {
                    double acc = q_xx[i * state_dim + j];
                    for (std::size_t a_i = 0; a_i < action_dim; ++a_i) {
                        for (std::size_t b_i = 0; b_i < action_dim; ++b_i) {
                            acc += k_mat[a_i * state_dim + i] * q_uu[a_i * action_dim + b_i] *
                                   k_mat[b_i * state_dim + j];
                        }
                        acc += k_mat[a_i * state_dim + i] * q_ux[a_i * state_dim + j];
                        acc += q_ux[a_i * state_dim + i] * k_mat[a_i * state_dim + j];
                    }
                    next_v_xx[i * state_dim + j] = acc;
                }
            }

            for (std::size_t i = 0; i < state_dim; ++i) {
                for (std::size_t j = i + 1; j < state_dim; ++j) {
                    const double sym = 0.5 * (next_v_xx[i * state_dim + j] + next_v_xx[j * state_dim + i]);
                    next_v_xx[i * state_dim + j] = sym;
                    next_v_xx[j * state_dim + i] = sym;
                }
            }

            v_x = std::move(next_v_x);
            v_xx = std::move(next_v_xx);
        }

        if (!backward_ok) {
            reg = std::min(1.0e8, reg * cfg.reg_factor);
            if (reg >= 1.0e8) {
                result.error = "ilqr: backward pass failed (ill-conditioned Q_uu)";
                planner_result err = make_error_result(planner_backend::ilqr, safe_action, result.error);
                err.trace.ilqr.available = true;
                err.trace.ilqr.iters = completed_iters;
                err.trace.ilqr.cost_init = cost_init;
                err.trace.ilqr.cost_final = cost;
                err.trace.ilqr.reg_final = reg;
                err.stats.work_done = completed_iters;
                return err;
            }
            continue;
        }

        bool accepted = false;
        std::array<double, 6> alphas = {1.0, 0.5, 0.25, 0.1, 0.05, 0.01};
        std::vector<planner_vector> best_u;
        std::vector<planner_vector> best_x;
        double best_cost = cost;

        for (double alpha : alphas) {
            std::vector<planner_vector> cand_u = u_seq;
            std::vector<planner_vector> cand_x(horizon + 1, planner_vector{});
            cand_x[0] = request.state;

            bool bad = false;
            double cand_cost = 0.0;
            for (std::size_t t = 0; t < horizon; ++t) {
                planner_vector du(action_dim, 0.0);
                for (std::size_t i = 0; i < action_dim; ++i) {
                    double val = alpha * k_seq[t][i];
                    for (std::size_t j = 0; j < state_dim; ++j) {
                        const double dx = cand_x[t][j] - x_seq[t][j];
                        val += k_gain[t][i * state_dim + j] * dx;
                    }
                    du[i] = val;
                }

                for (std::size_t i = 0; i < action_dim; ++i) {
                    cand_u[t][i] += du[i];
                }
                cand_u[t] = clamp_action_with_bounds(cand_u[t], bounds, model);

                if (t > 0 && !request.constraints.max_du.empty()) {
                    const std::size_t dims = std::min({action_dim, request.constraints.max_du.size(), cand_u[t - 1].size()});
                    for (std::size_t d = 0; d < dims; ++d) {
                        const double lim = std::fabs(request.constraints.max_du[d]);
                        if (!std::isfinite(lim) || lim <= 0.0) {
                            continue;
                        }
                        const double delta = cand_u[t][d] - cand_u[t - 1][d];
                        if (delta > lim) {
                            cand_u[t][d] = cand_u[t - 1][d] + lim;
                        } else if (delta < -lim) {
                            cand_u[t][d] = cand_u[t - 1][d] - lim;
                        }
                    }
                }

                planner_vector next;
                const std::optional<double> c = stage_cost_deterministic(model, cand_x[t], cand_u[t], &next);
                if (!c.has_value()) {
                    bad = true;
                    break;
                }
                cand_cost += *c;
                cand_x[t + 1] = std::move(next);
            }

            if (!bad && std::isfinite(cand_cost) && cand_cost < best_cost) {
                accepted = true;
                best_cost = cand_cost;
                best_u = std::move(cand_u);
                best_x = std::move(cand_x);
                break;
            }
        }

        if (accepted) {
            const double improvement = cost - best_cost;
            u_seq = std::move(best_u);
            x_seq = std::move(best_x);
            cost = best_cost;
            reg = std::max(1.0e-8, reg / cfg.reg_factor);
            ++completed_iters;

            if (improvement <= cfg.tol_cost || grad_inf_norm <= cfg.tol_grad) {
                converged = true;
                break;
            }
        } else {
            reg = std::min(1.0e8, reg * cfg.reg_factor);
            ++completed_iters;
            if (reg >= 1.0e8) {
                break;
            }
        }
    }

    result.action = make_action(action_schema, clamp_action_with_bounds(u_seq.front(), bounds, model));
    result.confidence = clamp_confidence((cost_init - cost) / std::max(1.0, std::fabs(cost_init)) + (converged ? 0.2 : 0.0));
    result.status = timed_out ? planner_status::timeout : planner_status::ok;
    result.stats.work_done = completed_iters;

    result.trace.ilqr.available = true;
    result.trace.ilqr.iters = completed_iters;
    result.trace.ilqr.cost_init = cost_init;
    result.trace.ilqr.cost_final = cost;
    result.trace.ilqr.reg_final = reg;

    if (result.status != planner_status::ok && result.status != planner_status::timeout) {
        result.status = planner_status::error;
    }

    return result;
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
    const double theta = 2.0 * kPi * u2;
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

std::vector<planner_bound> planner_model::action_bounds() const {
    return {};
}

std::int64_t planner_model::default_horizon() const {
    return 20;
}

std::int64_t planner_model::default_dt_ms() const {
    return 0;
}

bool planner_model::rollout_cost(const planner_vector&, const std::vector<planner_vector>&, double&) const {
    return false;
}

bool planner_model::deterministic_step(const planner_vector&, const planner_vector&, planner_step_result&) const {
    return false;
}

bool planner_model::linearise_dynamics(const planner_vector&, const planner_vector&, planner_linearisation&) const {
    return false;
}

bool planner_model::quadraticise_cost(const planner_vector&, const planner_vector&, planner_quadratic_cost&) const {
    return false;
}

bool planner_model::quadraticise_terminal_cost(const planner_vector&, planner_terminal_quadratic_cost&) const {
    return false;
}

planner_service::planner_service() {
    records_.reserve(record_capacity_);
    register_model("toy-1d", std::make_shared<toy_1d_model>());
    register_model("ptz-track", std::make_shared<ptz_track_model>());
    register_model("toy-unicycle", std::make_shared<toy_unicycle_goal_model>());
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
    const auto start = std::chrono::steady_clock::now();

    planner_result result;
    result.planner = request.planner;
    result.stats.budget_ms = std::max<std::int64_t>(0, request.budget_ms);
    result.stats.seed = request.seed;

    if (request.schema_version != "planner.request.v1") {
        result = make_error_result(request.planner, planner_action{}, "planner: unsupported request schema_version");
    }

    std::shared_ptr<planner_model> model;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = models_.find(request.model_service);
        if (it != models_.end()) {
            model = it->second;
        }
    }

    std::vector<planner_bound> bounds;
    planner_action safe_action;
    std::string action_schema = resolve_action_schema(request);

    if (model) {
        bounds = resolve_bounds(request, *model);
        safe_action = resolve_safe_action(request, *model, bounds, action_schema);
    } else {
        safe_action.action_schema = action_schema;
    }

    if (!model) {
        result = make_error_result(request.planner, safe_action, "planner model not found: " + request.model_service);
    } else if (!model->validate_state(request.state)) {
        result = make_error_result(request.planner, safe_action, "planner state validation failed");
    } else {
        const auto deadline = start + std::chrono::milliseconds(std::max<std::int64_t>(0, request.budget_ms));
        try {
            switch (request.planner) {
                case planner_backend::mcts:
                    result = run_mcts_backend(request, *model, bounds, safe_action, action_schema, deadline);
                    break;
                case planner_backend::mppi:
                    result = run_mppi_backend(request, *model, bounds, safe_action, action_schema, deadline);
                    break;
                case planner_backend::ilqr:
                    result = run_ilqr_backend(request, *model, bounds, safe_action, action_schema, deadline);
                    break;
            }
        } catch (const std::exception& e) {
            result = make_error_result(request.planner, safe_action, std::string("planner backend threw: ") + e.what());
        }
    }

    if (result.action.action_schema.empty()) {
        result.action.action_schema = action_schema;
    }
    if (result.action.u.empty() && model) {
        result.action.u = safe_action.u;
    }
    if (model) {
        if (result.action.u.size() != model->action_dims() || !vector_all_finite(result.action.u)) {
            result.action = safe_action;
            if (result.status != planner_status::error) {
                result.status = planner_status::error;
                if (result.error.empty()) {
                    result.error = "planner action dimensionality/finite mismatch";
                }
            }
        } else {
            result.action.u = clamp_action_with_bounds(result.action.u, bounds, *model);
        }
    }

    result.confidence = clamp_confidence(result.confidence);

    const auto end = std::chrono::steady_clock::now();
    result.stats.budget_ms = std::max<std::int64_t>(0, request.budget_ms);
    result.stats.time_used_ms = elapsed_ms(start, end);
    result.stats.seed = request.seed;
    result.stats.overrun = result.stats.time_used_ms > result.stats.budget_ms;

    planner_record rec;
    rec.schema_version = "planner.v1";
    rec.ts_ms = now_ms();
    rec.run_id = request.run_id;
    rec.tick_index = request.tick_index;
    rec.node_name = request.node_name;
    rec.planner = result.planner;
    rec.status = result.status;
    rec.budget_ms = result.stats.budget_ms;
    rec.time_used_ms = result.stats.time_used_ms;
    rec.work_done = result.stats.work_done;
    rec.action = result.action;
    rec.confidence = result.confidence;
    rec.seed = result.stats.seed;
    rec.overrun = result.stats.overrun;
    rec.note = result.stats.note;
    rec.trace = result.trace;
    rec.state_key = request.state_key;
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
    const std::string schema = record.schema_version.empty() ? "planner.v1" : record.schema_version;

    std::ostringstream out;
    out << '{'
        << "\"schema_version\":\"" << json_escape(schema) << "\",";

    out << "\"ts_ms\":" << record.ts_ms << ','
        << "\"run_id\":\"" << json_escape(record.run_id) << "\","
        << "\"tick_index\":" << record.tick_index << ','
        << "\"node_name\":\"" << json_escape(record.node_name) << "\","
        << "\"planner\":\"" << planner_backend_name(record.planner) << "\","
        << "\"status\":\"" << planner_status_name(record.status) << "\","
        << "\"budget_ms\":" << record.budget_ms << ','
        << "\"time_used_ms\":" << record.time_used_ms << ','
        << "\"work_done\":" << record.work_done << ','
        << "\"action\":" << action_to_json(record.action) << ','
        << "\"confidence\":" << record.confidence << ','
        << "\"seed\":" << record.seed;

    if (record.overrun) {
        out << ",\"overrun\":true";
    }
    if (!record.note.empty()) {
        out << ",\"note\":\"" << json_escape(record.note) << "\"";
    }
    if (!record.state_key.empty()) {
        out << ",\"state_key\":\"" << json_escape(record.state_key) << "\"";
    }

    if (record.planner == planner_backend::mcts && record.trace.mcts.available) {
        out << ",\"trace\":{";
        out << "\"root_visits\":" << record.trace.mcts.root_visits << ','
            << "\"root_children\":" << record.trace.mcts.root_children << ','
            << "\"widen_added\":" << record.trace.mcts.widen_added;
        if (!record.trace.mcts.top_k.empty()) {
            out << ",\"top_k\":[";
            for (std::size_t i = 0; i < record.trace.mcts.top_k.size(); ++i) {
                if (i != 0) {
                    out << ',';
                }
                const planner_top_choice_mcts& entry = record.trace.mcts.top_k[i];
                out << '{' << "\"action\":" << action_to_json(entry.action) << ",\"visits\":" << entry.visits
                    << ",\"q\":" << entry.q << '}';
            }
            out << ']';
        }
        out << '}';
    } else if (record.planner == planner_backend::mppi && record.trace.mppi.available) {
        out << ",\"trace\":{";
        out << "\"n_samples\":" << record.trace.mppi.n_samples << ','
            << "\"horizon\":" << record.trace.mppi.horizon;
        if (!record.trace.mppi.top_k.empty()) {
            out << ",\"top_k\":[";
            for (std::size_t i = 0; i < record.trace.mppi.top_k.size(); ++i) {
                if (i != 0) {
                    out << ',';
                }
                const planner_top_choice_mppi& entry = record.trace.mppi.top_k[i];
                out << '{' << "\"action\":" << action_to_json(entry.action) << ",\"weight\":" << entry.weight
                    << ",\"cost\":" << entry.cost << '}';
            }
            out << ']';
        }
        out << '}';
    } else if (record.planner == planner_backend::ilqr && record.trace.ilqr.available) {
        out << ",\"trace\":{";
        out << "\"iters\":" << record.trace.ilqr.iters << ','
            << "\"cost_init\":" << record.trace.ilqr.cost_init << ','
            << "\"cost_final\":" << record.trace.ilqr.cost_final << ','
            << "\"reg_final\":" << record.trace.ilqr.reg_final;
        if (!record.trace.ilqr.top_k.empty()) {
            out << ",\"top_k\":[";
            for (std::size_t i = 0; i < record.trace.ilqr.top_k.size(); ++i) {
                if (i != 0) {
                    out << ',';
                }
                const planner_top_choice_mppi& entry = record.trace.ilqr.top_k[i];
                out << '{' << "\"action\":" << action_to_json(entry.action) << ",\"weight\":" << entry.weight
                    << ",\"cost\":" << entry.cost << '}';
            }
            out << ']';
        }
        out << '}';
    }

    out << '}';
    return out.str();
}

void planner_service::append_jsonl_line(const std::string& line) {
    const std::string path = jsonl_path_;
    std::lock_guard<std::mutex> lock(file_mutex_);
    const std::filesystem::path fs_path(path);
    if (fs_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }
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

const char* planner_backend_name(planner_backend planner) noexcept {
    switch (planner) {
        case planner_backend::mcts:
            return "mcts";
        case planner_backend::mppi:
            return "mppi";
        case planner_backend::ilqr:
            return "ilqr";
    }
    return "mcts";
}

const char* planner_ilqr_derivatives_mode_name(planner_ilqr_derivatives_mode mode) noexcept {
    switch (mode) {
        case planner_ilqr_derivatives_mode::analytic:
            return "analytic";
        case planner_ilqr_derivatives_mode::finite_diff:
            return "finite_diff";
    }
    return "analytic";
}

bool planner_backend_from_string(std::string_view text, planner_backend& out) noexcept {
    const std::string norm = normalized_token(text);
    if (norm == "mcts") {
        out = planner_backend::mcts;
        return true;
    }
    if (norm == "mppi") {
        out = planner_backend::mppi;
        return true;
    }
    if (norm == "ilqr") {
        out = planner_backend::ilqr;
        return true;
    }
    return false;
}

bool planner_ilqr_derivatives_mode_from_string(std::string_view text,
                                               planner_ilqr_derivatives_mode& out) noexcept {
    const std::string norm = normalized_token(text);
    if (norm == "analytic") {
        out = planner_ilqr_derivatives_mode::analytic;
        return true;
    }
    if (norm == "finite_diff" || norm == "fd") {
        out = planner_ilqr_derivatives_mode::finite_diff;
        return true;
    }
    return false;
}

}  // namespace bt
