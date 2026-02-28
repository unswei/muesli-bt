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
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <webots/DistanceSensor.hpp>
#include <webots/Motor.hpp>
#include <webots/Robot.hpp>

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

class epuck_line_planner_model final : public bt::planner_model {
public:
    [[nodiscard]] bt::planner_step_result step(const bt::planner_vector& state,
                                               const bt::planner_vector& action,
                                               bt::planner_rng& rng) const override {
        const bt::planner_vector u = clamp_action(action);
        const double line_error = state.empty() ? 0.0 : state[0];
        const double steer = (u[1] - u[0]) / (2.0 * kMaxWheelSpeed);
        const double noise = rng.normal(0.0, 0.01);

        const double next_error = clampd(0.82 * line_error + 0.55 * steer + noise, -2.0, 2.0);
        const bool done = std::fabs(next_error) > 1.2;

        bt::planner_step_result out;
        out.next_state = {next_error};
        out.reward = -std::fabs(next_error) - 0.02 * std::fabs(steer);
        if (done) {
            out.reward -= 1.0;
        }
        out.done = done;
        return out;
    }

    [[nodiscard]] bt::planner_vector sample_action(const bt::planner_vector&, bt::planner_rng& rng) const override {
        const double left = rng.uniform(-kMaxWheelSpeed, kMaxWheelSpeed);
        const double right = rng.uniform(-kMaxWheelSpeed, kMaxWheelSpeed);
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
            return {0.0, 0.0};
        }
        if (action.size() == 1) {
            const double v = clampd(action[0], -kMaxWheelSpeed, kMaxWheelSpeed);
            return {v, v};
        }
        return {clampd(action[0], -kMaxWheelSpeed, kMaxWheelSpeed), clampd(action[1], -kMaxWheelSpeed, kMaxWheelSpeed)};
    }

    [[nodiscard]] bt::planner_vector zero_action() const override {
        return {0.0, 0.0};
    }

    [[nodiscard]] bool validate_state(const bt::planner_vector& state) const override {
        return state.size() >= 1 && std::isfinite(state[0]);
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
    }

    [[nodiscard]] std::array<double, 8> read_proximity_normalised() const {
        std::array<double, 8> values{};
        for (int i = 0; i < 8; ++i) {
            values[static_cast<std::size_t>(i)] = clampd(proximity[i]->getValue() / 4096.0, 0.0, 1.0);
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
            values[static_cast<std::size_t>(i)] = clampd(ground[i]->getValue() / 1000.0, 0.0, 1.0);
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
    }

    [[nodiscard]] std::string backend_version() const override {
        return "webots.epuck.v1";
    }

    [[nodiscard]] muslisp::env_backend_supports supports() const override {
        muslisp::env_backend_supports out;
        out.reset = false;
        out.debug_draw = false;
        out.headless = true;
        out.realtime_pacing = true;
        out.deterministic_seed = false;
        return out;
    }

    [[nodiscard]] std::string notes() const override {
        return "Webots e-puck adapter for env.api.v1";
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
            (void)require_int_value(*v, "env.configure :seed");
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
            const std::string demo = require_text_value(*v, "env.configure :demo");
            if (demo == "line") {
                obs_schema_ = "epuck.line.obs.v1";
            } else if (demo == "obstacle") {
                obs_schema_ = "epuck.obstacle.obs.v1";
            }
        }

        if (const auto v = map_lookup_option(opts, "obs_schema")) {
            obs_schema_ = require_text_value(*v, "env.configure :obs_schema");
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

        if (obs_schema_ == "epuck.line.obs.v1") {
            const std::array<double, 3> ground = devices_->read_ground_normalised();
            std::vector<double> ground_vec(ground.begin(), ground.end());
            muslisp::value ground_list = numeric_vector_to_lisp_list(ground_vec);
            roots.add(&ground_list);

            const double line_error = clampd(ground[2] - ground[0], -1.0, 1.0);

            map_set_symbol(obs, "ground", ground_list);
            map_set_symbol(obs, "line_error", muslisp::make_float(line_error));

            muslisp::value planner_hint = muslisp::make_map();
            roots.add(&planner_hint);
            map_set_symbol(planner_hint, "model_service", muslisp::make_string("epuck-line-v1"));
            map_set_symbol(planner_hint, "state_key", muslisp::make_string("planner_state"));
            map_set_symbol(obs, "planner_hint", planner_hint);
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

        for (std::int64_t i = 0; i < std::max<std::int64_t>(1, steps_per_tick_); ++i) {
            if (devices_->robot->step(devices_->time_step_ms) == -1) {
                return false;
            }
        }
        return true;
    }

private:
    epuck_devices* devices_ = nullptr;
    std::int64_t steps_per_tick_ = 1;
    std::string obs_schema_ = "epuck.obstacle.obs.v1";
    bool has_pending_ = false;
    double pending_left_ = 0.0;
    double pending_right_ = 0.0;
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
        webots::Robot robot;
        epuck_devices devices(&robot);

        bt::runtime_host& host = bt::default_runtime_host();
        host.clear_all();
        bt::install_demo_callbacks(host);
        install_epuck_callbacks(host);
        host.planner_ref().register_model("epuck-line-v1", std::make_shared<epuck_line_planner_model>());

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
