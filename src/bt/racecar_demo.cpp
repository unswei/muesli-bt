#include "bt/racecar_demo.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "bt/status.hpp"
#include "muslisp/value.hpp"

namespace bt {
namespace {

struct racecar_demo_state {
    std::shared_ptr<racecar_sim_adapter> adapter;
    std::mutex mutex;
};

racecar_demo_state& global_demo_state() {
    static racecar_demo_state state;
    return state;
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

double wrap_angle(double angle) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 6.28318530717958647692;
    while (angle > kPi) {
        angle -= kTwoPi;
    }
    while (angle < -kPi) {
        angle += kTwoPi;
    }
    return angle;
}

double finite_or_throw(double value, const std::string& where) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(where + ": expected finite numeric value");
    }
    return value;
}

const std::vector<double>* as_vector(const bb_value& value) {
    return std::get_if<std::vector<double>>(&value);
}

std::string require_key_like(const muslisp::value& arg, const std::string& where) {
    if (muslisp::is_symbol(arg)) {
        return muslisp::symbol_name(arg);
    }
    if (muslisp::is_string(arg)) {
        return muslisp::string_value(arg);
    }
    throw std::runtime_error(where + ": expected symbol/string key argument");
}

double require_number(const muslisp::value& arg, const std::string& where) {
    if (muslisp::is_integer(arg)) {
        return static_cast<double>(muslisp::integer_value(arg));
    }
    if (muslisp::is_float(arg)) {
        return muslisp::float_value(arg);
    }
    throw std::runtime_error(where + ": expected numeric argument");
}

bool read_bb_bool(const bb_entry* entry) {
    if (!entry) {
        return false;
    }
    if (const auto* b = std::get_if<bool>(&entry->value)) {
        return *b;
    }
    if (const auto* i = std::get_if<std::int64_t>(&entry->value)) {
        return *i != 0;
    }
    if (const auto* f = std::get_if<double>(&entry->value)) {
        return *f != 0.0;
    }
    return false;
}

double distance_to_goal(const racecar_state& state) {
    if (state.goal.size() < 2) {
        return std::numeric_limits<double>::infinity();
    }
    const double dx = state.goal[0] - state.x;
    const double dy = state.goal[1] - state.y;
    return std::hypot(dx, dy);
}

bool read_bb_number(const bb_entry* entry, double& out) {
    if (!entry) {
        return false;
    }
    if (const auto* i = std::get_if<std::int64_t>(&entry->value)) {
        out = static_cast<double>(*i);
        return true;
    }
    if (const auto* f = std::get_if<double>(&entry->value)) {
        out = *f;
        return true;
    }
    return false;
}

void validate_state_schema(const racecar_state& state) {
    if (state.state_schema != "racecar_state.v1") {
        throw std::runtime_error("sim.get-state: state_schema must be racecar_state.v1");
    }
    if (state.goal.size() < 2) {
        throw std::runtime_error("sim.get-state: goal must be [gx gy]");
    }
    if (state.state_vec.empty()) {
        throw std::runtime_error("sim.get-state: state_vec must not be empty");
    }
    for (double v : state.state_vec) {
        if (!std::isfinite(v)) {
            throw std::runtime_error("sim.get-state: state_vec contains non-finite value");
        }
    }
    for (double v : state.rays) {
        if (!std::isfinite(v)) {
            throw std::runtime_error("sim.get-state: rays contains non-finite value");
        }
    }
    (void)finite_or_throw(state.x, "sim.get-state x");
    (void)finite_or_throw(state.y, "sim.get-state y");
    (void)finite_or_throw(state.yaw, "sim.get-state yaw");
    (void)finite_or_throw(state.speed, "sim.get-state speed");
}

struct extracted_action {
    double steering = 0.0;
    double throttle = 0.0;
    bool used_fallback = true;
};

extracted_action extract_action(const bb_entry* entry) {
    extracted_action out;
    out.steering = 0.0;
    out.throttle = 0.0;
    out.used_fallback = true;

    if (!entry) {
        return out;
    }

    const auto* vec = as_vector(entry->value);
    if (!vec || vec->size() < 2) {
        return out;
    }

    const double s = (*vec)[0];
    const double t = (*vec)[1];
    if (!std::isfinite(s) || !std::isfinite(t)) {
        return out;
    }

    out.steering = clamp_double(s, -1.0, 1.0);
    out.throttle = clamp_double(t, -1.0, 1.0);
    out.used_fallback = false;
    return out;
}

