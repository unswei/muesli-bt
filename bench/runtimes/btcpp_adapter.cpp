#include "runtimes/btcpp_adapter.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <behaviortree_cpp/action_node.h>
#include <behaviortree_cpp/bt_factory.h>

#include "bench_config.hpp"
#include "fixtures/schedules.hpp"
#include "fixtures/source_factory.hpp"
#include "harness/allocation_tracker.hpp"

namespace muesli_bt::bench {
namespace {

constexpr const char* kTreeId = "MainTree";

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

run_status from_btcpp_status(BT::NodeStatus status) {
    switch (status) {
        case BT::NodeStatus::SUCCESS:
            return run_status::success;
        case BT::NodeStatus::FAILURE:
            return run_status::failure;
        case BT::NodeStatus::RUNNING:
            return run_status::running;
        case BT::NodeStatus::IDLE:
        case BT::NodeStatus::SKIPPED:
            return run_status::failure;
    }
    return run_status::failure;
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("btcpp adapter: failed to open file for writing: " + path.string());
    }
    out << text;
    if (!out) {
        throw std::runtime_error("btcpp adapter: failed to write file: " + path.string());
    }
}

struct benchmark_state {
    scenario_definition scenario;
    run_counters counters{};
    bool guard_current_tick_active = false;
    bool guard_last_value = false;
    bool async_running = false;
    bool activation_pending = false;
    bool activation_carried = false;
    std::uint64_t activation_tick = 0u;
    std::uint64_t current_tick_index = 0u;
    std::chrono::steady_clock::time_point activation_started_at{};

    void clear_counters() {
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
        current_tick_index = 0u;
    }
};

class follow_path_async_node final : public BT::StatefulActionNode {
public:
    follow_path_async_node(const std::string& name,
                           const BT::NodeConfig& config,
                           std::shared_ptr<benchmark_state> state)
        : BT::StatefulActionNode(name, config), state_(std::move(state)) {}

    static BT::PortsList providedPorts() {
        return {};
    }

    BT::NodeStatus onStart() override {
        if (state_->activation_pending) {
            ++state_->counters.extra_callback_polls_after_guard;
        }
        state_->async_running = true;
        state_->counters.live_async_action = true;
        return BT::NodeStatus::RUNNING;
    }

    BT::NodeStatus onRunning() override {
        if (state_->activation_pending) {
            ++state_->counters.extra_callback_polls_after_guard;
        }
        state_->async_running = true;
        state_->counters.live_async_action = true;
        return BT::NodeStatus::RUNNING;
    }

    void onHalted() override {
        if (!state_->async_running) {
            return;
        }
        state_->async_running = false;
        state_->counters.live_async_action = false;
        if (state_->activation_pending) {
            const auto finished_at = std::chrono::steady_clock::now();
            const auto interrupt_latency =
                std::chrono::duration_cast<std::chrono::nanoseconds>(finished_at - state_->activation_started_at).count();
            state_->counters.interrupt_latency_ticks.push_back(state_->current_tick_index - state_->activation_tick);
            state_->counters.interrupt_latency_ns.push_back(static_cast<std::uint64_t>(interrupt_latency));
            state_->counters.cancel_latency_ns.push_back(static_cast<std::uint64_t>(interrupt_latency));
            state_->activation_pending = false;
            state_->activation_carried = false;
        }
    }

private:
    std::shared_ptr<benchmark_state> state_;
};

void register_common_nodes(BT::BehaviorTreeFactory& factory, const std::shared_ptr<benchmark_state>& state) {
    factory.registerSimpleCondition("cond_ok", [](BT::TreeNode&) { return BT::NodeStatus::SUCCESS; });
    factory.registerSimpleCondition("cond_fail", [](BT::TreeNode&) { return BT::NodeStatus::FAILURE; });
    factory.registerSimpleCondition("cond_path_valid", [](BT::TreeNode&) { return BT::NodeStatus::SUCCESS; });
    factory.registerSimpleCondition(
        "cond_emergency_stop",
        [state](BT::TreeNode&) {
            const bool active = schedule_active(state->scenario.schedule, state->current_tick_index, state->scenario.seed);
            state->guard_current_tick_active = active;
            if (active && !state->guard_last_value && state->async_running && !state->activation_pending) {
                state->activation_pending = true;
                state->activation_tick = state->current_tick_index;
                state->activation_started_at = std::chrono::steady_clock::now();
            }
            state->guard_last_value = active;
            return active ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
        });

    factory.registerSimpleAction("act_ok", [](BT::TreeNode&) { return BT::NodeStatus::SUCCESS; });
    factory.registerSimpleAction("act_fail", [](BT::TreeNode&) { return BT::NodeStatus::FAILURE; });
    factory.registerSimpleAction("act_stop", [](BT::TreeNode&) { return BT::NodeStatus::SUCCESS; });
    factory.registerBuilder<follow_path_async_node>(
        "act_follow_path_async", BT::CreateBuilder<follow_path_async_node>(state));
}

bool supports_btcpp_scenario(const scenario_definition& scenario) {
    if (scenario.kind == benchmark_kind::memory_gc) {
        return false;
    }
    if (scenario.logging != logging_mode::off) {
        return false;
    }
    if (scenario.group_id == "B6") {
        return false;
    }
    if (scenario.kind == benchmark_kind::compile_lifecycle) {
        switch (scenario.lifecycle) {
            case lifecycle_phase::compile_definition:
            case lifecycle_phase::instantiate_one:
            case lifecycle_phase::instantiate_hundred:
            case lifecycle_phase::load_dsl:
                return true;
            case lifecycle_phase::none:
            case lifecycle_phase::parse_text:
            case lifecycle_phase::load_binary:
                return false;
        }
    }
    return true;
}

}  // namespace

