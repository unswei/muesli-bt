#include "muslisp/env_builtins.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "muslisp/env_api.hpp"
#include "muslisp/error.hpp"
#include "muslisp/eval.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"
#include "muslisp/value.hpp"

namespace muslisp {
namespace {

constexpr const char* kEnvApiVersion = "env.api.v1";
constexpr std::int64_t kDefaultTickHz = 20;
constexpr std::int64_t kDefaultStepsPerTick = 1;

struct env_runtime_state {
    std::int64_t tick_hz = kDefaultTickHz;
    std::int64_t steps_per_tick = kDefaultStepsPerTick;
    bool realtime = false;
    bool headless = false;
    std::optional<std::int64_t> seed{};
    std::string log_path{};
    std::int64_t episode = 0;
    std::int64_t step = 0;
    std::chrono::steady_clock::time_point time_origin = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point next_deadline{};
};

env_runtime_state& runtime_state() {
    static env_runtime_state state;
    return state;
}

void bind_primitive(env_ptr global_env, const std::string& name, primitive_fn fn) {
    define(global_env, name, make_primitive(name, std::move(fn)));
}

void require_arity(const std::string& name, const std::vector<value>& args, std::size_t expected) {
    if (args.size() != expected) {
        throw lisp_error(name + ": expected " + std::to_string(expected) + " arguments, got " + std::to_string(args.size()));
    }
}

bool is_callable(value v) {
    return is_primitive(v) || is_closure(v);
}

std::int64_t require_int(value v, const std::string& where) {
    if (!is_integer(v)) {
        throw lisp_error(where + ": expected integer");
    }
    return integer_value(v);
}

bool require_bool(value v, const std::string& where) {
    if (!is_boolean(v)) {
        throw lisp_error(where + ": expected boolean");
    }
    return boolean_value(v);
}

std::string require_text(value v, const std::string& where) {
    if (is_string(v)) {
        return string_value(v);
    }
    if (is_symbol(v)) {
        return symbol_name(v);
    }
    throw lisp_error(where + ": expected string or symbol");
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

std::optional<value> map_lookup_any(value map_obj, std::initializer_list<std::string> normalized_keys) {
    for (const std::string& key : normalized_keys) {
        if (const auto hit = map_lookup_option(map_obj, key)) {
            return hit;
        }
    }
    return std::nullopt;
}

std::int64_t monotonic_ms_from_origin(const env_runtime_state& state) {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - state.time_origin).count();
}

std::shared_ptr<env_backend> attached_backend_or_throw() {
    const std::shared_ptr<env_backend> backend = env_api_attached_backend();
    if (!backend) {
        throw lisp_error("env backend not attached");
    }
    return backend;
}

void apply_common_runtime_options(value opts_map, const std::string& where) {
    if (!is_map(opts_map)) {
        throw lisp_error(where + ": expected map");
    }

    env_runtime_state& state = runtime_state();

    if (const auto tick_hz = map_lookup_option(opts_map, "tick_hz")) {
        const std::int64_t hz = require_int(*tick_hz, where + " :tick_hz");
        if (hz <= 0) {
            throw lisp_error(where + " :tick_hz: expected > 0");
        }
        state.tick_hz = hz;
    }

    if (const auto steps_per_tick = map_lookup_option(opts_map, "steps_per_tick")) {
        const std::int64_t steps = require_int(*steps_per_tick, where + " :steps_per_tick");
        if (steps <= 0) {
            throw lisp_error(where + " :steps_per_tick: expected > 0");
        }
        state.steps_per_tick = steps;
    }

    if (const auto seed = map_lookup_option(opts_map, "seed")) {
        state.seed = require_int(*seed, where + " :seed");
    }

    if (const auto headless = map_lookup_option(opts_map, "headless")) {
        state.headless = require_bool(*headless, where + " :headless");
    }

    if (const auto realtime = map_lookup_option(opts_map, "realtime")) {
        state.realtime = require_bool(*realtime, where + " :realtime");
    }

    if (const auto log_path = map_lookup_option(opts_map, "log_path")) {
        if (!is_string(*log_path)) {
            throw lisp_error(where + " :log_path: expected string");
        }
        state.log_path = string_value(*log_path);
    }
}

void enrich_observation(value obs_map) {
    if (!is_map(obs_map)) {
        throw lisp_error("env.observe: backend returned non-map observation");
    }

    env_runtime_state& state = runtime_state();
    gc_root_scope roots(default_gc());
    roots.add(&obs_map);

    if (const auto obs_schema = map_lookup_option(obs_map, "obs_schema")) {
        if (!is_string(*obs_schema)) {
            throw lisp_error("env.observe: obs_schema must be string");
        }
    } else {
        map_set_symbol(obs_map, "obs_schema", make_string("env.obs.v1"));
    }

    if (const auto t_ms = map_lookup_option(obs_map, "t_ms")) {
        if (!is_integer(*t_ms)) {
            throw lisp_error("env.observe: t_ms must be integer");
        }
    } else {
        map_set_symbol(obs_map, "t_ms", make_integer(monotonic_ms_from_origin(state)));
    }

    map_set_symbol(obs_map, "episode", make_integer(state.episode));
    map_set_symbol(obs_map, "step", make_integer(state.step));
}

bool obs_done(value obs_map) {
    if (!is_map(obs_map)) {
        return false;
    }
    const auto done = map_lookup_option(obs_map, "done");
    return done && is_boolean(*done) && boolean_value(*done);
}

bool on_tick_result_indicates_success(value result) {
    if (is_boolean(result)) {
        return boolean_value(result);
    }
    if (is_symbol(result)) {
        const std::string& s = symbol_name(result);
        return s == "success" || s == ":success";
    }
    if (is_map(result)) {
        const auto done = map_lookup_option(result, "done");
        return done && is_boolean(*done) && boolean_value(*done);
    }
    return false;
}

bool looks_like_action_map(value candidate) {
    if (!is_map(candidate)) {
        return false;
    }
    const auto schema = map_lookup_option(candidate, "action_schema");
    const auto u = map_lookup_option(candidate, "u");
    return schema.has_value() && u.has_value();
}

std::optional<value> extract_action_from_on_tick(value on_tick_result) {
    if (looks_like_action_map(on_tick_result)) {
        return on_tick_result;
    }
    if (!is_map(on_tick_result)) {
        return std::nullopt;
    }
    const auto maybe_action = map_lookup_any(on_tick_result, {"action", ":action"});
    if (!maybe_action.has_value()) {
        return std::nullopt;
    }
    if (!looks_like_action_map(*maybe_action)) {
        return std::nullopt;
    }
    return *maybe_action;
}

std::string json_escape(std::string_view input) {
    std::ostringstream out;
    for (const char c : input) {
        switch (c) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    const unsigned char uc = static_cast<unsigned char>(c);
                    out << "\\u00";
                    constexpr char hex[] = "0123456789abcdef";
                    out << hex[(uc >> 4) & 0x0f] << hex[uc & 0x0f];
                } else {
                    out << c;
                }
                break;
        }
    }
    return out.str();
}

