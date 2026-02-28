#include "pybullet_racecar/extension.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "muslisp/env_api.hpp"
#include "muslisp/error.hpp"
#include "muslisp/extensions.hpp"
#include "muslisp/gc.hpp"
#include "racecar_demo.hpp"

namespace muslisp::ext::pybullet_racecar {
namespace {

void require_arity(const std::string& name, const std::vector<value>& args, std::size_t expected) {
    if (args.size() != expected) {
        throw lisp_error(name + ": expected " + std::to_string(expected) + " arguments, got " + std::to_string(args.size()));
    }
}

std::int64_t require_int_arg(value v, const std::string& where) {
    if (!is_integer(v)) {
        throw lisp_error(where + ": expected integer");
    }
    return integer_value(v);
}

std::int64_t require_bt_instance_handle(value v, const std::string& where) {
    if (!is_bt_instance(v)) {
        throw lisp_error(where + ": expected bt_instance");
    }
    return bt_handle(v);
}

double require_number_value(value v, const std::string& where) {
    if (is_integer(v)) {
        return static_cast<double>(integer_value(v));
    }
    if (is_float(v)) {
        return float_value(v);
    }
    throw lisp_error(where + ": expected numeric value");
}

std::string require_text_value(value v, const std::string& where) {
    if (is_string(v)) {
        return string_value(v);
    }
    if (is_symbol(v)) {
        return symbol_name(v);
    }
    throw lisp_error(where + ": expected string or symbol");
}

std::string normalize_option_key(std::string key) {
    if (!key.empty() && key.front() == ':') {
        key.erase(key.begin());
    }
    for (char& c : key) {
        if (c == '-') {
            c = '_';
        }
    }
    return key;
}

std::optional<value> map_lookup_option(value map_obj, const std::string& normalized_key) {
    for (const auto& [key, val] : map_obj->map_data) {
        if (key.type != map_key_type::symbol && key.type != map_key_type::string) {
            continue;
        }
        if (normalize_option_key(key.text_data) == normalized_key) {
            return val;
        }
    }
    return std::nullopt;
}

value numeric_vector_to_lisp_list(const std::vector<double>& values) {
    std::vector<value> items;
    items.reserve(values.size());
    gc_root_scope roots(default_gc());
    for (double v : values) {
        items.push_back(make_float(v));
        roots.add(&items.back());
    }
    return list_from_vector(items);
}

map_key symbol_key(const std::string& name) {
    map_key key;
    key.type = map_key_type::symbol;
    key.text_data = name;
    return key;
}

void map_set_symbol(value map_obj, const std::string& key_name, value v) {
    map_obj->map_data[symbol_key(key_name)] = v;
}

value racecar_state_to_lisp_map(const bt::racecar_state& state) {
    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);

    value state_vec = numeric_vector_to_lisp_list(state.state_vec);
    roots.add(&state_vec);
    value rays = numeric_vector_to_lisp_list(state.rays);
    roots.add(&rays);
    value goal = numeric_vector_to_lisp_list(state.goal);
    roots.add(&goal);

