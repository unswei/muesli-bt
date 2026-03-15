#include "runtimes/muesli_adapter.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "bench_config.hpp"
#include "bt/ast.hpp"
#include "bt/compiler.hpp"
#include "bt/event_log.hpp"
#include "bt/instance.hpp"
#include "bt/registry.hpp"
#include "bt/runtime.hpp"
#include "bt/serialisation.hpp"
#include "bt/status.hpp"
#include "fixtures/source_factory.hpp"
#include "fixtures/schedules.hpp"
#include "harness/allocation_tracker.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/reader.hpp"

namespace muesli_bt::bench {
namespace {

bt::node_kind to_bt_kind(fixture_node_kind kind) {
    switch (kind) {
        case fixture_node_kind::seq:
            return bt::node_kind::seq;
        case fixture_node_kind::sel:
            return bt::node_kind::sel;
        case fixture_node_kind::reactive_sel:
            return bt::node_kind::reactive_sel;
        case fixture_node_kind::cond:
            return bt::node_kind::cond;
        case fixture_node_kind::act:
            return bt::node_kind::act;
    }
    throw std::invalid_argument("muesli adapter: unsupported fixture node kind");
}

run_status from_bt_status(bt::status status) {
    switch (status) {
        case bt::status::success:
            return run_status::success;
        case bt::status::failure:
            return run_status::failure;
        case bt::status::running:
            return run_status::running;
    }
    return run_status::failure;
}

std::size_t estimate_interrupt_capacity(const scenario_definition& scenario, std::size_t estimated_tick_count) {
    switch (scenario.schedule) {
        case schedule_kind::none:
            return 0u;
        case schedule_kind::flip_every_100:
            return estimated_tick_count / 100u + 8u;
        case schedule_kind::flip_every_20:
            return estimated_tick_count / 20u + 8u;
        case schedule_kind::flip_every_5:
            return estimated_tick_count / 5u + 8u;
        case schedule_kind::bursty:
            return estimated_tick_count / 8u + 8u;
    }
    return 0u;
}

class allocation_measure_scope {
public:
    allocation_measure_scope() {
        allocation_tracker::reset();
        allocation_tracker::set_enabled(true);
    }

    ~allocation_measure_scope() {
        allocation_tracker::set_enabled(false);
    }
};

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("muesli adapter: failed to open file for reading: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("muesli adapter: failed to open file for writing: " + path.string());
    }
    out << text;
    if (!out) {
        throw std::runtime_error("muesli adapter: failed to write file: " + path.string());
    }
}

}  // namespace

class muesli_adapter::compiled_tree_impl final : public runtime_adapter::compiled_tree {
public:
    explicit compiled_tree_impl(bt::definition definition_value) : definition(std::move(definition_value)) {}

    bt::definition definition;
};

class muesli_adapter::instance_impl final : public runtime_adapter::instance_handle {
public:
    instance_impl(muesli_adapter& owner_value,
                  const compiled_tree_impl& compiled,
                  scenario_definition scenario_value,
                  bt::registry& registry_value)
        : owner(owner_value),
          compiled_tree(&compiled),
          scenario(std::move(scenario_value)),
          runtime_instance(&compiled.definition, 0u),
          event_log(0u) {
        runtime_instance.trace_enabled = false;
        runtime_instance.read_trace_enabled = false;

        services.sched = nullptr;
        services.obs.trace = nullptr;
        services.obs.logger = nullptr;
        services.obs.events = scenario.logging == logging_mode::fulltrace ? &event_log : nullptr;
        services.clock = nullptr;
        services.robot = nullptr;
        services.planner = nullptr;
        services.vla = nullptr;

        event_log.set_ring_capacity(0u);
        event_log.set_enabled(scenario.logging == logging_mode::fulltrace);
        event_log.set_file_enabled(false);
        event_log.set_flush_on_tick_end(false);
        event_log.set_git_sha(MUESLI_BT_BENCH_GIT_COMMIT);
        event_log.set_host_info("muesli-bt-bench", MUESLI_BT_BENCH_PROJECT_VERSION, "bench");
        event_log.set_run_id(scenario.scenario_id + "-warmup");

        owner.instance_index_.emplace(&runtime_instance, this);
        (void)registry_value;
    }