class btcpp_adapter::compiled_tree_impl final : public runtime_adapter::compiled_tree {
public:
    explicit compiled_tree_impl(std::string xml_value) : xml(std::move(xml_value)) {}

    std::string xml;
};

class btcpp_adapter::instance_impl final : public runtime_adapter::instance_handle {
public:
    instance_impl(const compiled_tree_impl& compiled_value, scenario_definition scenario_value)
        : compiled(&compiled_value),
          state(std::make_shared<benchmark_state>(benchmark_state{.scenario = std::move(scenario_value)})) {
        rebuild_tree();
    }

    void rebuild_tree() {
        state->clear_counters();
        state->scenario.logging = logging_mode::off;

        BT::BehaviorTreeFactory factory;
        register_common_nodes(factory, state);
        factory.registerBehaviorTreeFromText(compiled->xml);
        tree = factory.createTree(kTreeId);
    }

    void prime_hot_path() {
        BT::BehaviorTreeFactory factory;
        register_common_nodes(factory, state);
        factory.registerBehaviorTreeFromText(compiled->xml);
        BT::Tree primed = factory.createTree(kTreeId);
        (void)primed.tickExactlyOnce();
        primed.haltTree();
        rebuild_tree();
    }

    const compiled_tree_impl* compiled = nullptr;
    std::shared_ptr<benchmark_state> state;
    BT::Tree tree;
};

class btcpp_adapter::lifecycle_case_impl final : public runtime_adapter::lifecycle_case {
public:
    lifecycle_case_impl(const tree_fixture& fixture,
                        scenario_definition scenario,
                        std::filesystem::path scratch_dir)
        : scenario_(std::move(scenario)),
          scratch_dir_(std::move(scratch_dir)),
          xml_(make_fixture_btcpp_xml(fixture, kTreeId)) {
        std::filesystem::create_directories(scratch_dir_);
        prepare_inputs();
    }

    void prime() override {
        (void)run_once();
    }

