#include "harness/runner.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "bt/event_log.hpp"
#include "bt/scheduler.hpp"
#include "bt/vla.hpp"
#include "harness/allocation_tracker.hpp"
#include "harness/metadata.hpp"
#include "harness/stats.hpp"
#include "bench_config.hpp"
#include "fixtures/tree_factory.hpp"
#include "muesli_bt/contract/events.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/value.hpp"
#include "runtimes/muesli_adapter.hpp"
#if MUESLI_BT_BENCH_WITH_BTCPP
#include "runtimes/btcpp_adapter.hpp"
#endif

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
            if (scenario.group_id == "B7") {
                return "GC and memory evidence (" + scenario.variant + ")";
            }
            if (scenario.group_id == "B8") {
                return "async contract edge (" + scenario.variant + ")";
            }
            break;
    }
    return scenario.scenario_id;
}

run_summary_row make_base_run_row(const environment_info& environment,
                                  const runtime_adapter& adapter,
                                  const scenario_definition& scenario,
                                  const tree_fixture& fixture,
                                  std::size_t repetition);

std::string gc_lifecycle_payload_json(const muslisp::gc_lifecycle_event& event) {
    std::ostringstream data;
    data << "{\"schema_version\":\"gc.lifecycle.v1\","
         << "\"collection_id\":" << event.collection_id << ","
         << "\"phase\":\"" << (event.begin ? "begin" : "end") << "\","
         << "\"reason\":\"" << muslisp::gc::collection_reason_name(event.reason) << "\","
         << "\"policy\":\"" << muslisp::gc::policy_name(event.policy) << "\","
         << "\"forced\":" << (event.forced ? "true" : "false") << ","
         << "\"in_tick\":" << (event.in_tick ? "true" : "false") << ","
         << "\"heap_live_bytes_before\":" << event.heap_live_bytes_before << ","
         << "\"live_objects_before\":" << event.live_objects_before;
    if (!event.begin) {
        data << ",\"heap_live_bytes_after\":" << event.heap_live_bytes_after
             << ",\"live_objects_after\":" << event.live_objects_after
             << ",\"freed_objects\":" << event.freed_objects
             << ",\"mark_time_ns\":" << event.mark_time_ns
             << ",\"sweep_time_ns\":" << event.sweep_time_ns
             << ",\"pause_time_ns\":" << event.pause_time_ns;
    }
    data << '}';
    return data.str();
}

muslisp::gc_policy policy_for_gc_mode(gc_benchmark_mode mode) {
    switch (mode) {
        case gc_benchmark_mode::manual:
            return muslisp::gc_policy::manual;
        case gc_benchmark_mode::between_ticks:
            return muslisp::gc_policy::between_ticks;
        case gc_benchmark_mode::forced_pressure:
            return muslisp::gc_policy::default_policy;
        case gc_benchmark_mode::none:
            break;
    }
    return muslisp::gc_policy::default_policy;
}

void allocate_gc_pressure_batch(std::uint64_t tick_index, muslisp::value& retained) {
    muslisp::value transient = muslisp::make_nil();
    for (std::size_t i = 0; i < 64u; ++i) {
        const auto raw = static_cast<std::int64_t>(tick_index * 64u + i);
        transient = muslisp::make_cons(muslisp::make_integer(raw), transient);
    }
    if ((tick_index % 8u) == 0u) {
        retained = muslisp::make_cons(muslisp::make_integer(static_cast<std::int64_t>(tick_index)), retained);
    }
}

bt::vla_request make_async_contract_request(std::uint64_t operation_index, std::int64_t deadline_ms = 200) {
    bt::vla_request request;
    request.capability = "vla.rt2";
    request.task_id = "b8-task-" + std::to_string(operation_index);
    request.instruction = "follow the deterministic B8 contract path";
    request.observation.state = {0.0, 0.0, 0.1, -0.1};
    request.observation.timestamp_ms = static_cast<std::int64_t>(operation_index);
    request.action_space.type = "continuous";
    request.action_space.dims = 2;
    request.action_space.bounds = {{-1.0, 1.0}, {-1.0, 1.0}};
    request.constraints.max_abs_value = 1.0;
    request.constraints.max_delta = 1.0;
    request.deadline_ms = deadline_ms;
    request.seed = operation_index;
    request.run_id = "B8";
    request.tick_index = operation_index;
    request.node_name = "b8-async-node";
    request.model.name = "b8-contract";
    request.model.version = "1";
    return request;
}

