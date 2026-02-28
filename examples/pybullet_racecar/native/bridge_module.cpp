#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "racecar_demo.hpp"
#include "bt/runtime_host.hpp"
#include "bt/status.hpp"
#include "muslisp/error.hpp"
#include "muslisp/eval.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"
#include "muslisp/value.hpp"
#include "pybullet_racecar/extension.hpp"

namespace py = pybind11;

namespace {

std::string lisp_string_literal(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 2);
    out.push_back('"');
    for (char c : raw) {
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
    out.push_back('"');
    return out;
}

double require_py_number(const py::handle& value, const std::string& where) {
    if (py::isinstance<py::int_>(value)) {
        return py::cast<double>(value);
    }
    if (py::isinstance<py::float_>(value)) {
        return py::cast<double>(value);
    }
    throw std::runtime_error(where + ": expected numeric value");
}

std::vector<double> require_py_numeric_sequence(const py::handle& value, const std::string& where) {
    if (!py::isinstance<py::sequence>(value) || py::isinstance<py::str>(value)) {
        throw std::runtime_error(where + ": expected numeric sequence");
    }
    py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
    std::vector<double> out;
    out.reserve(seq.size());
    for (py::handle item : seq) {
        out.push_back(require_py_number(item, where));
    }
    return out;
}

std::string require_py_str_key(const py::dict& obj, const char* key, const std::string& where) {
    if (!obj.contains(py::str(key))) {
        throw std::runtime_error(where + ": missing key '" + std::string(key) + "'");
    }
    return py::cast<std::string>(obj[py::str(key)]);
}

double require_py_float_key(const py::dict& obj, const char* key, const std::string& where) {
    if (!obj.contains(py::str(key))) {
        throw std::runtime_error(where + ": missing key '" + std::string(key) + "'");
    }
    return require_py_number(obj[py::str(key)], where + "." + key);
}

std::int64_t require_py_int_key(const py::dict& obj, const char* key, const std::string& where) {
    if (!obj.contains(py::str(key))) {
        throw std::runtime_error(where + ": missing key '" + std::string(key) + "'");
    }
    if (!py::isinstance<py::int_>(obj[py::str(key)])) {
        throw std::runtime_error(where + "." + key + ": expected int");
    }
    return py::cast<std::int64_t>(obj[py::str(key)]);
}

bool require_py_bool_key(const py::dict& obj, const char* key, const std::string& where) {
    if (!obj.contains(py::str(key))) {
        throw std::runtime_error(where + ": missing key '" + std::string(key) + "'");
    }
    if (!py::isinstance<py::bool_>(obj[py::str(key)])) {
        throw std::runtime_error(where + "." + key + ": expected bool");
    }
    return py::cast<bool>(obj[py::str(key)]);
}

std::vector<double> require_py_vec_key(const py::dict& obj, const char* key, const std::string& where) {
    if (!obj.contains(py::str(key))) {
        throw std::runtime_error(where + ": missing key '" + std::string(key) + "'");
    }
    return require_py_numeric_sequence(obj[py::str(key)], where + "." + key);
}

py::object lisp_to_py(const muslisp::value& value) {
    using namespace muslisp;
    if (is_nil(value)) {
        return py::none();
    }
    if (is_boolean(value)) {
        return py::bool_(boolean_value(value));
    }
    if (is_integer(value)) {
        return py::int_(integer_value(value));
    }
    if (is_float(value)) {
        return py::float_(float_value(value));
    }
    if (is_symbol(value)) {
        return py::str(symbol_name(value));
    }
    if (is_string(value)) {
        return py::str(string_value(value));
    }
    if (is_proper_list(value)) {
        py::list out;
        for (muslisp::value item : vector_from_list(value)) {
            out.append(lisp_to_py(item));
        }
        return out;
    }
    if (is_map(value)) {
        py::dict out;
        for (const auto& [key, item] : value->map_data) {
            switch (key.type) {
                case map_key_type::symbol:
                case map_key_type::string:
                    out[py::str(key.text_data)] = lisp_to_py(item);
                    break;
                case map_key_type::integer:
                    out[py::int_(key.integer_data)] = lisp_to_py(item);
                    break;
                case map_key_type::floating:
                    out[py::float_(key.float_data)] = lisp_to_py(item);
                    break;
            }
        }
        return out;
    }
    if (is_bt_def(value) || is_bt_instance(value)) {
        return py::int_(bt_handle(value));
    }
    if (is_image_handle(value)) {
        return py::int_(image_handle_id(value));
    }
    if (is_blob_handle(value)) {
        return py::int_(blob_handle_id(value));
    }
    return py::str(print_value(value));
}

bt::bb_value py_to_bb_value(const py::handle& value, const std::string& where) {
    if (value.is_none()) {
        return bt::bb_value{std::monostate{}};
    }
    if (py::isinstance<py::bool_>(value)) {
        return bt::bb_value{py::cast<bool>(value)};
    }
    if (py::isinstance<py::int_>(value) && !py::isinstance<py::bool_>(value)) {
        return bt::bb_value{py::cast<std::int64_t>(value)};
    }
    if (py::isinstance<py::float_>(value)) {
        return bt::bb_value{py::cast<double>(value)};
    }
    if (py::isinstance<py::str>(value)) {
        return bt::bb_value{py::cast<std::string>(value)};
    }
    if (py::isinstance<py::sequence>(value) && !py::isinstance<py::str>(value)) {
        py::sequence seq = py::reinterpret_borrow<py::sequence>(value);
        std::vector<double> out;
        out.reserve(seq.size());
        for (py::handle item : seq) {
            out.push_back(require_py_number(item, where));
        }
        return bt::bb_value{std::move(out)};
    }
    throw std::runtime_error(where + ": unsupported value type for blackboard input");
}

class python_racecar_sim_adapter final : public bt::racecar_sim_adapter {
public:
    explicit python_racecar_sim_adapter(py::object sim_obj) : sim_obj_(std::move(sim_obj)) {}
    ~python_racecar_sim_adapter() override {
        if (Py_IsInitialized() == 0) {
            return;
        }
        py::gil_scoped_acquire gil;
        sim_obj_ = py::none();
    }