    ~instance_impl() override {
        owner.instance_index_.erase(&runtime_instance);
    }

    void clear_runtime_state() {
        if (runtime_instance.def) {
            bt::halt_subtree(runtime_instance, *owner.registry(), services, runtime_instance.def->root, "benchmark reset");
        }
        bt::reset(runtime_instance);
        runtime_instance.tick_index = 0u;
        runtime_instance.tree_stats = {};
        runtime_instance.node_stats.clear();
        runtime_instance.trace.clear();
    }

    void clear_measurements() {
        counters.semantic_errors = 0u;
        counters.log_events_total = 0u;
        counters.log_bytes_total = 0u;
        counters.interrupt_latency_ticks.clear();
        counters.interrupt_latency_ns.clear();
        counters.cancel_latency_ns.clear();
        counters.wasted_ticks_after_guard = 0u;
        counters.extra_callback_polls_after_guard = 0u;
        counters.live_async_action = false;

        guard_current_tick_active = false;
        guard_last_value = false;
        async_running = false;
        activation_pending = false;
        activation_carried = false;
        activation_tick = 0u;
        repetition_index = 0u;
    }

    void clear_measurement_counters_only() {
        counters.semantic_errors = 0u;
        counters.log_events_total = 0u;
        counters.log_bytes_total = 0u;
        counters.interrupt_latency_ticks.clear();
        counters.interrupt_latency_ns.clear();
        counters.cancel_latency_ns.clear();
        counters.wasted_ticks_after_guard = 0u;
        counters.extra_callback_polls_after_guard = 0u;
        counters.live_async_action = async_running;
        activation_pending = false;
        activation_carried = false;
        activation_tick = 0u;
    }

    muesli_adapter& owner;
    const compiled_tree_impl* compiled_tree = nullptr;
    scenario_definition scenario;
    bt::instance runtime_instance;
    bt::event_log event_log;
    bt::services services{};
    run_counters counters{};
    bool guard_current_tick_active = false;
    bool guard_last_value = false;
    bool async_running = false;
    bool activation_pending = false;
    bool activation_carried = false;
    std::uint64_t activation_tick = 0u;
    std::chrono::steady_clock::time_point activation_started_at{};
    std::size_t repetition_index = 0u;
};

class muesli_adapter::lifecycle_case_impl final : public runtime_adapter::lifecycle_case {
public:
    lifecycle_case_impl(const tree_fixture& fixture,
                        scenario_definition scenario,
                        std::filesystem::path scratch_dir)
        : scenario_(std::move(scenario)),
          scratch_dir_(std::move(scratch_dir)),
          source_(make_fixture_dsl(fixture)) {
        std::filesystem::create_directories(scratch_dir_);
        prepare_inputs();
    }

    ~lifecycle_case_impl() override {
        if (parsed_form_rooted_) {
            muslisp::default_gc().remove_root_slot(&parsed_form_);
        }
    }

    void prime() override {
        (void)run_once();
    }

    lifecycle_sample run_once() override {
        switch (scenario_.lifecycle) {
            case lifecycle_phase::parse_text:
                return parse_text_once();
            case lifecycle_phase::compile_definition:
                return compile_definition_once();
            case lifecycle_phase::instantiate_one:
                return instantiate_one_once();
            case lifecycle_phase::instantiate_hundred:
                return instantiate_hundred_once();
            case lifecycle_phase::load_binary:
                return load_binary_once();
            case lifecycle_phase::load_dsl:
                return load_dsl_once();
            case lifecycle_phase::none:
                break;
        }
        throw std::invalid_argument("muesli adapter: unsupported lifecycle phase");
    }

private:
    template <typename Fn>
    lifecycle_sample measure_phase(std::size_t operations_total, Fn&& fn) {
        allocation_measure_scope allocation_scope;
        const auto started = std::chrono::steady_clock::now();
        fn();
        const auto finished = std::chrono::steady_clock::now();
        const auto allocations = allocation_tracker::read();

        lifecycle_sample sample;
        sample.duration_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
        sample.operations_total = operations_total;
        sample.alloc_count_total = allocations.allocation_count;
        sample.alloc_bytes_total = allocations.allocation_bytes;
        return sample;
    }