std::string map_key_to_json_key(const map_key& key) {
    switch (key.type) {
        case map_key_type::symbol:
        case map_key_type::string:
            return key.text_data;
        case map_key_type::integer:
            return std::to_string(key.integer_data);
        case map_key_type::floating:
            return std::to_string(key.float_data);
    }
    return {};
}

std::string value_to_json(value v);

std::string list_to_json(value list_v) {
    if (!is_proper_list(list_v)) {
        return "null";
    }
    std::ostringstream out;
    out << '[';
    const auto items = vector_from_list(list_v);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << value_to_json(items[i]);
    }
    out << ']';
    return out.str();
}

std::string value_to_json(value v) {
    switch (type_of(v)) {
        case value_type::nil:
            return "null";
        case value_type::boolean:
            return boolean_value(v) ? "true" : "false";
        case value_type::integer:
            return std::to_string(integer_value(v));
        case value_type::floating:
            if (!std::isfinite(float_value(v))) {
                return "null";
            }
            return std::to_string(float_value(v));
        case value_type::string:
            return "\"" + json_escape(string_value(v)) + "\"";
        case value_type::symbol:
            return "\"" + json_escape(symbol_name(v)) + "\"";
        case value_type::cons:
            return list_to_json(v);
        case value_type::vec: {
            std::ostringstream out;
            out << '[';
            for (std::size_t i = 0; i < v->vec_data.size(); ++i) {
                if (i > 0) {
                    out << ',';
                }
                out << value_to_json(v->vec_data[i]);
            }
            out << ']';
            return out.str();
        }
        case value_type::map: {
            std::ostringstream out;
            out << '{';
            bool first = true;
            for (const auto& [k, mapped] : v->map_data) {
                if (!first) {
                    out << ',';
                }
                first = false;
                out << "\"" << json_escape(map_key_to_json_key(k)) << "\":" << value_to_json(mapped);
            }
            out << '}';
            return out.str();
        }
        default:
            return "\"" + json_escape(print_value(v)) + "\"";
    }
}

