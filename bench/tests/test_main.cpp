#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "bench_config.hpp"
#include "fixtures/tree_factory.hpp"
#include "harness/allocation_tracker.hpp"
#include "harness/runner.hpp"
#include "runtimes/muesli_adapter.hpp"

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
    check(find_scenario("B5-alt-31-compile-off") != nullptr, "missing B5 compile scenario");
    check(find_scenario("B6-reactive-31-flip20-fulltrace") != nullptr, "missing B6 logging scenario");
    check(find_scenario("B7-gc-between-ticks-smoke") != nullptr, "missing B7 GC/memory scenario");
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

void test_b5_lifecycle_benchmarks_run() {
    using namespace muesli_bt::bench;

    const std::filesystem::path output_dir =
        std::filesystem::temp_directory_path() / "muesli_bt_bench_b5_smoke";
    std::filesystem::remove_all(output_dir);

    run_request request;
    request.output_dir = output_dir;
    request.scenarios.push_back(*find_scenario("B5-alt-31-compile-off"));
    request.scenarios.push_back(*find_scenario("B5-alt-31-inst100-off"));
    request.repetitions_override = 1u;

    benchmark_runner runner;
    const run_result result = runner.run(request);

    check(result.run_rows.size() == 2u, "expected two B5 run rows");
    check(result.aggregate_rows.size() == 2u, "expected two B5 aggregate rows");

    const run_summary_row& compile_row = result.run_rows.front();
    check(compile_row.scenario_id == "B5-alt-31-compile-off", "unexpected B5 compile scenario id");
    check(compile_row.latency_ns_median > 0u, "B5 compile latency should be recorded");
    check(compile_row.ticks_total == 1u, "B5 compile phase should report one operation");

    const run_summary_row& inst100_row = result.run_rows.back();
    check(inst100_row.scenario_id == "B5-alt-31-inst100-off", "unexpected B5 inst100 scenario id");
    check(inst100_row.latency_ns_median > 0u, "B5 inst100 latency should be recorded");
    check(inst100_row.ticks_total == 100u, "B5 inst100 phase should report 100 operations");
}

void test_b7_gc_memory_benchmark_runs() {
    using namespace muesli_bt::bench;

    const std::filesystem::path output_dir =
        std::filesystem::temp_directory_path() / "muesli_bt_bench_b7_smoke";
    std::filesystem::remove_all(output_dir);

    run_request request;
    request.output_dir = output_dir;
    request.scenarios.push_back(*find_scenario("B7-gc-between-ticks-smoke"));
    request.warmup_override = std::chrono::milliseconds(5);
    request.run_override = std::chrono::milliseconds(10);
    request.repetitions_override = 1u;

    benchmark_runner runner;
    const run_result result = runner.run(request);

    check(result.run_rows.size() == 1u, "expected one B7 run row");
    check(result.aggregate_rows.size() == 1u, "expected one B7 aggregate row");
    const run_summary_row& row = result.run_rows.front();
    check(row.gc_collections_total > 0u, "B7 should record GC collections");
    check(row.gc_pause_ns_p99 > 0u, "B7 should record GC pause quantiles");
    check(row.log_events_total > 0u, "B7 should record canonical GC event count");
    check(row.event_log_bytes_per_tick > 0.0, "B7 should record event-log bytes per tick");
    check(std::filesystem::exists(output_dir / "B7-gc-between-ticks-smoke" / "rep-0" / "events.jsonl"),
          "B7 should write canonical GC events.jsonl");
}

class fail_on_unwhitelisted_allocation_scope final {
public:
    fail_on_unwhitelisted_allocation_scope() {
        muesli_bt::bench::allocation_tracker::reset();
        muesli_bt::bench::allocation_tracker::set_enabled(true);
        muesli_bt::bench::allocation_tracker::set_fail_on_unwhitelisted_allocation(true);
    }

    fail_on_unwhitelisted_allocation_scope(const fail_on_unwhitelisted_allocation_scope&) = delete;
    fail_on_unwhitelisted_allocation_scope& operator=(const fail_on_unwhitelisted_allocation_scope&) = delete;

    ~fail_on_unwhitelisted_allocation_scope() {
        muesli_bt::bench::allocation_tracker::set_fail_on_unwhitelisted_allocation(false);
        muesli_bt::bench::allocation_tracker::set_enabled(false);
    }
};

