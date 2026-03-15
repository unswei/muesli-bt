#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "harness/scenario.hpp"
#include "fixtures/tree_factory.hpp"

namespace muesli_bt::bench {

enum class run_status {
    success,
    failure,
    running
};

struct run_counters {
    std::uint64_t semantic_errors = 0;
    std::uint64_t log_events_total = 0;
    std::uint64_t log_bytes_total = 0;
    std::vector<std::uint64_t> interrupt_latency_ticks;
    std::vector<std::uint64_t> interrupt_latency_ns;
    std::vector<std::uint64_t> cancel_latency_ns;
    std::uint64_t wasted_ticks_after_guard = 0;
    std::uint64_t extra_callback_polls_after_guard = 0;
    bool live_async_action = false;
};

struct lifecycle_sample {
    std::uint64_t duration_ns = 0;
    std::size_t operations_total = 1;
    std::uint64_t alloc_count_total = 0;
    std::uint64_t alloc_bytes_total = 0;
    std::uint64_t semantic_errors = 0;
    std::string notes;
};

class runtime_adapter {
public:
    class compiled_tree {
    public:
        virtual ~compiled_tree() = default;
    };

    class instance_handle {
    public:
        virtual ~instance_handle() = default;
    };

    class lifecycle_case {
    public:
        virtual ~lifecycle_case() = default;
        virtual void prime() = 0;
        virtual lifecycle_sample run_once() = 0;
    };

    virtual ~runtime_adapter() = default;

    virtual std::string name() const = 0;
    virtual std::string version() const = 0;
    virtual std::string commit() const = 0;
    virtual bool supports_scenario(const scenario_definition& scenario) const = 0;
    virtual std::unique_ptr<compiled_tree> compile_tree(const tree_fixture& fixture) = 0;
    virtual std::unique_ptr<lifecycle_case> new_lifecycle_case(const tree_fixture& fixture,
                                                               const scenario_definition& scenario,
                                                               const std::filesystem::path& scratch_dir) = 0;
    virtual std::unique_ptr<instance_handle> new_instance(const compiled_tree& compiled,
                                                          const scenario_definition& scenario) = 0;
    virtual void prepare_for_timed_run(instance_handle& instance,
                                       std::size_t estimated_tick_count,
                                       std::size_t repetition_index) = 0;
    virtual run_status tick(instance_handle& instance) = 0;
    virtual run_counters read_counters(const instance_handle& instance) const = 0;
    virtual void teardown(instance_handle& instance) = 0;
};

}  // namespace muesli_bt::bench
