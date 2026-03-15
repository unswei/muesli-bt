#include "harness/runner.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "harness/allocation_tracker.hpp"
#include "harness/metadata.hpp"
#include "harness/stats.hpp"
#include "fixtures/tree_factory.hpp"
#include "runtimes/muesli_adapter.hpp"

namespace muesli_bt::bench {
namespace {

scenario_definition apply_overrides(scenario_definition scenario, const run_request& request) {
    if (request.warmup_override.has_value()) {
        scenario.timing.warmup = *request.warmup_override;
    }
    if (request.run_override.has_value()) {
        scenario.timing.run = *request.run_override;
    }
    if (request.repetitions_override.has_value()) {
        scenario.timing.repetitions = *request.repetitions_override;
    }
    if (request.seed_override.has_value()) {
        scenario.seed = *request.seed_override;
    }
    return scenario;
}

std::chrono::milliseconds expected_duration_for(const scenario_definition& scenario) {
    if (scenario.kind == benchmark_kind::compile_lifecycle) {
        return std::max(std::chrono::milliseconds(1000),
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::milliseconds(10) * scenario.timing.repetitions));
    }
    const auto per_repetition = scenario.timing.warmup + scenario.timing.run;
    return std::chrono::duration_cast<std::chrono::milliseconds>(per_repetition * scenario.timing.repetitions);
}

std::string format_duration_compact(std::chrono::milliseconds duration) {
    using namespace std::chrono;
    const auto total_seconds = duration_cast<seconds>(duration).count();
    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds % 3600) / 60;
    const auto seconds = total_seconds % 60;

    std::ostringstream out;
    if (hours > 0) {
        out << hours << "h " << minutes << "m";
        return out.str();
    }
    if (minutes > 0) {
        out << minutes << "m";
        if (seconds > 0) {
            out << ' ' << seconds << 's';
        }
        return out.str();
    }
    out << seconds << 's';
    return out.str();
}

std::string local_time_to_minute_now() {
    const std::time_t now = std::time(nullptr);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif
    std::ostringstream out;
    out << std::put_time(&local_tm, "%Y-%m-%d %H:%M");
    return out.str();
}

std::string scenario_description(const scenario_definition& scenario) {
    switch (scenario.group_id[0]) {
        case 'A':
            if (scenario.group_id == "A1") {
                return scenario.logging == logging_mode::off ? "single-leaf baseline" : "single-leaf logging";
            }
            if (scenario.group_id == "A2") {
                return "tick jitter (255 nodes)";
            }
            break;
        case 'B':
            if (scenario.group_id == "B1") {
                switch (scenario.family) {
                    case tree_family::seq:
                        return "sequence overhead (" + std::to_string(scenario.tree_size_nodes) + " nodes)";
                    case tree_family::sel:
                        return "selector overhead (" + std::to_string(scenario.tree_size_nodes) + " nodes)";
                    case tree_family::alt:
                        return "alternating overhead (" + std::to_string(scenario.tree_size_nodes) + " nodes)";
                    default:
                        break;
                }
            }
            if (scenario.group_id == "B2") {
                std::string schedule_text = "bursty flips";
                switch (scenario.schedule) {
                    case schedule_kind::flip_every_100:
                        schedule_text = "slow flips";
                        break;
                    case schedule_kind::flip_every_20:
                        schedule_text = "steady flips";
                        break;
                    case schedule_kind::flip_every_5:
                        schedule_text = "rapid flips";
                        break;
                    case schedule_kind::bursty:
                        schedule_text = "bursty flips";
                        break;
                    case schedule_kind::none:
                        schedule_text = "fixed guard";
                        break;
                }
                return "reactive interruption (" + std::to_string(scenario.tree_size_nodes) + ", " + schedule_text + ")";
            }
            if (scenario.group_id == "B5") {
                switch (scenario.lifecycle) {
                    case lifecycle_phase::parse_text:
                        return "parse DSL (" + std::to_string(scenario.tree_size_nodes) + " nodes)";
                    case lifecycle_phase::compile_definition:
                        return "compile BT (" + std::to_string(scenario.tree_size_nodes) + " nodes)";
                    case lifecycle_phase::instantiate_one:
                        return "instantiate 1 (" + std::to_string(scenario.tree_size_nodes) + " nodes)";
                    case lifecycle_phase::instantiate_hundred:
                        return "instantiate 100 (" + std::to_string(scenario.tree_size_nodes) + " nodes)";
                    case lifecycle_phase::load_binary:
                        return "load binary (" + std::to_string(scenario.tree_size_nodes) + " nodes)";
                    case lifecycle_phase::load_dsl:
                        return "load DSL (" + std::to_string(scenario.tree_size_nodes) + " nodes)";
                    case lifecycle_phase::none:
                        break;
                }
            }
            if (scenario.group_id == "B6") {
                return scenario.kind == benchmark_kind::reactive_interrupt ? "reactive logging trace" : "static logging trace";
            }
            break;
    }
    return scenario.scenario_id;
}

