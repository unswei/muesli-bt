#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace muesli_bt::bench {

inline constexpr std::string_view kSchemaVersion = "4";
inline constexpr std::string_view kBenchmarkSuiteVersion = "0.1.0-m1";

enum class benchmark_kind {
    single_leaf,
    static_tick,
    reactive_interrupt,
    compile_lifecycle,
    memory_gc,
    async_contract
};

enum class lifecycle_phase {
    none,
    parse_text,
    compile_definition,
    instantiate_one,
    instantiate_hundred,
    load_binary,
    load_dsl
};

enum class tree_family {
    single_leaf,
    seq,
    sel,
    alt,
    reactive
};

enum class logging_mode {
    off,
    fulltrace
};

enum class schedule_kind {
    none,
    flip_every_100,
    flip_every_20,
    flip_every_5,
    bursty
};

enum class gc_benchmark_mode {
    none,
    manual,
    between_ticks,
    forced_pressure
};

enum class async_contract_case {
    none,
    cancel_before_start,
    cancel_while_running,
    cancel_after_timeout,
    repeated_cancel,
    late_completion_after_cancel
};

struct timing_config {
    std::chrono::milliseconds warmup{2000};
    std::chrono::milliseconds run{10000};
    std::size_t repetitions = 20;
};

struct scenario_definition {
    std::string scenario_id;
    std::string group_id;
    benchmark_kind kind = benchmark_kind::static_tick;
    tree_family family = tree_family::seq;
    std::size_t tree_size_nodes = 0;
    logging_mode logging = logging_mode::off;
    schedule_kind schedule = schedule_kind::none;
    lifecycle_phase lifecycle = lifecycle_phase::none;
    gc_benchmark_mode gc_mode = gc_benchmark_mode::none;
    async_contract_case async_case = async_contract_case::none;
    std::string variant;
    timing_config timing{};
    std::uint64_t seed = 20260315ull;
    bool capture_tick_trace = false;
};

std::string_view benchmark_kind_name(benchmark_kind kind) noexcept;
std::string_view lifecycle_phase_name(lifecycle_phase phase) noexcept;
std::string_view tree_family_name(tree_family family) noexcept;
std::string_view logging_mode_name(logging_mode mode) noexcept;
std::string_view schedule_kind_name(schedule_kind kind) noexcept;

std::vector<scenario_definition> default_scenarios();
std::vector<scenario_definition> scenarios_for_group(std::string_view group_id);
const scenario_definition* find_scenario(std::string_view scenario_id);

}  // namespace muesli_bt::bench