void test_allocation_whitelist_allows_explicit_logging_paths_only() {
    using namespace muesli_bt::bench;

    bool unwhitelisted_allocation_failed = false;
    allocation_tracker::snapshot failed_snapshot{};
    {
        fail_on_unwhitelisted_allocation_scope fail_scope;
        try {
            std::string should_fail;
            should_fail.assign(4096u, 'x');
        } catch (const std::bad_alloc&) {
            unwhitelisted_allocation_failed = true;
        }
        failed_snapshot = allocation_tracker::read();
    }
    check(unwhitelisted_allocation_failed, "unwhitelisted allocation should fail while strict allocation mode is active");
    check(failed_snapshot.allocation_failure_count == 1u, "strict allocation mode should record one failed allocation");

    bool whitelisted_allocation_failed = false;
    allocation_tracker::snapshot whitelisted_snapshot{};
    {
        fail_on_unwhitelisted_allocation_scope fail_scope;
        try {
            allocation_tracker::whitelisted_allocation_scope logging_path;
            std::string allowed_logging_buffer;
            allowed_logging_buffer.assign(4096u, 'l');
        } catch (const std::bad_alloc&) {
            whitelisted_allocation_failed = true;
        }
        whitelisted_snapshot = allocation_tracker::read();
    }
    check(!whitelisted_allocation_failed, "explicitly whitelisted logging allocation should be allowed");
    check(whitelisted_snapshot.allocation_failure_count == 0u, "whitelisted allocation should not record a failure");
    check(whitelisted_snapshot.whitelisted_allocation_count > 0u, "whitelisted allocation count should be recorded");
}

void test_precompiled_ticks_fail_on_unwhitelisted_allocations() {
    using namespace muesli_bt::bench;

    const scenario_definition* scenario = find_scenario("B1-alt-255-base-off");
    check(scenario != nullptr, "missing B1 strict allocation scenario");

    muesli_adapter adapter;
    const tree_fixture fixture = make_fixture(*scenario);
    std::unique_ptr<runtime_adapter::compiled_tree> compiled = adapter.compile_tree(fixture);
    std::unique_ptr<runtime_adapter::instance_handle> instance = adapter.new_instance(*compiled, *scenario);
    adapter.prepare_for_timed_run(*instance, 512u, 0u);

    bool tick_failed_with_bad_alloc = false;
    bool tick_returned_wrong_status = false;
    allocation_tracker::snapshot snapshot{};
    {
        fail_on_unwhitelisted_allocation_scope fail_scope;
        try {
            for (std::size_t i = 0; i < 64u; ++i) {
                const run_status status = adapter.tick(*instance);
                tick_returned_wrong_status = tick_returned_wrong_status || status != run_status::success;
            }
        } catch (const std::bad_alloc&) {
            tick_failed_with_bad_alloc = true;
        }
        snapshot = allocation_tracker::read();
    }

    check(!tick_failed_with_bad_alloc, "precompiled hot-path tick allocated outside the logging whitelist");
    check(!tick_returned_wrong_status, "strict allocation precompiled tick should return success");
    check(snapshot.allocation_count == 0u, "precompiled hot-path tick should perform zero allocations");
    check(snapshot.allocation_bytes == 0u, "precompiled hot-path tick should allocate zero bytes");
    check(snapshot.allocation_failure_count == 0u, "precompiled hot-path tick should not hit allocation failures");
    check(snapshot.whitelisted_allocation_count == 0u, "logging-off strict lane should not use the logging whitelist");
}

