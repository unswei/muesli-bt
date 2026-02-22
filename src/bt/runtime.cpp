#include "bt/runtime.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "bt/blackboard.hpp"
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
    ev.ts = ctx.now;
    buffer->push(std::move(ev));
}

void emit_log(tick_context& ctx, log_level level, std::string category, std::string message) {
    if (!ctx.svc.obs.logger) {
        return;
    }
    log_record rec;
    rec.ts = ctx.now;
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
        throw std::runtime_error("BT runtime: invalid node id");
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

class tick_scope {
public:
    tick_scope(tick_context& ctx, std::chrono::steady_clock::time_point started_at) : ctx_(ctx), started_at_(started_at) {}

    void set_status(status st) { status_ = st; }

    ~tick_scope() {
        const auto end = std::chrono::steady_clock::now();
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
        : ctx_(ctx), node_(n), prev_node_(ctx.current_node), started_at_(std::chrono::steady_clock::now()) {
        ctx_.current_node = node_.id;
        trace_event ev = make_trace_event(trace_event_kind::node_enter);
        ev.node = node_.id;
        emit_trace(ctx_, std::move(ev));
    }

    void set_status(status st) { status_ = st; }

    ~node_scope() {
        const auto end = std::chrono::steady_clock::now();
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
    inst.bb.put(key, value, tick_index, now, current_node, std::move(writer_name));

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
        throw std::runtime_error("BT tick: instance has no definition");
    }

    ++inst.tick_index;
    tick_context ctx{.inst = inst,
                     .reg = reg,
                     .svc = svc,
                     .tick_index = inst.tick_index,
                     .now = std::chrono::steady_clock::now(),
                     .current_node = inst.def->root};

    trace_event ev = make_trace_event(trace_event_kind::tick_begin);
    ev.node = inst.def->root;
    emit_trace(ctx, std::move(ev));

    tick_scope scope(ctx, ctx.now);
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
            << " node=" << ev.node << " status=" << status_name(ev.node_status);
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
        out << key << "=" << bb_value_repr(entry.value) << " tick=" << entry.last_write_tick
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
