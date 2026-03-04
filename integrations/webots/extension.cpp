#include "webots/extension.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <webots/DistanceSensor.hpp>
#include <webots/Motor.hpp>
#include <webots/Robot.hpp>

#include "bt/runtime_host.hpp"
#include "muslisp/env_api.hpp"
#include "muslisp/error.hpp"
#include "muslisp/gc.hpp"

namespace {

constexpr double kMaxWheelSpeed = 6.28;
constexpr const char* kActionSchema = "epuck.action.v1";

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

std::optional<muslisp::value> map_lookup_option(muslisp::value map_obj, const std::string& normalized_key) {
    if (!muslisp::is_map(map_obj)) {
        return std::nullopt;
    }
    for (const auto& [key, val] : map_obj->map_data) {
        if (key.type != muslisp::map_key_type::symbol && key.type != muslisp::map_key_type::string) {
            continue;
        }
        if (normalize_option_key(key.text_data) == normalized_key) {
            return val;
        }
    }
    return std::nullopt;
}

muslisp::map_key symbol_key(const std::string& name) {
    muslisp::map_key key;
    key.type = muslisp::map_key_type::symbol;
    key.text_data = name;
    return key;
}

void map_set_symbol(muslisp::value map_obj, const std::string& key_name, muslisp::value v) {
    map_obj->map_data[symbol_key(key_name)] = v;
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

std::string require_text_value(muslisp::value v, const std::string& where) {
    if (muslisp::is_string(v)) {
        return muslisp::string_value(v);
    }
    if (muslisp::is_symbol(v)) {
        return muslisp::symbol_name(v);
    }
    throw muslisp::lisp_error(where + ": expected string or symbol");
}

double require_number_value(muslisp::value v, const std::string& where) {
    if (muslisp::is_integer(v)) {
        return static_cast<double>(muslisp::integer_value(v));
    }
    if (muslisp::is_float(v)) {
        return muslisp::float_value(v);
    }
    throw muslisp::lisp_error(where + ": expected numeric value");
}

std::int64_t require_int_value(muslisp::value v, const std::string& where) {
    if (!muslisp::is_integer(v)) {
        throw muslisp::lisp_error(where + ": expected integer");
    }
    return muslisp::integer_value(v);
}

std::vector<double> require_numeric_list(muslisp::value v, const std::string& where) {
    if (!muslisp::is_proper_list(v)) {
        throw muslisp::lisp_error(where + ": expected list");
    }
    const std::vector<muslisp::value> items = muslisp::vector_from_list(v);
    std::vector<double> out;
    out.reserve(items.size());
    for (muslisp::value item : items) {
        out.push_back(require_number_value(item, where));
    }
    return out;
}

muslisp::value numeric_vector_to_lisp_list(const std::vector<double>& values) {
    std::vector<muslisp::value> out;
    out.reserve(values.size());
    muslisp::gc_root_scope roots(muslisp::default_gc());
    for (double v : values) {
        out.push_back(muslisp::make_float(v));
        roots.add(&out.back());
    }
    return muslisp::list_from_vector(out);
}

class webots_env_backend final : public muslisp::env_backend {
public:
    webots_env_backend(::webots::Robot* robot, muslisp::integrations::webots::attach_options options)
        : robot_(robot),
          steps_per_tick_(std::max<std::int64_t>(1, options.steps_per_tick)),
          demo_(std::move(options.demo)),
          obs_schema_(std::move(options.obs_schema)) {
        if (!robot_) {
            throw muslisp::lisp_error("webots integration requires non-null webots::Robot");
        }
        if (demo_.empty()) {
            demo_ = "obstacle";
        }
        if (obs_schema_.empty()) {
            obs_schema_ = "epuck.obstacle.obs.v1";
        }
        initialise_devices();
    }

    [[nodiscard]] std::string backend_version() const override {
        return "webots.epuck.v2";
    }

    [[nodiscard]] muslisp::env_backend_supports supports() const override {
        muslisp::env_backend_supports out;
        out.reset = false;
        out.debug_draw = false;
        out.headless = true;
        out.realtime_pacing = true;
        out.deterministic_seed = true;
        return out;
    }

    [[nodiscard]] std::string notes() const override {
        return "Webots e-puck backend adapter for env.api.v1";
    }

    void configure(muslisp::value opts) override {
        if (!muslisp::is_map(opts)) {
            throw std::runtime_error("configure: expected map");
        }

        if (const auto v = map_lookup_option(opts, "steps_per_tick")) {
            const std::int64_t raw = require_int_value(*v, "env.configure :steps_per_tick");
            if (raw <= 0) {
                throw std::runtime_error("configure: steps_per_tick must be > 0");
            }
            steps_per_tick_ = raw;
        }
        if (const auto v = map_lookup_option(opts, "demo")) {
            demo_ = require_text_value(*v, "env.configure :demo");
        }
        if (const auto v = map_lookup_option(opts, "obs_schema")) {
            obs_schema_ = require_text_value(*v, "env.configure :obs_schema");
        }
        if (const auto v = map_lookup_option(opts, "seed")) {
            seed_ = require_int_value(*v, "env.configure :seed");
            has_seed_ = true;
        }
    }

    [[nodiscard]] muslisp::value reset(std::optional<std::int64_t> seed) override {
        (void)seed;
        throw std::runtime_error("reset is not supported by webots backend");
    }

    [[nodiscard]] muslisp::value observe() override {
        muslisp::value obs = muslisp::make_map();
        muslisp::gc_root_scope roots(muslisp::default_gc());
        roots.add(&obs);

        const std::array<double, 8> proximity = read_proximity_normalised();
        std::vector<double> proximity_vec(proximity.begin(), proximity.end());
        muslisp::value proximity_list = numeric_vector_to_lisp_list(proximity_vec);
        roots.add(&proximity_list);

        const double max_proximity = *std::max_element(proximity.begin(), proximity.end());
        const double min_obstacle = clamp_double(1.0 - max_proximity, 0.0, 1.0);
        const double left_activity = proximity[5] + proximity[6] + proximity[7];
        const double right_activity = proximity[0] + proximity[1] + proximity[2];

        map_set_symbol(obs, "obs_schema", muslisp::make_string(obs_schema_));
        map_set_symbol(obs, "t_ms", muslisp::make_integer(static_cast<std::int64_t>(std::llround(robot_->getTime() * 1000.0))));
        map_set_symbol(obs, "proximity", proximity_list);
        map_set_symbol(obs, "min_obstacle", muslisp::make_float(min_obstacle));
        map_set_symbol(obs, "wall_side", muslisp::make_string(left_activity >= right_activity ? "left" : "right"));
        map_set_symbol(obs, "demo", muslisp::make_string(demo_));
        map_set_symbol(obs, "done", muslisp::make_boolean(false));

        if (has_seed_) {
            map_set_symbol(obs, "seed", muslisp::make_integer(seed_));
        }

        if (demo_ == "line") {
            const std::array<double, 3> ground = read_ground_normalised();
            std::vector<double> ground_vec(ground.begin(), ground.end());
            muslisp::value ground_list = numeric_vector_to_lisp_list(ground_vec);
            roots.add(&ground_list);
            map_set_symbol(obs, "ground", ground_list);

            const double dark_left = clamp_double(1.0 - ground[0], 0.0, 1.0);
            const double dark_centre = clamp_double(1.0 - ground[1], 0.0, 1.0);
            const double dark_right = clamp_double(1.0 - ground[2], 0.0, 1.0);
            const double dark_sum = dark_left + dark_centre + dark_right;
            const double line_error = dark_sum > 1e-6 ? clamp_double((dark_right - dark_left) / dark_sum, -1.0, 1.0) : 0.0;
            map_set_symbol(obs, "line_error", muslisp::make_float(line_error));
        }

        return obs;
    }

    void act(muslisp::value action) override {
        if (!muslisp::is_map(action)) {
            throw std::runtime_error("env.act: expected action map");
        }

        const auto action_schema = map_lookup_option(action, "action_schema");
        if (!action_schema.has_value() || !muslisp::is_string(*action_schema)) {
            throw std::runtime_error("env.act: action_schema must be string");
        }
        if (muslisp::string_value(*action_schema) != kActionSchema) {
            throw std::runtime_error("env.act: action_schema must be epuck.action.v1");
        }

        const auto u_value = map_lookup_option(action, "u");
        if (!u_value.has_value()) {
            throw std::runtime_error("env.act: missing action key 'u'");
        }

        const std::vector<double> u = require_numeric_list(*u_value, "env.act :u");
        if (u.size() < 2) {
            throw std::runtime_error("env.act: u must contain [left right]");
        }

        pending_left_ = clamp_double(u[0], -kMaxWheelSpeed, kMaxWheelSpeed);
        pending_right_ = clamp_double(u[1], -kMaxWheelSpeed, kMaxWheelSpeed);
        has_pending_ = true;
    }

    [[nodiscard]] bool step() override {
        if (has_pending_) {
            left_motor_->setVelocity(pending_left_);
            right_motor_->setVelocity(pending_right_);
            has_pending_ = false;
        }

        const std::int64_t substeps = std::max<std::int64_t>(1, steps_per_tick_);
        for (std::int64_t i = 0; i < substeps; ++i) {
            if (robot_->step(time_step_ms_) == -1) {
                return false;
            }
        }
        return true;
    }

private:
    void initialise_devices() {
        time_step_ms_ = std::max(1, static_cast<int>(std::lround(robot_->getBasicTimeStep())));

        for (int i = 0; i < 8; ++i) {
            const std::string name = "ps" + std::to_string(i);
            proximity_[static_cast<std::size_t>(i)] = robot_->getDistanceSensor(name);
            if (!proximity_[static_cast<std::size_t>(i)]) {
                throw std::runtime_error("missing Webots proximity sensor: " + name);
            }
            proximity_[static_cast<std::size_t>(i)]->enable(time_step_ms_);
        }

        for (int i = 0; i < 3; ++i) {
            const std::string name = "gs" + std::to_string(i);
            ground_[static_cast<std::size_t>(i)] = robot_->getDistanceSensor(name);
            if (ground_[static_cast<std::size_t>(i)]) {
                ground_[static_cast<std::size_t>(i)]->enable(time_step_ms_);
            }
        }

        left_motor_ = robot_->getMotor("left wheel motor");
        right_motor_ = robot_->getMotor("right wheel motor");
        if (!left_motor_ || !right_motor_) {
            throw std::runtime_error("missing Webots wheel motors");
        }

        left_motor_->setPosition(INFINITY);
        right_motor_->setPosition(INFINITY);
        left_motor_->setVelocity(0.0);
        right_motor_->setVelocity(0.0);

        // Prime sensors so first observation has stable values.
        (void)robot_->step(time_step_ms_);
    }

    [[nodiscard]] std::array<double, 8> read_proximity_normalised() const {
        std::array<double, 8> values{};
        for (int i = 0; i < 8; ++i) {
            const double raw = proximity_[static_cast<std::size_t>(i)]->getValue();
            const double finite = std::isfinite(raw) ? raw : 0.0;
            values[static_cast<std::size_t>(i)] = clamp_double(finite / 4096.0, 0.0, 1.0);
        }
        return values;
    }

    [[nodiscard]] std::array<double, 3> read_ground_normalised() const {
        std::array<double, 3> values{};
        for (int i = 0; i < 3; ++i) {
            ::webots::DistanceSensor* sensor = ground_[static_cast<std::size_t>(i)];
            if (!sensor) {
                values[static_cast<std::size_t>(i)] = 1.0;
                continue;
            }
            const double raw = sensor->getValue();
            const double finite = std::isfinite(raw) ? raw : 1000.0;
            values[static_cast<std::size_t>(i)] = clamp_double(finite / 1000.0, 0.0, 1.0);
        }
        return values;
    }

    ::webots::Robot* robot_ = nullptr;
    int time_step_ms_ = 32;
    std::array<::webots::DistanceSensor*, 8> proximity_{};
    std::array<::webots::DistanceSensor*, 3> ground_{};
    ::webots::Motor* left_motor_ = nullptr;
    ::webots::Motor* right_motor_ = nullptr;

    std::int64_t steps_per_tick_ = 1;
    std::string demo_ = "obstacle";
    std::string obs_schema_ = "epuck.obstacle.obs.v1";

    bool has_pending_ = false;
    double pending_left_ = 0.0;
    double pending_right_ = 0.0;

    bool has_seed_ = false;
    std::int64_t seed_ = 0;
};

class webots_extension final : public muslisp::extension {
public:
    webots_extension(::webots::Robot* robot, muslisp::integrations::webots::attach_options options)
        : backend_(std::make_shared<webots_env_backend>(robot, std::move(options))) {}

    [[nodiscard]] std::string name() const override {
        return "integration.webots";
    }

    void register_lisp(muslisp::registrar& reg) const override {
        (void)reg;
        muslisp::env_api_register_backend("webots", backend_);
    }

    void register_bt(bt::runtime_host& host) const override;

private:
    std::shared_ptr<webots_env_backend> backend_;
};

}  // namespace

void bt::integrations::webots::install_callbacks(bt::runtime_host& host) {
    // Compatibility shim: callbacks are now part of core demo callbacks.
    bt::install_demo_callbacks(host);
}

void webots_extension::register_bt(bt::runtime_host& host) const {
    (void)host;
}

std::unique_ptr<muslisp::extension> muslisp::integrations::webots::make_extension(::webots::Robot* robot,
                                                                                   attach_options options) {
    return std::make_unique<webots_extension>(robot, std::move(options));
}