std::unique_ptr<runtime_adapter> make_runtime_adapter(const std::string& runtime_name) {
    if (runtime_name == "muesli") {
        return std::make_unique<muesli_adapter>();
    }
    throw std::invalid_argument("unsupported benchmark runtime: " + runtime_name);
}

std::size_t estimate_timed_ticks(std::size_t warmup_ticks,
                                 std::chrono::steady_clock::duration warmup_elapsed,
                                 std::chrono::milliseconds run_duration) {
    if (warmup_ticks == 0u || warmup_elapsed <= std::chrono::steady_clock::duration::zero()) {
        return 4096u;
    }

    const double warmup_seconds = std::chrono::duration<double>(warmup_elapsed).count();
    const double run_seconds = std::chrono::duration<double>(run_duration).count();
    const double ticks_per_second = static_cast<double>(warmup_ticks) / warmup_seconds;
    const double estimate = ticks_per_second * run_seconds * 1.5;
    return static_cast<std::size_t>(estimate) + 128u;
}

run_summary_row make_base_run_row(const environment_info& environment,
                                  const runtime_adapter& adapter,
                                  const scenario_definition& scenario,
                                  const tree_fixture& fixture,
                                  std::size_t repetition) {
    run_summary_row row;
    row.schema_version = std::string(kSchemaVersion);
    row.benchmark_suite_version = std::string(kBenchmarkSuiteVersion);
    row.timestamp_utc = timestamp_utc_now();
    row.machine_id = environment.machine_id;
    row.hostname = environment.hostname;
    row.os_name = environment.os_name;
    row.os_version = environment.os_version;
    row.kernel_version = environment.kernel_version;
    row.cpu_model = environment.cpu_model;
    row.physical_cores = environment.physical_cores;
    row.logical_cores = environment.logical_cores;
    row.compiler_name = environment.compiler_name;
    row.compiler_version = environment.compiler_version;
    row.build_type = environment.build_type;
    row.build_flags = environment.build_flags;
    row.runtime_name = adapter.name();
    row.runtime_version = adapter.version();
    row.runtime_commit = adapter.commit();
    row.harness_commit = environment.harness_commit;
    row.scenario_id = scenario.scenario_id;
    row.group_id = scenario.group_id;
    row.tree_family = std::string(tree_family_name(scenario.family));
    row.tree_size_nodes = fixture.node_count;
    row.leaf_count = fixture.leaf_count;
    row.async_leaf_count = fixture.async_leaf_count;
    row.blackboard_mode = "";
    row.payload_class = "";
    row.logging_mode = std::string(logging_mode_name(scenario.logging));
    row.repetition_index = repetition;
    row.seed = scenario.seed;
    row.replay_bytes_total = std::nullopt;
    return row;
}