    map_set_symbol(out, "state_schema", make_string(state.state_schema));
    map_set_symbol(out, "state_vec", state_vec);
    map_set_symbol(out, "x", make_float(state.x));
    map_set_symbol(out, "y", make_float(state.y));
    map_set_symbol(out, "yaw", make_float(state.yaw));
    map_set_symbol(out, "speed", make_float(state.speed));
    map_set_symbol(out, "rays", rays);
    map_set_symbol(out, "goal", goal);
    map_set_symbol(out, "collision_imminent", make_boolean(state.collision_imminent));
    map_set_symbol(out, "collision_count", make_integer(state.collision_count));
    map_set_symbol(out, "t_ms", make_integer(state.t_ms));
    return out;
}

std::array<double, 2> parse_action_pair(value v, const std::string& where) {
    if (!is_proper_list(v)) {
        throw lisp_error(where + ": expected action list [steering throttle]");
    }
    const std::vector<value> items = vector_from_list(v);
    if (items.size() < 2) {
        throw lisp_error(where + ": expected action list [steering throttle]");
    }
    return {require_number_value(items[0], where), require_number_value(items[1], where)};
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

std::array<double, 2> parse_canonical_action_map(value action_map, const std::string& where) {
    if (!is_map(action_map)) {
        throw lisp_error(where + ": expected action map");
    }

    const auto action_schema = map_lookup_option(action_map, "action_schema");
    if (!action_schema.has_value() || !is_string(*action_schema)) {
        throw lisp_error(where + ": action_schema must be string");
    }

    const auto u = map_lookup_option(action_map, "u");
    if (!u.has_value()) {
        throw lisp_error(where + ": missing action vector key 'u'");
    }

    if (!is_proper_list(*u)) {
        throw lisp_error(where + ": u must be list of numbers");
    }
    const std::vector<value> items = vector_from_list(*u);
    if (items.size() < 2) {
        throw lisp_error(where + ": u must contain at least 2 numeric entries");
    }
    const double steering = clamp_double(require_number_value(items[0], where), -1.0, 1.0);
    const double throttle = clamp_double(require_number_value(items[1], where), -1.0, 1.0);
    return {steering, throttle};
}

class pybullet_env_backend final : public env_backend {
public:
    [[nodiscard]] std::string backend_version() const override {
        return "pybullet.racecar.v1";
    }

    [[nodiscard]] env_backend_supports supports() const override {
        env_backend_supports out;
        out.reset = true;
        out.debug_draw = true;
        out.headless = true;
        out.realtime_pacing = true;
        out.deterministic_seed = false;
        return out;
    }

    [[nodiscard]] std::string notes() const override {
        return "Racecar demo adapter backing env.api.v1";
    }

    void configure(value opts) override {
        if (!is_map(opts)) {
            throw std::runtime_error("configure: expected map");
        }

        if (const auto steps = map_lookup_option(opts, "steps_per_tick")) {
            if (!is_integer(*steps)) {
                throw std::runtime_error("configure: steps_per_tick must be integer");
            }
            steps_per_tick_ = integer_value(*steps);
            if (steps_per_tick_ <= 0) {
                throw std::runtime_error("configure: steps_per_tick must be > 0");
            }
        }
        if (const auto tick_hz = map_lookup_option(opts, "tick_hz")) {
            const double hz = require_number_value(*tick_hz, "configure.tick_hz");
            if (!std::isfinite(hz) || hz <= 0.0) {
                throw std::runtime_error("configure: tick_hz must be finite and > 0");
            }
        }
        if (const auto seed = map_lookup_option(opts, "seed")) {
            if (!is_integer(*seed)) {
                throw std::runtime_error("configure: seed must be integer");
            }
        }
        if (const auto headless = map_lookup_option(opts, "headless")) {
            if (!is_boolean(*headless)) {
                throw std::runtime_error("configure: headless must be boolean");
            }
        }
        if (const auto realtime = map_lookup_option(opts, "realtime")) {
            if (!is_boolean(*realtime)) {
                throw std::runtime_error("configure: realtime must be boolean");
            }
        }
        if (const auto log_path = map_lookup_option(opts, "log_path")) {
            if (!is_string(*log_path)) {
                throw std::runtime_error("configure: log_path must be string");
            }
        }
    }

    [[nodiscard]] value reset(std::optional<std::int64_t> seed) override {
        (void)seed;
        bt::racecar_reset();
        return observe();
    }

    [[nodiscard]] value observe() override {
        const bt::racecar_state state = bt::racecar_get_state();

        value obs = make_map();
        gc_root_scope roots(default_gc());
        roots.add(&obs);

        value state_vec = numeric_vector_to_lisp_list(state.state_vec);
        roots.add(&state_vec);
        value info = make_map();
        roots.add(&info);
        value rays = numeric_vector_to_lisp_list(state.rays);
        roots.add(&rays);
        value goal = numeric_vector_to_lisp_list(state.goal);
        roots.add(&goal);

        const double dx = state.goal.size() > 0 ? (state.goal[0] - state.x) : 0.0;
        const double dy = state.goal.size() > 1 ? (state.goal[1] - state.y) : 0.0;
        const double dist_goal = std::hypot(dx, dy);
        const bool done = state.collision_imminent || (std::isfinite(dist_goal) && dist_goal <= 0.6);

        map_set_symbol(obs, "obs_schema", make_string("racecar.obs.v1"));
        map_set_symbol(obs, "t_ms", make_integer(state.t_ms));
        map_set_symbol(obs, "state_vec", state_vec);
        map_set_symbol(obs, "done", make_boolean(done));

        map_set_symbol(info, "state_schema", make_string(state.state_schema));
        map_set_symbol(info, "x", make_float(state.x));
        map_set_symbol(info, "y", make_float(state.y));
        map_set_symbol(info, "yaw", make_float(state.yaw));
        map_set_symbol(info, "speed", make_float(state.speed));
        map_set_symbol(info, "rays", rays);
        map_set_symbol(info, "goal", goal);
        map_set_symbol(info, "collision_imminent", make_boolean(state.collision_imminent));
        map_set_symbol(info, "collision_count", make_integer(state.collision_count));
        map_set_symbol(obs, "info", info);

        return obs;
    }

    void act(value action) override {
        const auto parsed = parse_canonical_action_map(action, "env.act");
        bt::racecar_apply_action(parsed[0], parsed[1]);
    }

    [[nodiscard]] bool step() override {
        const auto adapter = bt::racecar_sim_adapter_ptr();
        if (!adapter) {
            throw std::runtime_error("racecar sim adapter is not installed");
        }
        if (adapter->stop_requested()) {
            return false;
        }
        bt::racecar_step(std::max<std::int64_t>(1, steps_per_tick_));
        return !adapter->stop_requested();
    }

    void debug_draw(value payload) override {
        (void)payload;
        bt::racecar_debug_draw();
    }

private:
    std::int64_t steps_per_tick_ = 1;
};

value builtin_env_pybullet_get_state(const std::vector<value>& args) {
    require_arity("env.pybullet.get-state", args, 0);
    try {
        return racecar_state_to_lisp_map(bt::racecar_get_state());
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.pybullet.get-state: ") + e.what());
    }
}

value builtin_env_pybullet_apply_action(const std::vector<value>& args) {
    require_arity("env.pybullet.apply-action", args, 1);
    try {
        const auto action = parse_action_pair(args[0], "env.pybullet.apply-action");
        bt::racecar_apply_action(action[0], action[1]);
        return make_nil();
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.pybullet.apply-action: ") + e.what());
    }
}

value builtin_env_pybullet_step(const std::vector<value>& args) {
    if (args.size() > 1) {
        throw lisp_error("env.pybullet.step: expected 0 or 1 arguments");
    }
    std::int64_t steps = 1;
    if (!args.empty()) {
        steps = require_int_arg(args[0], "env.pybullet.step");
    }
    if (steps <= 0) {
        throw lisp_error("env.pybullet.step: steps must be > 0");
    }
    try {
        bt::racecar_step(steps);
        return make_nil();
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.pybullet.step: ") + e.what());
    }
}

value builtin_env_pybullet_reset(const std::vector<value>& args) {
    require_arity("env.pybullet.reset", args, 0);
    try {
        bt::racecar_reset();
        return make_nil();
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.pybullet.reset: ") + e.what());
    }
}

value builtin_env_pybullet_debug_draw(const std::vector<value>& args) {
    require_arity("env.pybullet.debug-draw", args, 0);
    try {
        bt::racecar_debug_draw();
        return make_nil();
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.pybullet.debug-draw: ") + e.what());
    }
}

value builtin_env_pybullet_run_loop(const std::vector<value>& args) {
    if (args.size() < 2) {
        throw lisp_error("env.pybullet.run-loop: expected bt_instance and options");
    }

    const std::int64_t inst_handle = require_bt_instance_handle(args[0], "env.pybullet.run-loop");
    bt::racecar_loop_options options;
    bool has_tick_hz = false;
    bool has_max_ticks = false;
    bool has_state_key = false;
    bool has_action_key = false;

    auto apply_option = [&](const std::string& key, value opt_value) {
        if (key == "tick_hz") {
            options.tick_hz = require_number_value(opt_value, "env.pybullet.run-loop :tick_hz");
            has_tick_hz = true;
            return;
        }
        if (key == "max_ticks") {
            options.max_ticks = require_int_arg(opt_value, "env.pybullet.run-loop :max_ticks");
            has_max_ticks = true;
            return;
        }
        if (key == "state_key") {
            options.state_key = require_text_value(opt_value, "env.pybullet.run-loop :state_key");
            has_state_key = true;
            return;
        }
        if (key == "action_key") {
            options.action_key = require_text_value(opt_value, "env.pybullet.run-loop :action_key");
            has_action_key = true;
            return;
        }
        if (key == "steps_per_tick") {
            options.steps_per_tick = require_int_arg(opt_value, "env.pybullet.run-loop :steps_per_tick");
            return;
        }
        if (key == "safe_action") {
            const auto action = parse_action_pair(opt_value, "env.pybullet.run-loop :safe_action");
            options.safe_action = action;
            return;
        }
        if (key == "draw_debug") {
            if (!is_boolean(opt_value)) {
                throw lisp_error("env.pybullet.run-loop :draw_debug: expected boolean");
            }
            options.draw_debug = boolean_value(opt_value);
            return;
        }
        if (key == "mode") {
            options.mode = require_text_value(opt_value, "env.pybullet.run-loop :mode");
            return;
        }
        if (key == "planner_meta_key") {
            options.planner_meta_key = require_text_value(opt_value, "env.pybullet.run-loop :planner_meta_key");
            return;
        }
        if (key == "run_id") {
            options.run_id = require_text_value(opt_value, "env.pybullet.run-loop :run_id");
            return;
        }
        if (key == "goal_tolerance") {
            options.goal_tolerance = require_number_value(opt_value, "env.pybullet.run-loop :goal_tolerance");
            return;
        }
        throw lisp_error("env.pybullet.run-loop: unknown option: " + key);
    };

    if (args.size() == 2 && is_map(args[1])) {
        value opts_map = args[1];
        for (const auto& [key, opt_value] : opts_map->map_data) {
            if (key.type != map_key_type::symbol && key.type != map_key_type::string) {
                continue;
            }
            apply_option(normalize_option_key(key.text_data), opt_value);
        }
    } else {
        if (((args.size() - 1) % 2) != 0) {
            throw lisp_error("env.pybullet.run-loop: expected key/value option pairs");
        }
        for (std::size_t i = 1; i < args.size(); i += 2) {
            const std::string raw_key = require_text_value(args[i], "env.pybullet.run-loop option key");
            apply_option(normalize_option_key(raw_key), args[i + 1]);
        }
    }

    if (!has_tick_hz || !has_max_ticks || !has_state_key || !has_action_key) {
        throw lisp_error("env.pybullet.run-loop: required options are :tick_hz :max_ticks :state_key :action_key");
    }

    bt::racecar_loop_result result;
    try {
        result = bt::run_racecar_loop(bt::default_runtime_host(), inst_handle, options);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.pybullet.run-loop: ") + e.what());
    }

    value out = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);

    map_set_symbol(out, "status", make_symbol(std::string(":") + bt::racecar_loop_status_name(result.status)));
    map_set_symbol(out, "ticks", make_integer(result.ticks));
    map_set_symbol(out, "reason", make_string(result.reason));
    map_set_symbol(out, "goal_reached", make_boolean(result.goal_reached));
    map_set_symbol(out, "collisions_total", make_integer(result.collisions_total));
    map_set_symbol(out, "fallback_count", make_integer(result.fallback_count));
    value final_state = racecar_state_to_lisp_map(result.final_state);
    roots.add(&final_state);
    map_set_symbol(out, "final_state", final_state);
    return out;
}

}  // namespace

