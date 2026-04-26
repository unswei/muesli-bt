#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace muesli_bt::bench {

struct run_summary_row {
    std::string schema_version;
    std::string benchmark_suite_version;
    std::string timestamp_utc;
    std::string machine_id;
    std::string hostname;
    std::string os_name;
    std::string os_version;
    std::string kernel_version;
    std::string cpu_model;
    std::size_t physical_cores = 0;
    std::size_t logical_cores = 0;
    std::string compiler_name;
    std::string compiler_version;
    std::string build_type;
    std::string build_flags;
    std::string runtime_name;
    std::string runtime_version;
    std::string runtime_commit;
    std::string harness_commit;
    std::string scenario_id;
    std::string group_id;
    std::string tree_family;
    std::size_t tree_size_nodes = 0;
    std::size_t leaf_count = 0;
    std::size_t async_leaf_count = 0;
    std::string blackboard_mode;
    std::string payload_class;
    std::string logging_mode;
    std::size_t repetition_index = 0;
    std::uint64_t seed = 0;
    double warmup_seconds = 0.0;
    double run_seconds = 0.0;
    std::uint64_t ticks_total = 0;
    double ticks_per_second = 0.0;
    std::uint64_t latency_ns_median = 0;
    std::uint64_t latency_ns_p95 = 0;
    std::uint64_t latency_ns_p99 = 0;
    std::uint64_t latency_ns_p999 = 0;
    std::uint64_t latency_ns_max = 0;
    double jitter_ratio_p99_over_median = 0.0;
    std::optional<std::uint64_t> interrupt_latency_ns_median;
    std::optional<std::uint64_t> interrupt_latency_ns_p95;
    std::optional<std::uint64_t> cancel_latency_ns_median;
    std::uint64_t alloc_count_total = 0;
    std::uint64_t alloc_bytes_total = 0;
    std::uint64_t rss_bytes_peak = 0;
    std::uint64_t log_events_total = 0;
    std::uint64_t log_bytes_total = 0;
    std::optional<std::uint64_t> replay_bytes_total;
    std::uint64_t gc_collections_total = 0;
    std::uint64_t gc_pause_ns_p50 = 0;
    std::uint64_t gc_pause_ns_p95 = 0;
    std::uint64_t gc_pause_ns_p99 = 0;
    std::uint64_t gc_pause_ns_p999 = 0;
    std::uint64_t heap_live_bytes_start = 0;
    std::uint64_t heap_live_bytes_end = 0;
    double heap_live_bytes_slope_per_tick = 0.0;
    std::uint64_t rss_bytes_start = 0;
    std::uint64_t rss_bytes_end = 0;
    double rss_bytes_slope_per_tick = 0.0;
    double event_log_bytes_per_tick = 0.0;
    std::uint64_t deadline_miss_count = 0;
    double deadline_miss_rate = 0.0;
    std::uint64_t fallback_activation_count = 0;
    std::uint64_t dropped_completion_count = 0;
    std::uint64_t semantic_errors = 0;
    std::string notes;
};

struct aggregate_summary_row {
    std::string schema_version;
    std::string benchmark_suite_version;
    std::string timestamp_utc;
    std::string machine_id;
    std::string runtime_name;
    std::string runtime_version;
    std::string runtime_commit;
    std::string scenario_id;
    std::size_t repetitions = 0;
    double run_seconds = 0.0;
    std::uint64_t latency_ns_median_of_medians = 0;
    std::uint64_t latency_ns_p95_of_runs = 0;
    std::uint64_t latency_ns_p99_of_runs = 0;
    std::uint64_t latency_ns_p999_of_runs = 0;
    std::uint64_t latency_ns_max_of_runs = 0;
    double jitter_ratio_p99_over_median_of_runs = 0.0;
    double ticks_per_second_median = 0.0;
    double ticks_per_second_mean = 0.0;
    double ticks_per_second_stddev = 0.0;
    std::optional<std::uint64_t> interrupt_latency_ns_median_of_medians;
    std::optional<std::uint64_t> cancel_latency_ns_median_of_medians;
    std::uint64_t alloc_count_total_median = 0;
    std::uint64_t alloc_bytes_total_median = 0;
    std::uint64_t rss_bytes_peak_max = 0;
    std::uint64_t gc_collections_total_median = 0;
    std::uint64_t gc_pause_ns_p50_of_runs = 0;
    std::uint64_t gc_pause_ns_p95_of_runs = 0;
    std::uint64_t gc_pause_ns_p99_of_runs = 0;
    std::uint64_t gc_pause_ns_p999_of_runs = 0;
    double heap_live_bytes_slope_per_tick_median = 0.0;
    double rss_bytes_slope_per_tick_median = 0.0;
    double event_log_bytes_per_tick_median = 0.0;
    double deadline_miss_rate_median = 0.0;
    std::uint64_t fallback_activation_count_median = 0;
    std::uint64_t dropped_completion_count_median = 0;
    std::size_t semantic_error_runs = 0;
    std::string notes;
};

struct environment_metadata_row {
    std::string schema_version;
    std::string benchmark_suite_version;
    std::string timestamp_utc;
    std::string machine_id;
    std::string hostname;
    std::string os_name;
    std::string os_version;
    std::string kernel_version;
    std::string cpu_model;
    std::string cpu_arch;
    std::size_t physical_cores = 0;
    std::size_t logical_cores = 0;
    std::string cpu_governor;
    std::string cpu_pinning;
    std::uint64_t ram_bytes_total = 0;
    std::string compiler_name;
    std::string compiler_version;
    std::string build_type;
    std::string build_flags;
    std::string runtime_name;
    std::string runtime_version;
    std::string runtime_commit;
    std::string harness_commit;
    std::string clock_source;
    std::string allocator_mode;
    std::string notes;
};

class csv_writer {
public:
    void write_summaries(const std::filesystem::path& output_dir,
                         const std::vector<run_summary_row>& run_rows,
                         const std::vector<aggregate_summary_row>& aggregate_rows,
                         const environment_metadata_row& environment_row) const;

    void append_jitter_trace(const std::filesystem::path& output_dir,
                             std::string_view scenario_id,
                             std::size_t repetition_index,
                             std::span<const std::uint64_t> latencies_ns) const;
};

}  // namespace muesli_bt::bench
