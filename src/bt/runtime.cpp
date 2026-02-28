#include "bt/runtime.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <vector>

#include "bt/blackboard.hpp"
#include "bt/planner.hpp"
#include "bt/vla.hpp"
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

std::string json_escape(std::string_view text);

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

bb_value action_to_blackboard(const planner_action& action) {
    if (action.u.size() == 1) {
        return bb_value{action.u[0]};
    }
    return bb_value{action.u};
}

std::string plan_meta_to_json(const planner_result& result, const planner_request& request) {
    std::ostringstream out;
    out << '{' << "\"schema_version\":\"planner.v1\","
        << "\"planner\":\"" << planner_backend_name(result.planner) << "\","
        << "\"status\":\"" << planner_status_name(result.status) << "\","
        << "\"tick_index\":" << request.tick_index << ',' << "\"seed\":" << request.seed << ','
        << "\"budget_ms\":" << result.stats.budget_ms << ',' << "\"time_used_ms\":" << result.stats.time_used_ms << ','
        << "\"work_done\":" << result.stats.work_done << ',' << "\"confidence\":" << result.confidence << ','
        << "\"action\":{\"action_schema\":\"" << json_escape(result.action.action_schema) << "\",\"u\":[";
    for (std::size_t i = 0; i < result.action.u.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << result.action.u[i];
    }
    out << "]}";

    if (result.planner == planner_backend::mcts && result.trace.mcts.available) {
        out << ",\"trace\":{\"root_visits\":" << result.trace.mcts.root_visits
            << ",\"root_children\":" << result.trace.mcts.root_children
            << ",\"widen_added\":" << result.trace.mcts.widen_added;
        if (!result.trace.mcts.top_k.empty()) {
            out << ",\"top_k\":[";
            for (std::size_t i = 0; i < result.trace.mcts.top_k.size(); ++i) {
                if (i != 0) {
                    out << ',';
                }
                const planner_top_choice_mcts& top = result.trace.mcts.top_k[i];
                out << "{\"action\":{\"action_schema\":\"" << json_escape(top.action.action_schema) << "\",\"u\":[";
                for (std::size_t j = 0; j < top.action.u.size(); ++j) {
                    if (j != 0) {
                        out << ',';
                    }
                    out << top.action.u[j];
                }
                out << "]},\"visits\":" << top.visits << ",\"q\":" << top.q << '}';
            }
            out << ']';
        }
        out << '}';
    } else if (result.planner == planner_backend::mppi && result.trace.mppi.available) {
        out << ",\"trace\":{\"n_samples\":" << result.trace.mppi.n_samples
            << ",\"horizon\":" << result.trace.mppi.horizon;
        if (!result.trace.mppi.top_k.empty()) {
            out << ",\"top_k\":[";
            for (std::size_t i = 0; i < result.trace.mppi.top_k.size(); ++i) {
                if (i != 0) {
                    out << ',';
                }
                const planner_top_choice_mppi& top = result.trace.mppi.top_k[i];
                out << "{\"action\":{\"action_schema\":\"" << json_escape(top.action.action_schema) << "\",\"u\":[";
                for (std::size_t j = 0; j < top.action.u.size(); ++j) {
                    if (j != 0) {
                        out << ',';
                    }
                    out << top.action.u[j];
                }
                out << "]},\"weight\":" << top.weight << ",\"cost\":" << top.cost << '}';
            }
            out << ']';
        }
        out << '}';
    } else if (result.planner == planner_backend::ilqr && result.trace.ilqr.available) {
        out << ",\"trace\":{\"iters\":" << result.trace.ilqr.iters << ",\"cost_init\":" << result.trace.ilqr.cost_init
            << ",\"cost_final\":" << result.trace.ilqr.cost_final << ",\"reg_final\":" << result.trace.ilqr.reg_final << '}';
    }

    if (result.stats.overrun) {
        out << ",\"overrun\":true";
    }
    if (!result.stats.note.empty()) {
        out << ",\"note\":\"" << json_escape(result.stats.note) << "\"";
    }
    if (!result.error.empty()) {
        out << ",\"error\":\"" << json_escape(result.error) << "\"";
    }
    out << '}';
    return out.str();
}

double clamp_confidence(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::clamp(value, 0.0, 1.0);
}

std::int64_t ms_since_epoch(std::chrono::steady_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
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
    return out;
}