void register_extension(registrar* r, void* user) {
    (void)user;
    if (!r) {
        throw lisp_error("env.pybullet extension: registrar must not be null");
    }
    env_api_register_backend("pybullet", std::make_shared<pybullet_env_backend>());

    r->register_builtin("env.pybullet.get-state",
                        builtin_env_pybullet_get_state,
                        "Get current simulator state as a map.",
                        "(-> map)");
    r->register_builtin("env.pybullet.apply-action",
                        builtin_env_pybullet_apply_action,
                        "Apply [steering throttle] action.",
                        "(action-list -> nil)");
    r->register_builtin("env.pybullet.step", builtin_env_pybullet_step, "Advance simulation by N steps.", "([steps] -> nil)");
    r->register_builtin("env.pybullet.reset", builtin_env_pybullet_reset, "Reset simulator state.", "(-> nil)");
    r->register_builtin(
        "env.pybullet.debug-draw", builtin_env_pybullet_debug_draw, "Render debug overlays for the simulator.", "(-> nil)");
    r->register_builtin("env.pybullet.run-loop",
                        builtin_env_pybullet_run_loop,
                        "Run BT-simulator loop for a BT instance with options.",
                        "(bt_instance options -> map)");
}

}  // namespace muslisp::ext::pybullet_racecar