bool is_vla_terminal(bt::vla_job_status status) {
    return status == bt::vla_job_status::done || status == bt::vla_job_status::error ||
           status == bt::vla_job_status::timeout || status == bt::vla_job_status::cancelled;
}

bt::vla_poll wait_for_vla_status(bt::vla_service& service,
                                 bt::vla_service::vla_job_id job,
                                 bt::vla_job_status target,
                                 std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bt::vla_poll poll;
    while (std::chrono::steady_clock::now() < deadline) {
        poll = service.poll(job);
        if (poll.status == target) {
            return poll;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return poll;
}

bt::vla_poll wait_for_vla_terminal(bt::vla_service& service,
                                   bt::vla_service::vla_job_id job,
                                   std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bt::vla_poll poll;
    while (std::chrono::steady_clock::now() < deadline) {
        poll = service.poll(job);
        if (is_vla_terminal(poll.status)) {
            return poll;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return poll;
}

bt::vla_response make_contract_response(const bt::vla_request& request, bt::vla_status status) {
    bt::vla_response response;
    response.status = status;
    response.model = request.model;
    response.action.type = bt::vla_action_type::continuous;
    response.action.u = {0.0, 0.0};
    response.confidence = status == bt::vla_status::ok ? 0.9 : 0.0;
    if (status == bt::vla_status::ok) {
        response.explanation = "b8 ok";
    } else if (status == bt::vla_status::timeout) {
        response.explanation = "b8 timeout";
    } else {
        response.explanation = "b8 cancelled";
    }
    return response;
}

class cancellable_contract_backend final : public bt::vla_backend {
public:
    explicit cancellable_contract_backend(bool ignore_cancel = false) : ignore_cancel_(ignore_cancel) {}

    bt::vla_response infer(const bt::vla_request& request,
                           std::function<bool(const bt::vla_partial&)> on_partial,
                           std::atomic<bool>& cancel_flag) override {
        bt::vla_partial partial;
        partial.sequence = 1u;
        partial.text_chunk = "b8 running";
        partial.confidence = 0.25;
        (void)on_partial(partial);

        if (request.deadline_ms <= 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            return make_contract_response(request, bt::vla_status::timeout);
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancel_flag.load() && !ignore_cancel_) {
                return make_contract_response(request, bt::vla_status::cancelled);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return make_contract_response(request, bt::vla_status::ok);
    }

private:
    bool ignore_cancel_ = false;
};

class blocking_contract_backend final : public bt::vla_backend {
public:
    bt::vla_response infer(const bt::vla_request& request,
                           std::function<bool(const bt::vla_partial&)>,
                           std::atomic<bool>& cancel_flag) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = true;
        }
        cv_.notify_all();

        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this, &cancel_flag] { return released_ || cancel_flag.load(); });
        return make_contract_response(request, cancel_flag.load() ? bt::vla_status::cancelled : bt::vla_status::ok);
    }

    bool wait_started(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] { return started_; });
    }

    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool started_ = false;
    bool released_ = false;
};

struct async_contract_sample {
    std::uint64_t latency_ns = 0u;
    std::uint64_t cancel_latency_ns = 0u;
    std::uint64_t semantic_errors = 0u;
    std::uint64_t operations = 1u;
};