    [[nodiscard]] bt::racecar_state get_state() override {
        py::gil_scoped_acquire gil;
        try {
            py::object out = sim_obj_.attr("get_state")();
            if (!py::isinstance<py::dict>(out)) {
                throw std::runtime_error("sim_adapter.get_state() must return dict");
            }
            py::dict obj = py::reinterpret_borrow<py::dict>(out);
            bt::racecar_state state;
            state.state_schema = require_py_str_key(obj, "state_schema", "sim_adapter.get_state");
            state.state_vec = require_py_vec_key(obj, "state_vec", "sim_adapter.get_state");
            state.x = require_py_float_key(obj, "x", "sim_adapter.get_state");
            state.y = require_py_float_key(obj, "y", "sim_adapter.get_state");
            state.yaw = require_py_float_key(obj, "yaw", "sim_adapter.get_state");
            state.speed = require_py_float_key(obj, "speed", "sim_adapter.get_state");
            state.rays = require_py_vec_key(obj, "rays", "sim_adapter.get_state");
            state.goal = require_py_vec_key(obj, "goal", "sim_adapter.get_state");
            state.collision_imminent = require_py_bool_key(obj, "collision_imminent", "sim_adapter.get_state");
            state.collision_count = require_py_int_key(obj, "collision_count", "sim_adapter.get_state");
            state.t_ms = require_py_int_key(obj, "t_ms", "sim_adapter.get_state");
            return state;
        } catch (const py::error_already_set& e) {
            throw std::runtime_error(std::string("sim_adapter.get_state failed: ") + e.what());
        }
    }

    void apply_action(double steering, double throttle) override {
        py::gil_scoped_acquire gil;
        try {
            sim_obj_.attr("apply_action")(py::make_tuple(steering, throttle));
        } catch (const py::error_already_set& e) {
            throw std::runtime_error(std::string("sim_adapter.apply_action failed: ") + e.what());
        }
    }

    void step(std::int64_t steps) override {
        py::gil_scoped_acquire gil;
        try {
            sim_obj_.attr("step")(steps);
        } catch (const py::error_already_set& e) {
            throw std::runtime_error(std::string("sim_adapter.step failed: ") + e.what());
        }
    }

    void reset() override {
        py::gil_scoped_acquire gil;
        try {
            sim_obj_.attr("reset")();
        } catch (const py::error_already_set& e) {
            throw std::runtime_error(std::string("sim_adapter.reset failed: ") + e.what());
        }
    }