std::string vla_action_to_json(const vla_action& action) {
    std::ostringstream out;
    out << "{\"type\":\"" << vla_action_type_name(action.type) << "\"";
    if (action.type == vla_action_type::continuous) {
        out << ",\"u\":[";
        for (std::size_t i = 0; i < action.u.size(); ++i) {
            if (i != 0) {
                out << ',';
            }
            out << action.u[i];
        }
        out << ']';
    } else if (action.type == vla_action_type::discrete) {
        out << ",\"id\":\"" << json_escape(action.discrete_id) << "\"";
    } else {
        out << ",\"steps\":[";
        for (std::size_t i = 0; i < action.steps.size(); ++i) {
            if (i != 0) {
                out << ',';
            }
            out << vla_action_to_json(action.steps[i]);
        }
        out << ']';
    }
    out << '}';
    return out.str();
}

std::string vla_poll_meta_json(const vla_poll& poll) {
    std::ostringstream out;
    out << '{' << "\"status\":\"" << vla_job_status_name(poll.status) << "\"";
    if (poll.partial.has_value()) {
        out << ",\"partial\":{\"sequence\":" << poll.partial->sequence << ",\"confidence\":" << poll.partial->confidence;
        if (!poll.partial->text_chunk.empty()) {
            out << ",\"text_chunk\":\"" << json_escape(poll.partial->text_chunk) << "\"";
        }
        if (poll.partial->action_candidate.has_value()) {
            out << ",\"action_candidate\":" << vla_action_to_json(*poll.partial->action_candidate);
        }
        out << '}';
    }
    if (poll.final.has_value()) {
        out << ",\"final\":{\"status\":\"" << vla_status_name(poll.final->status) << "\",\"confidence\":" << poll.final->confidence
            << ",\"model\":{\"name\":\"" << json_escape(poll.final->model.name) << "\",\"version\":\""
            << json_escape(poll.final->model.version) << "\"}";
        if (!poll.final->explanation.empty()) {
            out << ",\"explanation\":\"" << json_escape(poll.final->explanation) << "\"";
        }
        out << ",\"action\":" << vla_action_to_json(poll.final->action) << '}';
    }
    out << '}';
    return out.str();
}

bool parse_bool_arg(const muslisp::value& v, const std::string& where) {
    if (!muslisp::is_boolean(v)) {
        throw bt_runtime_error(where + ": expected boolean");
    }
    return muslisp::boolean_value(v);
}

std::optional<std::uint64_t> seed_from_lisp_arg(const muslisp::value& v) {
    if (muslisp::is_integer(v)) {
        const std::int64_t raw = muslisp::integer_value(v);
        if (raw < 0) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(raw);
    }
    if (muslisp::is_float(v)) {
        const double raw = muslisp::float_value(v);
        if (!std::isfinite(raw) || raw < 0.0) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(raw);
    }
    if (muslisp::is_symbol(v)) {
        return vla_service::hash64(muslisp::symbol_name(v));
    }
    if (muslisp::is_string(v)) {
        return vla_service::hash64(muslisp::string_value(v));
    }
    return std::nullopt;
}

std::optional<image_handle_ref> image_from_blackboard(const bb_value& value) {
    if (const auto* image = std::get_if<image_handle_ref>(&value)) {
        return *image;
    }
    return std::nullopt;
}

std::optional<blob_handle_ref> blob_from_blackboard(const bb_value& value) {
    if (const auto* blob = std::get_if<blob_handle_ref>(&value)) {
        return *blob;
    }
    return std::nullopt;
}

bool action_is_finite(const vla_action& action) {
    if (action.type == vla_action_type::continuous) {
        for (double u : action.u) {
            if (!std::isfinite(u)) {
                return false;
            }
        }
        return true;
    }
    if (action.type == vla_action_type::sequence) {
        for (const vla_action& step : action.steps) {
            if (!action_is_finite(step)) {
                return false;
            }
        }
    }
    return true;
}

bb_value action_to_blackboard(const vla_action& action) {
    if (action.type == vla_action_type::continuous) {
        if (action.u.size() == 1) {
            return bb_value{action.u[0]};
        }
        return bb_value{action.u};
    }
    if (action.type == vla_action_type::discrete) {
        return bb_value{action.discrete_id};
    }
    return bb_value{vla_action_to_json(action)};
}

struct vla_request_options {
    std::string node_name;
    std::string job_key;
    std::string instruction;
    std::string instruction_key = "instruction";
    std::string task_id = "task";
    std::string task_id_key;
    std::string state_key = "state";
    std::string image_key;
    std::string blob_key;
    std::string capability = "vla.rt2";
    std::string model_name = "rt2-stub";
    std::string model_version = "stub-1";
    std::string frame_id = "base";
    std::int64_t deadline_ms = 20;
    std::int64_t dims = 0;
    double bound_lo = -1.0;
    double bound_hi = 1.0;
    double max_abs = 1.0;
    double max_delta = 1.0;
    std::optional<std::pair<double, double>> forbidden_range{};
    std::string seed_key;
    std::optional<std::uint64_t> fixed_seed{};
};