void write_state_to_blackboard(instance& inst,
                               const racecar_state& state,
                               const racecar_loop_options& options,
                               std::uint64_t tick_index,
                               std::chrono::steady_clock::time_point now) {
    inst.bb.put(options.action_key, bb_value{std::monostate{}}, tick_index, now, 0, "sim.run-loop");
    if (!options.planner_meta_key.empty()) {
        inst.bb.put(options.planner_meta_key, bb_value{std::monostate{}}, tick_index, now, 0, "sim.run-loop");
    }
    inst.bb.put(options.state_key, bb_value{state.state_vec}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("state_schema", bb_value{state.state_schema}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("x", bb_value{state.x}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("y", bb_value{state.y}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("yaw", bb_value{state.yaw}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("speed", bb_value{state.speed}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("rays", bb_value{state.rays}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("goal", bb_value{state.goal}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("collision_imminent", bb_value{state.collision_imminent}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("collision_count", bb_value{state.collision_count}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("t_ms", bb_value{state.t_ms}, tick_index, now, 0, "sim.run-loop");
    inst.bb.put("distance_to_goal", bb_value{distance_to_goal(state)}, tick_index, now, 0, "sim.run-loop");
}

}  // namespace

void set_racecar_sim_adapter(std::shared_ptr<racecar_sim_adapter> adapter) {
    racecar_demo_state& state = global_demo_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.adapter = std::move(adapter);
}

std::shared_ptr<racecar_sim_adapter> racecar_sim_adapter_ptr() {
    racecar_demo_state& state = global_demo_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.adapter;
}

void clear_racecar_demo_state() {
    racecar_demo_state& state = global_demo_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.adapter.reset();
}

racecar_state racecar_get_state() {
    const std::shared_ptr<racecar_sim_adapter> adapter = racecar_sim_adapter_ptr();
    if (!adapter) {
        throw std::runtime_error("sim.get-state: racecar sim adapter is not installed");
    }
    racecar_state state = adapter->get_state();
    validate_state_schema(state);
    return state;
}

void racecar_apply_action(double steering, double throttle) {
    const std::shared_ptr<racecar_sim_adapter> adapter = racecar_sim_adapter_ptr();
    if (!adapter) {
        throw std::runtime_error("sim.apply-action: racecar sim adapter is not installed");
    }
    adapter->apply_action(clamp_double(steering, -1.0, 1.0), clamp_double(throttle, -1.0, 1.0));
}

void racecar_step(std::int64_t steps) {
    const std::shared_ptr<racecar_sim_adapter> adapter = racecar_sim_adapter_ptr();
    if (!adapter) {
        throw std::runtime_error("sim.step: racecar sim adapter is not installed");
    }
    if (steps < 1) {
        throw std::runtime_error("sim.step: steps must be >= 1");
    }
    adapter->step(steps);
}

void racecar_reset() {
    const std::shared_ptr<racecar_sim_adapter> adapter = racecar_sim_adapter_ptr();
    if (!adapter) {
        throw std::runtime_error("sim.reset: racecar sim adapter is not installed");
    }
    adapter->reset();
}

void racecar_debug_draw() {
    const std::shared_ptr<racecar_sim_adapter> adapter = racecar_sim_adapter_ptr();
    if (!adapter) {
        throw std::runtime_error("sim.debug-draw: racecar sim adapter is not installed");
    }
    adapter->debug_draw();
}

const char* racecar_loop_status_name(racecar_loop_status status) noexcept {
    switch (status) {
        case racecar_loop_status::ok:
            return "ok";
        case racecar_loop_status::stopped:
            return "stopped";
        case racecar_loop_status::error:
            return "error";
    }
    return "error";
}

racecar_loop_result run_racecar_loop(runtime_host& host, std::int64_t instance_handle, const racecar_loop_options& options) {
    if (options.tick_hz <= 0.0 || !std::isfinite(options.tick_hz)) {
        throw std::runtime_error("sim.run-loop: :tick_hz must be finite and > 0");
    }
    if (options.max_ticks <= 0) {
        throw std::runtime_error("sim.run-loop: :max_ticks must be > 0");
    }
    if (options.steps_per_tick <= 0) {
        throw std::runtime_error("sim.run-loop: :steps_per_tick must be > 0");
    }
    if (options.state_key.empty() || options.action_key.empty()) {
        throw std::runtime_error("sim.run-loop: :state_key and :action_key must be non-empty");
    }

    const std::shared_ptr<racecar_sim_adapter> adapter = racecar_sim_adapter_ptr();
    if (!adapter) {
        throw std::runtime_error("sim.run-loop: racecar sim adapter is not installed");
    }

    instance* inst = host.find_instance(instance_handle);
    if (!inst) {
        throw std::runtime_error("sim.run-loop: unknown bt instance");
    }

    const auto tick_period = std::chrono::duration<double>(1.0 / options.tick_hz);
    auto next_deadline = std::chrono::steady_clock::now();
    const auto wall_start = next_deadline;

    racecar_loop_result result;
    result.status = racecar_loop_status::stopped;
    result.reason = "max ticks reached";

    racecar_state last_state{};
    bool have_last_state = false;
    std::int64_t collisions_total = 0;
    std::int64_t fallback_count = 0;
    std::int64_t ticks = 0;

    const double safe_steer = clamp_double(options.safe_action[0], -1.0, 1.0);
    const double safe_throttle = clamp_double(options.safe_action[1], -1.0, 1.0);

    for (std::int64_t i = 0; i < options.max_ticks; ++i) {
        if (adapter->stop_requested()) {
            result.status = racecar_loop_status::stopped;
            result.reason = "stop requested";
            result.ticks = ticks;
            result.goal_reached = false;
            result.collisions_total = collisions_total;
            result.fallback_count = fallback_count;
            if (have_last_state) {
                result.final_state = last_state;
            }
            return result;
        }

        racecar_tick_record tick_record;
        tick_record.run_id = options.run_id;
        tick_record.mode = options.mode;
        tick_record.tick_index = ticks + 1;

        try {
            const racecar_state state = adapter->get_state();
            validate_state_schema(state);
            last_state = state;
            have_last_state = true;
            collisions_total = std::max(collisions_total, state.collision_count);

            const auto now = std::chrono::steady_clock::now();
            write_state_to_blackboard(*inst, state, options, inst->tick_index + 1, now);

            const status bt_status = host.tick_instance(instance_handle);
            const bb_entry* action_entry = inst->bb.get(options.action_key);
            extracted_action action = extract_action(action_entry);
            if (action.used_fallback) {
                ++fallback_count;
                action.steering = 0.0;
                action.throttle = 0.0;
            }

            adapter->apply_action(action.steering, action.throttle);
            adapter->step(options.steps_per_tick);
            if (options.draw_debug) {
                adapter->debug_draw();
            }

            ++ticks;
            const double dist = distance_to_goal(state);
            const bool goal_reached = std::isfinite(dist) && dist <= options.goal_tolerance;

            tick_record.state = state;
            tick_record.sim_time_s = static_cast<double>(state.t_ms) / 1000.0;
            tick_record.wall_time_s =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - wall_start).count() /
                1000000.0;
            tick_record.distance_to_goal = dist;
            tick_record.steering = action.steering;
            tick_record.throttle = action.throttle;
            tick_record.collisions_total = collisions_total;
            tick_record.goal_reached = goal_reached;
            tick_record.bt_status = status_name(bt_status);
            tick_record.used_fallback = action.used_fallback;

            if (!options.planner_meta_key.empty()) {
                const bb_entry* meta_entry = inst->bb.get(options.planner_meta_key);
                if (meta_entry && std::holds_alternative<std::string>(meta_entry->value)) {
                    tick_record.planner_meta_json = std::get<std::string>(meta_entry->value);
                }
            }

            adapter->on_tick_record(tick_record);

            if (goal_reached || bt_status == status::success) {
                result.status = racecar_loop_status::ok;
                result.reason = goal_reached ? "goal reached" : "bt returned success";
                result.ticks = ticks;
                result.final_state = state;
                result.goal_reached = goal_reached;
                result.collisions_total = collisions_total;
                result.fallback_count = fallback_count;
                return result;
            }
        } catch (const std::exception& e) {
            try {
                adapter->apply_action(safe_steer, safe_throttle);
                adapter->step(std::max<std::int64_t>(1, options.steps_per_tick));
            } catch (const std::exception&) {
                // Best-effort safety stop.
            }

            racecar_state error_state;
            if (have_last_state) {
                error_state = last_state;
            } else {
                try {
                    error_state = adapter->get_state();
                } catch (const std::exception&) {
                    error_state = racecar_state{};
                }
            }
            ++ticks;
            collisions_total = std::max(collisions_total, error_state.collision_count);

            tick_record.state = error_state;
            tick_record.sim_time_s = static_cast<double>(error_state.t_ms) / 1000.0;
            tick_record.wall_time_s =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - wall_start).count() /
                1000000.0;
            tick_record.distance_to_goal = distance_to_goal(error_state);
            tick_record.steering = safe_steer;
            tick_record.throttle = safe_throttle;
            tick_record.collisions_total = collisions_total;
            tick_record.goal_reached = false;
            tick_record.bt_status = "failure";
            tick_record.used_fallback = true;
            tick_record.is_error_record = true;
            tick_record.error_reason = e.what();
            adapter->on_tick_record(tick_record);

            result.status = racecar_loop_status::error;
            result.reason = e.what();
            result.ticks = ticks;
            result.final_state = error_state;
            result.goal_reached = false;
            result.collisions_total = collisions_total;
            result.fallback_count = fallback_count;
            return result;
        }

        next_deadline += std::chrono::duration_cast<std::chrono::steady_clock::duration>(tick_period);
        const auto now = std::chrono::steady_clock::now();
        if (now < next_deadline) {
            std::this_thread::sleep_until(next_deadline);
        } else {
            next_deadline = now;
        }
    }

    result.status = racecar_loop_status::stopped;
    result.reason = "max ticks reached";
    result.ticks = ticks;
    if (have_last_state) {
        result.final_state = last_state;
        result.goal_reached = distance_to_goal(last_state) <= options.goal_tolerance;
    } else {
        result.goal_reached = false;
    }
    result.collisions_total = collisions_total;
    result.fallback_count = fallback_count;
    return result;
}

void install_racecar_demo_callbacks(runtime_host& host) {
    registry& reg = host.callbacks();

    reg.register_condition("collision-imminent", [](tick_context& ctx, std::span<const muslisp::value> args) {
        std::string key = "collision_imminent";
        if (!args.empty()) {
            key = require_key_like(args[0], "collision-imminent");
        }
        return read_bb_bool(ctx.bb_get(key));
    });

    reg.register_condition("goal-reached-racecar", [](tick_context& ctx, std::span<const muslisp::value> args) {
        std::string dist_key = "distance_to_goal";
        double tolerance = 0.6;
        if (!args.empty()) {
            dist_key = require_key_like(args[0], "goal-reached-racecar");
        }
        if (args.size() > 1) {
            tolerance = require_number(args[1], "goal-reached-racecar");
        }
        const bb_entry* entry = ctx.bb_get(dist_key);
        if (!entry) {
            return false;
        }
        if (const auto* d = std::get_if<double>(&entry->value)) {
            return std::isfinite(*d) && (*d <= tolerance);
        }
        if (const auto* i = std::get_if<std::int64_t>(&entry->value)) {
            return static_cast<double>(*i) <= tolerance;
        }
        return false;
    });

    reg.register_action("avoid-obstacle", [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
        std::string rays_key = "rays";
        std::string action_key = "action";
        double steer_mag = 0.8;
        double near_throttle = 0.12;
        double far_throttle = 0.30;
        double near_dist = 0.80;

        if (args.size() > 0) {
            rays_key = require_key_like(args[0], "avoid-obstacle");
        }
        if (args.size() > 1) {
            action_key = require_key_like(args[1], "avoid-obstacle");
        }
        if (args.size() > 2) {
            steer_mag = std::fabs(require_number(args[2], "avoid-obstacle"));
        }
        if (args.size() > 3) {
            near_throttle = require_number(args[3], "avoid-obstacle");
        }
        if (args.size() > 4) {
            far_throttle = require_number(args[4], "avoid-obstacle");
        }
        if (args.size() > 5) {
            near_dist = require_number(args[5], "avoid-obstacle");
        }

        const bb_entry* rays_entry = ctx.bb_get(rays_key);
        const auto* rays = rays_entry ? std::get_if<std::vector<double>>(&rays_entry->value) : nullptr;
        if (!rays || rays->empty()) {
            ctx.bb_put(action_key, bb_value{std::vector<double>{0.0, 0.0}}, "avoid-obstacle");
            return status::failure;
        }

        const std::size_t n = rays->size();
        const std::size_t mid = n / 2;
        double left_sum = 0.0;
        double right_sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double d = (*rays)[i];
            if (i > mid) {
                left_sum += d;
            } else if (i < mid) {
                right_sum += d;
            }
        }

        const double steer = (left_sum >= right_sum) ? steer_mag : -steer_mag;
        const double min_dist = *std::min_element(rays->begin(), rays->end());
        const double throttle = (min_dist < near_dist) ? near_throttle : far_throttle;
        ctx.bb_put(action_key,
                   bb_value{std::vector<double>{clamp_double(steer, -1.0, 1.0), clamp_double(throttle, -1.0, 1.0)}},
                   "avoid-obstacle");
        return status::success;
    });

    reg.register_action("constant-drive", [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
        std::string action_key = "action";
        double steering = 0.0;
        double throttle = 0.45;
        if (!args.empty()) {
            action_key = require_key_like(args[0], "constant-drive");
        }
        if (args.size() > 1) {
            steering = require_number(args[1], "constant-drive");
        }
        if (args.size() > 2) {
            throttle = require_number(args[2], "constant-drive");
        }
        ctx.bb_put(action_key,
                   bb_value{std::vector<double>{clamp_double(steering, -1.0, 1.0), clamp_double(throttle, -1.0, 1.0)}},
                   "constant-drive");
        return status::success;
    });

    reg.register_action("drive-to-goal", [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
        std::string goal_key = "goal";
        std::string action_key = "action";
        double gain = 1.4;
        if (!args.empty()) {
            goal_key = require_key_like(args[0], "drive-to-goal");
        }
        if (args.size() > 1) {
            action_key = require_key_like(args[1], "drive-to-goal");
        }
        if (args.size() > 2) {
            gain = require_number(args[2], "drive-to-goal");
        }

        double x = 0.0;
        double y = 0.0;
        double yaw = 0.0;
        if (!read_bb_number(ctx.bb_get("x"), x) || !read_bb_number(ctx.bb_get("y"), y) || !read_bb_number(ctx.bb_get("yaw"), yaw)) {
            ctx.bb_put(action_key, bb_value{std::vector<double>{0.0, 0.0}}, "drive-to-goal");
            return status::failure;
        }

        const bb_entry* goal_entry = ctx.bb_get(goal_key);
        const auto* goal = goal_entry ? std::get_if<std::vector<double>>(&goal_entry->value) : nullptr;
        if (!goal || goal->size() < 2) {
            ctx.bb_put(action_key, bb_value{std::vector<double>{0.0, 0.0}}, "drive-to-goal");
            return status::failure;
        }

        const double dx = (*goal)[0] - x;
        const double dy = (*goal)[1] - y;
        const double dist = std::hypot(dx, dy);
        const double desired = std::atan2(dy, dx);
        const double heading_err = wrap_angle(desired - yaw);
        const double steering = clamp_double(gain * heading_err, -1.0, 1.0);
        const double throttle = (dist < 0.60) ? 0.0 : clamp_double(0.25 + 0.25 * dist, 0.0, 0.75);
        ctx.bb_put(action_key, bb_value{std::vector<double>{steering, throttle}}, "drive-to-goal");
        return status::success;
    });

    reg.register_action("apply-action", [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
        std::string action_key = "action";
        if (!args.empty()) {
            action_key = require_key_like(args[0], "apply-action");
        }
        const bb_entry* entry = ctx.bb_get(action_key);
        if (!entry) {
            return status::failure;
        }
        const auto* vec = std::get_if<std::vector<double>>(&entry->value);
        if (!vec || vec->size() < 2) {
            return status::failure;
        }
        if (!std::isfinite((*vec)[0]) || !std::isfinite((*vec)[1])) {
            return status::failure;
        }
        return status::success;
    });
}

}  // namespace bt