    void debug_draw() override {
        py::gil_scoped_acquire gil;
        try {
            if (py::hasattr(sim_obj_, "debug_draw")) {
                sim_obj_.attr("debug_draw")();
            }
        } catch (const py::error_already_set& e) {
            throw std::runtime_error(std::string("sim_adapter.debug_draw failed: ") + e.what());
        }
    }

    [[nodiscard]] bool stop_requested() const override {
        py::gil_scoped_acquire gil;
        try {
            if (!py::hasattr(sim_obj_, "stop_requested")) {
                return false;
            }
            return py::cast<bool>(sim_obj_.attr("stop_requested")());
        } catch (const py::error_already_set& e) {
            throw std::runtime_error(std::string("sim_adapter.stop_requested failed: ") + e.what());
        }
    }

    void on_tick_record(const bt::racecar_tick_record& record) override {
        py::gil_scoped_acquire gil;
        if (!py::hasattr(sim_obj_, "on_tick_record")) {
            return;
        }
        try {
            py::dict payload;
            payload[py::str("schema_version")] = py::str(record.schema_version);
            payload[py::str("run_id")] = py::str(record.run_id);
            payload[py::str("tick_index")] = py::int_(record.tick_index);
            payload[py::str("sim_time_s")] = py::float_(record.sim_time_s);
            payload[py::str("wall_time_s")] = py::float_(record.wall_time_s);
            payload[py::str("mode")] = py::str(record.mode);

            py::dict state_obj;
            state_obj[py::str("x")] = py::float_(record.state.x);
            state_obj[py::str("y")] = py::float_(record.state.y);
            state_obj[py::str("yaw")] = py::float_(record.state.yaw);
            state_obj[py::str("speed")] = py::float_(record.state.speed);
            payload[py::str("state")] = state_obj;

            py::dict goal_obj;
            goal_obj[py::str("x")] = py::float_(record.state.goal.size() > 0 ? record.state.goal[0] : 0.0);
            goal_obj[py::str("y")] = py::float_(record.state.goal.size() > 1 ? record.state.goal[1] : 0.0);
            payload[py::str("goal")] = goal_obj;

            payload[py::str("distance_to_goal")] = py::float_(record.distance_to_goal);
            payload[py::str("collision_imminent")] = py::bool_(record.state.collision_imminent);

            py::dict action_obj;
            action_obj[py::str("steering")] = py::float_(record.steering);
            action_obj[py::str("throttle")] = py::float_(record.throttle);
            payload[py::str("action")] = action_obj;

            payload[py::str("collisions_total")] = py::int_(record.collisions_total);
            payload[py::str("goal_reached")] = py::bool_(record.goal_reached);
            payload[py::str("bt_status")] = py::str(record.bt_status);
            if (!record.planner_meta_json.empty()) {
                payload[py::str("planner_meta_json")] = py::str(record.planner_meta_json);
            }
            payload[py::str("used_fallback")] = py::bool_(record.used_fallback);
            payload[py::str("is_error_record")] = py::bool_(record.is_error_record);
            if (!record.error_reason.empty()) {
                payload[py::str("error_reason")] = py::str(record.error_reason);
            }
            sim_obj_.attr("on_tick_record")(payload);
        } catch (const py::error_already_set& e) {
            throw std::runtime_error(std::string("sim_adapter.on_tick_record failed: ") + e.what());
        }
    }

private:
    py::object sim_obj_;
};

class runtime_bridge {
public:
    runtime_bridge() {
        muslisp::runtime_config config;
        config.extension_register_hook = muslisp::ext::pybullet_racecar::register_extension;
        env_ = muslisp::create_global_env(config);
    }
    ~runtime_bridge() {
        bt::clear_racecar_demo_state();
        sim_adapter_.reset();
    }

    void reset() {
        bt::runtime_host& host = bt::default_runtime_host();
        host.clear_all();
        bt::install_demo_callbacks(host);
        bt::clear_racecar_demo_state();
        sim_adapter_.reset();
        ++session_generation_;
    }

    void install_racecar_demo_extensions(py::object sim_adapter) {
        if (sim_adapter.is_none()) {
            throw std::runtime_error("Runtime.install_racecar_demo_extensions: sim_adapter is required");
        }
        sim_adapter_ = std::make_shared<python_racecar_sim_adapter>(std::move(sim_adapter));
        bt::set_racecar_sim_adapter(sim_adapter_);
        bt::install_racecar_demo_callbacks(bt::default_runtime_host());
    }

