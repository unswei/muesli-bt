#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "harness/runner.hpp"

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text;
}

void test_catalogue_contains_new_benchmarks() {
    using namespace muesli_bt::bench;

    check(find_scenario("A1-single-leaf-off") != nullptr, "missing A1 baseline scenario");
    check(find_scenario("A2-alt-255-jitter-off") != nullptr, "missing A2 jitter scenario");
    check(find_scenario("B6-reactive-31-flip20-fulltrace") != nullptr, "missing B6 logging scenario");
}

void test_runner_writes_expected_csv_files() {
    using namespace muesli_bt::bench;

    const std::filesystem::path output_dir =
        std::filesystem::temp_directory_path() / "muesli_bt_bench_smoke";
    std::filesystem::remove_all(output_dir);

    run_request request;
    request.output_dir = output_dir;
    request.scenarios.push_back(*find_scenario("A1-single-leaf-off"));
    request.warmup_override = std::chrono::milliseconds(10);
    request.run_override = std::chrono::milliseconds(20);
    request.repetitions_override = 2u;

    benchmark_runner runner;
    const run_result result = runner.run(request);

    check(std::filesystem::exists(result.output_dir / "run_summary.csv"), "run_summary.csv was not written");
    check(std::filesystem::exists(result.output_dir / "aggregate_summary.csv"), "aggregate_summary.csv was not written");
    check(std::filesystem::exists(result.output_dir / "environment_metadata.csv"),
          "environment_metadata.csv was not written");

    const std::string run_summary = read_text(result.output_dir / "run_summary.csv");
    check(run_summary.find("A1-single-leaf-off") != std::string::npos, "run summary missing scenario id");

    const std::string environment_metadata = read_text(result.output_dir / "environment_metadata.csv");
    check(environment_metadata.find("clock_source") != std::string::npos, "environment metadata header missing");
    check(environment_metadata.find("muesli-bt") != std::string::npos, "environment metadata missing runtime name");
}

void test_fulltrace_mode_emits_log_bytes() {
    using namespace muesli_bt::bench;

    const std::filesystem::path output_dir =
        std::filesystem::temp_directory_path() / "muesli_bt_bench_log_smoke";
    std::filesystem::remove_all(output_dir);

    run_request request;
    request.output_dir = output_dir;
    request.scenarios.push_back(*find_scenario("A1-single-leaf-log"));
    request.warmup_override = std::chrono::milliseconds(5);
    request.run_override = std::chrono::milliseconds(10);
    request.repetitions_override = 1u;

    benchmark_runner runner;
    const run_result result = runner.run(request);

    check(result.run_rows.size() == 1u, "expected one run row for A1 log smoke test");
    check(result.run_rows.front().log_events_total > 0u, "fulltrace mode should emit log events");
    check(result.run_rows.front().log_bytes_total > 0u, "fulltrace mode should emit log bytes");
}

void test_jitter_trace_is_written_for_a2() {
    using namespace muesli_bt::bench;

    const std::filesystem::path output_dir =
        std::filesystem::temp_directory_path() / "muesli_bt_bench_jitter_smoke";
    std::filesystem::remove_all(output_dir);

    run_request request;
    request.output_dir = output_dir;
    request.scenarios.push_back(*find_scenario("A2-alt-255-jitter-off"));
    request.warmup_override = std::chrono::milliseconds(5);
    request.run_override = std::chrono::milliseconds(10);
    request.repetitions_override = 1u;

    benchmark_runner runner;
    const run_result result = runner.run(request);

    check(std::filesystem::exists(result.output_dir / "jitter_trace.csv"), "jitter_trace.csv was not written");
    const std::string trace = read_text(result.output_dir / "jitter_trace.csv");
    check(trace.find("scenario_id,repetition_index,tick_index,latency_ns") != std::string::npos,
          "jitter trace header missing");
}

void test_runner_emits_progress_events() {
    using namespace muesli_bt::bench;

    std::vector<progress_event> events;
    run_request request;
    request.output_dir = std::filesystem::temp_directory_path() / "muesli_bt_bench_progress_smoke";
    request.scenarios.push_back(*find_scenario("A1-single-leaf-off"));
    request.warmup_override = std::chrono::milliseconds(5);
    request.run_override = std::chrono::milliseconds(10);
    request.repetitions_override = 1u;
    request.progress_callback = [&events](const progress_event& event) { events.push_back(event); };

    benchmark_runner runner;
    (void)runner.run(request);

    check(events.size() == 2u, "expected suite-started and scenario-started progress events");
    check(events[0].kind == progress_event_kind::suite_started, "first progress event kind mismatch");
    check(events[0].total_scenarios == 1u, "suite-started total scenario count mismatch");
    check(events[1].kind == progress_event_kind::scenario_started, "second progress event kind mismatch");
    check(events[1].description.find("single-leaf") != std::string::npos, "scenario progress description mismatch");
    check(!events[1].started_at_local_minute.empty(), "scenario progress timestamp missing");
}

}  // namespace

int main() {
    test_catalogue_contains_new_benchmarks();
    test_runner_writes_expected_csv_files();
    test_fulltrace_mode_emits_log_bytes();
    test_jitter_trace_is_written_for_a2();
    test_runner_emits_progress_events();
    return 0;
}