void append_jsonl(const std::string& path, value record) {
    if (path.empty()) {
        return;
    }
    std::ofstream out(path, std::ios::app);
    if (!out) {
        throw lisp_error("env.run-loop: failed to open log_path: " + path);
    }
    out << value_to_json(record) << '\n';
    if (!out) {
        throw lisp_error("env.run-loop: failed writing log_path: " + path);
    }
}

value invoke_callable_unary(value fn, value arg, const std::string& where) {
    if (!is_callable(fn)) {
        throw lisp_error(where + ": expected callable");
    }
    std::vector<value> call_args;
    call_args.push_back(arg);
    return invoke_callable(fn, call_args);
}

value build_tick_record(std::int64_t tick_index,
                        value obs,
                        value action,
                        value on_tick_result,
                        std::optional<std::string> schema_version,
                        double tick_budget_ms,
                        double tick_time_ms,
                        bool used_fallback,
                        bool overrun,
                        const std::string& error_reason) {
    value record = make_map();
    value budget = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&record);
    roots.add(&budget);
    roots.add(&obs);
    roots.add(&action);
    roots.add(&on_tick_result);

    map_set_symbol(record, "tick", make_integer(tick_index));
    if (const auto t_ms = map_lookup_option(obs, "t_ms")) {
        if (is_integer(*t_ms)) {
            map_set_symbol(record, "t_ms", *t_ms);
        }
    }
    if (schema_version.has_value()) {
        map_set_symbol(record, "schema_version", make_string(*schema_version));
    }
    map_set_symbol(record, "obs", obs);
    map_set_symbol(record, "action", action);
    map_set_symbol(record, "on_tick", on_tick_result);
    map_set_symbol(budget, "tick_budget_ms", make_float(tick_budget_ms));
    map_set_symbol(budget, "tick_time_ms", make_float(tick_time_ms));
    map_set_symbol(record, "budget", budget);

    if (is_map(on_tick_result)) {
        if (const auto bt_map = map_lookup_option(on_tick_result, "bt")) {
            if (is_map(*bt_map)) {
                map_set_symbol(record, "bt", *bt_map);
            }
        }
        if (const auto planner_map = map_lookup_option(on_tick_result, "planner")) {
            if (is_map(*planner_map)) {
                map_set_symbol(record, "planner", *planner_map);
            }
        }
    }

    map_set_symbol(record, "used_fallback", make_boolean(used_fallback));
    map_set_symbol(record, "overrun", make_boolean(overrun));
    if (!error_reason.empty()) {
        map_set_symbol(record, "error", make_string(error_reason));
    }
    return record;
}

void emit_record(value observer_fn, bool has_observer, const std::string& log_path, value record) {
    if (has_observer) {
        (void)invoke_callable_unary(observer_fn, record, "env.run-loop observer");
    }
    if (!log_path.empty()) {
        append_jsonl(log_path, record);
    }
}

bool perform_step_and_pacing(env_backend& backend, std::int64_t tick_hz, bool realtime) {
    env_runtime_state& state = runtime_state();
    const bool can_continue = backend.step();
    if (can_continue) {
        ++state.step;
    }

    if (!realtime) {
        state.next_deadline = std::chrono::steady_clock::time_point{};
        return can_continue;
    }

    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / static_cast<double>(tick_hz)));

    const auto now = std::chrono::steady_clock::now();
    if (state.next_deadline.time_since_epoch().count() == 0) {
        state.next_deadline = now + period;
    }
    if (now < state.next_deadline) {
        std::this_thread::sleep_until(state.next_deadline);
    }
    const auto after_sleep = std::chrono::steady_clock::now();
    state.next_deadline += period;
    if (state.next_deadline < after_sleep) {
        state.next_deadline = after_sleep;
    }
    return can_continue;
}