    std::int64_t load_bt_dsl(const std::string& path) {
        const std::string expr = "(bt.load-dsl " + lisp_string_literal(path) + ")";
        muslisp::value out = muslisp::eval_source(expr, env_);
        if (!muslisp::is_bt_def(out)) {
            throw std::runtime_error("Runtime.load_bt_dsl: bt.load-dsl did not return bt_def");
        }
        return muslisp::bt_handle(out);
    }

    std::int64_t compile_bt(const std::string& dsl_source) {
        const std::string expr = "(bt.compile (quote " + dsl_source + "))";
        muslisp::value out = muslisp::eval_source(expr, env_);
        if (!muslisp::is_bt_def(out)) {
            throw std::runtime_error("Runtime.compile_bt: bt.compile did not return bt_def");
        }
        return muslisp::bt_handle(out);
    }

    std::int64_t new_instance(std::int64_t definition_handle) {
        try {
            return bt::default_runtime_host().create_instance(definition_handle);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Runtime.new_instance failed: ") + e.what());
        }
    }

    std::string tick(std::int64_t instance_handle, py::object bb_inputs = py::none()) {
        bt::runtime_host& host = bt::default_runtime_host();
        bt::instance* inst = host.find_instance(instance_handle);
        if (!inst) {
            throw std::runtime_error("Runtime.tick: unknown instance handle");
        }

        if (!bb_inputs.is_none()) {
            if (!py::isinstance<py::dict>(bb_inputs)) {
                throw std::runtime_error("Runtime.tick: bb_inputs must be dict[str, value]");
            }
            py::dict inputs = py::reinterpret_borrow<py::dict>(bb_inputs);
            const auto now = std::chrono::steady_clock::now();
            const std::uint64_t write_tick = inst->tick_index + 1;
            for (const auto& kv : inputs) {
                if (!py::isinstance<py::str>(kv.first)) {
                    throw std::runtime_error("Runtime.tick: bb_inputs keys must be str");
                }
                const std::string key = py::cast<std::string>(kv.first);
                bt::bb_value bb_val = py_to_bb_value(kv.second, "Runtime.tick bb_inputs." + key);
                inst->bb.put(key, std::move(bb_val), write_tick, now, 0, "Runtime.tick");
            }
        }

        try {
            const bt::status st = host.tick_instance(instance_handle);
            return std::string(bt::status_name(st));
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Runtime.tick failed: ") + e.what());
        }
    }