#if MUESLI_BT_BENCH_WITH_BTCPP
void test_btcpp_runner_writes_expected_csv_files() {
    using namespace muesli_bt::bench;

    const std::filesystem::path output_dir =
        std::filesystem::temp_directory_path() / "muesli_bt_bench_btcpp_smoke";
    std::filesystem::remove_all(output_dir);

    run_request request;
    request.runtime_name = "btcpp";
    request.output_dir = output_dir;
    request.scenarios.push_back(*find_scenario("A1-single-leaf-off"));
    request.warmup_override = std::chrono::milliseconds(5);
    request.run_override = std::chrono::milliseconds(10);
    request.repetitions_override = 1u;

    benchmark_runner runner;
    const run_result result = runner.run(request);

    check(std::filesystem::exists(result.output_dir / "run_summary.csv"), "btcpp run_summary.csv was not written");
    check(std::filesystem::exists(result.output_dir / "aggregate_summary.csv"),
          "btcpp aggregate_summary.csv was not written");
    check(std::filesystem::exists(result.output_dir / "environment_metadata.csv"),
          "btcpp environment_metadata.csv was not written");

    check(result.run_rows.size() == 1u, "expected one btcpp run row");
    check(result.run_rows.front().runtime_name == "BehaviorTree.CPP", "unexpected btcpp runtime name");
    check(result.run_rows.front().latency_ns_median > 0u, "btcpp latency should be recorded");

    const std::string environment_metadata = read_text(result.output_dir / "environment_metadata.csv");
    check(environment_metadata.find("BehaviorTree.CPP") != std::string::npos,
          "btcpp environment metadata missing runtime name");
}

void test_btcpp_reactive_and_b5_benchmarks_run() {
    using namespace muesli_bt::bench;

    const std::filesystem::path output_dir =
        std::filesystem::temp_directory_path() / "muesli_bt_bench_btcpp_mix";
    std::filesystem::remove_all(output_dir);

    run_request request;
    request.runtime_name = "btcpp";
    request.output_dir = output_dir;
    request.scenarios.push_back(*find_scenario("B2-reactive-31-flip20-off"));
    request.scenarios.push_back(*find_scenario("B5-alt-31-compile-off"));
    request.warmup_override = std::chrono::milliseconds(5);
    request.run_override = std::chrono::milliseconds(10);
    request.repetitions_override = 1u;

    benchmark_runner runner;
    const run_result result = runner.run(request);

    check(result.run_rows.size() == 2u, "expected btcpp B2+B5 run rows");
    check(result.aggregate_rows.size() == 2u, "expected btcpp B2+B5 aggregate rows");
    check(result.run_rows.front().runtime_name == "BehaviorTree.CPP", "btcpp rows should record runtime name");
    check(result.run_rows.front().semantic_errors == 0u, "btcpp reactive smoke should be semantically clean");
    check(result.run_rows.back().scenario_id == "B5-alt-31-compile-off", "unexpected btcpp B5 scenario id");
    check(result.run_rows.back().latency_ns_median > 0u, "btcpp B5 compile latency should be recorded");
}

void test_btcpp_rejects_unsupported_b6() {
    using namespace muesli_bt::bench;

    run_request request;
    request.runtime_name = "btcpp";
    request.output_dir = std::filesystem::temp_directory_path() / "muesli_bt_bench_btcpp_reject";
    request.scenarios.push_back(*find_scenario("B6-alt-31-base-fulltrace"));
    request.warmup_override = std::chrono::milliseconds(5);
    request.run_override = std::chrono::milliseconds(10);
    request.repetitions_override = 1u;

    benchmark_runner runner;
    bool threw = false;
    try {
        (void)runner.run(request);
    } catch (const std::invalid_argument& error) {
        threw = std::string(error.what()).find("no scenarios supported by runtime btcpp") != std::string::npos;
    }
    check(threw, "btcpp should reject unsupported B6 scenarios");
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--strict-allocation") {
        test_allocation_whitelist_allows_explicit_logging_paths_only();
        test_precompiled_ticks_fail_on_unwhitelisted_allocations();
        return 0;
    }

    test_catalogue_contains_new_benchmarks();
    test_runner_writes_expected_csv_files();
    test_fulltrace_mode_emits_log_bytes();
    test_jitter_trace_is_written_for_a2();
    test_runner_emits_progress_events();
    test_b5_lifecycle_benchmarks_run();
    test_b7_gc_memory_benchmark_runs();
    test_allocation_whitelist_allows_explicit_logging_paths_only();
    test_precompiled_ticks_fail_on_unwhitelisted_allocations();
#if MUESLI_BT_BENCH_WITH_BTCPP
    test_btcpp_runner_writes_expected_csv_files();
    test_btcpp_reactive_and_b5_benchmarks_run();
    test_btcpp_rejects_unsupported_b6();
#endif
    return 0;
}
