#include "pybullet/extension.hpp"

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
#include "muslisp/gc.hpp"
#include "pybullet/racecar_demo.hpp"

namespace muslisp::integrations::pybullet {
namespace {

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

double require_number_value(value v, const std::string& where) {
    if (is_integer(v)) {
        return static_cast<double>(integer_value(v));
    }
    if (is_float(v)) {
        return float_value(v);
    }
    throw lisp_error(where + ": expected numeric value");
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
        return "Racecar integration backing env.api.v1";
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

class pybullet_extension final : public extension {
public:
    [[nodiscard]] std::string name() const override {
        return "integration.pybullet";
    }

    void register_lisp(registrar& reg) const override {
        (void)reg;
        env_api_register_backend("pybullet", std::make_shared<pybullet_env_backend>());
    }

    void register_bt(bt::runtime_host& host) const override {
        bt::install_racecar_demo_callbacks(host);
    }
};

}  // namespace

std::unique_ptr<extension> make_extension() {
    return std::make_unique<pybullet_extension>();
}

}  // namespace muslisp::integrations::pybullet