async_contract_sample run_async_contract_operation(async_contract_case async_case, std::uint64_t operation_index) {
    const auto started = std::chrono::steady_clock::now();
    async_contract_sample sample;

    if (async_case == async_contract_case::cancel_before_start) {
        bt::thread_pool_scheduler scheduler(1u);
        bt::vla_service service(&scheduler);
        auto blocker = std::make_shared<blocking_contract_backend>();
        service.register_backend("b8-blocking", blocker);
        service.set_default_backend("b8-blocking");

        const auto blocking_job = service.submit(make_async_contract_request(operation_index * 2u));
        if (!blocker->wait_started(std::chrono::milliseconds(100))) {
            ++sample.semantic_errors;
        }
        const auto target = service.submit(make_async_contract_request(operation_index * 2u + 1u));
        const auto cancel_started = std::chrono::steady_clock::now();
        const bool cancelled = service.cancel(target);
        const auto cancel_finished = std::chrono::steady_clock::now();
        sample.cancel_latency_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(cancel_finished - cancel_started).count());
        const bt::vla_poll target_poll = wait_for_vla_terminal(service, target, std::chrono::milliseconds(100));
        blocker->release();
        (void)wait_for_vla_terminal(service, blocking_job, std::chrono::milliseconds(100));
        if (!cancelled || target_poll.status != bt::vla_job_status::cancelled) {
            ++sample.semantic_errors;
        }
    } else {
        bt::thread_pool_scheduler scheduler(1u);
        bt::vla_service service(&scheduler);
        service.set_cache_capacity(0u);
        service.set_cache_ttl_ms(0);
        service.register_backend(
            "b8-contract",
            std::make_shared<cancellable_contract_backend>(
                async_case == async_contract_case::late_completion_after_cancel));
        service.set_default_backend("b8-contract");

        const std::int64_t deadline_ms = async_case == async_contract_case::cancel_after_timeout ? 1 : 200;
        const auto job = service.submit(make_async_contract_request(operation_index, deadline_ms));
        (void)wait_for_vla_status(service, job, bt::vla_job_status::streaming, std::chrono::milliseconds(100));

        const auto cancel_started = std::chrono::steady_clock::now();
        if (async_case == async_contract_case::cancel_after_timeout) {
            const bt::vla_poll timeout_poll =
                wait_for_vla_status(service, job, bt::vla_job_status::timeout, std::chrono::milliseconds(100));
            const bool cancelled_after_timeout = service.cancel(job);
            const auto cancel_finished = std::chrono::steady_clock::now();
            sample.cancel_latency_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(cancel_finished - cancel_started).count());
            if (!cancelled_after_timeout || timeout_poll.status != bt::vla_job_status::timeout) {
                ++sample.semantic_errors;
            }
        } else if (async_case == async_contract_case::repeated_cancel) {
            const bool first_cancel = service.cancel(job);
            const bool second_cancel = service.cancel(job);
            const auto cancel_finished = std::chrono::steady_clock::now();
            sample.cancel_latency_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(cancel_finished - cancel_started).count());
            const bt::vla_poll final_poll = wait_for_vla_terminal(service, job, std::chrono::milliseconds(100));
            if (!first_cancel || !second_cancel || final_poll.status != bt::vla_job_status::cancelled) {
                ++sample.semantic_errors;
            }
        } else {
            const bool cancelled = service.cancel(job);
            const auto cancel_finished = std::chrono::steady_clock::now();
            sample.cancel_latency_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(cancel_finished - cancel_started).count());
            const bt::vla_poll final_poll = wait_for_vla_terminal(service, job, std::chrono::milliseconds(100));
            if (!cancelled || final_poll.status != bt::vla_job_status::cancelled) {
                ++sample.semantic_errors;
            }
            if (async_case == async_contract_case::late_completion_after_cancel) {
                const std::vector<bt::vla_record> records = service.recent_records();
                const bool saw_drop = std::any_of(records.begin(), records.end(), [](const bt::vla_record& record) {
                    return record.completion_dropped;
                });
                if (!saw_drop) {
                    ++sample.semantic_errors;
                }
            }
        }
    }

    const auto finished = std::chrono::steady_clock::now();
    sample.latency_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count());
    return sample;
}

std::filesystem::path async_contract_fixture_events_path(const scenario_definition& scenario) {
    return std::filesystem::path(MUESLI_BT_BENCH_SOURCE_DIR) / "fixtures" /
           ("async-" + scenario.variant + "-case") / "events.jsonl";
}

std::filesystem::path copy_async_contract_evidence_log(const scenario_definition& scenario,
                                                       const std::filesystem::path& output_dir,
                                                       std::size_t repetition) {
    const std::filesystem::path source = async_contract_fixture_events_path(scenario);
    if (!std::filesystem::exists(source)) {
        throw std::runtime_error("B8 async contract fixture events missing: " + source.string());
    }

    const std::filesystem::path event_dir = output_dir / scenario.scenario_id / ("rep-" + std::to_string(repetition));
    std::filesystem::create_directories(event_dir);
    const std::filesystem::path target = event_dir / "events.jsonl";
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing);
    return target;
}