aggregate_summary_row build_aggregate_row(const environment_info& environment,
                                          const scenario_definition& scenario,
                                          const std::vector<run_summary_row>& rows) {
    std::vector<std::uint64_t> medians;
    std::vector<std::uint64_t> p95s;
    std::vector<std::uint64_t> p99s;
    std::vector<std::uint64_t> p999s;
    std::vector<std::uint64_t> maxes;
    std::vector<double> jitter_ratios;
    std::vector<double> throughput;
    std::vector<std::uint64_t> alloc_counts;
    std::vector<std::uint64_t> alloc_bytes;
    std::vector<std::uint64_t> rss_peaks;
    std::vector<std::uint64_t> interrupt_medians;
    std::vector<std::uint64_t> cancel_medians;

    std::size_t semantic_error_runs = 0u;
    for (const run_summary_row& row : rows) {
        medians.push_back(row.latency_ns_median);
        p95s.push_back(row.latency_ns_p95);
        p99s.push_back(row.latency_ns_p99);
        p999s.push_back(row.latency_ns_p999);
        maxes.push_back(row.latency_ns_max);
        jitter_ratios.push_back(row.jitter_ratio_p99_over_median);
        throughput.push_back(row.ticks_per_second);
        alloc_counts.push_back(row.alloc_count_total);
        alloc_bytes.push_back(row.alloc_bytes_total);
        rss_peaks.push_back(row.rss_bytes_peak);
        if (row.interrupt_latency_ns_median.has_value()) {
            interrupt_medians.push_back(*row.interrupt_latency_ns_median);
        }
        if (row.cancel_latency_ns_median.has_value()) {
            cancel_medians.push_back(*row.cancel_latency_ns_median);
        }
        if (row.semantic_errors != 0u) {
            ++semantic_error_runs;
        }
    }

    aggregate_summary_row aggregate;
    aggregate.schema_version = std::string(kSchemaVersion);
    aggregate.benchmark_suite_version = std::string(kBenchmarkSuiteVersion);
    aggregate.timestamp_utc = timestamp_utc_now();
    aggregate.machine_id = environment.machine_id;
    aggregate.runtime_name = environment.runtime_name;
    aggregate.runtime_version = environment.runtime_version;
    aggregate.runtime_commit = environment.runtime_commit;
    aggregate.scenario_id = scenario.scenario_id;
    aggregate.repetitions = rows.size();
    aggregate.run_seconds = rows.empty() ? 0.0 : rows.front().run_seconds;
    aggregate.latency_ns_median_of_medians = percentile_u64(medians, 0.50);
    aggregate.latency_ns_p95_of_runs = percentile_u64(p95s, 0.95);
    aggregate.latency_ns_p99_of_runs = percentile_u64(p99s, 0.99);
    aggregate.latency_ns_p999_of_runs = percentile_u64(p999s, 0.999);
    aggregate.latency_ns_max_of_runs = percentile_u64(maxes, 1.0);
    aggregate.jitter_ratio_p99_over_median_of_runs = percentile_double(jitter_ratios, 0.50);
    aggregate.ticks_per_second_median = percentile_double(throughput, 0.50);
    aggregate.ticks_per_second_mean = mean_double(throughput);
    aggregate.ticks_per_second_stddev = stddev_double(throughput);
    if (!interrupt_medians.empty()) {
        aggregate.interrupt_latency_ns_median_of_medians = percentile_u64(interrupt_medians, 0.50);
    }
    if (!cancel_medians.empty()) {
        aggregate.cancel_latency_ns_median_of_medians = percentile_u64(cancel_medians, 0.50);
    }
    aggregate.alloc_count_total_median = percentile_u64(alloc_counts, 0.50);
    aggregate.alloc_bytes_total_median = percentile_u64(alloc_bytes, 0.50);
    aggregate.rss_bytes_peak_max = percentile_u64(rss_peaks, 1.0);
    aggregate.semantic_error_runs = semantic_error_runs;
    return aggregate;
}

environment_metadata_row build_environment_row(const environment_info& environment) {
    environment_metadata_row row;
    row.schema_version = std::string(kSchemaVersion);
    row.benchmark_suite_version = std::string(kBenchmarkSuiteVersion);
    row.timestamp_utc = timestamp_utc_now();
    row.machine_id = environment.machine_id;
    row.hostname = environment.hostname;
    row.os_name = environment.os_name;
    row.os_version = environment.os_version;
    row.kernel_version = environment.kernel_version;
    row.cpu_model = environment.cpu_model;
    row.cpu_arch = environment.cpu_arch;
    row.physical_cores = environment.physical_cores;
    row.logical_cores = environment.logical_cores;
    row.cpu_governor = environment.cpu_governor;
    row.cpu_pinning = environment.cpu_pinning;
    row.ram_bytes_total = environment.ram_bytes_total;
    row.compiler_name = environment.compiler_name;
    row.compiler_version = environment.compiler_version;
    row.build_type = environment.build_type;
    row.build_flags = environment.build_flags;
    row.runtime_name = environment.runtime_name;
    row.runtime_version = environment.runtime_version;
    row.runtime_commit = environment.runtime_commit;
    row.harness_commit = environment.harness_commit;
    row.clock_source = environment.clock_source;
    row.allocator_mode = environment.allocator_mode;
    row.notes = environment.notes;
    return row;
}

}  // namespace