    void ensure_parsed_form() {
        if (parsed_form_rooted_) {
            return;
        }
        parsed_form_ = muslisp::read_one(source_);
        muslisp::default_gc().add_root_slot(&parsed_form_);
        parsed_form_rooted_ = true;
    }

    void ensure_compiled_definition() {
        if (compiled_definition_) {
            return;
        }
        const muslisp::value parsed = muslisp::read_one(source_);
        compiled_definition_ = std::make_unique<bt::definition>(bt::compile_definition(parsed));
        muslisp::default_gc().collect();
    }

    void prepare_inputs() {
        switch (scenario_.lifecycle) {
            case lifecycle_phase::parse_text:
                return;
            case lifecycle_phase::compile_definition:
                ensure_parsed_form();
                return;
            case lifecycle_phase::instantiate_one:
            case lifecycle_phase::instantiate_hundred:
                ensure_compiled_definition();
                return;
            case lifecycle_phase::load_binary:
                ensure_compiled_definition();
                binary_path_ = scratch_dir_ / "definition.mbtbin";
                bt::save_definition_binary(*compiled_definition_, binary_path_.string());
                return;
            case lifecycle_phase::load_dsl:
                dsl_path_ = scratch_dir_ / "definition.lisp";
                write_text_file(dsl_path_, source_);
                return;
            case lifecycle_phase::none:
                break;
        }
        throw std::invalid_argument("muesli adapter: missing lifecycle phase");
    }

    lifecycle_sample parse_text_once() {
        muslisp::default_gc().collect();
        lifecycle_sample sample = measure_phase(1u, [this] {
            const muslisp::value parsed = muslisp::read_one(source_);
            sink_ = parsed == nullptr ? 0u : 1u;
        });
        muslisp::default_gc().collect();
        return sample;
    }

    lifecycle_sample compile_definition_once() {
        return measure_phase(1u, [this] {
            bt::definition compiled = bt::compile_definition(parsed_form_);
            sink_ = compiled.nodes.size();
        });
    }

    lifecycle_sample instantiate_one_once() {
        return measure_phase(1u, [this] {
            std::optional<bt::instance> instance;
            instance.emplace(compiled_definition_.get(), 0u);
            sink_ = instance->halt_stack.capacity();
        });
    }

    lifecycle_sample instantiate_hundred_once() {
        return measure_phase(100u, [this] {
            std::array<std::optional<bt::instance>, 100u> instances;
            for (auto& instance : instances) {
                instance.emplace(compiled_definition_.get(), 0u);
            }
            sink_ = instances.back()->halt_stack.capacity();
        });
    }

    lifecycle_sample load_binary_once() {
        return measure_phase(1u, [this] {
            bt::definition definition = bt::load_definition_binary(binary_path_.string());
            sink_ = definition.nodes.size();
        });
    }

    lifecycle_sample load_dsl_once() {
        muslisp::default_gc().collect();
        lifecycle_sample sample = measure_phase(1u, [this] {
            const std::string source = read_text_file(dsl_path_);
            const muslisp::value parsed = muslisp::read_one(source);
            bt::definition definition = bt::compile_definition(parsed);
            sink_ = definition.nodes.size();
        });
        muslisp::default_gc().collect();
        return sample;
    }

    scenario_definition scenario_;
    std::filesystem::path scratch_dir_;
    std::string source_;
    std::unique_ptr<bt::definition> compiled_definition_;
    muslisp::value parsed_form_ = nullptr;
    bool parsed_form_rooted_ = false;
    std::filesystem::path binary_path_;
    std::filesystem::path dsl_path_;
    volatile std::size_t sink_ = 0u;
};

muesli_adapter::muesli_adapter() : registry_(std::make_unique<bt::registry>()) {
    register_callbacks();
}

std::string muesli_adapter::name() const {
    return "muesli-bt";
}

std::string muesli_adapter::version() const {
    return MUESLI_BT_BENCH_PROJECT_VERSION;
}