run_summary_row run_memory_gc_once(const environment_info& environment,
                                   const runtime_adapter& adapter,
                                   const scenario_definition& scenario,
                                   const tree_fixture& fixture,
                                   const std::filesystem::path& output_dir,
                                   std::size_t repetition) {
    muslisp::gc& heap = muslisp::default_gc();
    const muslisp::gc_policy old_policy = heap.policy();
    heap.clear_lifecycle_listener();
    heap.set_policy(policy_for_gc_mode(scenario.gc_mode));
    heap.collect();

    const std::filesystem::path event_dir = output_dir / scenario.scenario_id / ("rep-" + std::to_string(repetition));
    std::filesystem::create_directories(event_dir);
    const std::filesystem::path event_path = event_dir / "events.jsonl";
    std::filesystem::remove(event_path);

    bt::event_log events(0u);
    events.set_path(event_path.string());
    events.set_file_enabled(true);
    events.set_flush_each_message(false);
    events.set_flush_on_tick_end(false);
    events.set_capture_stats_enabled(true);
    events.set_run_id(scenario.scenario_id + "-rep-" + std::to_string(repetition));
    events.set_git_sha(MUESLI_BT_BENCH_GIT_COMMIT);
    events.set_host_info("muesli-bt-bench", MUESLI_BT_BENCH_PROJECT_VERSION, "bench");
    events.ensure_run_started("", "{\"reset\":false,\"benchmark_group\":\"B7\"}");

    std::vector<std::uint64_t> latencies_ns;
    std::vector<std::uint64_t> gc_pause_ns;
    muslisp::value retained = muslisp::make_nil();
    muslisp::gc_root_scope roots(heap);
    roots.add(&retained);

    heap.set_lifecycle_listener([&](const muslisp::gc_lifecycle_event& event) {
        const std::string payload = gc_lifecycle_payload_json(event);
        (void)events.emit(event.begin ? muesli_bt::contract::kEventGcBegin : muesli_bt::contract::kEventGcEnd,
                          std::nullopt,
                          payload);
        if (!event.begin) {
            gc_pause_ns.push_back(event.pause_time_ns);
        }
    });

    const auto warmup_started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - warmup_started < scenario.timing.warmup) {
        allocate_gc_pressure_batch(0u, retained);
        heap.collect();
    }
    heap.collect();

    const auto stats_start = heap.stats();
    const std::uint64_t rss_start = peak_rss_bytes();
    const auto run_started = std::chrono::steady_clock::now();
    std::uint64_t ticks_total = 0u;

    allocation_tracker::reset();
    allocation_tracker::set_enabled(true);
    while (std::chrono::steady_clock::now() - run_started < scenario.timing.run) {
        const auto tick_started = std::chrono::steady_clock::now();
        {
            muslisp::gc_tick_scope tick_scope(heap);
            allocate_gc_pressure_batch(ticks_total + 1u, retained);
            if (scenario.gc_mode == gc_benchmark_mode::manual || scenario.gc_mode == gc_benchmark_mode::between_ticks) {
                heap.request_collection();
            }
        }
        if (scenario.gc_mode == gc_benchmark_mode::manual || scenario.gc_mode == gc_benchmark_mode::forced_pressure) {
            heap.collect();
        } else {
            heap.maybe_collect();
        }
        const auto tick_finished = std::chrono::steady_clock::now();
        latencies_ns.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(tick_finished - tick_started).count()));
        ++ticks_total;
    }
    allocation_tracker::set_enabled(false);

    heap.collect();
    heap.clear_lifecycle_listener();
    const auto stats_end = heap.stats();
    const std::uint64_t rss_end = peak_rss_bytes();
    const auto run_finished = std::chrono::steady_clock::now();
    const auto allocations = allocation_tracker::read();
    const bt::event_log_stats event_stats = events.capture_stats();

    heap.set_policy(old_policy);

    const latency_summary latency = summarise_latencies(latencies_ns);
    run_summary_row row = make_base_run_row(environment, adapter, scenario, fixture, repetition);
    row.warmup_seconds = std::chrono::duration<double>(scenario.timing.warmup).count();
    row.run_seconds = std::chrono::duration<double>(run_finished - run_started).count();
    row.ticks_total = ticks_total;
    row.ticks_per_second = row.run_seconds > 0.0 ? static_cast<double>(ticks_total) / row.run_seconds : 0.0;
    row.latency_ns_median = latency.median;
    row.latency_ns_p95 = latency.p95;
    row.latency_ns_p99 = latency.p99;
    row.latency_ns_p999 = latency.p999;
    row.latency_ns_max = latency.max;
    row.jitter_ratio_p99_over_median = latency.jitter_ratio_p99_over_median;
    row.alloc_count_total = allocations.allocation_count;
    row.alloc_bytes_total = allocations.allocation_bytes;
    row.rss_bytes_peak = rss_end;
    row.log_events_total = event_stats.event_count;
    row.log_bytes_total = event_stats.byte_count;
    row.gc_collections_total = stats_end.collection_count - stats_start.collection_count;
    row.gc_pause_ns_p50 = percentile_u64(gc_pause_ns, 0.50);
    row.gc_pause_ns_p95 = percentile_u64(gc_pause_ns, 0.95);
    row.gc_pause_ns_p99 = percentile_u64(gc_pause_ns, 0.99);
    row.gc_pause_ns_p999 = percentile_u64(gc_pause_ns, 0.999);
    row.heap_live_bytes_start = static_cast<std::uint64_t>(stats_start.bytes_allocated);
    row.heap_live_bytes_end = static_cast<std::uint64_t>(stats_end.bytes_allocated);
    row.heap_live_bytes_slope_per_tick =
        ticks_total == 0u ? 0.0 : (static_cast<double>(row.heap_live_bytes_end) -
                                   static_cast<double>(row.heap_live_bytes_start)) /
                                      static_cast<double>(ticks_total);
    row.rss_bytes_start = rss_start;
    row.rss_bytes_end = rss_end;
    row.rss_bytes_slope_per_tick =
        ticks_total == 0u ? 0.0 : (static_cast<double>(row.rss_bytes_end) -
                                   static_cast<double>(row.rss_bytes_start)) /
                                      static_cast<double>(ticks_total);
    row.event_log_bytes_per_tick =
        ticks_total == 0u ? 0.0 : static_cast<double>(event_stats.byte_count) / static_cast<double>(ticks_total);
    row.notes = "events=" + (event_dir / "events.jsonl").string();
    return row;
}