    lifecycle_sample run_once() override {
        switch (scenario_.lifecycle) {
            case lifecycle_phase::compile_definition:
                return compile_definition_once();
            case lifecycle_phase::instantiate_one:
                return instantiate_one_once();
            case lifecycle_phase::instantiate_hundred:
                return instantiate_hundred_once();
            case lifecycle_phase::load_dsl:
                return load_dsl_once();
            case lifecycle_phase::none:
            case lifecycle_phase::parse_text:
            case lifecycle_phase::load_binary:
                break;
        }
        throw std::invalid_argument("btcpp adapter: unsupported lifecycle phase");
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

    void prepare_inputs() {
        if (scenario_.lifecycle == lifecycle_phase::instantiate_one ||
            scenario_.lifecycle == lifecycle_phase::instantiate_hundred) {
            base_factory_ = std::make_unique<BT::BehaviorTreeFactory>();
            register_common_nodes(*base_factory_, std::make_shared<benchmark_state>(benchmark_state{.scenario = scenario_}));
            base_factory_->registerBehaviorTreeFromText(xml_);
        } else if (scenario_.lifecycle == lifecycle_phase::load_dsl) {
            dsl_path_ = scratch_dir_ / "definition.xml";
            write_text_file(dsl_path_, xml_);
        }
    }

    lifecycle_sample compile_definition_once() {
        return measure_phase(1u, [this] {
            BT::BehaviorTreeFactory factory;
            register_common_nodes(factory, std::make_shared<benchmark_state>(benchmark_state{.scenario = scenario_}));
            factory.registerBehaviorTreeFromText(xml_);
        });
    }

    lifecycle_sample instantiate_one_once() {
        return measure_phase(1u, [this] {
            BT::Tree tree = base_factory_->createTree(kTreeId);
            sink_ = tree.subtrees.size();
        });
    }

    lifecycle_sample instantiate_hundred_once() {
        return measure_phase(100u, [this] {
            std::array<std::optional<BT::Tree>, 100u> trees;
            for (auto& tree : trees) {
                tree.emplace(base_factory_->createTree(kTreeId));
            }
            sink_ = trees.back()->subtrees.size();
        });
    }

    lifecycle_sample load_dsl_once() {
        return measure_phase(1u, [this] {
            BT::BehaviorTreeFactory factory;
            register_common_nodes(factory, std::make_shared<benchmark_state>(benchmark_state{.scenario = scenario_}));
            factory.registerBehaviorTreeFromFile(dsl_path_);
        });
    }

    scenario_definition scenario_;
    std::filesystem::path scratch_dir_;
    std::filesystem::path dsl_path_;
    std::string xml_;
    std::unique_ptr<BT::BehaviorTreeFactory> base_factory_;
    volatile std::size_t sink_ = 0u;
};

btcpp_adapter::btcpp_adapter() = default;

std::string btcpp_adapter::name() const {
    return "BehaviorTree.CPP";
}

std::string btcpp_adapter::version() const {
    return MUESLI_BT_BENCH_BTCPP_VERSION;
}

std::string btcpp_adapter::commit() const {
    return MUESLI_BT_BENCH_BTCPP_COMMIT;
}

bool btcpp_adapter::supports_scenario(const scenario_definition& scenario) const {
    return supports_btcpp_scenario(scenario);
}

std::unique_ptr<runtime_adapter::compiled_tree> btcpp_adapter::compile_tree(const tree_fixture& fixture) {
    return std::make_unique<compiled_tree_impl>(make_fixture_btcpp_xml(fixture, kTreeId));
}

std::unique_ptr<runtime_adapter::lifecycle_case> btcpp_adapter::new_lifecycle_case(
    const tree_fixture& fixture,
    const scenario_definition& scenario,
    const std::filesystem::path& scratch_dir) {
    return std::make_unique<lifecycle_case_impl>(fixture, scenario, scratch_dir);
}

std::unique_ptr<runtime_adapter::instance_handle> btcpp_adapter::new_instance(const runtime_adapter::compiled_tree& compiled,
                                                                              const scenario_definition& scenario) {
    const auto* typed = dynamic_cast<const compiled_tree_impl*>(&compiled);
    if (!typed) {
        throw std::invalid_argument("btcpp adapter: compiled tree type mismatch");
    }
    return std::make_unique<instance_impl>(*typed, scenario);
}

void btcpp_adapter::prepare_for_timed_run(runtime_adapter::instance_handle& instance,
                                          std::size_t estimated_tick_count,
                                          std::size_t repetition_index) {
    auto& typed = dynamic_cast<instance_impl&>(instance);
    (void)estimated_tick_count;
    (void)repetition_index;
    typed.prime_hot_path();
}

run_status btcpp_adapter::tick(runtime_adapter::instance_handle& instance) {
    auto& typed = dynamic_cast<instance_impl&>(instance);
    auto& state = *typed.state;
    if (state.activation_carried) {
        ++state.counters.wasted_ticks_after_guard;
    }
    state.activation_carried = false;
    state.guard_current_tick_active = false;
    ++state.current_tick_index;

    const BT::NodeStatus status = typed.tree.tickExactlyOnce();
    const run_status run_state = from_btcpp_status(status);

    if (state.scenario.kind == benchmark_kind::reactive_interrupt) {
        const run_status expected = state.guard_current_tick_active ? run_status::success : run_status::running;
        if (run_state != expected) {
            ++state.counters.semantic_errors;
        }
    } else if (run_state != run_status::success) {
        ++state.counters.semantic_errors;
    }

    if (state.activation_pending) {
        state.activation_carried = true;
    }
    state.counters.live_async_action = state.async_running;
    return run_state;
}

run_counters btcpp_adapter::read_counters(const runtime_adapter::instance_handle& instance) const {
    const auto& typed = dynamic_cast<const instance_impl&>(instance);
    run_counters out = typed.state->counters;
    out.live_async_action = typed.state->async_running;
    return out;
}

void btcpp_adapter::teardown(runtime_adapter::instance_handle& instance) {
    auto& typed = dynamic_cast<instance_impl&>(instance);
    typed.tree.haltTree();
    if (typed.state->activation_pending) {
        ++typed.state->counters.semantic_errors;
        typed.state->activation_pending = false;
    }
    if (typed.state->async_running) {
        ++typed.state->counters.semantic_errors;
        typed.state->async_running = false;
    }
    typed.state->counters.live_async_action = false;
}

}  // namespace muesli_bt::bench