struct vla_wait_options {
    std::string node_name;
    std::string job_key;
    std::string action_key = "action";
    std::string meta_key;
    double early_confidence = 1.1;
    bool early_commit = false;
    bool cancel_on_early_commit = true;
    bool clear_job = true;
};

struct vla_cancel_options {
    std::string node_name;
    std::string job_key;
};

vla_request_options parse_vla_request_options(const node& n, const std::vector<muslisp::value>& args) {
    vla_request_options opts;
    opts.node_name = n.leaf_name.empty() ? ("vla-request-" + std::to_string(n.id)) : n.leaf_name;
    opts.job_key = opts.node_name + ".job_id";

    for (std::size_t i = 0; i < args.size(); i += 2) {
        const std::string raw_key = arg_as_text(args[i], "vla-request");
        const std::string key = normalize_plan_option(raw_key);
        const muslisp::value value = args[i + 1];

        if (key == "name") {
            opts.node_name = arg_as_text(value, "vla-request :name");
            opts.job_key = opts.node_name + ".job_id";
        } else if (key == "job_key") {
            opts.job_key = arg_as_text(value, "vla-request :job_key");
        } else if (key == "instruction") {
            opts.instruction = arg_as_text(value, "vla-request :instruction");
        } else if (key == "instruction_key") {
            opts.instruction_key = arg_as_text(value, "vla-request :instruction_key");
        } else if (key == "task_id") {
            opts.task_id = arg_as_text(value, "vla-request :task_id");
        } else if (key == "task_key") {
            opts.task_id_key = arg_as_text(value, "vla-request :task_key");
        } else if (key == "state_key") {
            opts.state_key = arg_as_text(value, "vla-request :state_key");
        } else if (key == "image_key") {
            opts.image_key = arg_as_text(value, "vla-request :image_key");
        } else if (key == "blob_key") {
            opts.blob_key = arg_as_text(value, "vla-request :blob_key");
        } else if (key == "capability") {
            opts.capability = arg_as_text(value, "vla-request :capability");
        } else if (key == "model_name") {
            opts.model_name = arg_as_text(value, "vla-request :model_name");
        } else if (key == "model_version") {
            opts.model_version = arg_as_text(value, "vla-request :model_version");
        } else if (key == "frame_id") {
            opts.frame_id = arg_as_text(value, "vla-request :frame_id");
        } else if (key == "deadline_ms" || key == "budget_ms") {
            opts.deadline_ms = arg_as_int(value, "vla-request :deadline_ms");
        } else if (key == "dims") {
            opts.dims = arg_as_int(value, "vla-request :dims");
        } else if (key == "bound_lo") {
            opts.bound_lo = arg_as_number(value, "vla-request :bound_lo");
        } else if (key == "bound_hi") {
            opts.bound_hi = arg_as_number(value, "vla-request :bound_hi");
        } else if (key == "max_abs") {
            opts.max_abs = arg_as_number(value, "vla-request :max_abs");
        } else if (key == "max_delta") {
            opts.max_delta = arg_as_number(value, "vla-request :max_delta");
        } else if (key == "forbidden_lo") {
            const double lo = arg_as_number(value, "vla-request :forbidden_lo");
            if (!opts.forbidden_range.has_value()) {
                opts.forbidden_range = std::make_pair(lo, lo);
            } else {
                opts.forbidden_range->first = lo;
            }
        } else if (key == "forbidden_hi") {
            const double hi = arg_as_number(value, "vla-request :forbidden_hi");
            if (!opts.forbidden_range.has_value()) {
                opts.forbidden_range = std::make_pair(hi, hi);
            } else {
                opts.forbidden_range->second = hi;
            }
        } else if (key == "seed_key") {
            opts.seed_key = arg_as_text(value, "vla-request :seed_key");
        } else if (key == "seed") {
            const std::optional<std::uint64_t> seed = seed_from_lisp_arg(value);
            if (!seed.has_value()) {
                throw bt_runtime_error("vla-request :seed: expected non-negative numeric/string/symbol");
            }
            opts.fixed_seed = seed;
        } else {
            throw bt_runtime_error("vla-request: unknown option: " + raw_key);
        }
    }

    return opts;
}

