#include "bt/runtime.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <vector>

#include "bt/blackboard.hpp"
#include "bt/planner.hpp"
#include "muslisp/error.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"

namespace bt {
namespace {

trace_event make_trace_event(trace_event_kind kind) {
    trace_event ev{};
    ev.kind = kind;
    return ev;
}

std::int64_t ns_since_epoch(std::chrono::steady_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

std::chrono::steady_clock::time_point tick_now(tick_context& ctx) {
    if (ctx.svc.clock) {
        return ctx.svc.clock->now();
    }
    return std::chrono::steady_clock::now();
}

trace_buffer* resolve_trace_buffer(tick_context& ctx) {
    if (ctx.svc.obs.trace) {
        return ctx.svc.obs.trace;
    }
    return &ctx.inst.trace;
}

void emit_trace(tick_context& ctx, trace_event ev) {
    if (!ctx.inst.trace_enabled) {
        return;
    }

    trace_buffer* buffer = resolve_trace_buffer(ctx);
    if (!buffer) {
        return;
    }

    ev.tick_index = ctx.tick_index;
    ev.ts = tick_now(ctx);
    buffer->push(std::move(ev));
}

void emit_log(tick_context& ctx, log_level level, std::string category, std::string message) {
    if (!ctx.svc.obs.logger) {
        return;
    }
    log_record rec;
    rec.ts = tick_now(ctx);
    rec.level = level;
    rec.tick_index = ctx.tick_index;
    rec.node = ctx.current_node;
    rec.category = std::move(category);
    rec.message = std::move(message);
    ctx.svc.obs.logger->write(rec);
}

node_profile_stats& node_stats_for(instance& inst, const node& n) {
    auto it = inst.node_stats.find(n.id);
    if (it == inst.node_stats.end()) {
        node_profile_stats stats;
        stats.id = n.id;
        stats.name = n.leaf_name.empty() ? std::string("node-") + std::to_string(n.id) : n.leaf_name;
        it = inst.node_stats.emplace(n.id, std::move(stats)).first;
    }
    return it->second;
}

node_memory& node_memory_for(instance& inst, node_id id) {
    return inst.memory[id];
}

const node& get_node(const definition& def, node_id id) {
    if (id >= def.nodes.size()) {
        throw bt_runtime_error("BT runtime: invalid node id");
    }
    return def.nodes[id];
}

muslisp::value materialize_arg(const arg_value& arg) {
    switch (arg.kind) {
        case arg_kind::nil:
            return muslisp::make_nil();
        case arg_kind::boolean:
            return muslisp::make_boolean(arg.bool_v);
        case arg_kind::integer:
            return muslisp::make_integer(arg.int_v);
        case arg_kind::floating:
            return muslisp::make_float(arg.float_v);
        case arg_kind::symbol:
            return muslisp::make_symbol(arg.text);
        case arg_kind::string:
            return muslisp::make_string(arg.text);
    }
    return muslisp::make_nil();
}

std::string normalize_plan_option(std::string key) {
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

std::string arg_as_text(const muslisp::value& v, const std::string& where) {
    if (muslisp::is_symbol(v)) {
        return muslisp::symbol_name(v);
    }
    if (muslisp::is_string(v)) {
        return muslisp::string_value(v);
    }
    throw bt_runtime_error(where + ": expected symbol or string");
}

std::int64_t arg_as_int(const muslisp::value& v, const std::string& where) {
    if (!muslisp::is_integer(v)) {
        throw bt_runtime_error(where + ": expected integer value");
    }
    return muslisp::integer_value(v);
}

double arg_as_number(const muslisp::value& v, const std::string& where) {
    if (muslisp::is_integer(v)) {
        return static_cast<double>(muslisp::integer_value(v));
    }
    if (muslisp::is_float(v)) {
        return muslisp::float_value(v);
    }
    throw bt_runtime_error(where + ": expected numeric value");
}

planner_vector state_from_blackboard(const bb_value& value, const std::string& where) {
    if (const std::int64_t* i = std::get_if<std::int64_t>(&value)) {
        return {static_cast<double>(*i)};
    }
    if (const double* f = std::get_if<double>(&value)) {
        return {*f};
    }
    if (const planner_vector* vec = std::get_if<planner_vector>(&value)) {
        return *vec;
    }
    throw bt_runtime_error(where + ": state must be int, float, or float vector");
}

std::optional<std::uint64_t> seed_from_blackboard(const bb_value& value) {
    if (const std::int64_t* i = std::get_if<std::int64_t>(&value)) {
        if (*i < 0) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(*i);
    }
    if (const double* f = std::get_if<double>(&value)) {
        if (!std::isfinite(*f) || *f < 0.0) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(*f);
    }
    if (const std::string* s = std::get_if<std::string>(&value)) {
        return planner_service::hash64(*s);
    }
    return std::nullopt;
}

bb_value action_to_blackboard(const planner_vector& action) {
    if (action.size() == 1) {
        return bb_value{action[0]};
    }
    return bb_value{action};
}

std::string plan_meta_to_json(const planner_result& result, const planner_request& request) {
    std::ostringstream out;
    out << '{' << "\"status\":\"" << planner_status_name(result.status) << "\","
        << "\"tick_index\":" << request.tick_index << ',' << "\"seed\":" << request.seed << ','
        << "\"budget_ms\":" << request.config.budget_ms << ',' << "\"time_used_ms\":" << result.stats.time_used_ms << ','
        << "\"iters\":" << result.stats.iters << ',' << "\"root_visits\":" << result.stats.root_visits << ','
        << "\"root_children\":" << result.stats.root_children << ',' << "\"widen_added\":" << result.stats.widen_added << ','
        << "\"confidence\":" << result.confidence << ',' << "\"value_est\":" << result.stats.value_est << ','
        << "\"action\":[";
    for (std::size_t i = 0; i < result.action.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << result.action[i];
    }
    out << "]}";
    return out.str();
}

class tick_scope {
public:
    tick_scope(tick_context& ctx, std::chrono::steady_clock::time_point started_at) : ctx_(ctx), started_at_(started_at) {}

    void set_status(status st) { status_ = st; }

    ~tick_scope() {
        const auto end = tick_now(ctx_);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - started_at_);

        ctx_.inst.tree_stats.tick_duration.observe(elapsed, ctx_.inst.tree_stats.configured_tick_budget);
        ++ctx_.inst.tree_stats.tick_count;
        if (ctx_.inst.tree_stats.configured_tick_budget.count() > 0 &&
            elapsed > ctx_.inst.tree_stats.configured_tick_budget) {
            ++ctx_.inst.tree_stats.tick_overrun_count;

            trace_event warning = make_trace_event(trace_event_kind::warning);
            warning.node_status = status_;
            warning.duration = elapsed;
            warning.message = "tick budget overrun";
            emit_trace(ctx_, std::move(warning));
            emit_log(ctx_, log_level::warn, "bt", "tick budget overrun");
        }

        trace_event ev = make_trace_event(trace_event_kind::tick_end);
        ev.node_status = status_;
        ev.duration = elapsed;
        emit_trace(ctx_, std::move(ev));
    }

private:
    tick_context& ctx_;
    std::chrono::steady_clock::time_point started_at_;
    status status_ = status::failure;
};

class node_scope {
public:
    node_scope(tick_context& ctx, const node& n)
        : ctx_(ctx), node_(n), prev_node_(ctx.current_node), started_at_(tick_now(ctx_)) {
        ctx_.current_node = node_.id;
        trace_event ev = make_trace_event(trace_event_kind::node_enter);
        ev.node = node_.id;
        emit_trace(ctx_, std::move(ev));
    }

    void set_status(status st) { status_ = st; }

    ~node_scope() {
        const auto end = tick_now(ctx_);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - started_at_);

        node_profile_stats& stats = node_stats_for(ctx_.inst, node_);
        stats.tick_duration.observe(elapsed);
        switch (status_) {
            case status::success:
                ++stats.success_returns;
                break;
            case status::failure:
                ++stats.failure_returns;
                break;
            case status::running:
                ++stats.running_returns;
                break;
        }

        trace_event ev = make_trace_event(trace_event_kind::node_exit);
        ev.node = node_.id;
        ev.node_status = status_;
        ev.duration = elapsed;
        emit_trace(ctx_, std::move(ev));

        ctx_.current_node = prev_node_;
    }

private:
    tick_context& ctx_;
    const node& node_;
    node_id prev_node_ = 0;
    std::chrono::steady_clock::time_point started_at_{};
    status status_ = status::failure;
};

status execute_plan_action(const node& n, tick_context& ctx, const std::vector<muslisp::value>& args) {
    if (!ctx.svc.planner) {
        trace_event ev = make_trace_event(trace_event_kind::error);
        ev.node = n.id;
        ev.message = "planner service is not available";
        emit_trace(ctx, std::move(ev));
        emit_log(ctx, log_level::error, "planner", "planner service is not available");
        return status::failure;
    }

    planner_request request;
    request.node_name = n.leaf_name.empty() ? ("plan-action-" + std::to_string(n.id)) : n.leaf_name;
    request.tick_index = ctx.tick_index;
    request.run_id = "inst-" + std::to_string(ctx.inst.instance_handle);

    std::string state_key = "state";
    std::string action_key = "action";
    std::string model_service = "toy-1d";
    std::string meta_key;
    std::string seed_key;

    for (std::size_t i = 0; i < args.size(); i += 2) {
        const std::string raw_key = arg_as_text(args[i], "plan-action");
        const std::string key = normalize_plan_option(raw_key);
        const muslisp::value value = args[i + 1];

        if (key == "name") {
            request.node_name = arg_as_text(value, "plan-action :name");
            continue;
        }
        if (key == "budget_ms") {
            request.config.budget_ms = arg_as_int(value, "plan-action :budget_ms");
            continue;
        }
        if (key == "iters_max") {
            request.config.iters_max = arg_as_int(value, "plan-action :iters_max");
            continue;
        }
        if (key == "model_service") {
            model_service = arg_as_text(value, "plan-action :model_service");
            continue;
        }
        if (key == "state_key") {
            state_key = arg_as_text(value, "plan-action :state_key");
            continue;
        }
        if (key == "action_key") {
            action_key = arg_as_text(value, "plan-action :action_key");
            continue;
        }
        if (key == "meta_key") {
            meta_key = arg_as_text(value, "plan-action :meta_key");
            continue;
        }
        if (key == "seed_key") {
            seed_key = arg_as_text(value, "plan-action :seed_key");
            continue;
        }
        if (key == "fallback_action") {
            request.config.fallback_action = {arg_as_number(value, "plan-action :fallback_action")};
            continue;
        }
        if (key == "gamma") {
            request.config.gamma = arg_as_number(value, "plan-action :gamma");
            continue;
        }
        if (key == "max_depth") {
            request.config.max_depth = arg_as_int(value, "plan-action :max_depth");
            continue;
        }
        if (key == "c_ucb") {
            request.config.c_ucb = arg_as_number(value, "plan-action :c_ucb");
            continue;
        }
        if (key == "pw_k") {
            request.config.pw_k = arg_as_number(value, "plan-action :pw_k");
            continue;
        }
        if (key == "pw_alpha") {
            request.config.pw_alpha = arg_as_number(value, "plan-action :pw_alpha");
            continue;
        }
        if (key == "rollout_policy") {
            request.config.rollout_policy = arg_as_text(value, "plan-action :rollout_policy");
            continue;
        }
        if (key == "action_sampler") {
            request.config.action_sampler = arg_as_text(value, "plan-action :action_sampler");
            continue;
        }
        if (key == "top_k") {
            request.config.top_k = arg_as_int(value, "plan-action :top_k");
            continue;
        }

        throw bt_runtime_error("plan-action: unknown option: " + raw_key);
    }

    request.model_service = model_service;
    request.state_key = state_key;

    const bb_entry* state_entry = ctx.bb_get(state_key);
    if (!state_entry) {
        trace_event ev = make_trace_event(trace_event_kind::error);
        ev.node = n.id;
        ev.message = "plan-action: missing state key: " + state_key;
        emit_trace(ctx, std::move(ev));
        emit_log(ctx, log_level::error, "planner", "plan-action: missing state key: " + state_key);
        return status::failure;
    }

    try {
        request.state = state_from_blackboard(state_entry->value, "plan-action");
    } catch (const std::exception& e) {
        trace_event ev = make_trace_event(trace_event_kind::error);
        ev.node = n.id;
        ev.message = e.what();
        emit_trace(ctx, std::move(ev));
        emit_log(ctx, log_level::error, "planner", e.what());
        return status::failure;
    }

    bool has_explicit_seed = false;
    if (!seed_key.empty()) {
        const bb_entry* seed_entry = ctx.bb_get(seed_key);
        if (seed_entry) {
            const std::optional<std::uint64_t> seed = seed_from_blackboard(seed_entry->value);
            if (seed.has_value()) {
                request.seed = *seed;
                has_explicit_seed = true;
            }
        }
    }
    if (!has_explicit_seed) {
        request.seed = ctx.svc.planner->derive_seed(request.node_name, ctx.tick_index);
    }

    planner_result result;
    try {
        result = ctx.svc.planner->plan(request);
    } catch (const std::exception& e) {
        trace_event ev = make_trace_event(trace_event_kind::error);
        ev.node = n.id;
        ev.message = std::string("plan-action: planner threw: ") + e.what();
        emit_trace(ctx, std::move(ev));
        emit_log(ctx, log_level::error, "planner", std::string("plan-action: planner threw: ") + e.what());
        return status::failure;
    }

    if (result.status == planner_status::error || result.action.empty()) {
        std::string message = "plan-action: planner error";
        if (!result.error.empty()) {
            message += ": ";
            message += result.error;
        }
        trace_event ev = make_trace_event(trace_event_kind::error);
        ev.node = n.id;
        ev.message = message;
        emit_trace(ctx, std::move(ev));
        emit_log(ctx, log_level::error, "planner", std::move(message));
        return status::failure;
    }

    for (double v : result.action) {
        if (!std::isfinite(v)) {
            trace_event ev = make_trace_event(trace_event_kind::error);
            ev.node = n.id;
            ev.message = "plan-action: non-finite action value";
            emit_trace(ctx, std::move(ev));
            emit_log(ctx, log_level::error, "planner", "plan-action: non-finite action value");
            return status::failure;
        }
    }

    ctx.bb_put(action_key, action_to_blackboard(result.action), request.node_name);
    if (!meta_key.empty()) {
        ctx.bb_put(meta_key, bb_value{plan_meta_to_json(result, request)}, request.node_name);
    }

    std::ostringstream msg;
    msg << "status=" << planner_status_name(result.status) << " action=";
    for (std::size_t i = 0; i < result.action.size(); ++i) {
        if (i != 0) {
            msg << ',';
        }
        msg << result.action[i];
    }
    msg << " confidence=" << result.confidence << " iters=" << result.stats.iters
        << " time_ms=" << result.stats.time_used_ms;
    emit_log(ctx, log_level::info, "planner", msg.str());

    return status::success;
}

status tick_node(node_id id, tick_context& ctx) {
    const node& n = get_node(*ctx.inst.def, id);
    node_scope scope(ctx, n);

    auto finalize = [&scope](status st) {
        scope.set_status(st);
        return st;
    };

    switch (n.kind) {
        case node_kind::seq: {
            for (node_id child : n.children) {
                const status st = tick_node(child, ctx);
                if (st == status::failure) {
                    return finalize(status::failure);
                }
                if (st == status::running) {
                    return finalize(status::running);
                }
            }
            return finalize(status::success);
        }

        case node_kind::sel: {
            for (node_id child : n.children) {
                const status st = tick_node(child, ctx);
                if (st == status::success) {
                    return finalize(status::success);
                }
                if (st == status::running) {
                    return finalize(status::running);
                }
            }
            return finalize(status::failure);
        }

        case node_kind::invert: {
            const status st = tick_node(n.children[0], ctx);
            if (st == status::success) {
                return finalize(status::failure);
            }
            if (st == status::failure) {
                return finalize(status::success);
            }
            return finalize(status::running);
        }

        case node_kind::repeat: {
            node_memory& mem = node_memory_for(ctx.inst, n.id);
            if (mem.i0 >= n.int_param) {
                return finalize(status::success);
            }

            const status child_st = tick_node(n.children[0], ctx);
            if (child_st == status::failure) {
                return finalize(status::failure);
            }
            if (child_st == status::running) {
                return finalize(status::running);
            }

            ++mem.i0;
            if (mem.i0 >= n.int_param) {
                return finalize(status::success);
            }
            return finalize(status::running);
        }

        case node_kind::retry: {
            node_memory& mem = node_memory_for(ctx.inst, n.id);
            const status child_st = tick_node(n.children[0], ctx);
            if (child_st == status::success) {
                mem.i0 = 0;
                return finalize(status::success);
            }
            if (child_st == status::running) {
                return finalize(status::running);
            }

            ++mem.i0;
            if (mem.i0 <= n.int_param) {
                return finalize(status::running);
            }
            return finalize(status::failure);
        }

        case node_kind::cond: {
            const condition_fn* fn = ctx.reg.find_condition(n.leaf_name);
            if (!fn) {
                trace_event ev = make_trace_event(trace_event_kind::error);
                ev.node = n.id;
                ev.message = "missing condition callback: " + n.leaf_name;
                emit_trace(ctx, std::move(ev));
                emit_log(ctx, log_level::error, "bt", "missing condition callback: " + n.leaf_name);
                return finalize(status::failure);
            }

            std::vector<muslisp::value> args;
            args.reserve(n.args.size());
            muslisp::gc_root_scope roots(muslisp::default_gc());
            for (const arg_value& compiled : n.args) {
                args.push_back(materialize_arg(compiled));
                roots.add(&args.back());
            }

            try {
                const bool out = (*fn)(ctx, std::span<const muslisp::value>(args));
                return finalize(out ? status::success : status::failure);
            } catch (const std::exception& e) {
                trace_event ev = make_trace_event(trace_event_kind::error);
                ev.node = n.id;
                ev.message = std::string("condition threw: ") + e.what();
                emit_trace(ctx, std::move(ev));
                emit_log(ctx, log_level::error, "bt", std::string("condition threw: ") + e.what());
                return finalize(status::failure);
            }
        }

        case node_kind::act: {
            const action_fn* fn = ctx.reg.find_action(n.leaf_name);
            if (!fn) {
                trace_event ev = make_trace_event(trace_event_kind::error);
                ev.node = n.id;
                ev.message = "missing action callback: " + n.leaf_name;
                emit_trace(ctx, std::move(ev));
                emit_log(ctx, log_level::error, "bt", "missing action callback: " + n.leaf_name);
                return finalize(status::failure);
            }

            node_memory& mem = node_memory_for(ctx.inst, n.id);
            std::vector<muslisp::value> args;
            args.reserve(n.args.size());
            muslisp::gc_root_scope roots(muslisp::default_gc());
            for (const arg_value& compiled : n.args) {
                args.push_back(materialize_arg(compiled));
                roots.add(&args.back());
            }

            try {
                return finalize((*fn)(ctx, n.id, mem, std::span<const muslisp::value>(args)));
            } catch (const std::exception& e) {
                trace_event ev = make_trace_event(trace_event_kind::error);
                ev.node = n.id;
                ev.message = std::string("action threw: ") + e.what();
                emit_trace(ctx, std::move(ev));
                emit_log(ctx, log_level::error, "bt", std::string("action threw: ") + e.what());
                return finalize(status::failure);
            }
        }

        case node_kind::plan_action: {
            std::vector<muslisp::value> args;
            args.reserve(n.args.size());
            muslisp::gc_root_scope roots(muslisp::default_gc());
            for (const arg_value& compiled : n.args) {
                args.push_back(materialize_arg(compiled));
                roots.add(&args.back());
            }
            try {
                return finalize(execute_plan_action(n, ctx, args));
            } catch (const std::exception& e) {
                trace_event ev = make_trace_event(trace_event_kind::error);
                ev.node = n.id;
                ev.message = std::string("plan-action failed: ") + e.what();
                emit_trace(ctx, std::move(ev));
                emit_log(ctx, log_level::error, "planner", std::string("plan-action failed: ") + e.what());
                return finalize(status::failure);
            }
        }

        case node_kind::succeed:
            return finalize(status::success);
        case node_kind::fail:
            return finalize(status::failure);
        case node_kind::running:
            return finalize(status::running);
    }

    return finalize(status::failure);
}

}  // namespace

void tick_context::bb_put(std::string key, bb_value value, std::string writer_name) {
    const auto ts = tick_now(*this);
    inst.bb.put(key, value, tick_index, ts, current_node, std::move(writer_name));

    trace_event ev = make_trace_event(trace_event_kind::bb_write);
    ev.node = current_node;
    ev.key = key;
    ev.value_repr = bb_value_repr(value);
    emit_trace(*this, std::move(ev));
}

const bb_entry* tick_context::bb_get(std::string_view key) {
    const bb_entry* out = inst.bb.get(key);
    if (inst.read_trace_enabled) {
        trace_event ev = make_trace_event(trace_event_kind::bb_read);
        ev.node = current_node;
        ev.key = std::string(key);
        ev.value_repr = out ? bb_value_repr(out->value) : "<missing>";
        emit_trace(*this, std::move(ev));
    }
    return out;
}

void tick_context::scheduler_event(trace_event_kind kind, job_id job, job_status st, std::string message) {
    std::string log_message = message;
    trace_event ev = make_trace_event(kind);
    ev.node = current_node;
    ev.job = job;
    ev.job_st = st;
    ev.message = std::move(message);
    emit_trace(*this, std::move(ev));

    if (kind == trace_event_kind::warning || kind == trace_event_kind::error) {
        emit_log(*this, kind == trace_event_kind::error ? log_level::error : log_level::warn, "scheduler", std::move(log_message));
        return;
    }
    emit_log(*this, log_level::debug, "scheduler", std::move(log_message));
}

status tick(instance& inst, registry& reg, services& svc) {
    if (!inst.def) {
        throw bt_runtime_error("BT tick: instance has no definition");
    }

    ++inst.tick_index;
    const auto tick_start = svc.clock ? svc.clock->now() : std::chrono::steady_clock::now();
    tick_context ctx{.inst = inst,
                     .reg = reg,
                     .svc = svc,
                     .tick_index = inst.tick_index,
                     .now = tick_start,
                     .current_node = inst.def->root};

    trace_event ev = make_trace_event(trace_event_kind::tick_begin);
    ev.node = inst.def->root;
    emit_trace(ctx, std::move(ev));

    tick_scope scope(ctx, tick_start);
    const status result = tick_node(inst.def->root, ctx);
    scope.set_status(result);
    return result;
}

void reset(instance& inst) {
    inst.memory.clear();
    inst.bb.clear();
}

std::string dump_stats(const instance& inst) {
    std::ostringstream out;

    out << "tick_count=" << inst.tree_stats.tick_count << '\n';
    out << "tick_overrun_count=" << inst.tree_stats.tick_overrun_count << '\n';
    out << "tick_last_ns=" << inst.tree_stats.tick_duration.last.count() << '\n';
    out << "tick_max_ns=" << inst.tree_stats.tick_duration.max.count() << '\n';
    out << "tick_total_ns=" << inst.tree_stats.tick_duration.total.count() << '\n';

    std::vector<node_id> ids;
    ids.reserve(inst.node_stats.size());
    for (const auto& [id, _] : inst.node_stats) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());

