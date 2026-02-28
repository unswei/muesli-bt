#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <webots/DistanceSensor.hpp>
#include <webots/Field.hpp>
#include <webots/Motor.hpp>
#include <webots/Node.hpp>
#include <webots/Robot.hpp>
#include <webots/Supervisor.hpp>

#include "bt/blackboard.hpp"
#include "bt/instance.hpp"
#include "bt/planner.hpp"
#include "bt/registry.hpp"
#include "bt/runtime.hpp"
#include "bt/runtime_host.hpp"
#include "muslisp/env_api.hpp"
#include "muslisp/error.hpp"
#include "muslisp/eval.hpp"
#include "muslisp/extensions.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"
#include "muslisp/reader.hpp"

namespace {

constexpr double kMaxWheelSpeed = 6.28;
constexpr const char* kActionSchema = "epuck.action.v1";
constexpr double kPi = 3.14159265358979323846;

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

bool require_bool_value(muslisp::value v, const std::string& where) {
    if (!muslisp::is_boolean(v)) {
        throw muslisp::lisp_error(where + ": expected boolean");
    }
    return muslisp::boolean_value(v);
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

std::vector<double> require_numeric_list(muslisp::value v, const std::string& where) {
    if (!muslisp::is_proper_list(v)) {
        throw muslisp::lisp_error(where + ": expected numeric list");
    }
    const std::vector<muslisp::value> items = muslisp::vector_from_list(v);
    std::vector<double> out;
    out.reserve(items.size());
    for (muslisp::value item : items) {
        out.push_back(require_number_value(item, where));
    }
    return out;
}

double clampd(double value, double lo, double hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

double wrap_angle(double a) {
    while (a > kPi) {
        a -= 2.0 * kPi;
    }
    while (a < -kPi) {
        a += 2.0 * kPi;
    }
    return a;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw muslisp::lisp_error("failed to open file: " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int run_script(const std::filesystem::path& path, muslisp::env_ptr env) {
    const std::string source = read_text_file(path);
    std::vector<muslisp::value> exprs = muslisp::read_all(source);

    muslisp::gc_root_scope roots(muslisp::default_gc());
    for (muslisp::value& expr : exprs) {
        roots.add(&expr);
    }

    muslisp::value result = muslisp::make_nil();
    roots.add(&result);

    for (muslisp::value expr : exprs) {
        result = muslisp::eval(expr, env);
        muslisp::default_gc().maybe_collect();
    }

    return 0;
}

std::optional<std::string> opt_bb_key(std::span<const muslisp::value> args, std::size_t index) {
    if (index >= args.size()) {
        return std::nullopt;
    }
    if (muslisp::is_symbol(args[index])) {
        return muslisp::symbol_name(args[index]);
    }
    if (muslisp::is_string(args[index])) {
        return muslisp::string_value(args[index]);
    }
    throw std::runtime_error("expected symbol/string key argument");
}

std::string require_bb_key(std::span<const muslisp::value> args, std::size_t index, const std::string& where) {
    const auto key = opt_bb_key(args, index);
    if (!key.has_value()) {
        throw std::runtime_error(where + ": missing key argument");
    }
    return *key;
}

std::int64_t require_bb_int(std::span<const muslisp::value> args, std::size_t index, const std::string& where) {
    if (index >= args.size() || !muslisp::is_integer(args[index])) {
        throw std::runtime_error(where + ": expected integer argument");
    }
    return muslisp::integer_value(args[index]);
}

std::vector<double> bb_to_vector(const bt::bb_value& value) {
    if (const auto* vec = std::get_if<std::vector<double>>(&value)) {
        return *vec;
    }
    if (const auto* f = std::get_if<double>(&value)) {
        return {*f, *f};
    }
    if (const auto* i = std::get_if<std::int64_t>(&value)) {
        const double d = static_cast<double>(*i);
        return {d, d};
    }
    return {};
}

bool bb_truthy(const bt::bb_entry* entry) {
    if (!entry) {
        return false;
    }
    const bt::bb_value& value = entry->value;
    if (std::holds_alternative<std::monostate>(value)) {
        return false;
    }
    if (const auto* b = std::get_if<bool>(&value)) {
        return *b;
    }
    if (const auto* i = std::get_if<std::int64_t>(&value)) {
        return *i != 0;
    }
    if (const auto* f = std::get_if<double>(&value)) {
        return std::isfinite(*f) && std::fabs(*f) > 1e-9;
    }
    if (const auto* s = std::get_if<std::string>(&value)) {
        return !s->empty() && *s != "0" && *s != "false";
    }
    if (const auto* vec = std::get_if<std::vector<double>>(&value)) {
        return !vec->empty();
    }
    return true;
}

std::optional<std::array<double, 3>> node_translation(webots::Node* node) {
    if (!node) {
        return std::nullopt;
    }
    webots::Field* field = node->getField("translation");
    if (!field) {
        return std::nullopt;
    }
    const double* t = field->getSFVec3f();
    if (!t) {
        return std::nullopt;
    }
    return std::array<double, 3>{t[0], t[1], t[2]};
}

bool set_node_translation(webots::Node* node, const std::array<double, 3>& t) {
    if (!node) {
        return false;
    }
    webots::Field* field = node->getField("translation");
    if (!field) {
        return false;
    }
    field->setSFVec3f(t.data());
    return true;
}

std::optional<std::array<double, 2>> node_xy(webots::Node* node) {
    const auto t = node_translation(node);
    if (!t.has_value()) {
        return std::nullopt;
    }
    return std::array<double, 2>{(*t)[0], (*t)[1]};
}

double node_yaw(webots::Node* node) {
    if (!node) {
        return 0.0;
    }
    const double* o = node->getOrientation();
    if (!o) {
        return 0.0;
    }
    return std::atan2(o[3], o[0]);
}

double planar_distance(const std::array<double, 2>& a, const std::array<double, 2>& b) {
    const double dx = b[0] - a[0];
    const double dy = b[1] - a[1];
    return std::sqrt(dx * dx + dy * dy);
}

double relative_bearing(const std::array<double, 2>& origin,
                        double origin_yaw,
                        const std::array<double, 2>& target) {
    const double heading = std::atan2(target[1] - origin[1], target[0] - origin[0]);
    return wrap_angle(heading - origin_yaw);
}

std::vector<double> lidar_lite_from_proximity(const std::array<double, 8>& p) {
    const double left = std::max(p[5], p[6]);
    const double front_left = std::max(p[6], p[7]);
    const double front = std::max(std::max(p[7], p[0]), std::max(p[6], p[1]));
    const double front_right = std::max(p[0], p[1]);
    const double right = std::max(p[1], p[2]);
    return {left, front_left, front, front_right, right};
}

class epuck_line_planner_model final : public bt::planner_model {
public:
    [[nodiscard]] bt::planner_step_result step(const bt::planner_vector& state,
                                               const bt::planner_vector& action,
                                               bt::planner_rng& rng) const override {
        const bt::planner_vector u = clamp_action(action);
        const double line_error = state.empty() ? 0.0 : state[0];
        const double line_strength = state.size() > 1 ? state[1] : 0.5;
        const double steer = (u[1] - u[0]) / (2.0 * kMaxWheelSpeed);
        const double noise_error = rng.normal(0.0, 0.01);
        const double noise_strength = rng.normal(0.0, 0.02);

        const double next_error = clampd(0.72 * line_error + 0.40 * steer + noise_error, -1.5, 1.5);
        const double next_strength = clampd(0.80 * line_strength + 0.20 * (1.0 - std::fabs(next_error)) + noise_strength, 0.0, 1.0);
        const bool done = next_strength < 0.05;

        bt::planner_step_result out;
        out.next_state = {next_error, next_strength};
        out.reward = next_strength - std::fabs(next_error) - 0.02 * std::fabs(steer);
        if (done) {
            out.reward -= 1.0;
        }
        out.done = done;
        return out;
    }

    [[nodiscard]] bt::planner_vector sample_action(const bt::planner_vector&, bt::planner_rng& rng) const override {
        const double base = rng.uniform(2.4, 4.6);
        const double steer = rng.uniform(-1.1, 1.1);
        const double left = clampd(base + 1.8 * steer, 0.0, kMaxWheelSpeed);
        const double right = clampd(base - 1.8 * steer, 0.0, kMaxWheelSpeed);
        return {left, right};
    }

    [[nodiscard]] bt::planner_vector rollout_action(const bt::planner_vector& state, bt::planner_rng&) const override {
        const double line_error = state.empty() ? 0.0 : state[0];
        const double steer = clampd(-1.8 * line_error, -1.0, 1.0);
        const double base = 3.2;
        return {clampd(base + 2.0 * steer, -kMaxWheelSpeed, kMaxWheelSpeed),
                clampd(base - 2.0 * steer, -kMaxWheelSpeed, kMaxWheelSpeed)};
    }

    [[nodiscard]] bt::planner_vector clamp_action(const bt::planner_vector& action) const override {
        if (action.empty()) {
            return {2.8, 2.8};
        }
        if (action.size() == 1) {
            const double v = clampd(action[0], 0.0, kMaxWheelSpeed);
            return {v, v};
        }
        return {clampd(action[0], 0.0, kMaxWheelSpeed), clampd(action[1], 0.0, kMaxWheelSpeed)};
    }

    [[nodiscard]] bt::planner_vector zero_action() const override {
        return {2.8, 2.8};
    }

    [[nodiscard]] bool validate_state(const bt::planner_vector& state) const override {
        return state.size() >= 2 && std::isfinite(state[0]) && std::isfinite(state[1]);
    }

    [[nodiscard]] std::size_t action_dims() const override {
        return 2;
    }
};

class epuck_target_planner_model final : public bt::planner_model {
public:
    [[nodiscard]] bt::planner_step_result step(const bt::planner_vector& state,
                                               const bt::planner_vector& action,
                                               bt::planner_rng& rng) const override {
        const bt::planner_vector u = clamp_action(action);
        const double dist = state.empty() ? 1.0 : clampd(state[0], 0.0, 3.0);
        const double bearing = state.size() > 1 ? wrap_angle(state[1]) : 0.0;
        const double obstacle = state.size() > 2 ? clampd(state[2], 0.0, 1.0) : 0.0;

        const double forward = clampd((u[0] + u[1]) / (2.0 * kMaxWheelSpeed), -1.0, 1.0);
        const double steer = clampd((u[1] - u[0]) / (2.0 * kMaxWheelSpeed), -1.0, 1.0);

        const double next_bearing = wrap_angle(0.80 * bearing - 0.95 * steer + rng.normal(0.0, 0.04));
        const double progress = 0.11 * std::max(0.0, forward) * std::max(0.05, 1.0 - std::fabs(bearing));
        const double next_dist = clampd(dist - progress + 0.04 * std::fabs(steer) + 0.10 * obstacle + rng.normal(0.0, 0.01),
                                        0.0,
                                        3.0);
        const double next_obstacle = clampd(0.75 * obstacle + 0.10 * std::fabs(steer) + rng.normal(0.0, 0.03), 0.0, 1.0);

        const bool done = next_dist < 0.06;

        bt::planner_step_result out;
        out.next_state = {next_dist, next_bearing, next_obstacle};
        out.reward = 1.4 * (dist - next_dist) - 0.35 * std::fabs(next_bearing) - 0.45 * next_obstacle - 0.05 * std::fabs(steer);
        if (next_obstacle > 0.92) {
            out.reward -= 1.0;
        }
        if (done) {
            out.reward += 1.5;
        }
        out.done = done;
        return out;
    }

    [[nodiscard]] bt::planner_vector sample_action(const bt::planner_vector&, bt::planner_rng& rng) const override {
        const double base = rng.uniform(1.8, 5.2);
        const double steer = rng.uniform(-1.2, 1.2);
        return clamp_action({base + 1.8 * steer, base - 1.8 * steer});
    }

    [[nodiscard]] bt::planner_vector rollout_action(const bt::planner_vector& state, bt::planner_rng&) const override {
        const double dist = state.empty() ? 1.0 : clampd(state[0], 0.0, 3.0);
        const double bearing = state.size() > 1 ? wrap_angle(state[1]) : 0.0;
        const double base = clampd(2.2 + 2.0 * dist, 1.2, 5.6);
        const double steer = clampd(2.6 * bearing, -1.4, 1.4);
        return clamp_action({base + steer, base - steer});
    }

    [[nodiscard]] bt::planner_vector clamp_action(const bt::planner_vector& action) const override {
        if (action.empty()) {
            return {2.2, 2.2};
        }
        if (action.size() == 1) {
            const double v = clampd(action[0], -kMaxWheelSpeed, kMaxWheelSpeed);
            return {v, v};
        }
        return {clampd(action[0], -kMaxWheelSpeed, kMaxWheelSpeed), clampd(action[1], -kMaxWheelSpeed, kMaxWheelSpeed)};
    }

    [[nodiscard]] bt::planner_vector zero_action() const override {
        return {2.2, 2.2};
    }

    [[nodiscard]] bool validate_state(const bt::planner_vector& state) const override {
        return state.size() >= 3 && std::isfinite(state[0]) && std::isfinite(state[1]) && std::isfinite(state[2]);
    }

    [[nodiscard]] std::size_t action_dims() const override {
        return 2;
    }
};

void install_epuck_callbacks(bt::runtime_host& host) {
    bt::registry& reg = host.callbacks();

    reg.register_condition("bb-truthy", [](bt::tick_context& ctx, std::span<const muslisp::value> args) {
        const std::string key = require_bb_key(args, 0, "bb-truthy");
        return bb_truthy(ctx.bb_get(key));
    });

    reg.register_action("select-action", [](bt::tick_context& ctx,
                                             bt::node_id,
                                             bt::node_memory&,
                                             std::span<const muslisp::value> args) {
        const std::string source_key = require_bb_key(args, 0, "select-action");
        const std::int64_t branch_id = require_bb_int(args, 1, "select-action");
        const std::string out_key = opt_bb_key(args, 2).value_or("action_vec");

        const bt::bb_entry* source = ctx.bb_get(source_key);
        if (!source) {
            return bt::status::failure;
        }

        std::vector<double> action = bb_to_vector(source->value);
        if (action.size() < 2) {
            return bt::status::failure;
        }
        action[0] = clampd(action[0], -kMaxWheelSpeed, kMaxWheelSpeed);
        action[1] = clampd(action[1], -kMaxWheelSpeed, kMaxWheelSpeed);

        ctx.bb_put(out_key, bt::bb_value{std::move(action)}, "select-action");
        ctx.bb_put("active_branch", bt::bb_value{branch_id}, "select-action");
        return bt::status::success;
    });
}

struct epuck_devices {
    explicit epuck_devices(webots::Robot* robot) : robot(robot) {
        if (!robot) {
            throw std::runtime_error("webots robot pointer must not be null");
        }

        time_step_ms = std::max(1, static_cast<int>(std::lround(robot->getBasicTimeStep())));

        for (int i = 0; i < 8; ++i) {
            const std::string name = "ps" + std::to_string(i);
            proximity[i] = robot->getDistanceSensor(name);
            if (!proximity[i]) {
                throw std::runtime_error("missing proximity sensor: " + name);
            }
            proximity[i]->enable(time_step_ms);
        }

        for (int i = 0; i < 3; ++i) {
            const std::string name = "gs" + std::to_string(i);
            ground[i] = robot->getDistanceSensor(name);
            if (ground[i]) {
                ground[i]->enable(time_step_ms);
            }
        }

        left_motor = robot->getMotor("left wheel motor");
        right_motor = robot->getMotor("right wheel motor");
        if (!left_motor || !right_motor) {
            throw std::runtime_error("missing wheel motors");
        }

        left_motor->setPosition(INFINITY);
        right_motor->setPosition(INFINITY);
        left_motor->setVelocity(0.0);
        right_motor->setVelocity(0.0);

        // Prime sensor values so the first observation does not contain startup NaNs.
        robot->step(time_step_ms);
    }

    [[nodiscard]] std::array<double, 8> read_proximity_normalised() const {
        std::array<double, 8> values{};
        for (int i = 0; i < 8; ++i) {
            const double raw = proximity[i]->getValue();
            const double finite = std::isfinite(raw) ? raw : 0.0;
            values[static_cast<std::size_t>(i)] = clampd(finite / 4096.0, 0.0, 1.0);
        }
        return values;
    }

    [[nodiscard]] std::array<double, 3> read_ground_normalised() const {
        std::array<double, 3> values{};
        for (int i = 0; i < 3; ++i) {
            if (!ground[i]) {
                values[static_cast<std::size_t>(i)] = 1.0;
                continue;
            }
            const double raw = ground[i]->getValue();
            const double finite = std::isfinite(raw) ? raw : 1000.0;
            values[static_cast<std::size_t>(i)] = clampd(finite / 1000.0, 0.0, 1.0);
        }
        return values;
    }

    void set_wheel_velocity(double left, double right) {
        left_motor->setVelocity(clampd(left, -kMaxWheelSpeed, kMaxWheelSpeed));
        right_motor->setVelocity(clampd(right, -kMaxWheelSpeed, kMaxWheelSpeed));
    }

    webots::Robot* robot = nullptr;
    int time_step_ms = 32;
    std::array<webots::DistanceSensor*, 8> proximity{};
    std::array<webots::DistanceSensor*, 3> ground{};
    webots::Motor* left_motor = nullptr;
    webots::Motor* right_motor = nullptr;
};

class webots_env_backend final : public muslisp::env_backend {
public:
    explicit webots_env_backend(epuck_devices* devices) : devices_(devices) {
        if (!devices_) {
            throw std::runtime_error("webots_env_backend requires non-null devices");
        }
        supervisor_ = dynamic_cast<webots::Supervisor*>(devices_->robot);
        refresh_scene_handles();
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
        return "Webots e-puck adapter for env.api.v1 (line/obstacle/goal/foraging/tag)";
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

        if (const auto v = map_lookup_option(opts, "tick_hz")) {
            const double hz = require_number_value(*v, "env.configure :tick_hz");
            if (!std::isfinite(hz) || hz <= 0.0) {
                throw std::runtime_error("configure: tick_hz must be finite and > 0");
            }
        }

        if (const auto v = map_lookup_option(opts, "seed")) {
            configured_seed_ = require_int_value(*v, "env.configure :seed");
            rng_.seed(static_cast<std::uint64_t>(configured_seed_));
            has_seed_ = true;
        }
        if (const auto v = map_lookup_option(opts, "headless")) {
            (void)require_bool_value(*v, "env.configure :headless");
        }
        if (const auto v = map_lookup_option(opts, "realtime")) {
            (void)require_bool_value(*v, "env.configure :realtime");
        }
        if (const auto v = map_lookup_option(opts, "log_path")) {
            if (!muslisp::is_string(*v)) {
                throw std::runtime_error("configure: log_path must be string");
            }
        }

        if (const auto v = map_lookup_option(opts, "demo")) {
            demo_ = require_text_value(*v, "env.configure :demo");
            if (demo_ == "line") {
                obs_schema_ = "epuck.line.obs.v1";
            } else if (demo_ == "obstacle") {
                obs_schema_ = "epuck.obstacle.obs.v1";
            } else if (demo_ == "goal") {
                obs_schema_ = "epuck.goal.obs.v1";
            } else if (demo_ == "foraging") {
                obs_schema_ = "epuck.foraging.obs.v1";
            } else if (demo_ == "tag") {
                obs_schema_ = "epuck.tag.obs.v1";
            }
        }

        if (const auto v = map_lookup_option(opts, "obs_schema")) {
            obs_schema_ = require_text_value(*v, "env.configure :obs_schema");
        }

        refresh_scene_handles();
    }

    [[nodiscard]] muslisp::value reset(std::optional<std::int64_t> seed) override {
        (void)seed;
        throw std::runtime_error("reset is not supported by webots backend");
    }

    [[nodiscard]] muslisp::value observe() override {
        muslisp::value obs = muslisp::make_map();
        muslisp::gc_root_scope roots(muslisp::default_gc());
        roots.add(&obs);

        const std::array<double, 8> proximity = devices_->read_proximity_normalised();
        std::vector<double> proximity_vec(proximity.begin(), proximity.end());
        muslisp::value proximity_list = numeric_vector_to_lisp_list(proximity_vec);
        roots.add(&proximity_list);

        const double max_proximity = *std::max_element(proximity.begin(), proximity.end());
        const double min_obstacle = clampd(1.0 - max_proximity, 0.0, 1.0);

        const double left_activity = proximity[5] + proximity[6] + proximity[7];
        const double right_activity = proximity[0] + proximity[1] + proximity[2];
        const std::string wall_side = left_activity >= right_activity ? "left" : "right";

        map_set_symbol(obs, "obs_schema", muslisp::make_string(obs_schema_));
        map_set_symbol(obs, "t_ms", muslisp::make_integer(static_cast<std::int64_t>(std::llround(devices_->robot->getTime() * 1000.0))));
        map_set_symbol(obs, "proximity", proximity_list);
        map_set_symbol(obs, "min_obstacle", muslisp::make_float(min_obstacle));
        map_set_symbol(obs, "wall_side", muslisp::make_string(wall_side));

        if (has_seed_) {
            map_set_symbol(obs, "seed", muslisp::make_integer(configured_seed_));
        }

        const auto robot_xy = node_xy(robot_node_);
        if (robot_xy.has_value()) {
            muslisp::value robot_xy_list = numeric_vector_to_lisp_list({(*robot_xy)[0], (*robot_xy)[1]});
            roots.add(&robot_xy_list);
            map_set_symbol(obs, "robot_xy", robot_xy_list);
            map_set_symbol(obs, "robot_yaw", muslisp::make_float(node_yaw(robot_node_)));
        }

        if (obs_schema_ == "epuck.line.obs.v1") {
            add_line_observation(obs, roots);
        } else if (obs_schema_ == "epuck.goal.obs.v1") {
            add_goal_observation(obs, proximity, min_obstacle, roots);
        } else if (obs_schema_ == "epuck.foraging.obs.v1") {
            add_foraging_observation(obs, min_obstacle, roots);
        } else if (obs_schema_ == "epuck.tag.obs.v1") {
            add_tag_observation(obs, min_obstacle, roots);
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

        pending_left_ = clampd(u[0], -kMaxWheelSpeed, kMaxWheelSpeed);
        pending_right_ = clampd(u[1], -kMaxWheelSpeed, kMaxWheelSpeed);
        has_pending_ = true;
    }

    [[nodiscard]] bool step() override {
        if (has_pending_) {
            devices_->set_wheel_velocity(pending_left_, pending_right_);
            has_pending_ = false;
        }

        const std::int64_t substeps = std::max<std::int64_t>(1, steps_per_tick_);
        for (std::int64_t i = 0; i < substeps; ++i) {
            if (devices_->robot->step(devices_->time_step_ms) == -1) {
                return false;
            }
            if (demo_ == "foraging") {
                update_foraging_state();
            } else if (demo_ == "tag") {
                update_tag_state();
            }
        }
        return true;
    }

private:
    void refresh_scene_handles() {
        if (!supervisor_) {
            robot_node_ = nullptr;
            goal_node_ = nullptr;
            base_node_ = nullptr;
            evader_node_ = nullptr;
            puck_nodes_.clear();
            puck_spawn_.clear();
            carrying_ = false;
            carried_puck_ = -1;
            collected_count_ = 0;
            intercept_count_ = 0;
            return;
        }

        robot_node_ = supervisor_->getFromDef("EPUCK_MAIN");
        if (!robot_node_) {
            robot_node_ = supervisor_->getSelf();
        }
        goal_node_ = supervisor_->getFromDef("GOAL");
        base_node_ = supervisor_->getFromDef("BASE");
        evader_node_ = supervisor_->getFromDef("EVADER");

        puck_nodes_.clear();
        puck_spawn_.clear();
        for (int i = 1; i <= 8; ++i) {
            const std::string def_name = "PUCK_" + std::to_string(i);
            if (webots::Node* node = supervisor_->getFromDef(def_name)) {
                puck_nodes_.push_back(node);
                if (const auto spawn = node_translation(node)) {
                    puck_spawn_.push_back(*spawn);
                } else {
                    puck_spawn_.push_back({0.0, 0.0, 0.018});
                }
            }
        }
        for (char c = 'A'; c <= 'F'; ++c) {
            const std::string def_name = std::string("PUCK_") + c;
            if (webots::Node* node = supervisor_->getFromDef(def_name)) {
                puck_nodes_.push_back(node);
                if (const auto spawn = node_translation(node)) {
                    puck_spawn_.push_back(*spawn);
                } else {
                    puck_spawn_.push_back({0.0, 0.0, 0.018});
                }
            }
        }

        carrying_ = false;
        carried_puck_ = -1;
        collected_count_ = 0;
        intercept_count_ = 0;
        evader_phase_ = 0.0;
    }

    std::optional<std::pair<int, double>> nearest_puck_to_robot() const {
        const auto rxy = node_xy(robot_node_);
        if (!rxy.has_value()) {
            return std::nullopt;
        }
        int best_index = -1;
        double best_dist = 1e9;
        for (std::size_t i = 0; i < puck_nodes_.size(); ++i) {
            const auto pxy = node_xy(puck_nodes_[i]);
            if (!pxy.has_value()) {
                continue;
            }
            const double d = planar_distance(*rxy, *pxy);
            if (d < best_dist) {
                best_dist = d;
                best_index = static_cast<int>(i);
            }
        }
        if (best_index < 0) {
            return std::nullopt;
        }
        return std::make_pair(best_index, best_dist);
    }

    double rand_uniform(double lo, double hi) {
        std::uniform_real_distribution<double> dist(lo, hi);
        return dist(rng_);
    }

    void respawn_puck(int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= puck_nodes_.size()) {
            return;
        }
        std::array<double, 3> spawn = {0.0, 0.0, 0.018};
        if (static_cast<std::size_t>(index) < puck_spawn_.size()) {
            spawn = puck_spawn_[static_cast<std::size_t>(index)];
        }
        spawn[0] = clampd(spawn[0] + rand_uniform(-0.08, 0.08), -0.45, 0.45);
        spawn[1] = clampd(spawn[1] + rand_uniform(-0.08, 0.08), -0.45, 0.45);
        set_node_translation(puck_nodes_[static_cast<std::size_t>(index)], spawn);
        puck_nodes_[static_cast<std::size_t>(index)]->resetPhysics();
    }

    void update_foraging_state() {
        if (!supervisor_ || !robot_node_ || !base_node_ || puck_nodes_.empty()) {
            return;
        }

        const auto rxy = node_xy(robot_node_);
        const auto bxy = node_xy(base_node_);
        if (!rxy.has_value() || !bxy.has_value()) {
            return;
        }

        const double yaw = node_yaw(robot_node_);

        if (carrying_ && carried_puck_ >= 0 && static_cast<std::size_t>(carried_puck_) < puck_nodes_.size()) {
            const std::array<double, 3> carried_pose = {
                (*rxy)[0] + 0.05 * std::cos(yaw),
                (*rxy)[1] + 0.05 * std::sin(yaw),
                0.018,
            };
            set_node_translation(puck_nodes_[static_cast<std::size_t>(carried_puck_)], carried_pose);
            puck_nodes_[static_cast<std::size_t>(carried_puck_)]->resetPhysics();

            if (planar_distance(*rxy, *bxy) < 0.085) {
                carrying_ = false;
                ++collected_count_;
                respawn_puck(carried_puck_);
                carried_puck_ = -1;
            }
            return;
        }

        const auto nearest = nearest_puck_to_robot();
        if (nearest.has_value() && nearest->second < 0.055) {
            carrying_ = true;
            carried_puck_ = nearest->first;
        }
    }

    void update_tag_state() {
        if (!supervisor_ || !evader_node_) {
            return;
        }

        evader_phase_ += 0.038;
        const double radius = 0.24 + 0.07 * std::sin(0.47 * evader_phase_);
        const std::array<double, 3> evader_pose = {
            clampd(radius * std::cos(evader_phase_), -0.42, 0.42),
            clampd(radius * std::sin(0.82 * evader_phase_), -0.42, 0.42),
            0.0,
        };
        set_node_translation(evader_node_, evader_pose);
        evader_node_->resetPhysics();

        if (!robot_node_) {
            return;
        }
        const auto rxy = node_xy(robot_node_);
        const auto exy = node_xy(evader_node_);
        if (!rxy.has_value() || !exy.has_value()) {
            return;
        }
        if (planar_distance(*rxy, *exy) < 0.095) {
            ++intercept_count_;
            evader_phase_ += rand_uniform(0.7, 2.4);
        }
    }

    void add_line_observation(muslisp::value obs, muslisp::gc_root_scope& roots) {
        const std::array<double, 3> ground = devices_->read_ground_normalised();
        std::vector<double> ground_vec(ground.begin(), ground.end());
        muslisp::value ground_list = numeric_vector_to_lisp_list(ground_vec);
        roots.add(&ground_list);

        const double dark_left = clampd(1.0 - ground[0], 0.0, 1.0);
        const double dark_centre = clampd(1.0 - ground[1], 0.0, 1.0);
        const double dark_right = clampd(1.0 - ground[2], 0.0, 1.0);
        const double dark_sum = dark_left + dark_centre + dark_right;
        const double line_error = dark_sum > 1e-6 ? clampd((dark_right - dark_left) / dark_sum, -1.0, 1.0) : 0.0;

        map_set_symbol(obs, "ground", ground_list);
        map_set_symbol(obs, "line_error", muslisp::make_float(line_error));

        muslisp::value planner_hint = muslisp::make_map();
        roots.add(&planner_hint);
        map_set_symbol(planner_hint, "model_service", muslisp::make_string("epuck-line-v1"));
        map_set_symbol(planner_hint, "state_key", muslisp::make_string("planner_state"));
        map_set_symbol(obs, "planner_hint", planner_hint);
    }

    void add_goal_observation(muslisp::value obs,
                              const std::array<double, 8>& proximity,
                              double min_obstacle,
                              muslisp::gc_root_scope& roots) {
        map_set_symbol(obs, "obs_schema", muslisp::make_string("epuck.goal.obs.v1"));

        const auto rxy = node_xy(robot_node_);
        const auto gxy = node_xy(goal_node_);
        double goal_dist = 1.0;
        double goal_bearing = 0.0;
        if (rxy.has_value() && gxy.has_value()) {
            goal_dist = planar_distance(*rxy, *gxy);
            goal_bearing = relative_bearing(*rxy, node_yaw(robot_node_), *gxy);
            muslisp::value goal_xy_list = numeric_vector_to_lisp_list({(*gxy)[0], (*gxy)[1]});
            roots.add(&goal_xy_list);
            map_set_symbol(obs, "goal_xy", goal_xy_list);
        }

        const std::vector<double> lidar = lidar_lite_from_proximity(proximity);
        muslisp::value lidar_list = numeric_vector_to_lisp_list(lidar);
        roots.add(&lidar_list);

        map_set_symbol(obs, "goal_dist", muslisp::make_float(goal_dist));
        map_set_symbol(obs, "goal_bearing", muslisp::make_float(goal_bearing));
        map_set_symbol(obs, "obstacle_front", muslisp::make_float(1.0 - min_obstacle));
        map_set_symbol(obs, "lidar_lite", lidar_list);

        muslisp::value planner_hint = muslisp::make_map();
        roots.add(&planner_hint);
        map_set_symbol(planner_hint, "model_service", muslisp::make_string("epuck-goal-v1"));
        map_set_symbol(planner_hint, "state_key", muslisp::make_string("planner_state"));
        map_set_symbol(obs, "planner_hint", planner_hint);
    }

    void add_foraging_observation(muslisp::value obs, double min_obstacle, muslisp::gc_root_scope& roots) {
        map_set_symbol(obs, "obs_schema", muslisp::make_string("epuck.foraging.obs.v1"));
        map_set_symbol(obs, "carrying", muslisp::make_boolean(carrying_));
        map_set_symbol(obs, "collected", muslisp::make_integer(collected_count_));
        map_set_symbol(obs, "puck_total", muslisp::make_integer(static_cast<std::int64_t>(puck_nodes_.size())));
        map_set_symbol(obs, "obstacle_front", muslisp::make_float(1.0 - min_obstacle));

        const auto rxy = node_xy(robot_node_);
        const auto bxy = node_xy(base_node_);

        double target_dist = 1.5;
        double target_bearing = 0.0;
        std::string target_kind = "none";

        if (rxy.has_value() && bxy.has_value()) {
            const double yaw = node_yaw(robot_node_);
            const double base_dist = planar_distance(*rxy, *bxy);
            const double base_bearing = relative_bearing(*rxy, yaw, *bxy);
            map_set_symbol(obs, "base_dist", muslisp::make_float(base_dist));
            map_set_symbol(obs, "base_bearing", muslisp::make_float(base_bearing));

            muslisp::value base_xy_list = numeric_vector_to_lisp_list({(*bxy)[0], (*bxy)[1]});
            roots.add(&base_xy_list);
            map_set_symbol(obs, "base_xy", base_xy_list);

            if (carrying_) {
                target_dist = base_dist;
                target_bearing = base_bearing;
                target_kind = "base";
                map_set_symbol(obs, "target_xy", base_xy_list);
            } else {
                const auto nearest = nearest_puck_to_robot();
                if (nearest.has_value() && static_cast<std::size_t>(nearest->first) < puck_nodes_.size()) {
                    const auto pxy = node_xy(puck_nodes_[static_cast<std::size_t>(nearest->first)]);
                    if (pxy.has_value()) {
                        target_dist = nearest->second;
                        target_bearing = relative_bearing(*rxy, yaw, *pxy);
                        target_kind = "puck";
                        muslisp::value target_xy_list = numeric_vector_to_lisp_list({(*pxy)[0], (*pxy)[1]});
                        roots.add(&target_xy_list);
                        map_set_symbol(obs, "target_xy", target_xy_list);
                        map_set_symbol(obs, "target_index", muslisp::make_integer(nearest->first));
                    }
                }
            }
        }

        map_set_symbol(obs, "target_kind", muslisp::make_string(target_kind));
        map_set_symbol(obs, "target_dist", muslisp::make_float(target_dist));
        map_set_symbol(obs, "target_bearing", muslisp::make_float(target_bearing));

        muslisp::value planner_hint = muslisp::make_map();
        roots.add(&planner_hint);
        map_set_symbol(planner_hint, "model_service", muslisp::make_string("epuck-target-v1"));
        map_set_symbol(planner_hint, "state_key", muslisp::make_string("planner_state"));
        map_set_symbol(obs, "planner_hint", planner_hint);
    }

    void add_tag_observation(muslisp::value obs, double min_obstacle, muslisp::gc_root_scope& roots) {
        map_set_symbol(obs, "obs_schema", muslisp::make_string("epuck.tag.obs.v1"));
        map_set_symbol(obs, "intercepts", muslisp::make_integer(intercept_count_));
        map_set_symbol(obs, "obstacle_front", muslisp::make_float(1.0 - min_obstacle));

        const auto rxy = node_xy(robot_node_);
        const auto exy = node_xy(evader_node_);

        double evader_dist = 2.0;
        double evader_bearing = 0.0;
        bool evader_seen = false;
        if (rxy.has_value() && exy.has_value()) {
            evader_dist = planar_distance(*rxy, *exy);
            evader_bearing = relative_bearing(*rxy, node_yaw(robot_node_), *exy);
            evader_seen = true;
            muslisp::value evader_xy_list = numeric_vector_to_lisp_list({(*exy)[0], (*exy)[1]});
            roots.add(&evader_xy_list);
            map_set_symbol(obs, "evader_xy", evader_xy_list);
        }

        map_set_symbol(obs, "evader_seen", muslisp::make_boolean(evader_seen));
        map_set_symbol(obs, "evader_dist", muslisp::make_float(evader_dist));
        map_set_symbol(obs, "evader_bearing", muslisp::make_float(evader_bearing));

        muslisp::value planner_hint = muslisp::make_map();
        roots.add(&planner_hint);
        map_set_symbol(planner_hint, "model_service", muslisp::make_string("epuck-intercept-v1"));
        map_set_symbol(planner_hint, "state_key", muslisp::make_string("planner_state"));
        map_set_symbol(obs, "planner_hint", planner_hint);
    }

    epuck_devices* devices_ = nullptr;
    webots::Supervisor* supervisor_ = nullptr;
    webots::Node* robot_node_ = nullptr;
    webots::Node* goal_node_ = nullptr;
    webots::Node* base_node_ = nullptr;
    webots::Node* evader_node_ = nullptr;

    std::vector<webots::Node*> puck_nodes_;
    std::vector<std::array<double, 3>> puck_spawn_;

    std::int64_t steps_per_tick_ = 1;
    std::string demo_ = "obstacle";
    std::string obs_schema_ = "epuck.obstacle.obs.v1";

    bool has_pending_ = false;
    double pending_left_ = 0.0;
    double pending_right_ = 0.0;

    bool carrying_ = false;
    int carried_puck_ = -1;
    int collected_count_ = 0;
    int intercept_count_ = 0;
    double evader_phase_ = 0.0;

    bool has_seed_ = false;
    std::int64_t configured_seed_ = 0;
    std::mt19937_64 rng_{0xC0FFEEu};
};

struct extension_context {
    std::shared_ptr<webots_env_backend> backend;
};

void register_extension(muslisp::registrar* r, void* user) {
    (void)r;
    auto* context = static_cast<extension_context*>(user);
    if (!context || !context->backend) {
        throw muslisp::lisp_error("webots extension context is invalid");
    }
    muslisp::env_api_register_backend("webots", context->backend);
}

std::filesystem::path resolve_example_root(const char* argv0) {
    std::filesystem::path exe_path = argv0 ? std::filesystem::path(argv0) : std::filesystem::path("muesli_epuck");
    if (exe_path.is_relative()) {
        exe_path = std::filesystem::current_path() / exe_path;
    }
    std::error_code ec;
    exe_path = std::filesystem::weakly_canonical(exe_path, ec);
    if (ec) {
        exe_path = std::filesystem::absolute(exe_path);
    }

    std::filesystem::path controller_dir = exe_path.parent_path();
    return controller_dir.parent_path().parent_path();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        webots::Supervisor robot;
        epuck_devices devices(&robot);

        bt::runtime_host& host = bt::default_runtime_host();
        host.clear_all();
        bt::install_demo_callbacks(host);
        install_epuck_callbacks(host);
        host.planner_ref().register_model("epuck-line-v1", std::make_shared<epuck_line_planner_model>());

        const auto target_model = std::make_shared<epuck_target_planner_model>();
        host.planner_ref().register_model("epuck-goal-v1", target_model);
        host.planner_ref().register_model("epuck-target-v1", target_model);
        host.planner_ref().register_model("epuck-intercept-v1", target_model);

        extension_context context;
        context.backend = std::make_shared<webots_env_backend>(&devices);

        muslisp::runtime_config config;
        config.extension_register_hook = register_extension;
        config.extension_register_user = &context;

        muslisp::env_ptr env = muslisp::create_global_env(config);

        const std::filesystem::path example_root = resolve_example_root(argc > 0 ? argv[0] : nullptr);
        const std::filesystem::path lisp_entry = example_root / "lisp" / "main.mueslisp";

        std::error_code ec;
        std::filesystem::current_path(example_root, ec);
        if (ec) {
            std::cerr << "warning: failed to set cwd to " << example_root << ": " << ec.message() << '\n';
        }

        return run_script(lisp_entry, env);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
}