vla_wait_options parse_vla_wait_options(const node& n, const std::vector<muslisp::value>& args) {
    vla_wait_options opts;
    opts.node_name = n.leaf_name.empty() ? ("vla-request-" + std::to_string(n.id)) : n.leaf_name;
    opts.job_key = opts.node_name + ".job_id";

    for (std::size_t i = 0; i < args.size(); i += 2) {
        const std::string raw_key = arg_as_text(args[i], "vla-wait");
        const std::string key = normalize_plan_option(raw_key);
        const muslisp::value value = args[i + 1];

        if (key == "name") {
            opts.node_name = arg_as_text(value, "vla-wait :name");
            opts.job_key = opts.node_name + ".job_id";
        } else if (key == "job_key") {
            opts.job_key = arg_as_text(value, "vla-wait :job_key");
        } else if (key == "action_key") {
            opts.action_key = arg_as_text(value, "vla-wait :action_key");
        } else if (key == "meta_key") {
            opts.meta_key = arg_as_text(value, "vla-wait :meta_key");
        } else if (key == "early_confidence") {
            opts.early_confidence = arg_as_number(value, "vla-wait :early_confidence");
        } else if (key == "early_commit") {
            opts.early_commit = parse_bool_arg(value, "vla-wait :early_commit");
        } else if (key == "cancel_on_early_commit") {
            opts.cancel_on_early_commit = parse_bool_arg(value, "vla-wait :cancel_on_early_commit");
        } else if (key == "clear_job") {
            opts.clear_job = parse_bool_arg(value, "vla-wait :clear_job");
        } else {
            throw bt_runtime_error("vla-wait: unknown option: " + raw_key);
        }
    }
    return opts;
}

vla_cancel_options parse_vla_cancel_options(const node& n, const std::vector<muslisp::value>& args) {
    vla_cancel_options opts;
    opts.node_name = n.leaf_name.empty() ? ("vla-request-" + std::to_string(n.id)) : n.leaf_name;
    opts.job_key = opts.node_name + ".job_id";

    for (std::size_t i = 0; i < args.size(); i += 2) {
        const std::string raw_key = arg_as_text(args[i], "vla-cancel");
        const std::string key = normalize_plan_option(raw_key);
        const muslisp::value value = args[i + 1];

        if (key == "name") {
            opts.node_name = arg_as_text(value, "vla-cancel :name");
            opts.job_key = opts.node_name + ".job_id";
        } else if (key == "job_key") {
            opts.job_key = arg_as_text(value, "vla-cancel :job_key");
        } else {
            throw bt_runtime_error("vla-cancel: unknown option: " + raw_key);
        }
    }
    return opts;
}

