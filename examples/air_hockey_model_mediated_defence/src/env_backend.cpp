#include "env_backend.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "muslisp/gc.hpp"
#include "muslisp/value.hpp"

namespace air_hockey_demo {
namespace {

muslisp::map_key symbol_key(const std::string& name) {
    muslisp::map_key key;
    key.type = muslisp::map_key_type::symbol;
    key.text_data = name;
    return key;
}

void map_set_symbol(muslisp::value map, const std::string& key, muslisp::value value) {
    map->map_data[symbol_key(key)] = value;
}

std::string normalise_key(std::string key) {
    if (!key.empty() && key.front() == ':') {
        key.erase(key.begin());
    }
    std::replace(key.begin(), key.end(), '-', '_');
    return key;
}

std::optional<muslisp::value> map_lookup(muslisp::value map, std::string_view requested) {
    if (!muslisp::is_map(map)) {
        return std::nullopt;
    }
    for (const auto& [key, value] : map->map_data) {
        if ((key.type == muslisp::map_key_type::symbol || key.type == muslisp::map_key_type::string) &&
            normalise_key(key.text_data) == requested) {
            return value;
        }
    }
    return std::nullopt;
}

std::int64_t require_integer(muslisp::value value, std::string_view where) {
    if (!muslisp::is_integer(value)) {
        throw std::runtime_error(std::string(where) + " must be an integer");
    }
    return muslisp::integer_value(value);
}

double require_number(muslisp::value value, std::string_view where) {
    double result = 0.0;
    if (muslisp::is_integer(value)) {
        result = static_cast<double>(muslisp::integer_value(value));
    } else if (muslisp::is_float(value)) {
        result = muslisp::float_value(value);
    } else {
        throw std::runtime_error(std::string(where) + " must be numeric");
    }
    if (!std::isfinite(result)) {
        throw std::runtime_error(std::string(where) + " must be finite");
    }
    return result;
}

std::string require_text(muslisp::value value, std::string_view where) {
    if (muslisp::is_string(value)) {
        return muslisp::string_value(value);
    }
    if (muslisp::is_symbol(value)) {
        return muslisp::symbol_name(value);
    }
    throw std::runtime_error(std::string(where) + " must be text");
}

std::vector<std::int64_t> require_integer_list(muslisp::value value, std::string_view where) {
    if (!muslisp::is_proper_list(value)) {
        throw std::runtime_error(std::string(where) + " must be a proper integer list");
    }
    std::vector<std::int64_t> result;
    for (muslisp::value item : muslisp::vector_from_list(value)) {
        result.push_back(require_integer(item, where));
    }
    return result;
}

std::array<double, kActionDimension> require_action_target(muslisp::value value) {
    if (!muslisp::is_proper_list(value)) {
        throw std::runtime_error("env.act target must be a proper numeric list");
    }
    const std::vector<muslisp::value> items = muslisp::vector_from_list(value);
    if (items.size() != kActionDimension) {
        throw std::runtime_error("env.act target must contain exactly two values");
    }
    return {require_number(items[0], "env.act target"), require_number(items[1], "env.act target")};
}

muslisp::value numeric_list(const double* begin, const double* end) {
    std::vector<muslisp::value> values;
    values.reserve(static_cast<std::size_t>(end - begin));
    muslisp::gc_root_scope roots(muslisp::default_gc());
    for (const double* item = begin; item != end; ++item) {
        values.push_back(muslisp::make_float(*item));
        roots.add(&values.back());
    }
    return muslisp::list_from_vector(values);
}

void validate_configuration(const host_configuration& configuration) {
    if (configuration.blackout_start_step < 0 || configuration.blackout_length_steps < 0 ||
        configuration.timeout_steps <= 0 || configuration.action_lock_steps < 0 ||
        configuration.blackout_start_step + configuration.blackout_length_steps >
            configuration.timeout_steps ||
        configuration.action_lock_steps > configuration.timeout_steps ||
        std::any_of(configuration.replace_track_steps.begin(), configuration.replace_track_steps.end(),
                    [&](std::int64_t step) { return step <= 0 || step > configuration.timeout_steps; }) ||
        (configuration.terminate_at_step.has_value() &&
         (*configuration.terminate_at_step <= 0 ||
          *configuration.terminate_at_step > configuration.timeout_steps))) {
        throw std::runtime_error("air-hockey host configuration violates the v1 bounds");
    }
}

}  // namespace

air_hockey_env_backend::air_hockey_env_backend(std::filesystem::path socket_path)
    : client_(std::move(socket_path)) {
    (void)client_.info();
}

std::string air_hockey_env_backend::backend_version() const {
    return "airhockey.env.v1";
}

muslisp::env_backend_supports air_hockey_env_backend::supports() const {
    return muslisp::env_backend_supports{
        .reset = true,
        .debug_draw = false,
        .headless = true,
        .realtime_pacing = false,
        .deterministic_seed = true,
    };
}

std::string air_hockey_env_backend::notes() const {
    return "Local air-hockey host adapter; MuJoCo remains outside muesli-bt";
}

muslisp::value air_hockey_env_backend::info() const {
    const host_info remote = client_.info();
    muslisp::value result = muslisp::make_map();
    muslisp::gc_root_scope roots(muslisp::default_gc());
    roots.add(&result);
    map_set_symbol(result, "protocol_version", muslisp::make_string(remote.protocol_version));
    map_set_symbol(result, "host_backend", muslisp::make_string(remote.backend));
    map_set_symbol(result, "obs_schema", muslisp::make_string(remote.observation_schema));
    map_set_symbol(result, "action_schema", muslisp::make_string(remote.action_schema));
    map_set_symbol(result, "observation_dimension", muslisp::make_integer(remote.observation_dimension));
    map_set_symbol(result, "action_dimension", muslisp::make_integer(remote.action_dimension));
    map_set_symbol(result, "control_period_ms", muslisp::make_integer(remote.control_period_ms));
    map_set_symbol(result, "max_deadline_ms", muslisp::make_integer(remote.max_deadline_ms));
    map_set_symbol(result, "privileged_fields_available",
                   muslisp::make_boolean(remote.privileged_fields_available));
    return result;
}

void air_hockey_env_backend::configure(muslisp::value options) {
    if (!muslisp::is_map(options)) {
        throw std::runtime_error("air-hockey configure expects a map");
    }
    const std::unordered_set<std::string> common_options{
        "tick_hz", "steps_per_tick", "seed", "headless", "realtime", "log_path",
        "event_log_path", "event_log_ring_size", "event_log_flush_each_message"};
    const std::unordered_set<std::string> host_options{
        "blackout_start_step", "blackout_length_steps", "timeout_steps", "action_lock_steps",
        "replace_track_steps", "terminate_at_step"};
    for (const auto& [key, _] : options->map_data) {
        if (key.type != muslisp::map_key_type::symbol && key.type != muslisp::map_key_type::string) {
            throw std::runtime_error("air-hockey configure keys must be strings or symbols");
        }
        const std::string name = normalise_key(key.text_data);
        if (!common_options.contains(name) && !host_options.contains(name)) {
            throw std::runtime_error("air-hockey configure rejects unknown key: " + name);
        }
    }
    if (const auto value = map_lookup(options, "blackout_start_step")) {
        configuration_.blackout_start_step = require_integer(*value, "blackout_start_step");
    }
    if (const auto value = map_lookup(options, "blackout_length_steps")) {
        configuration_.blackout_length_steps = require_integer(*value, "blackout_length_steps");
    }
    if (const auto value = map_lookup(options, "timeout_steps")) {
        configuration_.timeout_steps = require_integer(*value, "timeout_steps");
    }
    if (const auto value = map_lookup(options, "action_lock_steps")) {
        configuration_.action_lock_steps = require_integer(*value, "action_lock_steps");
    }
    if (const auto value = map_lookup(options, "replace_track_steps")) {
        configuration_.replace_track_steps = require_integer_list(*value, "replace_track_steps");
    }
    if (const auto value = map_lookup(options, "terminate_at_step")) {
        configuration_.terminate_at_step = muslisp::is_nil(*value)
                                                   ? std::nullopt
                                                   : std::optional<std::int64_t>{require_integer(
                                                         *value, "terminate_at_step")};
    }
    validate_configuration(configuration_);
    configuration_ = client_.configure(configuration_);
}

muslisp::value air_hockey_env_backend::reset(std::optional<std::int64_t> seed) {
    std::optional<std::uint32_t> converted;
    if (seed.has_value()) {
        if (*seed < 0 || static_cast<std::uint64_t>(*seed) > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("air-hockey reset seed must fit uint32");
        }
        converted = static_cast<std::uint32_t>(*seed);
    }
    return state_to_lisp(reset_host(converted));
}

muslisp::value air_hockey_env_backend::observe() {
    return state_to_lisp(observe_host());
}

void air_hockey_env_backend::act(muslisp::value action) {
    if (!muslisp::is_map(action)) {
        throw std::runtime_error("air-hockey env.act expects an action map");
    }
    if (action->map_data.size() != 2) {
        throw std::runtime_error("air-hockey env.act action map must contain exactly two fields");
    }
    const auto schema = map_lookup(action, "action_schema");
    const auto target = map_lookup(action, "target");
    if (!schema || !target || require_text(*schema, "env.act action_schema") !=
                                  "airhockey.normalised_mallet_target.v1") {
        throw std::runtime_error("air-hockey env.act requires action_schema v1 and target");
    }
    act_target(require_action_target(*target));
}

bool air_hockey_env_backend::step() {
    return step_host().state.episode_active;
}

void air_hockey_env_backend::configure_host(const host_configuration& configuration) {
    validate_configuration(configuration);
    configuration_ = client_.configure(configuration);
}

public_state air_hockey_env_backend::reset_host(std::optional<std::uint32_t> seed) {
    last_state_ = client_.reset(seed);
    return *last_state_;
}

public_state air_hockey_env_backend::observe_host() {
    last_state_ = client_.observe();
    return *last_state_;
}

void air_hockey_env_backend::act_target(const std::array<double, kActionDimension>& action) {
    if (std::any_of(action.begin(), action.end(), [](double value) {
            return !std::isfinite(value) || value < -1.0 || value > 1.0;
        })) {
        throw std::runtime_error("air-hockey target must contain two finite values in [-1, 1]");
    }
    client_.act(action);
}

host_step_result air_hockey_env_backend::step_host() {
    host_step_result result = client_.step();
    last_state_ = result.state;
    return result;
}

const public_state& air_hockey_env_backend::last_state() const {
    if (!last_state_.has_value()) {
        throw std::runtime_error("air-hockey host has no state before reset or observe");
    }
    return *last_state_;
}

muslisp::value air_hockey_env_backend::state_to_lisp(const public_state& state) const {
    muslisp::value result = muslisp::make_map();
    muslisp::value observation = numeric_list(state.observation.data(),
                                              state.observation.data() + state.observation.size());
    muslisp::gc_root_scope roots(muslisp::default_gc());
    roots.add(&result);
    roots.add(&observation);
    map_set_symbol(result, "obs_schema", muslisp::make_string(state.observation_schema));
    map_set_symbol(result, "state", observation);
    map_set_symbol(result, "observation_step", muslisp::make_integer(state.observation_step));
    map_set_symbol(result, "puck_visible", muslisp::make_boolean(state.puck_visible));
    map_set_symbol(result, "action_locked", muslisp::make_boolean(state.action_locked));
    map_set_symbol(result, "episode_active", muslisp::make_boolean(state.episode_active));
    map_set_symbol(result, "terminated", muslisp::make_boolean(state.terminated));
    map_set_symbol(result, "truncated", muslisp::make_boolean(state.truncated));
    map_set_symbol(result, "done", muslisp::make_boolean(state.terminated || state.truncated));
    map_set_symbol(result, "defence_context_id", muslisp::make_string(state.defence_context_id));
    map_set_symbol(result, "episode_id", muslisp::make_string(state.episode_id));
    return result;
}

void register_air_hockey_env_backend(const std::string& name,
                                     const std::shared_ptr<air_hockey_env_backend>& backend) {
    if (!backend) {
        throw std::invalid_argument("air-hockey env backend must not be null");
    }
    muslisp::env_api_register_backend(name, backend);
}

}  // namespace air_hockey_demo