    for (node_id id : ids) {
        const auto it = inst.node_stats.find(id);
        if (it == inst.node_stats.end()) {
            continue;
        }
        const node_profile_stats& n = it->second;
        out << "node " << n.id << " (" << n.name << ")"
            << " success=" << n.success_returns << " failure=" << n.failure_returns
            << " running=" << n.running_returns << " last_ns=" << n.tick_duration.last.count()
            << " max_ns=" << n.tick_duration.max.count() << '\n';
    }

    return out.str();
}

std::string dump_trace(const instance& inst) {
    std::ostringstream out;
    for (const trace_event& ev : inst.trace.snapshot()) {
        out << ev.sequence << " kind=" << trace_event_kind_name(ev.kind) << " tick=" << ev.tick_index
            << " node=" << ev.node << " ts_ns=" << ns_since_epoch(ev.ts);
        if (ev.kind == trace_event_kind::node_exit || ev.kind == trace_event_kind::tick_end) {
            out << " status=" << status_name(ev.node_status);
        }
        if (ev.duration.count() > 0) {
            out << " duration_ns=" << ev.duration.count();
        }
        if (ev.job != 0 || ev.job_st != job_status::unknown) {
            out << " job=" << ev.job << " job_status=" << job_status_name(ev.job_st);
        }
        if (!ev.key.empty()) {
            out << " key=" << ev.key;
        }
        if (!ev.value_repr.empty()) {
            out << " value=" << ev.value_repr;
        }
        if (!ev.message.empty()) {
            out << " msg=" << ev.message;
        }
        out << '\n';
    }
    return out.str();
}

std::string dump_blackboard(const instance& inst) {
    std::ostringstream out;

    const auto entries = inst.bb.snapshot();
    for (const auto& [key, entry] : entries) {
        out << key << "=" << bb_value_repr(entry.value) << " type=" << bb_value_type_name(entry.value)
            << " tick=" << entry.last_write_tick << " ts_ns=" << ns_since_epoch(entry.last_write_ts)
            << " writer_node=" << entry.last_writer_node_id;
        if (!entry.last_writer_name.empty()) {
            out << " writer_name=" << entry.last_writer_name;
        }
        out << '\n';
    }

    return out.str();
}

void set_tick_budget_ms(instance& inst, std::int64_t budget_ms) {
    if (budget_ms < 0) {
        throw std::invalid_argument("tick budget must be non-negative");
    }
    inst.tree_stats.configured_tick_budget = std::chrono::milliseconds(budget_ms);
}

}  // namespace bt