std::string muesli_adapter::commit() const {
    return MUESLI_BT_BENCH_GIT_COMMIT;
}

std::unique_ptr<runtime_adapter::compiled_tree> muesli_adapter::compile_tree(const tree_fixture& fixture) {
    bt::definition definition;
    definition.nodes.reserve(fixture.node_count);

    const auto append_node = [&](const auto& self, const fixture_node& source) -> bt::node_id {
        const bt::node_id id = static_cast<bt::node_id>(definition.nodes.size());
        definition.nodes.push_back(bt::node{});
        bt::node& target = definition.nodes.back();
        target.id = id;
        target.kind = to_bt_kind(source.kind);
        target.leaf_name = source.leaf_name;
        for (const fixture_node& child : source.children) {
            const bt::node_id child_id = self(self, child);
            definition.nodes[id].children.push_back(child_id);
        }
        return id;
    };

    definition.root = append_node(append_node, fixture.root);
    return std::make_unique<compiled_tree_impl>(std::move(definition));
}

std::unique_ptr<runtime_adapter::lifecycle_case> muesli_adapter::new_lifecycle_case(
    const tree_fixture& fixture,
    const scenario_definition& scenario,
    const std::filesystem::path& scratch_dir) {
    return std::make_unique<lifecycle_case_impl>(fixture, scenario, scratch_dir);
}

std::unique_ptr<runtime_adapter::instance_handle> muesli_adapter::new_instance(const runtime_adapter::compiled_tree& compiled,
                                                                               const scenario_definition& scenario) {
    const auto* typed = dynamic_cast<const compiled_tree_impl*>(&compiled);
    if (!typed) {
        throw std::invalid_argument("muesli adapter: compiled tree type mismatch");
    }
    return std::make_unique<instance_impl>(*this, *typed, scenario, *registry());
}

void muesli_adapter::prepare_for_timed_run(runtime_adapter::instance_handle& instance,
                                           std::size_t estimated_tick_count,
                                           std::size_t repetition_index) {
    auto& typed = dynamic_cast<instance_impl&>(instance);
    typed.clear_runtime_state();
    typed.clear_measurements();
    typed.repetition_index = repetition_index;

    const std::size_t interrupt_capacity = estimate_interrupt_capacity(typed.scenario, estimated_tick_count);
    typed.counters.interrupt_latency_ticks.reserve(interrupt_capacity);
    typed.counters.interrupt_latency_ns.reserve(interrupt_capacity);
    typed.counters.cancel_latency_ns.reserve(interrupt_capacity);

    typed.event_log.clear_ring();
    typed.event_log.set_enabled(typed.scenario.logging == logging_mode::fulltrace);
    typed.event_log.clear_line_listener();

    (void)bt::tick(typed.runtime_instance, *registry(), typed.services);

    typed.runtime_instance.tick_index = 0u;
    typed.clear_measurement_counters_only();
    typed.event_log.set_run_id(typed.scenario.scenario_id + "-rep-" + std::to_string(repetition_index));
    typed.event_log.set_line_listener([state = &typed](const std::string& line) {
        ++state->counters.log_events_total;
        state->counters.log_bytes_total += static_cast<std::uint64_t>(line.size() + 1u);
    });
}

run_status muesli_adapter::tick(runtime_adapter::instance_handle& instance) {
    auto& typed = dynamic_cast<instance_impl&>(instance);
    if (typed.activation_carried) {
        ++typed.counters.wasted_ticks_after_guard;
    }
    typed.activation_carried = false;
    typed.guard_current_tick_active = false;

    const bt::status status = bt::tick(typed.runtime_instance, *registry(), typed.services);
    const run_status run_state = from_bt_status(status);

    if (typed.scenario.kind == benchmark_kind::reactive_interrupt) {
        const run_status expected = typed.guard_current_tick_active ? run_status::success : run_status::running;
        if (run_state != expected) {
            ++typed.counters.semantic_errors;
        }
    } else if (run_state != run_status::success) {
        ++typed.counters.semantic_errors;
    }

    if (typed.activation_pending) {
        typed.activation_carried = true;
    }

    return run_state;
}