run_summary_row run_async_contract_once(const environment_info& environment,
                                        const runtime_adapter& adapter,
                                        const scenario_definition& scenario,
                                        const tree_fixture& fixture,
                                        const std::filesystem::path& output_dir,
                                        std::size_t repetition) {
    const auto warmup_started = std::chrono::steady_clock::now();
    std::uint64_t warmup_operations = 0u;
    while (std::chrono::steady_clock::now() - warmup_started < scenario.timing.warmup) {
        const async_contract_sample sample = run_async_contract_operation(scenario.async_case, warmup_operations + 1u);
        if (sample.semantic_errors != 0u) {
            break;
        }
        ++warmup_operations;
    }
    const auto warmup_finished = std::chrono::steady_clock::now();

    std::vector<std::uint64_t> latencies_ns;
    std::vector<std::uint64_t> cancel_latencies_ns;
    std::uint64_t operations_total = 0u;
    std::uint64_t semantic_errors = 0u;

    allocation_tracker::reset();
    allocation_tracker::set_enabled(true);
    const auto run_started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - run_started < scenario.timing.run || operations_total == 0u) {
        const async_contract_sample sample =
            run_async_contract_operation(scenario.async_case, repetition * 100000u + operations_total + 1u);
        latencies_ns.push_back(sample.latency_ns);
        cancel_latencies_ns.push_back(sample.cancel_latency_ns);
        semantic_errors += sample.semantic_errors;
        operations_total += sample.operations;
    }
    const auto run_finished = std::chrono::steady_clock::now();
    allocation_tracker::set_enabled(false);

    const auto allocations = allocation_tracker::read();
    const latency_summary latency = summarise_latencies(latencies_ns);
    run_summary_row row = make_base_run_row(environment, adapter, scenario, fixture, repetition);
    row.warmup_seconds = std::chrono::duration<double>(warmup_finished - warmup_started).count();
    row.run_seconds = std::chrono::duration<double>(run_finished - run_started).count();
    row.ticks_total = operations_total;
    row.ticks_per_second = row.run_seconds > 0.0 ? static_cast<double>(operations_total) / row.run_seconds : 0.0;
    row.latency_ns_median = latency.median;
    row.latency_ns_p95 = latency.p95;
    row.latency_ns_p99 = latency.p99;
    row.latency_ns_p999 = latency.p999;
    row.latency_ns_max = latency.max;
    row.jitter_ratio_p99_over_median = latency.jitter_ratio_p99_over_median;
    row.cancel_latency_ns_median = percentile_u64(cancel_latencies_ns, 0.50);
    row.alloc_count_total = allocations.allocation_count;
    row.alloc_bytes_total = allocations.allocation_bytes;
    row.rss_bytes_peak = peak_rss_bytes();
    row.semantic_errors = semantic_errors;
    const std::filesystem::path event_path = copy_async_contract_evidence_log(scenario, output_dir, repetition);
    row.log_events_total = 0u;
    row.log_bytes_total = std::filesystem::file_size(event_path);
    row.event_log_bytes_per_tick =
        operations_total == 0u ? 0.0 : static_cast<double>(row.log_bytes_total) / static_cast<double>(operations_total);
    row.notes = "events=" + event_path.string() + "; fixture=fixtures/async-" + scenario.variant + "-case";
    return row;
}