value builtin_env_info(const std::vector<value>& args) {
    require_arity("env.info", args, 0);

    value out = make_map();
    value supports = make_map();
    gc_root_scope roots(default_gc());
    roots.add(&out);
    roots.add(&supports);

    map_set_symbol(out, "api_version", make_string(kEnvApiVersion));
    map_set_symbol(out, "attached", make_boolean(env_api_is_attached()));

    if (!env_api_is_attached()) {
        map_set_symbol(out, "backend", make_nil());
        map_set_symbol(out, "backend_version", make_nil());
        map_set_symbol(supports, "reset", make_boolean(false));
        map_set_symbol(supports, "debug_draw", make_boolean(false));
        map_set_symbol(supports, "headless", make_boolean(false));
        map_set_symbol(supports, "realtime_pacing", make_boolean(false));
        map_set_symbol(supports, "deterministic_seed", make_boolean(false));
        map_set_symbol(out, "supports", supports);
        return out;
    }

    const std::shared_ptr<env_backend> backend = attached_backend_or_throw();
    const env_backend_supports flags = backend->supports();

    map_set_symbol(out, "backend", make_string(env_api_attached_backend_name()));
    const std::string version = backend->backend_version();
    if (version.empty()) {
        map_set_symbol(out, "backend_version", make_nil());
    } else {
        map_set_symbol(out, "backend_version", make_string(version));
    }

    map_set_symbol(supports, "reset", make_boolean(flags.reset));
    map_set_symbol(supports, "debug_draw", make_boolean(flags.debug_draw));
    map_set_symbol(supports, "headless", make_boolean(flags.headless));
    map_set_symbol(supports, "realtime_pacing", make_boolean(flags.realtime_pacing));
    map_set_symbol(supports, "deterministic_seed", make_boolean(flags.deterministic_seed));
    map_set_symbol(out, "supports", supports);

    const std::string notes = backend->notes();
    if (!notes.empty()) {
        map_set_symbol(out, "notes", make_string(notes));
    }
    return out;
}

value builtin_env_attach(const std::vector<value>& args) {
    require_arity("env.attach", args, 1);
    const std::string backend_name = require_text(args[0], "env.attach");
    try {
        env_api_attach(backend_name);
    } catch (const std::exception& e) {
        throw lisp_error(e.what());
    }

    env_runtime_state& state = runtime_state();
    state.episode = 0;
    state.step = 0;
    state.time_origin = std::chrono::steady_clock::now();
    state.next_deadline = std::chrono::steady_clock::time_point{};
    return make_nil();
}

value builtin_env_configure(const std::vector<value>& args) {
    require_arity("env.configure", args, 1);
    value opts = args[0];
    if (!is_map(opts)) {
        throw lisp_error("env.configure: expected map");
    }

    const std::shared_ptr<env_backend> backend = attached_backend_or_throw();
    apply_common_runtime_options(opts, "env.configure");
    try {
        backend->configure(opts);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.configure: ") + e.what());
    }
    return make_nil();
}

value builtin_env_reset(const std::vector<value>& args) {
    require_arity("env.reset", args, 1);
    const std::shared_ptr<env_backend> backend = attached_backend_or_throw();
    if (!backend->supports().reset) {
        throw lisp_error("env.reset: backend does not support reset");
    }

    std::optional<std::int64_t> seed{};
    if (!is_nil(args[0])) {
        seed = require_int(args[0], "env.reset");
    }

    value obs = make_nil();
    gc_root_scope roots(default_gc());
    roots.add(&obs);

    try {
        obs = backend->reset(seed);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.reset: ") + e.what());
    }

    env_runtime_state& state = runtime_state();
    ++state.episode;
    state.step = 0;
    state.time_origin = std::chrono::steady_clock::now();
    state.next_deadline = std::chrono::steady_clock::time_point{};
    if (seed.has_value()) {
        state.seed = seed;
    }
    enrich_observation(obs);
    return obs;
}

value builtin_env_observe(const std::vector<value>& args) {
    require_arity("env.observe", args, 0);
    const std::shared_ptr<env_backend> backend = attached_backend_or_throw();
    value obs = make_nil();
    gc_root_scope roots(default_gc());
    roots.add(&obs);
    try {
        obs = backend->observe();
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.observe: ") + e.what());
    }
    enrich_observation(obs);
    return obs;
}

