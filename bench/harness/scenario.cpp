#include "harness/scenario.hpp"

#include <array>
#include <stdexcept>

namespace muesli_bt::bench {
namespace {

scenario_definition make_static_scenario(std::string scenario_id,
                                         std::string group_id,
                                         tree_family family,
                                         std::size_t tree_size_nodes,
                                         logging_mode logging,
                                         std::string variant,
                                         timing_config timing = {},
                                         bool capture_tick_trace = false) {
    return scenario_definition{
        .scenario_id = std::move(scenario_id),
        .group_id = std::move(group_id),
        .kind = family == tree_family::single_leaf ? benchmark_kind::single_leaf : benchmark_kind::static_tick,
        .family = family,
        .tree_size_nodes = tree_size_nodes,
        .logging = logging,
        .schedule = schedule_kind::none,
        .variant = std::move(variant),
        .timing = timing,
        .seed = 20260315ull,
        .capture_tick_trace = capture_tick_trace,
    };
}

scenario_definition make_reactive_scenario(std::string scenario_id,
                                           std::string group_id,
                                           std::size_t tree_size_nodes,
                                           logging_mode logging,
                                           schedule_kind schedule,
                                           std::string variant,
                                           timing_config timing = {}) {
    return scenario_definition{
        .scenario_id = std::move(scenario_id),
        .group_id = std::move(group_id),
        .kind = benchmark_kind::reactive_interrupt,
        .family = tree_family::reactive,
        .tree_size_nodes = tree_size_nodes,
        .logging = logging,
        .schedule = schedule,
        .variant = std::move(variant),
        .timing = timing,
        .seed = 20260315ull,
        .capture_tick_trace = false,
    };
}

scenario_definition make_lifecycle_scenario(std::string scenario_id,
                                            std::size_t tree_size_nodes,
                                            lifecycle_phase lifecycle,
                                            std::string variant,
                                            timing_config timing) {
    return scenario_definition{
        .scenario_id = std::move(scenario_id),
        .group_id = "B5",
        .kind = benchmark_kind::compile_lifecycle,
        .family = tree_family::alt,
        .tree_size_nodes = tree_size_nodes,
        .logging = logging_mode::off,
        .schedule = schedule_kind::none,
        .lifecycle = lifecycle,
        .variant = std::move(variant),
        .timing = timing,
        .seed = 20260315ull,
        .capture_tick_trace = false,
    };
}

const std::vector<scenario_definition>& scenario_catalogue() {
    static const std::vector<scenario_definition> catalogue = [] {
        std::vector<scenario_definition> scenarios;
        scenarios.reserve(40);

        scenarios.push_back(
            make_static_scenario("A1-single-leaf-off", "A1", tree_family::single_leaf, 1, logging_mode::off, "base"));
        scenarios.push_back(make_static_scenario(
            "A1-single-leaf-log", "A1", tree_family::single_leaf, 1, logging_mode::fulltrace, "log"));

        for (const std::size_t size : {31u, 255u}) {
            scenarios.push_back(
                make_static_scenario("B1-seq-" + std::to_string(size) + "-base-off",
                                     "B1",
                                     tree_family::seq,
                                     size,
                                     logging_mode::off,
                                     "base"));
            scenarios.push_back(
                make_static_scenario("B1-sel-" + std::to_string(size) + "-base-off",
                                     "B1",
                                     tree_family::sel,
                                     size,
                                     logging_mode::off,
                                     "base"));
            scenarios.push_back(
                make_static_scenario("B1-alt-" + std::to_string(size) + "-base-off",
                                     "B1",
                                     tree_family::alt,
                                     size,
                                     logging_mode::off,
                                     "base"));
        }

        for (const std::size_t size : {31u, 255u}) {
            scenarios.push_back(make_reactive_scenario("B2-reactive-" + std::to_string(size) + "-flip100-off",
                                                       "B2",
                                                       size,
                                                       logging_mode::off,
                                                       schedule_kind::flip_every_100,
                                                       "flip100"));
            scenarios.push_back(make_reactive_scenario("B2-reactive-" + std::to_string(size) + "-flip20-off",
                                                       "B2",
                                                       size,
                                                       logging_mode::off,
                                                       schedule_kind::flip_every_20,
                                                       "flip20"));
            scenarios.push_back(make_reactive_scenario("B2-reactive-" + std::to_string(size) + "-flip5-off",
                                                       "B2",
                                                       size,
                                                       logging_mode::off,
                                                       schedule_kind::flip_every_5,
                                                       "flip5"));
            scenarios.push_back(make_reactive_scenario("B2-reactive-" + std::to_string(size) + "-bursty-off",
                                                       "B2",
                                                       size,
                                                       logging_mode::off,
                                                       schedule_kind::bursty,
                                                       "bursty"));
        }

        timing_config lifecycle_timing;
        lifecycle_timing.warmup = std::chrono::milliseconds::zero();
        lifecycle_timing.run = std::chrono::milliseconds::zero();
        lifecycle_timing.repetitions = 100u;
        for (const std::size_t size : {31u, 255u, 1023u}) {
            scenarios.push_back(make_lifecycle_scenario(
                "B5-alt-" + std::to_string(size) + "-parse-off", size, lifecycle_phase::parse_text, "parse", lifecycle_timing));
            scenarios.push_back(make_lifecycle_scenario("B5-alt-" + std::to_string(size) + "-compile-off",
                                                        size,
                                                        lifecycle_phase::compile_definition,
                                                        "compile",
                                                        lifecycle_timing));
            scenarios.push_back(make_lifecycle_scenario("B5-alt-" + std::to_string(size) + "-inst1-off",
                                                        size,
                                                        lifecycle_phase::instantiate_one,
                                                        "inst1",
                                                        lifecycle_timing));
            scenarios.push_back(make_lifecycle_scenario("B5-alt-" + std::to_string(size) + "-inst100-off",
                                                        size,
                                                        lifecycle_phase::instantiate_hundred,
                                                        "inst100",
                                                        lifecycle_timing));
            scenarios.push_back(make_lifecycle_scenario(
                "B5-alt-" + std::to_string(size) + "-loadbin-off", size, lifecycle_phase::load_binary, "loadbin", lifecycle_timing));
            scenarios.push_back(make_lifecycle_scenario(
                "B5-alt-" + std::to_string(size) + "-loaddsl-off", size, lifecycle_phase::load_dsl, "loaddsl", lifecycle_timing));
        }

        scenarios.push_back(
            make_static_scenario("B6-alt-31-base-fulltrace", "B6", tree_family::alt, 31, logging_mode::fulltrace, "base"));
        scenarios.push_back(make_reactive_scenario(
            "B6-reactive-31-flip20-fulltrace", "B6", 31, logging_mode::fulltrace, schedule_kind::flip_every_20, "flip20"));

        timing_config jitter_timing;
        jitter_timing.warmup = std::chrono::milliseconds(2000);
        jitter_timing.run = std::chrono::milliseconds(60000);
        jitter_timing.repetitions = 5;
        scenarios.push_back(make_static_scenario(
            "A2-alt-255-jitter-off", "A2", tree_family::alt, 255, logging_mode::off, "jitter", jitter_timing, true));

        return scenarios;
    }();
    return catalogue;
}

}  // namespace

std::string_view benchmark_kind_name(benchmark_kind kind) noexcept {
    switch (kind) {
        case benchmark_kind::single_leaf:
            return "single_leaf";
        case benchmark_kind::static_tick:
            return "static_tick";
        case benchmark_kind::reactive_interrupt:
            return "reactive_interrupt";
        case benchmark_kind::compile_lifecycle:
            return "compile_lifecycle";
    }
    return "unknown";
}

std::string_view lifecycle_phase_name(lifecycle_phase phase) noexcept {
    switch (phase) {
        case lifecycle_phase::none:
            return "";
        case lifecycle_phase::parse_text:
            return "parse";
        case lifecycle_phase::compile_definition:
            return "compile";
        case lifecycle_phase::instantiate_one:
            return "inst1";
        case lifecycle_phase::instantiate_hundred:
            return "inst100";
        case lifecycle_phase::load_binary:
            return "loadbin";
        case lifecycle_phase::load_dsl:
            return "loaddsl";
    }
    return "unknown";
}

std::string_view tree_family_name(tree_family family) noexcept {
    switch (family) {
        case tree_family::single_leaf:
            return "single-leaf";
        case tree_family::seq:
            return "seq";
        case tree_family::sel:
            return "sel";
        case tree_family::alt:
            return "alt";
        case tree_family::reactive:
            return "reactive";
    }
    return "unknown";
}

std::string_view logging_mode_name(logging_mode mode) noexcept {
    switch (mode) {
        case logging_mode::off:
            return "off";
        case logging_mode::fulltrace:
            return "fulltrace";
    }
    return "unknown";
}

std::string_view schedule_kind_name(schedule_kind kind) noexcept {
    switch (kind) {
        case schedule_kind::none:
            return "";
        case schedule_kind::flip_every_100:
            return "flip100";
        case schedule_kind::flip_every_20:
            return "flip20";
        case schedule_kind::flip_every_5:
            return "flip5";
        case schedule_kind::bursty:
            return "bursty";
    }
    return "unknown";
}

std::vector<scenario_definition> default_scenarios() {
    return scenario_catalogue();
}

std::vector<scenario_definition> scenarios_for_group(std::string_view group_id) {
    std::vector<scenario_definition> out;
    for (const scenario_definition& scenario : scenario_catalogue()) {
        if (scenario.group_id == group_id) {
            out.push_back(scenario);
        }
    }
    return out;
}

const scenario_definition* find_scenario(std::string_view scenario_id) {
    for (const scenario_definition& scenario : scenario_catalogue()) {
        if (scenario.scenario_id == scenario_id) {
            return &scenario;
        }
    }
    return nullptr;
}

}  // namespace muesli_bt::bench