    py::dict run_loop(std::int64_t instance_handle, py::dict options_dict) {
        bt::racecar_loop_options options;
        bool has_tick_hz = false;
        bool has_max_ticks = false;
        bool has_state_key = false;
        bool has_action_key = false;

        for (const auto& kv : options_dict) {
            if (!py::isinstance<py::str>(kv.first)) {
                throw std::runtime_error("Runtime.run_loop: options keys must be str");
            }
            const std::string key = py::cast<std::string>(kv.first);
            const py::handle val = kv.second;
            if (key == "tick_hz") {
                options.tick_hz = require_py_number(val, "Runtime.run_loop.tick_hz");
                has_tick_hz = true;
            } else if (key == "max_ticks") {
                if (!py::isinstance<py::int_>(val)) {
                    throw std::runtime_error("Runtime.run_loop.max_ticks: expected int");
                }
                options.max_ticks = py::cast<std::int64_t>(val);
                has_max_ticks = true;
            } else if (key == "state_key") {
                if (!py::isinstance<py::str>(val)) {
                    throw std::runtime_error("Runtime.run_loop.state_key: expected str");
                }
                options.state_key = py::cast<std::string>(val);
                has_state_key = true;
            } else if (key == "action_key") {
                if (!py::isinstance<py::str>(val)) {
                    throw std::runtime_error("Runtime.run_loop.action_key: expected str");
                }
                options.action_key = py::cast<std::string>(val);
                has_action_key = true;
            } else if (key == "steps_per_tick") {
                if (!py::isinstance<py::int_>(val)) {
                    throw std::runtime_error("Runtime.run_loop.steps_per_tick: expected int");
                }
                options.steps_per_tick = py::cast<std::int64_t>(val);
            } else if (key == "safe_action") {
                const std::vector<double> action = require_py_numeric_sequence(val, "Runtime.run_loop.safe_action");
                if (action.size() < 2) {
                    throw std::runtime_error("Runtime.run_loop.safe_action: expected [steering throttle]");
                }
                options.safe_action = {action[0], action[1]};
            } else if (key == "draw_debug") {
                if (!py::isinstance<py::bool_>(val)) {
                    throw std::runtime_error("Runtime.run_loop.draw_debug: expected bool");
                }
                options.draw_debug = py::cast<bool>(val);
            } else if (key == "mode") {
                if (!py::isinstance<py::str>(val)) {
                    throw std::runtime_error("Runtime.run_loop.mode: expected str");
                }
                options.mode = py::cast<std::string>(val);
            } else if (key == "planner_meta_key") {
                if (!py::isinstance<py::str>(val)) {
                    throw std::runtime_error("Runtime.run_loop.planner_meta_key: expected str");
                }
                options.planner_meta_key = py::cast<std::string>(val);
            } else if (key == "run_id") {
                if (!py::isinstance<py::str>(val)) {
                    throw std::runtime_error("Runtime.run_loop.run_id: expected str");
                }
                options.run_id = py::cast<std::string>(val);
            } else if (key == "goal_tolerance") {
                options.goal_tolerance = require_py_number(val, "Runtime.run_loop.goal_tolerance");
            } else {
                throw std::runtime_error("Runtime.run_loop: unknown option: " + key);
            }
        }

        if (!has_tick_hz || !has_max_ticks || !has_state_key || !has_action_key) {
            throw std::runtime_error(
                "Runtime.run_loop: required options are tick_hz, max_ticks, state_key, action_key");
        }

        bt::racecar_loop_result result =
            bt::run_racecar_loop(bt::default_runtime_host(), instance_handle, options);

        py::dict out;
        out[py::str("status")] = py::str(bt::racecar_loop_status_name(result.status));
        out[py::str("ticks")] = py::int_(result.ticks);
        out[py::str("reason")] = py::str(result.reason);
        out[py::str("goal_reached")] = py::bool_(result.goal_reached);
        out[py::str("collisions_total")] = py::int_(result.collisions_total);
        out[py::str("fallback_count")] = py::int_(result.fallback_count);

        py::dict final_state;
        final_state[py::str("state_schema")] = py::str(result.final_state.state_schema);
        final_state[py::str("state_vec")] = py::cast(result.final_state.state_vec);
        final_state[py::str("x")] = py::float_(result.final_state.x);
        final_state[py::str("y")] = py::float_(result.final_state.y);
        final_state[py::str("yaw")] = py::float_(result.final_state.yaw);
        final_state[py::str("speed")] = py::float_(result.final_state.speed);
        final_state[py::str("rays")] = py::cast(result.final_state.rays);
        final_state[py::str("goal")] = py::cast(result.final_state.goal);
        final_state[py::str("collision_imminent")] = py::bool_(result.final_state.collision_imminent);
        final_state[py::str("collision_count")] = py::int_(result.final_state.collision_count);
        final_state[py::str("t_ms")] = py::int_(result.final_state.t_ms);
        out[py::str("final_state")] = final_state;
        return out;
    }

    py::object eval(const std::string& source) {
        muslisp::value out = muslisp::eval_source(source, env_);
        return lisp_to_py(out);
    }

    std::uint64_t session_generation() const noexcept {
        return session_generation_;
    }

private:
    muslisp::env_ptr env_;
    std::shared_ptr<python_racecar_sim_adapter> sim_adapter_;
    std::uint64_t session_generation_ = 1;
};

}  // namespace

PYBIND11_MODULE(muesli_bt_bridge, m) {
    m.doc() = "muesli-bt python bridge";

    py::class_<runtime_bridge>(m, "Runtime")
        .def(py::init<>())
        .def("reset", &runtime_bridge::reset)
        .def("install_racecar_demo_extensions", &runtime_bridge::install_racecar_demo_extensions, py::arg("sim_adapter"))
        .def("load_bt_dsl", &runtime_bridge::load_bt_dsl, py::arg("path"))
        .def("compile_bt", &runtime_bridge::compile_bt, py::arg("dsl_source"))
        .def("new_instance", &runtime_bridge::new_instance, py::arg("definition_handle"))
        .def("tick", &runtime_bridge::tick, py::arg("instance_handle"), py::arg("bb_inputs") = py::none())
        .def("run_loop", &runtime_bridge::run_loop, py::arg("instance_handle"), py::arg("options"))
        .def("eval", &runtime_bridge::eval, py::arg("source"))
        .def_property_readonly("session_generation", &runtime_bridge::session_generation);
}