value builtin_env_act(const std::vector<value>& args) {
    require_arity("env.act", args, 1);
    const std::shared_ptr<env_backend> backend = attached_backend_or_throw();
    try {
        backend->act(args[0]);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.act: ") + e.what());
    }
    return make_nil();
}

value builtin_env_step(const std::vector<value>& args) {
    require_arity("env.step", args, 0);
    const std::shared_ptr<env_backend> backend = attached_backend_or_throw();
    const env_runtime_state& state = runtime_state();
    const bool can_continue = perform_step_and_pacing(*backend, state.tick_hz, state.realtime);
    return make_boolean(can_continue);
}

value builtin_env_debug_draw(const std::vector<value>& args) {
    require_arity("env.debug-draw", args, 1);
    const std::shared_ptr<env_backend> backend = attached_backend_or_throw();
    if (!backend->supports().debug_draw) {
        return make_nil();
    }
    try {
        backend->debug_draw(args[0]);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.debug-draw: ") + e.what());
    }
    return make_nil();
}

value builtin_env_run_loop(const std::vector<value>& args) {
    require_arity("env.run-loop", args, 2);
    if (!is_map(args[0])) {
        throw lisp_error("env.run-loop: expected config map");
    }
    if (!is_callable(args[1])) {
        throw lisp_error("env.run-loop: expected callable on_tick");
    }

    const std::shared_ptr<env_backend> backend = attached_backend_or_throw();

    value config = args[0];
    value on_tick_fn = args[1];
    value safe_action = make_nil();
    value success_predicate = make_nil();
    value observer_fn = make_nil();
    value final_obs = make_map();
    value obs = make_nil();
    value on_tick_result = make_nil();
    value chosen_action = make_nil();
    value tick_record = make_nil();
    value safety_action = make_nil();
    gc_root_scope roots(default_gc());
    roots.add(&config);
    roots.add(&on_tick_fn);
    roots.add(&safe_action);
    roots.add(&success_predicate);
    roots.add(&observer_fn);
    roots.add(&final_obs);
    roots.add(&obs);
    roots.add(&on_tick_result);
    roots.add(&chosen_action);
    roots.add(&tick_record);
    roots.add(&safety_action);

    apply_common_runtime_options(config, "env.run-loop");

    const auto tick_hz_opt = map_lookup_option(config, "tick_hz");
    const auto max_ticks_opt = map_lookup_option(config, "max_ticks");
    if (!tick_hz_opt.has_value() || !max_ticks_opt.has_value()) {
        throw lisp_error("env.run-loop: required keys are tick_hz and max_ticks");
    }
    const std::int64_t tick_hz = require_int(*tick_hz_opt, "env.run-loop :tick_hz");
    const std::int64_t max_ticks = require_int(*max_ticks_opt, "env.run-loop :max_ticks");
    if (tick_hz <= 0) {
        throw lisp_error("env.run-loop :tick_hz: expected > 0");
    }
    if (max_ticks <= 0) {
        throw lisp_error("env.run-loop :max_ticks: expected > 0");
    }

    std::int64_t episode_max = 1;
    if (const auto episode_max_value = map_lookup_option(config, "episode_max")) {
        episode_max = require_int(*episode_max_value, "env.run-loop :episode_max");
        if (episode_max <= 0) {
            throw lisp_error("env.run-loop :episode_max: expected > 0");
        }
    }

    bool stop_on_success = true;
    if (const auto stop_flag = map_lookup_option(config, "stop_on_success")) {
        stop_on_success = require_bool(*stop_flag, "env.run-loop :stop_on_success");
    }

    bool has_safe_action = false;
    if (const auto candidate = map_lookup_option(config, "safe_action")) {
        safe_action = *candidate;
        has_safe_action = !is_nil(safe_action);
    }

    bool has_success_predicate = false;
    if (const auto candidate = map_lookup_option(config, "success_predicate")) {
        if (!is_callable(*candidate)) {
            throw lisp_error("env.run-loop :success_predicate: expected callable");
        }
        success_predicate = *candidate;
        has_success_predicate = true;
    }

    bool has_observer = false;
    if (const auto candidate = map_lookup_option(config, "observer")) {
        if (!is_callable(*candidate)) {
            throw lisp_error("env.run-loop :observer: expected callable");
        }
        observer_fn = *candidate;
        has_observer = true;
    }

    std::string log_path{};
    if (const auto path_opt = map_lookup_option(config, "log_path")) {
        if (!is_string(*path_opt)) {
            throw lisp_error("env.run-loop :log_path: expected string");
        }
        log_path = string_value(*path_opt);
    } else {
        log_path = runtime_state().log_path;
    }

    bool realtime = runtime_state().realtime;
    if (const auto realtime_opt = map_lookup_option(config, "realtime")) {
        realtime = require_bool(*realtime_opt, "env.run-loop :realtime");
    }

    std::optional<std::string> schema_version{};
    if (const auto schema_opt = map_lookup_option(config, "schema_version")) {
        if (!is_string(*schema_opt)) {
            throw lisp_error("env.run-loop :schema_version: expected string");
        }
        schema_version = string_value(*schema_opt);
    }

    if (const auto steps = map_lookup_option(config, "steps_per_tick")) {
        const std::int64_t parsed = require_int(*steps, "env.run-loop :steps_per_tick");
        if (parsed <= 0) {
            throw lisp_error("env.run-loop :steps_per_tick: expected > 0");
        }
        runtime_state().steps_per_tick = parsed;
    }

    try {
        backend->configure(config);
    } catch (const std::exception& e) {
        throw lisp_error(std::string("env.run-loop: ") + e.what());
    }

    if (backend->supports().reset) {
        std::optional<std::int64_t> seed{};
        if (const auto seed_opt = map_lookup_option(config, "seed")) {
            seed = require_int(*seed_opt, "env.run-loop :seed");
        } else if (runtime_state().seed.has_value()) {
            seed = runtime_state().seed;
        }
        try {
            obs = backend->reset(seed);
        } catch (const std::exception& e) {
            throw lisp_error(std::string("env.run-loop: reset failed: ") + e.what());
        }
        env_runtime_state& state = runtime_state();
        ++state.episode;
        state.step = 0;
        state.time_origin = std::chrono::steady_clock::now();
        state.next_deadline = std::chrono::steady_clock::time_point{};
        enrich_observation(obs);
        final_obs = obs;
    }

    const auto tick_period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / static_cast<double>(tick_hz)));
    std::int64_t episodes_completed = std::min<std::int64_t>(1, episode_max);
    std::int64_t ticks = 0;
    std::int64_t fallback_count = 0;
    std::int64_t overrun_count = 0;
    bool have_last_good_action = false;
    value last_good_action = make_nil();
    roots.add(&last_good_action);

    auto make_result = [&](const std::string& status_symbol, const std::string& reason) {
        value out = make_map();
        gc_root_scope result_roots(default_gc());
        result_roots.add(&out);
        result_roots.add(&final_obs);

        map_set_symbol(out, "status", make_symbol(status_symbol));
        map_set_symbol(out, "episodes", make_integer(episodes_completed));
        map_set_symbol(out, "ticks", make_integer(ticks));
        map_set_symbol(out, "reason", make_string(reason));
        map_set_symbol(out, "final_obs", final_obs);
        map_set_symbol(out, "fallback_count", make_integer(fallback_count));
        map_set_symbol(out, "overrun_count", make_integer(overrun_count));
        return out;
    };

    for (std::int64_t k = 0; k < max_ticks; ++k) {
        bool used_fallback = false;
        bool overrun = false;
        std::optional<std::string> tick_schema = schema_version;
        const auto tick_started = std::chrono::steady_clock::now();

        try {
            obs = backend->observe();
            enrich_observation(obs);
            final_obs = obs;

            on_tick_result = invoke_callable_unary(on_tick_fn, obs, "env.run-loop on_tick");
            if (is_map(on_tick_result)) {
                if (const auto on_tick_schema = map_lookup_option(on_tick_result, "schema_version")) {
                    if (!is_string(*on_tick_schema)) {
                        throw lisp_error("env.run-loop on_tick: schema_version must be string");
                    }
                    tick_schema = string_value(*on_tick_schema);
                }
            }

            const auto action_candidate = extract_action_from_on_tick(on_tick_result);

            // Use a per-tick deadline so long external pauses (e.g. paused simulator UI)
            // do not permanently force fallback actions after resume.
            const auto tick_deadline = tick_started + tick_period;
            if (std::chrono::steady_clock::now() > tick_deadline) {
                overrun = true;
                ++overrun_count;
            }

            if (!overrun && action_candidate.has_value()) {
                chosen_action = *action_candidate;
                last_good_action = chosen_action;
                have_last_good_action = true;
            } else if (have_last_good_action) {
                chosen_action = last_good_action;
                used_fallback = true;
                ++fallback_count;
            } else if (has_safe_action) {
                chosen_action = safe_action;
                used_fallback = true;
                ++fallback_count;
            } else {
                throw lisp_error("env.run-loop: no action returned and safe_action is not configured");
            }

            backend->act(chosen_action);
            const bool can_continue = perform_step_and_pacing(*backend, tick_hz, realtime);
            ++ticks;
            const auto tick_finished = std::chrono::steady_clock::now();
            const double tick_time_ms =
                std::chrono::duration<double, std::milli>(tick_finished - tick_started).count();
            const double tick_budget_ms = 1000.0 / static_cast<double>(tick_hz);

            tick_record = build_tick_record(
                ticks, obs, chosen_action, on_tick_result, tick_schema, tick_budget_ms, tick_time_ms, used_fallback, overrun, {});
            emit_record(observer_fn, has_observer, log_path, tick_record);

            if (!can_continue) {
                return make_result(":stopped", "backend step returned false");
            }

            bool success = false;
            if (has_success_predicate) {
                success = is_truthy(invoke_callable_unary(success_predicate, obs, "env.run-loop success_predicate"));
            } else {
                success = obs_done(obs) || on_tick_result_indicates_success(on_tick_result);
            }

            if (stop_on_success && success) {
                return make_result(":ok", "success predicate satisfied");
            }
        } catch (const std::exception& e) {
            std::string error_reason = e.what();
            safety_action = make_nil();
            if (have_last_good_action) {
                safety_action = last_good_action;
            } else if (has_safe_action) {
                safety_action = safe_action;
            }

            if (!is_nil(safety_action)) {
                try {
                    backend->act(safety_action);
                    (void)perform_step_and_pacing(*backend, tick_hz, realtime);
                } catch (const std::exception&) {
                    // best effort
                }
            }

            try {
                final_obs = backend->observe();
                enrich_observation(final_obs);
            } catch (const std::exception&) {
                final_obs = make_map();
                enrich_observation(final_obs);
            }

            ++ticks;
            const auto tick_finished = std::chrono::steady_clock::now();
            const double tick_time_ms =
                std::chrono::duration<double, std::milli>(tick_finished - tick_started).count();
            const double tick_budget_ms = 1000.0 / static_cast<double>(tick_hz);
            tick_record = build_tick_record(
                ticks, final_obs, safety_action, on_tick_result, tick_schema, tick_budget_ms, tick_time_ms, true, overrun, error_reason);
            emit_record(observer_fn, has_observer, log_path, tick_record);
            return make_result(":error", error_reason);
        }
    }

    return make_result(":stopped", "max ticks reached");
}

}  // namespace

