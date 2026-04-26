#include "harness/csv_writer.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace muesli_bt::bench {
namespace {

std::string csv_escape(std::string_view value) {
    bool needs_quotes = false;
    for (const char ch : value) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return std::string(value);
    }

    std::string escaped;
    escaped.reserve(value.size() + 4u);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::string format_double(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

template <typename T>
std::string format_value(const T& value) {
    return std::to_string(value);
}

template <>
std::string format_value<double>(const double& value) {
    return format_double(value);
}

template <typename T>
std::string format_optional(const std::optional<T>& value) {
    if (!value.has_value()) {
        return "";
    }
    return format_value(*value);
}

void write_csv_line(std::ofstream& out, const std::vector<std::string>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0u) {
            out << ',';
        }
        out << csv_escape(values[index]);
    }
    out << '\n';
}

}  // namespace

void csv_writer::write_summaries(const std::filesystem::path& output_dir,
                                 const std::vector<run_summary_row>& run_rows,
                                 const std::vector<aggregate_summary_row>& aggregate_rows,
                                 const environment_metadata_row& environment_row) const {
    std::filesystem::create_directories(output_dir);

    {
        std::ofstream out(output_dir / "run_summary.csv", std::ios::trunc);
        write_csv_line(out,
                       {"schema_version",
                        "benchmark_suite_version",
                        "timestamp_utc",
                        "machine_id",
                        "hostname",
                        "os_name",
                        "os_version",
                        "kernel_version",
                        "cpu_model",
                        "physical_cores",
                        "logical_cores",
                        "compiler_name",
                        "compiler_version",
                        "build_type",
                        "build_flags",
                        "runtime_name",
                        "runtime_version",
                        "runtime_commit",
                        "harness_commit",
                        "scenario_id",
                        "group_id",
                        "tree_family",
                        "tree_size_nodes",
                        "leaf_count",
                        "async_leaf_count",
                        "blackboard_mode",
                        "payload_class",
                        "logging_mode",
                        "repetition_index",
                        "seed",
                        "warmup_seconds",
                        "run_seconds",
                        "ticks_total",
                        "ticks_per_second",
                        "latency_ns_median",
                        "latency_ns_p95",
                        "latency_ns_p99",
                        "latency_ns_p999",
                        "latency_ns_max",
                        "jitter_ratio_p99_over_median",
                        "interrupt_latency_ns_median",
                        "interrupt_latency_ns_p95",
                        "cancel_latency_ns_median",
                        "alloc_count_total",
                        "alloc_bytes_total",
                        "rss_bytes_peak",
                        "log_events_total",
                        "log_bytes_total",
                        "replay_bytes_total",
                        "gc_collections_total",
                        "gc_pause_ns_p50",
                        "gc_pause_ns_p95",
                        "gc_pause_ns_p99",
                        "gc_pause_ns_p999",
                        "heap_live_bytes_start",
                        "heap_live_bytes_end",
                        "heap_live_bytes_slope_per_tick",
                        "rss_bytes_start",
                        "rss_bytes_end",
                        "rss_bytes_slope_per_tick",
                        "event_log_bytes_per_tick",
                        "deadline_miss_count",
                        "deadline_miss_rate",
                        "fallback_activation_count",
                        "fallback_activation_rate",
                        "dropped_completion_count",
                        "dropped_completion_rate",
                        "semantic_errors",
                        "notes"});

        for (const run_summary_row& row : run_rows) {
            write_csv_line(out,
                           {row.schema_version,
                            row.benchmark_suite_version,
                            row.timestamp_utc,
                            row.machine_id,
                            row.hostname,
                            row.os_name,
                            row.os_version,
                            row.kernel_version,
                            row.cpu_model,
                            format_value(row.physical_cores),
                            format_value(row.logical_cores),
                            row.compiler_name,
                            row.compiler_version,
                            row.build_type,
                            row.build_flags,
                            row.runtime_name,
                            row.runtime_version,
                            row.runtime_commit,
                            row.harness_commit,
                            row.scenario_id,
                            row.group_id,
                            row.tree_family,
                            format_value(row.tree_size_nodes),
                            format_value(row.leaf_count),
                            format_value(row.async_leaf_count),
                            row.blackboard_mode,
                            row.payload_class,
                            row.logging_mode,
                            format_value(row.repetition_index),
                            format_value(row.seed),
                            format_value(row.warmup_seconds),
                            format_value(row.run_seconds),
                            format_value(row.ticks_total),
                            format_value(row.ticks_per_second),
                            format_value(row.latency_ns_median),
                            format_value(row.latency_ns_p95),
                            format_value(row.latency_ns_p99),
                            format_value(row.latency_ns_p999),
                            format_value(row.latency_ns_max),
                            format_value(row.jitter_ratio_p99_over_median),
                            format_optional(row.interrupt_latency_ns_median),
                            format_optional(row.interrupt_latency_ns_p95),
                            format_optional(row.cancel_latency_ns_median),
                            format_value(row.alloc_count_total),
                            format_value(row.alloc_bytes_total),
                            format_value(row.rss_bytes_peak),
                            format_value(row.log_events_total),
                            format_value(row.log_bytes_total),
                            format_optional(row.replay_bytes_total),
                            format_value(row.gc_collections_total),
                            format_value(row.gc_pause_ns_p50),
                            format_value(row.gc_pause_ns_p95),
                            format_value(row.gc_pause_ns_p99),
                            format_value(row.gc_pause_ns_p999),
                            format_value(row.heap_live_bytes_start),
                            format_value(row.heap_live_bytes_end),
                            format_value(row.heap_live_bytes_slope_per_tick),
                            format_value(row.rss_bytes_start),
                            format_value(row.rss_bytes_end),
                            format_value(row.rss_bytes_slope_per_tick),
                            format_value(row.event_log_bytes_per_tick),
                            format_value(row.deadline_miss_count),
                            format_value(row.deadline_miss_rate),
                            format_value(row.fallback_activation_count),
                            format_value(row.fallback_activation_rate),
                            format_value(row.dropped_completion_count),
                            format_value(row.dropped_completion_rate),
                            format_value(row.semantic_errors),
                            row.notes});
        }
    }

    {
        std::ofstream out(output_dir / "aggregate_summary.csv", std::ios::trunc);
        write_csv_line(out,
                       {"schema_version",
                        "benchmark_suite_version",
                        "timestamp_utc",
                        "machine_id",
                        "runtime_name",
                        "runtime_version",
                        "runtime_commit",
                        "scenario_id",
                        "repetitions",
                        "run_seconds",
                        "latency_ns_median_of_medians",
                        "latency_ns_p95_of_runs",
                        "latency_ns_p99_of_runs",
                        "latency_ns_p999_of_runs",
                        "latency_ns_max_of_runs",
                        "jitter_ratio_p99_over_median_of_runs",
                        "ticks_per_second_median",
                        "ticks_per_second_mean",
                        "ticks_per_second_stddev",
                        "interrupt_latency_ns_median_of_medians",
                        "cancel_latency_ns_median_of_medians",
                        "alloc_count_total_median",
                        "alloc_bytes_total_median",
                        "rss_bytes_peak_max",
                        "gc_collections_total_median",
                        "gc_pause_ns_p50_of_runs",
                        "gc_pause_ns_p95_of_runs",
                        "gc_pause_ns_p99_of_runs",
                        "gc_pause_ns_p999_of_runs",
                        "heap_live_bytes_slope_per_tick_median",
                        "rss_bytes_slope_per_tick_median",
                        "event_log_bytes_per_tick_median",
                        "deadline_miss_count_median",
                        "deadline_miss_rate_median",
                        "fallback_activation_count_median",
                        "fallback_activation_rate_median",
                        "dropped_completion_count_median",
                        "dropped_completion_rate_median",
                        "semantic_error_runs",
                        "notes"});

        for (const aggregate_summary_row& row : aggregate_rows) {
            write_csv_line(out,
                           {row.schema_version,
                            row.benchmark_suite_version,
                            row.timestamp_utc,
                            row.machine_id,
                            row.runtime_name,
                            row.runtime_version,
                            row.runtime_commit,
                            row.scenario_id,
                            format_value(row.repetitions),
                            format_value(row.run_seconds),
                            format_value(row.latency_ns_median_of_medians),
                            format_value(row.latency_ns_p95_of_runs),
                            format_value(row.latency_ns_p99_of_runs),
                            format_value(row.latency_ns_p999_of_runs),
                            format_value(row.latency_ns_max_of_runs),
                            format_value(row.jitter_ratio_p99_over_median_of_runs),
                            format_value(row.ticks_per_second_median),
                            format_value(row.ticks_per_second_mean),
                            format_value(row.ticks_per_second_stddev),
                            format_optional(row.interrupt_latency_ns_median_of_medians),
                            format_optional(row.cancel_latency_ns_median_of_medians),
                            format_value(row.alloc_count_total_median),
                            format_value(row.alloc_bytes_total_median),
                            format_value(row.rss_bytes_peak_max),
                            format_value(row.gc_collections_total_median),
                            format_value(row.gc_pause_ns_p50_of_runs),
                            format_value(row.gc_pause_ns_p95_of_runs),
                            format_value(row.gc_pause_ns_p99_of_runs),
                            format_value(row.gc_pause_ns_p999_of_runs),
                            format_value(row.heap_live_bytes_slope_per_tick_median),
                            format_value(row.rss_bytes_slope_per_tick_median),
                            format_value(row.event_log_bytes_per_tick_median),
                            format_value(row.deadline_miss_count_median),
                            format_value(row.deadline_miss_rate_median),
                            format_value(row.fallback_activation_count_median),
                            format_value(row.fallback_activation_rate_median),
                            format_value(row.dropped_completion_count_median),
                            format_value(row.dropped_completion_rate_median),
                            format_value(row.semantic_error_runs),
                            row.notes});
        }
    }

    {
        std::ofstream out(output_dir / "environment_metadata.csv", std::ios::trunc);
        write_csv_line(out,
                       {"schema_version",
                        "benchmark_suite_version",
                        "timestamp_utc",
                        "machine_id",
                        "hostname",
                        "os_name",
                        "os_version",
                        "kernel_version",
                        "cpu_model",
                        "cpu_arch",
                        "physical_cores",
                        "logical_cores",
                        "cpu_governor",
                        "cpu_pinning",
                        "ram_bytes_total",
                        "compiler_name",
                        "compiler_version",
                        "build_type",
                        "build_flags",
                        "runtime_name",
                        "runtime_version",
                        "runtime_commit",
                        "harness_commit",
                        "clock_source",
                        "allocator_mode",
                        "notes"});
        write_csv_line(out,
                       {environment_row.schema_version,
                        environment_row.benchmark_suite_version,
                        environment_row.timestamp_utc,
                        environment_row.machine_id,
                        environment_row.hostname,
                        environment_row.os_name,
                        environment_row.os_version,
                        environment_row.kernel_version,
                        environment_row.cpu_model,
                        environment_row.cpu_arch,
                        format_value(environment_row.physical_cores),
                        format_value(environment_row.logical_cores),
                        environment_row.cpu_governor,
                        environment_row.cpu_pinning,
                        format_value(environment_row.ram_bytes_total),
                        environment_row.compiler_name,
                        environment_row.compiler_version,
                        environment_row.build_type,
                        environment_row.build_flags,
                        environment_row.runtime_name,
                        environment_row.runtime_version,
                        environment_row.runtime_commit,
                        environment_row.harness_commit,
                        environment_row.clock_source,
                        environment_row.allocator_mode,
                        environment_row.notes});
    }
}

void csv_writer::append_jitter_trace(const std::filesystem::path& output_dir,
                                     std::string_view scenario_id,
                                     std::size_t repetition_index,
                                     std::span<const std::uint64_t> latencies_ns) const {
    std::filesystem::create_directories(output_dir);
    const std::filesystem::path trace_path = output_dir / "jitter_trace.csv";
    const bool write_header = !std::filesystem::exists(trace_path);
    std::ofstream out(trace_path, std::ios::app);
    if (write_header) {
        write_csv_line(out, {"scenario_id", "repetition_index", "tick_index", "latency_ns"});
    }

    for (std::size_t index = 0; index < latencies_ns.size(); ++index) {
        write_csv_line(out,
                       {std::string(scenario_id),
                        format_value(repetition_index),
                        format_value(index + 1u),
                        format_value(latencies_ns[index])});
    }
}

}  // namespace muesli_bt::bench