run_counters muesli_adapter::read_counters(const runtime_adapter::instance_handle& instance) const {
    const auto& typed = dynamic_cast<const instance_impl&>(instance);
    run_counters out = typed.counters;
    out.live_async_action = typed.async_running;
    return out;
}

void muesli_adapter::teardown(runtime_adapter::instance_handle& instance) {
    auto& typed = dynamic_cast<instance_impl&>(instance);
    if (typed.runtime_instance.def) {
        bt::halt_subtree(typed.runtime_instance, *registry(), typed.services, typed.runtime_instance.def->root, "benchmark teardown");
    }
    typed.event_log.clear_line_listener();
    if (typed.activation_pending) {
        ++typed.counters.semantic_errors;
        typed.activation_pending = false;
    }
    if (typed.async_running) {
        ++typed.counters.semantic_errors;
        typed.async_running = false;
    }
    typed.counters.live_async_action = false;
}

bt::registry* muesli_adapter::registry() noexcept {
    return registry_.get();
}

const bt::registry* muesli_adapter::registry() const noexcept {
    return registry_.get();
}

muesli_adapter::instance_impl& muesli_adapter::state_for(bt::instance& instance) const {
    auto it = instance_index_.find(&instance);
    if (it == instance_index_.end()) {
        throw std::runtime_error("muesli adapter: missing instance state");
    }
    return *it->second;
}

const muesli_adapter::instance_impl& muesli_adapter::state_for(const bt::instance& instance) const {
    auto it = instance_index_.find(&instance);
    if (it == instance_index_.end()) {
        throw std::runtime_error("muesli adapter: missing instance state");
    }
    return *it->second;
}

void muesli_adapter::register_callbacks() {
    registry()->register_condition("cond_ok", [](bt::tick_context&, std::span<const muslisp::value>) { return true; });
    registry()->register_condition("cond_fail", [](bt::tick_context&, std::span<const muslisp::value>) { return false; });
    registry()->register_condition("cond_path_valid", [](bt::tick_context&, std::span<const muslisp::value>) { return true; });

    registry()->register_condition(
        "cond_emergency_stop",
        [this](bt::tick_context& ctx, std::span<const muslisp::value>) {
            auto& state = state_for(ctx.inst);
            const bool active = schedule_active(state.scenario.schedule, ctx.tick_index, state.scenario.seed);
            state.guard_current_tick_active = active;
            if (active && !state.guard_last_value && state.async_running && !state.activation_pending) {
                state.activation_pending = true;
                state.activation_tick = ctx.tick_index;
                state.activation_started_at = std::chrono::steady_clock::now();
            }
            state.guard_last_value = active;
            return active;
        });

    registry()->register_action("act_ok",
                                [](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                                    return bt::status::success;
                                });
    registry()->register_action("act_fail",
                                [](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                                    return bt::status::failure;
                                });
    registry()->register_action("act_stop",
                                [](bt::tick_context&, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
                                    return bt::status::success;
                                });

    registry()->register_action(
        "act_follow_path_async",
        [this](bt::tick_context& ctx, bt::node_id, bt::node_memory&, std::span<const muslisp::value>) {
            auto& state = state_for(ctx.inst);
            if (state.activation_pending) {
                ++state.counters.extra_callback_polls_after_guard;
            }
            state.async_running = true;
            state.counters.live_async_action = true;
            return bt::status::running;
        },
        [this](bt::tick_context& ctx, bt::node_id, bt::node_memory&) {
            auto& state = state_for(ctx.inst);
            if (!state.async_running) {
                return;
            }

            state.async_running = false;
            state.counters.live_async_action = false;
            if (state.activation_pending) {
                const auto finished_at = std::chrono::steady_clock::now();
                const auto interrupt_latency =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(finished_at - state.activation_started_at).count();
                state.counters.interrupt_latency_ticks.push_back(ctx.tick_index - state.activation_tick);
                state.counters.interrupt_latency_ns.push_back(static_cast<std::uint64_t>(interrupt_latency));
                state.counters.cancel_latency_ns.push_back(static_cast<std::uint64_t>(interrupt_latency));
                state.activation_pending = false;
                state.activation_carried = false;
            }
        });
}

}  // namespace muesli_bt::bench