std::unique_ptr<runtime_adapter> make_runtime_adapter(const std::string& runtime_name) {
    if (runtime_name == "muesli") {
        return std::make_unique<muesli_adapter>();
    }
#if MUESLI_BT_BENCH_WITH_BTCPP
    if (runtime_name == "btcpp") {
        return std::make_unique<btcpp_adapter>();
    }
#endif
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
    std::vector<std::uint64_t> gc_collections;
    std::vector<std::uint64_t> gc_pause_p50s;
    std::vector<std::uint64_t> gc_pause_p95s;
    std::vector<std::uint64_t> gc_pause_p99s;
    std::vector<std::uint64_t> gc_pause_p999s;
    std::vector<double> heap_slopes;
    std::vector<double> rss_slopes;
    std::vector<double> event_log_bytes_per_tick;

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
        gc_collections.push_back(row.gc_collections_total);
        gc_pause_p50s.push_back(row.gc_pause_ns_p50);
        gc_pause_p95s.push_back(row.gc_pause_ns_p95);
        gc_pause_p99s.push_back(row.gc_pause_ns_p99);
        gc_pause_p999s.push_back(row.gc_pause_ns_p999);
        heap_slopes.push_back(row.heap_live_bytes_slope_per_tick);
        rss_slopes.push_back(row.rss_bytes_slope_per_tick);
        event_log_bytes_per_tick.push_back(row.event_log_bytes_per_tick);
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
    aggregate.gc_collections_total_median = percentile_u64(gc_collections, 0.50);
    aggregate.gc_pause_ns_p50_of_runs = percentile_u64(gc_pause_p50s, 0.50);
    aggregate.gc_pause_ns_p95_of_runs = percentile_u64(gc_pause_p95s, 0.95);
    aggregate.gc_pause_ns_p99_of_runs = percentile_u64(gc_pause_p99s, 0.99);
    aggregate.gc_pause_ns_p999_of_runs = percentile_u64(gc_pause_p999s, 0.999);
    aggregate.heap_live_bytes_slope_per_tick_median = percentile_double(heap_slopes, 0.50);
    aggregate.rss_bytes_slope_per_tick_median = percentile_double(rss_slopes, 0.50);
    aggregate.event_log_bytes_per_tick_median = percentile_double(event_log_bytes_per_tick, 0.50);
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

    std::unique_ptr<runtime_adapter> adapter = make_runtime_adapter(request.runtime_name);

    std::vector<scenario_definition> scenarios;
    scenarios.reserve(request.scenarios.size());
    std::chrono::milliseconds total_expected_duration{0};
    for (const scenario_definition& scenario_base : request.scenarios) {
        scenario_definition scenario = apply_overrides(scenario_base, request);
        if (!adapter->supports_scenario(scenario)) {
            continue;
        }
        total_expected_duration += expected_duration_for(scenario);
        scenarios.push_back(std::move(scenario));
    }
    if (scenarios.empty()) {
        throw std::invalid_argument("benchmark runner: no scenarios supported by runtime " + request.runtime_name);
    }
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

        if (scenario.kind == benchmark_kind::memory_gc) {
            if (adapter->name() != "muesli-bt") {
                throw std::invalid_argument("B7 memory/GC benchmarks are muesli-bt only");
            }
            for (std::size_t repetition = 0; repetition < scenario.timing.repetitions; ++repetition) {
                run_summary_row row = run_memory_gc_once(environment, *adapter, scenario, fixture, result.output_dir, repetition);
                scenario_rows.push_back(row);
                result.run_rows.push_back(row);
            }
            result.aggregate_rows.push_back(build_aggregate_row(environment, scenario, scenario_rows));
            continue;
        }

        if (scenario.kind == benchmark_kind::async_contract) {
            if (adapter->name() != "muesli-bt") {
                throw std::invalid_argument("B8 async contract benchmarks are muesli-bt only");
            }
            for (std::size_t repetition = 0; repetition < scenario.timing.repetitions; ++repetition) {
                run_summary_row row =
                    run_async_contract_once(environment, *adapter, scenario, fixture, result.output_dir, repetition);
                scenario_rows.push_back(row);
                result.run_rows.push_back(row);
            }
            result.aggregate_rows.push_back(build_aggregate_row(environment, scenario, scenario_rows));
            continue;
        }

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