void clear_job_key_if_present(tick_context& ctx, const std::string& key, const std::string& writer_name) {
    const bb_entry* existing = ctx.bb_get(key);
    if (existing) {
        ctx.bb_put(key, bb_value{std::monostate{}}, writer_name);
    }
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
    request.schema_version = "planner.request.v1";
    request.node_name = n.leaf_name.empty() ? ("plan-action-" + std::to_string(n.id)) : n.leaf_name;
    request.tick_index = ctx.tick_index;
    request.run_id = "inst-" + std::to_string(ctx.inst.instance_handle);

    std::string state_key = "state";
    std::string action_key = "action";
    std::string model_service = "toy-1d";
    std::string meta_key;
    std::string seed_key;
    std::string safe_action_key;
    std::string sigma_key;
    std::string max_du_key;

    for (std::size_t i = 0; i < args.size(); i += 2) {
        const std::string raw_key = arg_as_text(args[i], "plan-action");
        const std::string key = normalize_plan_option(raw_key);
        const muslisp::value value = args[i + 1];

        if (key == "name") {
            request.node_name = arg_as_text(value, "plan-action :name");
            continue;
        }
        if (key == "planner") {
            const std::string planner_name = arg_as_text(value, "plan-action :planner");
            planner_backend backend = planner_backend::mcts;
            if (!planner_backend_from_string(planner_name, backend)) {
                throw bt_runtime_error("plan-action: unsupported planner: " + planner_name);
            }
            request.planner = backend;
            continue;
        }
        if (key == "budget_ms") {
            request.budget_ms = arg_as_int(value, "plan-action :budget_ms");
            continue;
        }
        if (key == "work_max" || key == "iters_max") {
            request.work_max = arg_as_int(value, "plan-action :work_max");
            continue;
        }
        if (key == "horizon") {
            request.horizon = arg_as_int(value, "plan-action :horizon");
            continue;
        }
        if (key == "dt_ms") {
            request.dt_ms = arg_as_int(value, "plan-action :dt_ms");
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
        if (key == "safe_action_key") {
            safe_action_key = arg_as_text(value, "plan-action :safe_action_key");
            continue;
        }
        if (key == "safe_action" || key == "fallback_action") {
            request.safe_action.u = {arg_as_number(value, "plan-action :safe_action")};
            continue;
        }
        if (key == "action_schema") {
            request.action_schema = arg_as_text(value, "plan-action :action_schema");
            continue;
        }
        if (key == "top_k") {
            request.top_k = arg_as_int(value, "plan-action :top_k");
            continue;
        }

        if (key == "gamma") {
            request.mcts.gamma = arg_as_number(value, "plan-action :gamma");
            continue;
        }
        if (key == "max_depth") {
            request.mcts.max_depth = arg_as_int(value, "plan-action :max_depth");
            continue;
        }
        if (key == "c_ucb") {
            request.mcts.c_ucb = arg_as_number(value, "plan-action :c_ucb");
            continue;
        }
        if (key == "pw_k") {
            request.mcts.pw_k = arg_as_number(value, "plan-action :pw_k");
            continue;
        }
        if (key == "pw_alpha") {
            request.mcts.pw_alpha = arg_as_number(value, "plan-action :pw_alpha");
            continue;
        }
        if (key == "rollout_policy") {
            request.mcts.rollout_policy = arg_as_text(value, "plan-action :rollout_policy");
            continue;
        }
        if (key == "action_sampler") {
            request.mcts.action_sampler = arg_as_text(value, "plan-action :action_sampler");
            continue;
        }

        if (key == "lambda") {
            request.mppi.lambda = arg_as_number(value, "plan-action :lambda");
            continue;
        }
        if (key == "sigma") {
            request.mppi.sigma = {arg_as_number(value, "plan-action :sigma")};
            continue;
        }
        if (key == "sigma_key") {
            sigma_key = arg_as_text(value, "plan-action :sigma_key");
            continue;
        }
        if (key == "n_samples") {
            request.mppi.n_samples = arg_as_int(value, "plan-action :n_samples");
            continue;
        }
        if (key == "n_elite") {
            request.mppi.n_elite = arg_as_int(value, "plan-action :n_elite");
            continue;
        }

        if (key == "max_iters") {
            request.ilqr.max_iters = arg_as_int(value, "plan-action :max_iters");
            continue;
        }
        if (key == "reg_init") {
            request.ilqr.reg_init = arg_as_number(value, "plan-action :reg_init");
            continue;
        }
        if (key == "reg_factor") {
            request.ilqr.reg_factor = arg_as_number(value, "plan-action :reg_factor");
            continue;
        }
        if (key == "tol_cost") {
            request.ilqr.tol_cost = arg_as_number(value, "plan-action :tol_cost");
            continue;
        }
        if (key == "tol_grad") {
            request.ilqr.tol_grad = arg_as_number(value, "plan-action :tol_grad");
            continue;
        }
        if (key == "fd_eps") {
            request.ilqr.fd_eps = arg_as_number(value, "plan-action :fd_eps");
            continue;
        }
        if (key == "derivatives") {
            const std::string mode_text = arg_as_text(value, "plan-action :derivatives");
            planner_ilqr_derivatives_mode mode = planner_ilqr_derivatives_mode::analytic;
            if (!planner_ilqr_derivatives_mode_from_string(mode_text, mode)) {
                throw bt_runtime_error("plan-action: unsupported derivatives mode: " + mode_text);
            }
            request.ilqr.derivatives = mode;
            continue;
        }

        if (key == "max_du") {
            request.constraints.max_du = {arg_as_number(value, "plan-action :max_du")};
            continue;
        }
        if (key == "max_du_key") {
            max_du_key = arg_as_text(value, "plan-action :max_du_key");
            continue;
        }
        if (key == "smoothness_weight") {
            request.constraints.smoothness_weight = arg_as_number(value, "plan-action :smoothness_weight");
            request.constraints.has_smoothness_weight = true;
            continue;
        }
        if (key == "collision_weight") {
            request.constraints.collision_weight = arg_as_number(value, "plan-action :collision_weight");
            request.constraints.has_collision_weight = true;
            continue;
        }
        if (key == "goal_tolerance") {
            request.constraints.goal_tolerance = arg_as_number(value, "plan-action :goal_tolerance");
            request.constraints.has_goal_tolerance = true;
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

    if (!safe_action_key.empty()) {
        const bb_entry* safe_entry = ctx.bb_get(safe_action_key);
        if (safe_entry) {
            try {
                request.safe_action.u = state_from_blackboard(safe_entry->value, "plan-action safe_action");
            } catch (const std::exception& e) {
                emit_log(ctx, log_level::warn, "planner", std::string("plan-action: invalid safe_action_key: ") + e.what());
            }
        }
    }
    if (!sigma_key.empty()) {
        const bb_entry* sigma_entry = ctx.bb_get(sigma_key);
        if (sigma_entry) {
            try {
                request.mppi.sigma = state_from_blackboard(sigma_entry->value, "plan-action sigma");
            } catch (const std::exception& e) {
                emit_log(ctx, log_level::warn, "planner", std::string("plan-action: invalid sigma_key: ") + e.what());
            }
        }
    }
    if (!max_du_key.empty()) {
        const bb_entry* max_du_entry = ctx.bb_get(max_du_key);
        if (max_du_entry) {
            try {
                request.constraints.max_du = state_from_blackboard(max_du_entry->value, "plan-action max_du");
            } catch (const std::exception& e) {
                emit_log(ctx, log_level::warn, "planner", std::string("plan-action: invalid max_du_key: ") + e.what());
            }
        }
    }
    if (!request.safe_action.u.empty()) {
        request.safe_action.action_schema = request.action_schema.empty() ? "action.u.v1" : request.action_schema;
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

    if (result.action.u.empty()) {
        std::string message = "plan-action: planner returned empty action";
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

    for (double v : result.action.u) {
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
    msg << "planner=" << planner_backend_name(result.planner) << " status=" << planner_status_name(result.status)
        << " action=";
    for (std::size_t i = 0; i < result.action.u.size(); ++i) {
        if (i != 0) {
            msg << ',';
        }
        msg << result.action.u[i];
    }
    msg << " confidence=" << result.confidence << " work=" << result.stats.work_done
        << " time_ms=" << result.stats.time_used_ms;
    emit_log(ctx, log_level::info, "planner", msg.str());

    if (result.status != planner_status::ok) {
        std::string message = "plan-action: planner status is not :ok";
        if (!result.error.empty()) {
            message += ": ";
            message += result.error;
        }
        trace_event ev = make_trace_event(result.status == planner_status::error ? trace_event_kind::error
                                                                                  : trace_event_kind::warning);
        ev.node = n.id;
        ev.message = message;
        emit_trace(ctx, std::move(ev));
        emit_log(ctx, result.status == planner_status::error ? log_level::error : log_level::warn, "planner", message);
        return status::failure;
    }

    return status::success;
}

status execute_vla_request(const node& n, tick_context& ctx, const std::vector<muslisp::value>& args) {
    if (!ctx.svc.vla) {
        trace_event ev = make_trace_event(trace_event_kind::error);
        ev.node = n.id;
        ev.message = "vla-request: VLA service is not available";
        emit_trace(ctx, std::move(ev));
        emit_log(ctx, log_level::error, "vla", "vla-request: VLA service is not available");
        return status::failure;
    }

    const vla_request_options opts = parse_vla_request_options(n, args);

    if (const bb_entry* existing = ctx.bb_get(opts.job_key); existing) {
        if (const auto* existing_id = std::get_if<std::int64_t>(&existing->value); existing_id && *existing_id > 0) {
            return status::running;
        }
    }

    const bb_entry* state_entry = ctx.bb_get(opts.state_key);
    if (!state_entry) {
        emit_log(ctx, log_level::error, "vla", "vla-request: missing state key: " + opts.state_key);
        return status::failure;
    }

    planner_vector state;
    try {
        state = state_from_blackboard(state_entry->value, "vla-request state");
    } catch (const std::exception& e) {
        emit_log(ctx, log_level::error, "vla", std::string("vla-request: invalid state: ") + e.what());
        return status::failure;
    }
    if (state.empty()) {
        emit_log(ctx, log_level::error, "vla", "vla-request: state must not be empty");
        return status::failure;
    }

    std::string instruction = opts.instruction;
    if (instruction.empty()) {
        const bb_entry* instruction_entry = ctx.bb_get(opts.instruction_key);
        if (!instruction_entry) {
            emit_log(ctx, log_level::error, "vla", "vla-request: missing instruction key: " + opts.instruction_key);
            return status::failure;
        }
        if (const auto* s = std::get_if<std::string>(&instruction_entry->value)) {
            instruction = *s;
        } else {
            emit_log(ctx, log_level::error, "vla", "vla-request: instruction must be string");
            return status::failure;
        }
    }

    std::string task_id = opts.task_id;
    if (!opts.task_id_key.empty()) {
        const bb_entry* task_entry = ctx.bb_get(opts.task_id_key);
        if (task_entry) {
            if (const auto* s = std::get_if<std::string>(&task_entry->value)) {
                task_id = *s;
            } else if (const auto* i = std::get_if<std::int64_t>(&task_entry->value)) {
                task_id = std::to_string(*i);
            } else {
                emit_log(ctx, log_level::error, "vla", "vla-request: task_key must map to string or int");
                return status::failure;
            }
        }
    }

    const std::int64_t dims = (opts.dims > 0) ? opts.dims : static_cast<std::int64_t>(state.size());
    if (dims <= 0) {
        emit_log(ctx, log_level::error, "vla", "vla-request: dims must be > 0");
        return status::failure;
    }
    if (!std::isfinite(opts.bound_lo) || !std::isfinite(opts.bound_hi) || opts.bound_lo > opts.bound_hi) {
        emit_log(ctx, log_level::error, "vla", "vla-request: invalid bounds");
        return status::failure;
    }
    if (opts.deadline_ms <= 0) {
        emit_log(ctx, log_level::error, "vla", "vla-request: deadline_ms must be > 0");
        return status::failure;
    }

    vla_request request;
    request.capability = opts.capability;
    request.task_id = task_id;
    request.instruction = instruction;
    request.deadline_ms = opts.deadline_ms;
    request.model.name = opts.model_name;
    request.model.version = opts.model_version;
    request.node_name = opts.node_name;
    request.run_id = "inst-" + std::to_string(ctx.inst.instance_handle);
    request.tick_index = ctx.tick_index;
    request.action_space.type = "continuous";
    request.action_space.dims = dims;
    request.action_space.bounds.assign(static_cast<std::size_t>(dims), {opts.bound_lo, opts.bound_hi});
    request.constraints.max_abs_value = std::max(0.0, opts.max_abs);
    request.constraints.max_delta = std::max(0.0, opts.max_delta);
    if (opts.forbidden_range.has_value()) {
        if (opts.forbidden_range->first > opts.forbidden_range->second) {
            emit_log(ctx, log_level::error, "vla", "vla-request: forbidden range must be ordered");
            return status::failure;
        }
        request.constraints.forbidden_ranges.push_back(*opts.forbidden_range);
    }

    request.observation.state = state;
    request.observation.frame_id = opts.frame_id;
    request.observation.timestamp_ms = ms_since_epoch(state_entry->last_write_ts);

    if (!opts.image_key.empty()) {
        const bb_entry* image_entry = ctx.bb_get(opts.image_key);
        if (!image_entry) {
            emit_log(ctx, log_level::error, "vla", "vla-request: missing image key: " + opts.image_key);
            return status::failure;
        }
        const std::optional<image_handle_ref> image = image_from_blackboard(image_entry->value);
        if (!image.has_value()) {
            emit_log(ctx, log_level::error, "vla", "vla-request: image key does not hold image_handle");
            return status::failure;
        }
        request.observation.image = *image;
    }

    if (!opts.blob_key.empty()) {
        const bb_entry* blob_entry = ctx.bb_get(opts.blob_key);
        if (!blob_entry) {
            emit_log(ctx, log_level::error, "vla", "vla-request: missing blob key: " + opts.blob_key);
            return status::failure;
        }
        const std::optional<blob_handle_ref> blob = blob_from_blackboard(blob_entry->value);
        if (!blob.has_value()) {
            emit_log(ctx, log_level::error, "vla", "vla-request: blob key does not hold blob_handle");
            return status::failure;
        }
        request.observation.blob = *blob;
    }

    if (opts.fixed_seed.has_value()) {
        request.seed = *opts.fixed_seed;
    }
    if (!opts.seed_key.empty()) {
        const bb_entry* seed_entry = ctx.bb_get(opts.seed_key);
        if (seed_entry) {
            const std::optional<std::uint64_t> seeded = seed_from_blackboard(seed_entry->value);
            if (seeded.has_value()) {
                request.seed = *seeded;
            }
        }
    }
    if (!request.seed.has_value()) {
        request.seed = vla_service::hash64(request.run_id + "::" + request.node_name + "::" + std::to_string(ctx.tick_index));
    }

    const vla_service::vla_job_id id = ctx.svc.vla->submit(request);
    ctx.bb_put(opts.job_key, bb_value{static_cast<std::int64_t>(id)}, opts.node_name);

    std::ostringstream msg;
    msg << "submitted job=" << id << " capability=" << request.capability << " model=" << request.model.name
        << " deadline_ms=" << request.deadline_ms;
    emit_log(ctx, log_level::info, "vla", msg.str());
    return status::running;
}

status execute_vla_wait(const node& n, tick_context& ctx, const std::vector<muslisp::value>& args) {
    if (!ctx.svc.vla) {
        emit_log(ctx, log_level::error, "vla", "vla-wait: VLA service is not available");
        return status::failure;
    }

    const vla_wait_options opts = parse_vla_wait_options(n, args);

    const bb_entry* job_entry = ctx.bb_get(opts.job_key);
    if (!job_entry) {
        emit_log(ctx, log_level::error, "vla", "vla-wait: missing job key: " + opts.job_key);
        return status::failure;
    }
    const auto* id_raw = std::get_if<std::int64_t>(&job_entry->value);
    if (!id_raw || *id_raw <= 0) {
        emit_log(ctx, log_level::error, "vla", "vla-wait: job key does not hold a valid job id");
        return status::failure;
    }

    const auto id = static_cast<vla_service::vla_job_id>(*id_raw);
    const vla_poll poll = ctx.svc.vla->poll(id);

    if (!opts.meta_key.empty()) {
        ctx.bb_put(opts.meta_key, bb_value{vla_poll_meta_json(poll)}, opts.node_name);
    }

    if ((poll.status == vla_job_status::queued || poll.status == vla_job_status::running ||
         poll.status == vla_job_status::streaming) &&
        opts.early_commit && poll.partial.has_value() && poll.partial->action_candidate.has_value() &&
        clamp_confidence(poll.partial->confidence) >= opts.early_confidence && action_is_finite(*poll.partial->action_candidate)) {
        ctx.bb_put(opts.action_key, action_to_blackboard(*poll.partial->action_candidate), opts.node_name);
        if (opts.cancel_on_early_commit) {
            (void)ctx.svc.vla->cancel(id);
        }
        if (opts.clear_job) {
            clear_job_key_if_present(ctx, opts.job_key, opts.node_name);
        }
        emit_log(ctx,
                 log_level::info,
                 "vla",
                 "vla-wait: early-committed partial action with confidence=" + std::to_string(poll.partial->confidence));
        return status::success;
    }

    if (poll.status == vla_job_status::queued || poll.status == vla_job_status::running || poll.status == vla_job_status::streaming) {
        return status::running;
    }

    if (poll.status == vla_job_status::done && poll.final.has_value() && poll.final->status == vla_status::ok &&
        action_is_finite(poll.final->action)) {
        ctx.bb_put(opts.action_key, action_to_blackboard(poll.final->action), opts.node_name);
        if (opts.clear_job) {
            clear_job_key_if_present(ctx, opts.job_key, opts.node_name);
        }
        emit_log(ctx, log_level::info, "vla", "vla-wait: committed final action");
        return status::success;
    }

    if (opts.clear_job) {
        clear_job_key_if_present(ctx, opts.job_key, opts.node_name);
    }

    std::string failure_reason = "vla-wait: job failed";
    if (poll.final.has_value()) {
        failure_reason = "vla-wait: status=" + std::string(vla_status_name(poll.final->status));
        if (!poll.final->explanation.empty()) {
            failure_reason += " ";
            failure_reason += poll.final->explanation;
        }
    } else {
        failure_reason = "vla-wait: job status=" + std::string(vla_job_status_name(poll.status));
    }
    emit_log(ctx, log_level::warn, "vla", failure_reason);
    return status::failure;
}

status execute_vla_cancel(const node& n, tick_context& ctx, const std::vector<muslisp::value>& args) {
    if (!ctx.svc.vla) {
        emit_log(ctx, log_level::error, "vla", "vla-cancel: VLA service is not available");
        return status::failure;
    }

    const vla_cancel_options opts = parse_vla_cancel_options(n, args);
    const bb_entry* job_entry = ctx.bb_get(opts.job_key);
    if (!job_entry) {
        return status::success;
    }
    const auto* id_raw = std::get_if<std::int64_t>(&job_entry->value);
    if (!id_raw || *id_raw <= 0) {
        clear_job_key_if_present(ctx, opts.job_key, opts.node_name);
        return status::success;
    }

    const auto id = static_cast<vla_service::vla_job_id>(*id_raw);
    (void)ctx.svc.vla->cancel(id);
    clear_job_key_if_present(ctx, opts.job_key, opts.node_name);
    emit_log(ctx, log_level::info, "vla", "vla-cancel: cancelled job=" + std::to_string(id));
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

        case node_kind::vla_request: {
            std::vector<muslisp::value> args;
            args.reserve(n.args.size());
            muslisp::gc_root_scope roots(muslisp::default_gc());
            for (const arg_value& compiled : n.args) {
                args.push_back(materialize_arg(compiled));
                roots.add(&args.back());
            }
            try {
                return finalize(execute_vla_request(n, ctx, args));
            } catch (const std::exception& e) {
                emit_log(ctx, log_level::error, "vla", std::string("vla-request failed: ") + e.what());
                return finalize(status::failure);
            }
        }

        case node_kind::vla_wait: {
            std::vector<muslisp::value> args;
            args.reserve(n.args.size());
            muslisp::gc_root_scope roots(muslisp::default_gc());
            for (const arg_value& compiled : n.args) {
                args.push_back(materialize_arg(compiled));
                roots.add(&args.back());
            }
            try {
                return finalize(execute_vla_wait(n, ctx, args));
            } catch (const std::exception& e) {
                emit_log(ctx, log_level::error, "vla", std::string("vla-wait failed: ") + e.what());
                return finalize(status::failure);
            }
        }

        case node_kind::vla_cancel: {
            std::vector<muslisp::value> args;
            args.reserve(n.args.size());
            muslisp::gc_root_scope roots(muslisp::default_gc());
            for (const arg_value& compiled : n.args) {
                args.push_back(materialize_arg(compiled));
                roots.add(&args.back());
            }
            try {
                return finalize(execute_vla_cancel(n, ctx, args));
            } catch (const std::exception& e) {
                emit_log(ctx, log_level::error, "vla", std::string("vla-cancel failed: ") + e.what());
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