void install_env_capability_builtins(env_ptr global_env) {
    bind_primitive(global_env, "env.info", builtin_env_info);
    bind_primitive(global_env, "env.attach", builtin_env_attach);
    bind_primitive(global_env, "env.configure", builtin_env_configure);
    bind_primitive(global_env, "env.reset", builtin_env_reset);
    bind_primitive(global_env, "env.observe", builtin_env_observe);
    bind_primitive(global_env, "env.act", builtin_env_act);
    bind_primitive(global_env, "env.step", builtin_env_step);
    bind_primitive(global_env, "env.run-loop", builtin_env_run_loop);
    bind_primitive(global_env, "env.debug-draw", builtin_env_debug_draw);

    bind_primitive(global_env, "sim.info", builtin_env_info);
    bind_primitive(global_env, "sim.attach", builtin_env_attach);
    bind_primitive(global_env, "sim.configure", builtin_env_configure);
    bind_primitive(global_env, "sim.reset", builtin_env_reset);
    bind_primitive(global_env, "sim.observe", builtin_env_observe);
    bind_primitive(global_env, "sim.act", builtin_env_act);
    bind_primitive(global_env, "sim.step", builtin_env_step);
    bind_primitive(global_env, "sim.run-loop", builtin_env_run_loop);
    bind_primitive(global_env, "sim.debug-draw", builtin_env_debug_draw);
}

void reset_env_capability_runtime_state() {
    env_runtime_state& state = runtime_state();
    state = env_runtime_state{};
}

}  // namespace muslisp
