#include "bt/runtime_host.hpp"

#include <chrono>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include "muslisp/error.hpp"
#include "muslisp/printer.hpp"

namespace bt {
namespace {

class system_clock_service final : public clock_interface {
public:
    std::chrono::steady_clock::time_point now() const override { return std::chrono::steady_clock::now(); }
};

class demo_robot_service final : public robot_interface {
public:
    bool battery_ok(tick_context&) override { return true; }

    bool target_visible(tick_context& ctx) override {
        const bb_entry* entry = ctx.bb_get("target-visible");
        if (!entry) {
            return false;
        }
        if (const bool* b = std::get_if<bool>(&entry->value)) {
            return *b;
        }
        if (const std::int64_t* i = std::get_if<std::int64_t>(&entry->value)) {
            return *i != 0;
        }
        return false;
    }

    status approach_target(tick_context&, node_memory& mem) override {
        if (mem.i0 == 0) {
            mem.i0 = 1;
            return status::running;
        }
        mem.i0 = 0;
        return status::success;
    }

    status grasp(tick_context&, node_memory&) override { return status::success; }

    status search_target(tick_context& ctx, node_memory&) override {
        ctx.bb_put("target-visible", bb_value{true}, "robot.search-target");
        return status::success;
    }
};

std::string require_key_arg(const std::span<const muslisp::value> args, std::size_t index, const std::string& where) {
    if (index >= args.size()) {
        throw std::runtime_error(where + ": missing key argument");
    }
    if (muslisp::is_symbol(args[index])) {
        return muslisp::symbol_name(args[index]);
    }
    if (muslisp::is_string(args[index])) {
        return muslisp::string_value(args[index]);
    }
    throw std::runtime_error(where + ": key must be symbol or string");
}

std::int64_t require_int_arg(const std::span<const muslisp::value> args, std::size_t index, const std::string& where) {
    if (index >= args.size() || !muslisp::is_integer(args[index])) {
        throw std::runtime_error(where + ": expected integer argument at index " + std::to_string(index));
    }
    return muslisp::integer_value(args[index]);
}

double require_floaty_arg(const std::span<const muslisp::value> args, std::size_t index, const std::string& where) {
    if (index >= args.size()) {
        throw std::runtime_error(where + ": missing numeric argument");
    }
    if (muslisp::is_integer(args[index])) {
        return static_cast<double>(muslisp::integer_value(args[index]));
    }
    if (muslisp::is_float(args[index])) {
        return muslisp::float_value(args[index]);
    }
    throw std::runtime_error(where + ": expected numeric argument");
}

job_status status_from_memory(std::int64_t raw) {
    if (raw < 0 || raw > static_cast<std::int64_t>(job_status::unknown)) {
        return job_status::unknown;
    }
    return static_cast<job_status>(raw);
}

std::int64_t status_to_memory(job_status st) {
    return static_cast<std::int64_t>(st);
}

}  // namespace

runtime_host::runtime_host()
    : scheduler_(0),
      logs_(4096),
      owned_clock_(std::make_unique<system_clock_service>()),
      owned_robot_(std::make_unique<demo_robot_service>()) {
    clock_ = owned_clock_.get();
    robot_ = owned_robot_.get();
}

std::int64_t runtime_host::store_definition(definition def) {
    const std::int64_t handle = next_definition_handle_++;
    definitions_[handle] = std::move(def);
    return handle;
}

std::int64_t runtime_host::create_instance(std::int64_t definition_handle) {
    const definition* def = find_definition(definition_handle);
    if (!def) {
        throw std::invalid_argument("create_instance: unknown definition handle");
    }

    const std::int64_t handle = next_instance_handle_++;
    auto inst = std::make_unique<instance>(def);
    set_tick_budget_ms(*inst, 20);
    instances_[handle] = std::move(inst);
    return handle;
}

definition* runtime_host::find_definition(std::int64_t handle) {
    const auto it = definitions_.find(handle);
    return it == definitions_.end() ? nullptr : &it->second;
}

const definition* runtime_host::find_definition(std::int64_t handle) const {
    const auto it = definitions_.find(handle);
    return it == definitions_.end() ? nullptr : &it->second;
}

instance* runtime_host::find_instance(std::int64_t handle) {
    const auto it = instances_.find(handle);
    return it == instances_.end() ? nullptr : it->second.get();
}

const instance* runtime_host::find_instance(std::int64_t handle) const {
    const auto it = instances_.find(handle);
    return it == instances_.end() ? nullptr : it->second.get();
}

status runtime_host::tick_instance(std::int64_t handle) {
    instance* inst = find_instance(handle);
    if (!inst) {
        throw std::invalid_argument("tick_instance: unknown instance handle");
    }

    services svc;
    svc.sched = &scheduler_;
    svc.obs.trace = &inst->trace;
    svc.obs.logger = &logs_;
    svc.clock = clock_;
    svc.robot = robot_;

    return tick(*inst, registry_, svc);
}

void runtime_host::reset_instance(std::int64_t handle) {
    instance* inst = find_instance(handle);
    if (!inst) {
        throw std::invalid_argument("reset_instance: unknown instance handle");
    }
    reset(*inst);
}

registry& runtime_host::callbacks() noexcept {
    return registry_;
}

const registry& runtime_host::callbacks() const noexcept {
    return registry_;
}

scheduler& runtime_host::scheduler_ref() {
    return scheduler_;
}

const scheduler& runtime_host::scheduler_ref() const {
    return scheduler_;
}

memory_log_sink& runtime_host::logs() noexcept {
    return logs_;
}

const memory_log_sink& runtime_host::logs() const noexcept {
    return logs_;
}

void runtime_host::set_clock_interface(clock_interface* clock) noexcept {
    clock_ = clock ? clock : owned_clock_.get();
}

clock_interface* runtime_host::clock_interface_ptr() noexcept {
    return clock_;
}

const clock_interface* runtime_host::clock_interface_ptr() const noexcept {
    return clock_;
}

void runtime_host::set_robot_interface(robot_interface* robot) noexcept {
    robot_ = robot ? robot : owned_robot_.get();
}

robot_interface* runtime_host::robot_interface_ptr() noexcept {
    return robot_;
}

const robot_interface* runtime_host::robot_interface_ptr() const noexcept {
    return robot_;
}

void runtime_host::clear_logs() {
    logs_.clear();
}

void runtime_host::clear_all() {
    definitions_.clear();
    instances_.clear();
    registry_.clear();
    logs_.clear();
}

std::string runtime_host::dump_instance_stats(std::int64_t handle) const {
    const instance* inst = find_instance(handle);
    if (!inst) {
        throw std::invalid_argument("dump_instance_stats: unknown instance handle");
    }
    return dump_stats(*inst);
}

std::string runtime_host::dump_instance_trace(std::int64_t handle) const {
    const instance* inst = find_instance(handle);
    if (!inst) {
        throw std::invalid_argument("dump_instance_trace: unknown instance handle");
    }
    return dump_trace(*inst);
}

std::string runtime_host::dump_instance_blackboard(std::int64_t handle) const {
    const instance* inst = find_instance(handle);
    if (!inst) {
        throw std::invalid_argument("dump_instance_blackboard: unknown instance handle");
    }
    return dump_blackboard(*inst);
}

std::string runtime_host::dump_scheduler_stats() const {
    const scheduler_profile_stats stats = scheduler_.stats_snapshot();

    std::ostringstream out;
    out << "submitted=" << stats.submitted << '\n';
    out << "started=" << stats.started << '\n';
    out << "completed=" << stats.completed << '\n';
    out << "failed=" << stats.failed << '\n';
    out << "cancelled=" << stats.cancelled << '\n';
    out << "queue_delay_last_ns=" << stats.queue_delay.last.count() << '\n';
    out << "run_time_last_ns=" << stats.run_time.last.count() << '\n';
    return out.str();
}

std::string runtime_host::dump_logs() const {
    std::ostringstream out;
    for (const log_record& rec : logs_.snapshot()) {
        out << rec.sequence << " level=" << log_level_name(rec.level) << " ts_ns="
            << std::chrono::duration_cast<std::chrono::nanoseconds>(rec.ts.time_since_epoch()).count()
            << " tick=" << rec.tick_index << " node=" << rec.node << " category=" << rec.category
            << " msg=" << rec.message << '\n';
    }
    return out.str();
}

runtime_host& default_runtime_host() {
    static runtime_host host;
    return host;
}

void install_demo_callbacks(runtime_host& host) {
    registry& reg = host.callbacks();

    reg.register_condition("always-true", [](tick_context&, std::span<const muslisp::value>) { return true; });
    reg.register_condition("always-false", [](tick_context&, std::span<const muslisp::value>) { return false; });

    reg.register_condition("bb-has", [](tick_context& ctx, std::span<const muslisp::value> args) {
        const std::string key = require_key_arg(args, 0, "bb-has");
        return ctx.bb_get(key) != nullptr;
    });

    reg.register_condition("battery-ok", [](tick_context& ctx, std::span<const muslisp::value>) {
        if (!ctx.svc.robot) {
            return false;
        }
        return ctx.svc.robot->battery_ok(ctx);
    });

    reg.register_condition("target-visible", [](tick_context& ctx, std::span<const muslisp::value>) {
        if (!ctx.svc.robot) {
            return false;
        }
        return ctx.svc.robot->target_visible(ctx);
    });

    reg.register_action("always-success",
                        [](tick_context&, node_id, node_memory&, std::span<const muslisp::value>) { return status::success; });

    reg.register_action("always-fail",
                        [](tick_context&, node_id, node_memory&, std::span<const muslisp::value>) { return status::failure; });

    reg.register_action("running-then-success", [](tick_context&, node_id, node_memory& mem, std::span<const muslisp::value> args) {
        const std::int64_t target = args.empty() ? 1 : require_int_arg(args, 0, "running-then-success");
        if (mem.i0 < target) {
            ++mem.i0;
            return status::running;
        }
        mem.i0 = 0;
        return status::success;
    });

    reg.register_action("bb-put-int", [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
        const std::string key = require_key_arg(args, 0, "bb-put-int");
        const std::int64_t v = require_int_arg(args, 1, "bb-put-int");
        ctx.bb_put(key, bb_value{v}, "bb-put-int");
        return status::success;
    });

    reg.register_action("bb-put-float", [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
        const std::string key = require_key_arg(args, 0, "bb-put-float");
        const double v = require_floaty_arg(args, 1, "bb-put-float");
        ctx.bb_put(key, bb_value{v}, "bb-put-float");
        return status::success;
    });

    reg.register_action("approach-target",
                        [](tick_context& ctx, node_id, node_memory& mem, std::span<const muslisp::value>) {
                            if (!ctx.svc.robot) {
                                return status::failure;
                            }
                            return ctx.svc.robot->approach_target(ctx, mem);
                        });

    reg.register_action("grasp", [](tick_context& ctx, node_id, node_memory& mem, std::span<const muslisp::value>) {
        if (!ctx.svc.robot) {
            return status::failure;
        }
        return ctx.svc.robot->grasp(ctx, mem);
    });

    reg.register_action("search-target", [](tick_context& ctx, node_id, node_memory& mem, std::span<const muslisp::value>) {
        if (!ctx.svc.robot) {
            return status::failure;
        }
        return ctx.svc.robot->search_target(ctx, mem);
    });

    reg.register_action("async-sleep-ms", [](tick_context& ctx, node_id, node_memory& mem, std::span<const muslisp::value> args) {
        if (!ctx.svc.sched) {
            return status::failure;
        }

        const std::int64_t delay_ms = args.empty() ? 1 : require_int_arg(args, 0, "async-sleep-ms");

        if (!mem.b0) {
            job_request req;
            req.task_name = "async-sleep-ms";
            req.fn = [delay_ms] {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                job_result out;
                out.payload = delay_ms;
                return out;
            };

            const job_id id = ctx.svc.sched->submit(std::move(req));
            mem.i0 = static_cast<std::int64_t>(id);
            mem.b0 = true;
            mem.i1 = status_to_memory(job_status::queued);
            ctx.scheduler_event(trace_event_kind::scheduler_submit,
                                id,
                                job_status::queued,
                                "scheduler submitted task async-sleep-ms");
            return status::running;
        }

        const job_id id = static_cast<job_id>(mem.i0);
        const job_info info = ctx.svc.sched->get_info(id);

        const job_status prev_status = status_from_memory(mem.i1);
        if (info.status != prev_status) {
            if (info.status == job_status::running) {
                ctx.scheduler_event(trace_event_kind::scheduler_start, id, info.status, "scheduler started task async-sleep-ms");
            } else if (info.status == job_status::cancelled) {
                ctx.scheduler_event(trace_event_kind::scheduler_cancel, id, info.status, "scheduler cancelled task async-sleep-ms");
            } else if (info.status == job_status::done || info.status == job_status::failed) {
                std::string message = "scheduler finished task async-sleep-ms";
                if (!info.error_text.empty()) {
                    message += ": ";
                    message += info.error_text;
                }
                ctx.scheduler_event(trace_event_kind::scheduler_finish, id, info.status, std::move(message));
            }
            mem.i1 = status_to_memory(info.status);
        }

        if (info.status == job_status::queued || info.status == job_status::running) {
            return status::running;
        }

        mem.b0 = false;
        mem.i0 = 0;
        mem.i1 = status_to_memory(job_status::unknown);
        if (info.status == job_status::done) {
            job_result out;
            (void)ctx.svc.sched->try_get_result(id, out);
            return status::success;
        }

        return status::failure;
    });
}

}  // namespace bt
