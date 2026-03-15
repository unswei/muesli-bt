#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "harness/csv_writer.hpp"
#include "harness/scenario.hpp"

namespace muesli_bt::bench {

enum class progress_event_kind {
    suite_started,
    scenario_started
};

struct progress_event {
    progress_event_kind kind = progress_event_kind::suite_started;
    std::size_t total_scenarios = 0;
    std::size_t scenario_index = 0;
    std::string description;
    std::string started_at_local_minute;
    std::string expected_duration_text;
};

struct run_request {
    std::vector<scenario_definition> scenarios;
    std::string runtime_name = "muesli";
    std::filesystem::path output_dir;
    std::optional<std::chrono::milliseconds> warmup_override;
    std::optional<std::chrono::milliseconds> run_override;
    std::optional<std::size_t> repetitions_override;
    std::optional<std::uint64_t> seed_override;
    std::function<void(const progress_event&)> progress_callback;
};

struct run_result {
    std::filesystem::path output_dir;
    std::vector<run_summary_row> run_rows;
    std::vector<aggregate_summary_row> aggregate_rows;
    environment_metadata_row environment_row;
};

class benchmark_runner {
public:
    run_result run(const run_request& request) const;
};

}  // namespace muesli_bt::bench
