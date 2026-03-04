#include "ros2/extension.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "muslisp/env_api.hpp"
#include "muslisp/gc.hpp"

namespace muslisp::integrations::ros2 {
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
    if (!is_map(map_obj)) {
        return std::nullopt;
    }
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
    throw std::runtime_error(where + ": expected numeric value");
}

std::string require_text_value(value v, const std::string& where) {
    if (is_string(v)) {
        return string_value(v);
    }
    if (is_symbol(v)) {
        return symbol_name(v);
    }
    throw std::runtime_error(where + ": expected string or symbol");
}

std::vector<double> require_numeric_list(value v, const std::string& where) {
    if (!is_proper_list(v)) {
        throw std::runtime_error(where + ": expected list");
    }

    const auto items = vector_from_list(v);
    std::vector<double> out;
    out.reserve(items.size());
    for (value item : items) {
        out.push_back(require_number_value(item, where));
    }
    return out;
}

value numeric_vector_to_lisp_list(const std::vector<double>& values) {
    std::vector<value> out;
    out.reserve(values.size());
    gc_root_scope roots(default_gc());
    for (double v : values) {
        out.push_back(make_float(v));
        roots.add(&out.back());
    }
    return list_from_vector(out);
}

class ros2_env_backend final : public env_backend {
public:
    [[nodiscard]] std::string backend_version() const override {
        return "ros2.skeleton.v0";
    }

    [[nodiscard]] env_backend_supports supports() const override {
        env_backend_supports out;
        out.reset = true;
        out.debug_draw = false;
        out.headless = true;
        out.realtime_pacing = true;
        out.deterministic_seed = true;
        return out;
    }

    [[nodiscard]] std::string notes() const override {
        return "ROS2 integration skeleton backend (no topic/action transport binding yet)";
    }

    void configure(value opts) override {
        if (!is_map(opts)) {
            throw std::runtime_error("configure: expected map");
        }

        if (const auto candidate = map_lookup_option(opts, "obs_schema")) {
            obs_schema_ = require_text_value(*candidate, "configure.obs_schema");
        }
        if (const auto candidate = map_lookup_option(opts, "state_schema")) {
            state_schema_ = require_text_value(*candidate, "configure.state_schema");
        }
        if (const auto candidate = map_lookup_option(opts, "action_schema")) {
            action_schema_ = require_text_value(*candidate, "configure.action_schema");
        }
        if (const auto candidate = map_lookup_option(opts, "steps_per_tick")) {
            if (!is_integer(*candidate)) {
                throw std::runtime_error("configure: steps_per_tick must be integer");
            }
            steps_per_tick_ = std::max<std::int64_t>(1, integer_value(*candidate));
        }
        if (const auto candidate = map_lookup_option(opts, "seed")) {
            if (!is_integer(*candidate)) {
                throw std::runtime_error("configure: seed must be integer");
            }
            seed_ = integer_value(*candidate);
        }
    }

    [[nodiscard]] value reset(std::optional<std::int64_t> seed) override {
        if (seed.has_value()) {
            seed_ = *seed;
        }
        state_ = {0.0, 0.0, 0.0, 0.0};
        step_index_ = 0;
        started_at_ = std::chrono::steady_clock::now();
        return observe();
    }

    [[nodiscard]] value observe() override {
        value obs = make_map();
        gc_root_scope roots(default_gc());
        roots.add(&obs);

        value state_vec = numeric_vector_to_lisp_list(state_);
        roots.add(&state_vec);
        value info = make_map();
        roots.add(&info);

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at_).count();

        map_set_symbol(obs, "obs_schema", make_string(obs_schema_));
        map_set_symbol(obs, "t_ms", make_integer(static_cast<std::int64_t>(elapsed)));
        map_set_symbol(obs, "state_vec", state_vec);
        map_set_symbol(obs, "done", make_boolean(false));

        map_set_symbol(info, "state_schema", make_string(state_schema_));
        map_set_symbol(info, "x", make_float(state_[0]));
        map_set_symbol(info, "y", make_float(state_[1]));
        map_set_symbol(info, "yaw", make_float(state_[2]));
        map_set_symbol(info, "speed", make_float(state_[3]));
        map_set_symbol(info, "step", make_integer(step_index_));
        if (seed_.has_value()) {
            map_set_symbol(info, "seed", make_integer(*seed_));
        }
        map_set_symbol(obs, "info", info);

        return obs;
    }

    void act(value action) override {
        if (!is_map(action)) {
            throw std::runtime_error("env.act: expected action map");
        }
        const auto schema_value = map_lookup_option(action, "action_schema");
        if (!schema_value.has_value()) {
            throw std::runtime_error("env.act: missing action_schema");
        }
        if (!is_string(*schema_value)) {
            throw std::runtime_error("env.act: action_schema must be string");
        }
        if (string_value(*schema_value) != action_schema_) {
            throw std::runtime_error("env.act: action_schema mismatch");
        }

        const auto u_value = map_lookup_option(action, "u");
        if (!u_value.has_value()) {
            throw std::runtime_error("env.act: missing action key 'u'");
        }

        const auto u = require_numeric_list(*u_value, "env.act :u");
        if (u.size() < 2) {
            throw std::runtime_error("env.act: u must contain [steer throttle]");
        }
        pending_steer_ = std::clamp(u[0], -1.0, 1.0);
        pending_throttle_ = std::clamp(u[1], -1.0, 1.0);
    }

    [[nodiscard]] bool step() override {
        const std::int64_t substeps = std::max<std::int64_t>(1, steps_per_tick_);
        for (std::int64_t i = 0; i < substeps; ++i) {
            state_[0] += pending_throttle_ * 0.05;
            state_[1] += pending_steer_ * 0.02;
            state_[2] += pending_steer_ * 0.01;
            state_[3] = pending_throttle_;
            ++step_index_;
        }
        return true;
    }

private:
    std::string obs_schema_ = "ros2.obs.v1";
    std::string state_schema_ = "ros2.state.v1";
    std::string action_schema_ = "ros2.action.v1";
    std::int64_t steps_per_tick_ = 1;
    std::optional<std::int64_t> seed_{};
    std::vector<double> state_{0.0, 0.0, 0.0, 0.0};
    double pending_steer_ = 0.0;
    double pending_throttle_ = 0.0;
    std::int64_t step_index_ = 0;
    std::chrono::steady_clock::time_point started_at_ = std::chrono::steady_clock::now();
};

class ros2_extension final : public extension {
public:
    [[nodiscard]] std::string name() const override {
        return "integration.ros2";
    }

    void register_lisp(registrar& reg) const override {
        (void)reg;
        env_api_register_backend("ros2", std::make_shared<ros2_env_backend>());
    }
};

}  // namespace

std::unique_ptr<extension> make_extension() {
    return std::make_unique<ros2_extension>();
}

}  // namespace muslisp::integrations::ros2