run_result benchmark_runner::run(const run_request& request) const {
    if (request.scenarios.empty()) {
        throw std::invalid_argument("benchmark runner: no scenarios selected");
    }

    std::vector<scenario_definition> scenarios;
    scenarios.reserve(request.scenarios.size());
    std::chrono::milliseconds total_expected_duration{0};
    for (const scenario_definition& scenario_base : request.scenarios) {
        scenario_definition scenario = apply_overrides(scenario_base, request);
        total_expected_duration += expected_duration_for(scenario);
        scenarios.push_back(std::move(scenario));
    }

    std::unique_ptr<runtime_adapter> adapter = make_runtime_adapter(request.runtime_name);
    environment_info environment = collect_environment();
    environment.runtime_name = adapter->name();
    environment.runtime_version = adapter->version();
    environment.runtime_commit = adapter->commit();
    csv_writer writer;

    run_result result;
    result.output_dir = request.output_dir.empty() ? default_results_root() / timestamp_utc_slug_now() : request.output_dir;
    result.environment_row = build_environment_row(environment);
    std::filesystem::create_directories(result.output_dir);
    if (request.progress_callback) {
        request.progress_callback(progress_event{
            .kind = progress_event_kind::suite_started,
            .total_scenarios = scenarios.size(),
            .scenario_index = 0,
            .description = "",
            .started_at_local_minute = local_time_to_minute_now(),
            .expected_duration_text = format_duration_compact(total_expected_duration),
        });
    }
    if (std::any_of(scenarios.begin(),
                    scenarios.end(),
                    [](const scenario_definition& scenario) { return scenario.capture_tick_trace; })) {
        std::filesystem::remove(result.output_dir / "jitter_trace.csv");
    }

    for (std::size_t scenario_index = 0; scenario_index < scenarios.size(); ++scenario_index) {
        const scenario_definition& scenario = scenarios[scenario_index];
        if (request.progress_callback) {
            request.progress_callback(progress_event{
                .kind = progress_event_kind::scenario_started,
                .total_scenarios = scenarios.size(),
                .scenario_index = scenario_index + 1u,
                .description = scenario_description(scenario),
                .started_at_local_minute = local_time_to_minute_now(),
                .expected_duration_text = format_duration_compact(expected_duration_for(scenario)),
            });
        }
        const tree_fixture fixture = make_fixture(scenario);
        std::vector<run_summary_row> scenario_rows;
        scenario_rows.reserve(scenario.timing.repetitions);

        if (scenario.kind == benchmark_kind::compile_lifecycle) {
            const std::filesystem::path scratch_dir = result.output_dir / ".scratch" / scenario.scenario_id;
            std::unique_ptr<runtime_adapter::lifecycle_case> lifecycle =
                adapter->new_lifecycle_case(fixture, scenario, scratch_dir);
            lifecycle->prime();

            for (std::size_t repetition = 0; repetition < scenario.timing.repetitions; ++repetition) {
                const lifecycle_sample sample = lifecycle->run_once();
                const double run_seconds = static_cast<double>(sample.duration_ns) / 1'000'000'000.0;
                const double throughput =
                    run_seconds > 0.0 ? static_cast<double>(sample.operations_total) / run_seconds : 0.0;

                run_summary_row row = make_base_run_row(environment, *adapter, scenario, fixture, repetition);
                row.warmup_seconds = 0.0;
                row.run_seconds = run_seconds;
                row.ticks_total = static_cast<std::uint64_t>(sample.operations_total);
                row.ticks_per_second = throughput;
                row.latency_ns_median = sample.duration_ns;
                row.latency_ns_p95 = sample.duration_ns;
                row.latency_ns_p99 = sample.duration_ns;
                row.latency_ns_p999 = sample.duration_ns;
                row.latency_ns_max = sample.duration_ns;
                row.jitter_ratio_p99_over_median = sample.duration_ns == 0u ? 0.0 : 1.0;
                row.alloc_count_total = sample.alloc_count_total;
                row.alloc_bytes_total = sample.alloc_bytes_total;
                row.rss_bytes_peak = peak_rss_bytes();
                row.log_events_total = 0u;
                row.log_bytes_total = 0u;
                row.semantic_errors = sample.semantic_errors;
                row.notes = sample.notes;

                scenario_rows.push_back(row);
                result.run_rows.push_back(row);
            }

            std::filesystem::remove_all(scratch_dir);
            result.aggregate_rows.push_back(build_aggregate_row(environment, scenario, scenario_rows));
            continue;
        }

        std::unique_ptr<runtime_adapter::compiled_tree> compiled = adapter->compile_tree(fixture);

        for (std::size_t repetition = 0; repetition < scenario.timing.repetitions; ++repetition) {
            std::unique_ptr<runtime_adapter::instance_handle> instance = adapter->new_instance(*compiled, scenario);

            std::size_t warmup_ticks = 0u;
            const auto warmup_started = std::chrono::steady_clock::now();
            if (scenario.timing.warmup > std::chrono::milliseconds::zero()) {
                do {
                    (void)adapter->tick(*instance);
                    ++warmup_ticks;
                } while (std::chrono::steady_clock::now() - warmup_started < scenario.timing.warmup);
            }
            const auto warmup_elapsed = std::chrono::steady_clock::now() - warmup_started;
            const std::size_t estimated_ticks =
                estimate_timed_ticks(warmup_ticks, warmup_elapsed, scenario.timing.run);

            adapter->prepare_for_timed_run(*instance, estimated_ticks, repetition);

            std::vector<std::uint64_t> latencies_ns;
            latencies_ns.reserve(estimated_ticks);

            allocation_tracker::reset();
            allocation_tracker::set_enabled(true);

            std::uint64_t ticks_total = 0u;
            const auto run_started = std::chrono::steady_clock::now();
            if (scenario.timing.run > std::chrono::milliseconds::zero()) {
                do {
                    const auto tick_started = std::chrono::steady_clock::now();
                    (void)adapter->tick(*instance);
                    const auto tick_finished = std::chrono::steady_clock::now();
                    latencies_ns.push_back(static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(tick_finished - tick_started).count()));
                    ++ticks_total;
                } while (std::chrono::steady_clock::now() - run_started < scenario.timing.run);
            }
            const auto run_finished = std::chrono::steady_clock::now();

            allocation_tracker::set_enabled(false);
            adapter->teardown(*instance);

            const auto allocations = allocation_tracker::read();
            const run_counters counters = adapter->read_counters(*instance);
            const latency_summary latency = summarise_latencies(latencies_ns);
            const double run_seconds = std::chrono::duration<double>(run_finished - run_started).count();
            const double warmup_seconds = std::chrono::duration<double>(warmup_elapsed).count();
            const double ticks_per_second = run_seconds > 0.0 ? static_cast<double>(ticks_total) / run_seconds : 0.0;

            if (scenario.capture_tick_trace) {
                writer.append_jitter_trace(result.output_dir, scenario.scenario_id, repetition, latencies_ns);
            }

            run_summary_row row = make_base_run_row(environment, *adapter, scenario, fixture, repetition);
            row.warmup_seconds = warmup_seconds;
            row.run_seconds = run_seconds;
            row.ticks_total = ticks_total;
            row.ticks_per_second = ticks_per_second;
            row.latency_ns_median = latency.median;
            row.latency_ns_p95 = latency.p95;
            row.latency_ns_p99 = latency.p99;
            row.latency_ns_p999 = latency.p999;
            row.latency_ns_max = latency.max;
            row.jitter_ratio_p99_over_median = latency.jitter_ratio_p99_over_median;
            if (!counters.interrupt_latency_ns.empty()) {
                row.interrupt_latency_ns_median = percentile_u64(counters.interrupt_latency_ns, 0.50);
                row.interrupt_latency_ns_p95 = percentile_u64(counters.interrupt_latency_ns, 0.95);
            }
            if (!counters.cancel_latency_ns.empty()) {
                row.cancel_latency_ns_median = percentile_u64(counters.cancel_latency_ns, 0.50);
            }
            row.alloc_count_total = allocations.allocation_count;
            row.alloc_bytes_total = allocations.allocation_bytes;
            row.rss_bytes_peak = peak_rss_bytes();
            row.log_events_total = counters.log_events_total;
            row.log_bytes_total = counters.log_bytes_total;
            row.semantic_errors = counters.semantic_errors;
            if (scenario.group_id == "A2" && latency.median != 0u && latency.p99 >= latency.median * 5u) {
                row.notes = "p99 exceeded 5x median";
            }

            scenario_rows.push_back(row);
            result.run_rows.push_back(row);
        }

        result.aggregate_rows.push_back(build_aggregate_row(environment, scenario, scenario_rows));
    }

    writer.write_summaries(result.output_dir, result.run_rows, result.aggregate_rows, result.environment_row);
    return result;
}

}  // namespace muesli_bt::bench
