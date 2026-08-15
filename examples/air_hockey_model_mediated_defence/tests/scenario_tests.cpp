#include "action_gate.hpp"
#include "bt/compiler.hpp"
#include "bt/runtime.hpp"
#include "bt/runtime_host.hpp"
#include "env_backend.hpp"
#include "muslisp/env_api.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"
#include "muslisp/reader.hpp"
#include "muslisp/value.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void predicate(std::string_view name, bool condition, const std::string& message) {
    check(condition, message);
    std::cout << "PREDICATE " << name << " PASS\n";
}

template <typename Predicate>
void wait_for(Predicate&& condition,
              const std::string& message,
              std::chrono::milliseconds timeout = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw std::runtime_error(message);
        }
        std::this_thread::sleep_for(1ms);
    }
}

bool event_has(const bt::event_log& events,
               std::string_view type,
               std::initializer_list<std::string_view> fields = {}) {
    const std::string type_field = "\"type\":\"" + std::string(type) + "\"";
    for (const std::string& line : events.snapshot()) {
        if (line.find(type_field) == std::string::npos) {
            continue;
        }
        if (std::all_of(fields.begin(), fields.end(),
                        [&](std::string_view field) { return line.find(field) != std::string::npos; })) {
            return true;
        }
    }
    return false;
}

std::size_t event_count(const bt::event_log& events,
                        std::string_view type,
                        std::initializer_list<std::string_view> fields = {}) {
    const std::string type_field = "\"type\":\"" + std::string(type) + "\"";
    std::size_t count = 0;
    for (const std::string& line : events.snapshot()) {
        if (line.find(type_field) == std::string::npos) {
            continue;
        }
        if (std::all_of(fields.begin(), fields.end(),
                        [&](std::string_view field) { return line.find(field) != std::string::npos; })) {
            ++count;
        }
    }
    return count;
}

class manual_clock final : public bt::clock_interface {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point now() const override { return now_; }

    void advance(std::chrono::milliseconds duration) { now_ += duration; }

private:
    std::chrono::steady_clock::time_point now_{std::chrono::seconds(100)};
};

struct completion_gate {
    std::vector<double> action{0.25, -0.4};
    bool ignore_cancel = false;

    void mark_started() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            started = true;
        }
        condition.notify_all();
    }

    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
        }
        condition.notify_all();
    }

    bool wait_until_released(std::atomic<bool>& cancel_flag) {
        std::unique_lock<std::mutex> lock(mutex);
        while (!released) {
            if (cancel_flag.load() && !ignore_cancel) {
                return false;
            }
            condition.wait_for(lock, 1ms);
        }
        return true;
    }

    void mark_finished() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            finished = true;
        }
        condition.notify_all();
    }

    void wait_for_start() {
        std::unique_lock<std::mutex> lock(mutex);
        check(condition.wait_for(lock, 2000ms, [&] { return started; }),
              "air-hockey provider did not start");
    }

    void wait_for_finish() {
        std::unique_lock<std::mutex> lock(mutex);
        check(condition.wait_for(lock, 2000ms, [&] { return finished; }),
              "air-hockey provider did not finish");
    }

private:
    std::mutex mutex;
    std::condition_variable condition;
    bool started = false;
    bool released = false;
    bool finished = false;
};

class gated_provider final : public bt::vla_backend {
public:
    explicit gated_provider(std::vector<std::shared_ptr<completion_gate>> gates,
                            std::optional<bt::vla_response> replay_response = std::nullopt)
        : gates_(std::move(gates)), replay_response_(std::move(replay_response)) {}

    bt::vla_response infer(const bt::vla_request& request,
                           std::function<bool(const bt::vla_partial&)>,
                           std::atomic<bool>& cancel_flag) override {
        const auto started_at = std::chrono::steady_clock::now();
        const std::size_t index = next_gate_.fetch_add(1);
        if (index >= gates_.size()) {
            throw std::runtime_error("air-hockey provider received an unexpected invocation");
        }
        const std::shared_ptr<completion_gate>& gate = gates_[index];
        gate->mark_started();
        bt::vla_response response;
        if (replay_response_.has_value()) {
            response = *replay_response_;
        } else {
            response.model = request.model;
            response.action.type = bt::vla_action_type::continuous;
            response.action.frame_id = request.action_space.frame_id;
            response.action.u = gate->action;
            response.confidence = 1.0;
            response.explanation = "deterministic air-hockey proposal";
        }
        if (!gate->wait_until_released(cancel_flag)) {
            response.status = bt::vla_status::cancelled;
            response.explanation = "cancelled before deterministic completion";
            store_completed_response(response);
            store_wall_duration(started_at);
            gate->mark_finished();
            return response;
        }
        response.status = bt::vla_status::ok;
        store_completed_response(response);
        store_wall_duration(started_at);
        gate->mark_finished();
        return response;
    }

    void release_all() {
        for (const auto& gate : gates_) {
            gate->release();
        }
    }

    [[nodiscard]] bool replay_mode() const noexcept { return replay_response_.has_value(); }

    [[nodiscard]] bt::vla_response completed_response() const {
        std::lock_guard<std::mutex> lock(response_mutex_);
        if (!completed_response_.has_value()) {
            throw std::runtime_error("air-hockey provider has no completed response");
        }
        return *completed_response_;
    }

    [[nodiscard]] std::vector<std::int64_t> wall_durations_ns() const {
        std::lock_guard<std::mutex> lock(response_mutex_);
        return wall_durations_ns_;
    }

private:
    void store_completed_response(const bt::vla_response& response) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        completed_response_ = response;
    }

    void store_wall_duration(std::chrono::steady_clock::time_point started_at) {
        const auto elapsed = std::chrono::steady_clock::now() - started_at;
        std::lock_guard<std::mutex> lock(response_mutex_);
        wall_durations_ns_.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    }

    std::vector<std::shared_ptr<completion_gate>> gates_;
    std::optional<bt::vla_response> replay_response_;
    mutable std::mutex response_mutex_;
    std::optional<bt::vla_response> completed_response_;
    std::vector<std::int64_t> wall_durations_ns_;
    std::atomic<std::size_t> next_gate_{0};
};

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open air-hockey BT: " + path.string());
    }
    std::ostringstream result;
    result << input.rdbuf();
    return result.str();
}

bt::definition load_definition(const std::filesystem::path& path) {
    const std::string source = read_text(path);
    std::vector<muslisp::value> forms = muslisp::read_all(source);
    muslisp::gc_root_scope roots(muslisp::default_gc());
    for (muslisp::value& form : forms) {
        roots.add(&form);
    }
    check(forms.size() == 1 && muslisp::is_proper_list(forms.front()),
          "air-hockey BT must contain one defbt form");
    const std::vector<muslisp::value> defbt = muslisp::vector_from_list(forms.front());
    check(defbt.size() == 3 && muslisp::is_symbol(defbt[0]) &&
              muslisp::symbol_name(defbt[0]) == "defbt",
          "air-hockey BT must use (defbt name tree)");
    bt::definition definition = bt::compile_definition(defbt[2]);
    definition.source_hash = bt::event_log::hash64_hex(source);
    definition.canonical_dsl = muslisp::write_value(defbt[2]);
    definition.canonical_dsl_hash = bt::event_log::hash64_hex(definition.canonical_dsl);
    return definition;
}

bt::vla_invocation* latest_invocation(bt::instance& instance, bt::vla_authority_state state) {
    bt::vla_invocation* latest = nullptr;
    for (auto& [_, invocation] : instance.vla_invocations) {
        if (invocation.authority_state == state && (!latest || invocation.generation > latest->generation)) {
            latest = &invocation;
        }
    }
    return latest;
}

class scenario_rig {
public:
    scenario_rig(std::filesystem::path socket_path,
                 const std::filesystem::path& tree_path,
                 std::string scenario,
                 bt::vla_acceptance_policy policy,
                 std::vector<std::shared_ptr<completion_gate>> gates,
                 bool configure_remote = true,
                 std::optional<std::filesystem::path> event_path = std::nullopt,
                 std::optional<bt::vla_response> replay_response = std::nullopt,
                 std::optional<air_hockey_demo::host_configuration> remote_configuration =
                     std::nullopt)
        : policy_(policy),
          scenario_(std::move(scenario)),
          backend_(std::make_shared<air_hockey_demo::air_hockey_env_backend>(std::move(socket_path))),
          provider_(std::make_shared<gated_provider>(std::move(gates),
                                                     std::move(replay_response))),
          validator_(air_hockey_demo::action_validator_config{
                         .frame_id = "airhockey.normalised_mallet_target.v1",
                         .minimum = -1.0,
                         .maximum = 1.0,
                         .max_source_age_steps = 6,
                         .enforce_context = policy == bt::vla_acceptance_policy::invocation_scoped,
                     },
                     [this] { return action_state(); }),
          dispatch_gate_(*backend_, validator_, [this] { return action_state(); }, clock_, host_.events()) {
        muslisp::env_api_reset();
        air_hockey_demo::register_air_hockey_env_backend("air-hockey-direct-launch", backend_);
        muslisp::env_api_attach("air-hockey-direct-launch");
        check(muslisp::env_api_attached_backend().get() == backend_.get(),
              "air-hockey env backend registration did not attach the socket adapter");

        if (configure_remote) {
            backend_->configure_host(remote_configuration.value_or(
                air_hockey_demo::host_configuration{
                    .blackout_start_step = 1,
                    .blackout_length_steps = 1,
                    .timeout_steps = 12,
                    .action_lock_steps = 0,
                    .replace_track_steps = {},
                    .terminate_at_step = std::nullopt,
                }));
        }
        (void)backend_->reset_host(6302);

        host_.enable_deterministic_test_mode(6302, "air-hockey-" + scenario_, 1735689610000, 1);
        host_.set_clock_interface(&clock_);
        host_.set_vla_commit_validator(&validator_);
        host_.vla_ref().set_cache_ttl_ms(0);
        host_.vla_ref().register_backend("airhockey-delayed-fake", provider_);
        if (event_path.has_value()) {
            host_.events().set_path(event_path->string());
            host_.events().set_file_enabled(true);
            host_.events().set_flush_each_message(true);
        }
        host_.events().set_tick_hz(50.0);
        host_.events().set_git_sha("fixture");
        host_.events().set_host_info("air-hockey-wp2", "v1", "local");
        register_callbacks();

        bt::definition definition = load_definition(tree_path);
        host_.events().ensure_run_started(
            definition.canonical_dsl_hash,
            "{\"reset\":true,\"air_hockey_action_dispatch\":true,\"physical_motion\":false}");
        const std::int64_t definition_handle = host_.store_definition(std::move(definition));
        instance_handle_ = host_.create_instance(definition_handle);
        instance_ = host_.find_instance(instance_handle_);
        check(instance_ != nullptr, "air-hockey scenario instance was not created");
    }

    ~scenario_rig() {
        provider_->release_all();
        host_.set_vla_commit_validator(nullptr);
        host_.set_clock_interface(nullptr);
        muslisp::env_api_detach();
        muslisp::env_api_reset();
    }

    scenario_rig(const scenario_rig&) = delete;
    scenario_rig& operator=(const scenario_rig&) = delete;

    bt::status tick() {
        const auto started_at = std::chrono::steady_clock::now();
        const bt::status result = host_.tick_instance(instance_handle_);
        tick_durations_ns_.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started_at)
                .count());
        for (const auto& [job_id, invocation] : instance_->vla_invocations) {
            if (!validator_.source_step(job_id).has_value()) {
                validator_.record_source_step(job_id, backend_->last_state().observation_step);
            }
            check(invocation.acceptance_policy == policy_,
                  "air-hockey BT acceptance policy does not match its trial configuration");
        }
        return result;
    }

    air_hockey_demo::host_step_result step_control() {
        air_hockey_demo::host_step_result result = backend_->step_host();
        clock_.advance(20ms);
        return result;
    }

    void force_context_change(bt::tick_context* context = nullptr) {
        const std::string initial = backend_->last_state().defence_context_id;
        for (int index = 0; index < 2; ++index) {
            const auto& state = backend_->last_state();
            backend_->act_target({state.observation[14], state.observation[15]});
            step_control();
        }
        const air_hockey_demo::public_state state = backend_->observe_host();
        check(state.defence_context_id != initial,
              "fake host did not change context on blackout reacquisition");
        if (context) {
            sync_blackboard(*context, state);
        }
    }

    void set_defence_available(bool available) { defence_available_ = available; }

    void set_before_dispatch(std::function<void(bt::tick_context&)> callback) {
        before_dispatch_ = std::move(callback);
    }

    void set_hold_after_dispatch(bool hold) { hold_after_dispatch_ = hold; }

    void clear_job_key() {
        instance_->bb.put("defence-job", bt::bb_value{std::monostate{}}, instance_->tick_index,
                          clock_.now(), 0, "air-hockey-test");
    }

    std::uint64_t only_job_id() const {
        check(instance_->vla_invocations.size() == 1,
              "air-hockey scenario expected exactly one retained invocation");
        return instance_->vla_invocations.begin()->first;
    }

    bt::vla_invocation& invocation(std::uint64_t job_id) {
        return instance_->vla_invocations.at(job_id);
    }

    void advance_clock(std::chrono::milliseconds duration) { clock_.advance(duration); }

    air_hockey_demo::action_dispatch_result dispatch_again(std::uint64_t job_id) {
        return dispatch_gate_.dispatch(*instance_, job_id, 999, invocation(job_id).accepted_action);
    }

    [[nodiscard]] std::size_t accepted_dispatches() const {
        return dispatch_gate_.accepted_dispatches();
    }

    [[nodiscard]] std::size_t obsolete_dispatches() const {
        return dispatch_gate_.obsolete_dispatches();
    }

    [[nodiscard]] const std::optional<air_hockey_demo::action_dispatch_result>& last_dispatch() const {
        return last_dispatch_;
    }

    [[nodiscard]] const air_hockey_demo::public_state& state() const {
        return backend_->last_state();
    }

    [[nodiscard]] std::size_t fallback_requests() const noexcept {
        return fallback_requests_;
    }

    [[nodiscard]] const std::optional<std::array<double, air_hockey_demo::kActionDimension>>&
    last_fallback_target() const noexcept {
        return last_fallback_target_;
    }

    void finish_events() {
        if (events_finished_) {
            return;
        }
        std::ostringstream data;
        data << "{\"status\":\"success\",\"scenario\":\""
             << bt::event_log::json_escape(scenario_) << "\",\"observation_step\":"
             << backend_->last_state().observation_step << '}';
        (void)host_.events().emit("run_end", instance_->tick_index, data.str());
        std::cout << "TIMING {\"schema_version\":\"airhockey.wp6.timing.v1\",\"scenario\":\""
                  << bt::event_log::json_escape(scenario_) << "\",\"tick_duration_ns\":[";
        for (std::size_t index = 0; index < tick_durations_ns_.size(); ++index) {
            if (index != 0) {
                std::cout << ',';
            }
            std::cout << tick_durations_ns_[index];
        }
        std::cout << "],\"provider_wall_duration_ns\":[";
        const std::vector<std::int64_t> provider_durations =
            provider_->wall_durations_ns();
        for (std::size_t index = 0; index < provider_durations.size(); ++index) {
            if (index != 0) {
                std::cout << ',';
            }
            std::cout << provider_durations[index];
        }
        std::cout << "]}\n";
        events_finished_ = true;
    }

    [[nodiscard]] bool provider_replay_mode() const noexcept {
        return provider_->replay_mode();
    }

    [[nodiscard]] bt::vla_response completed_provider_response() const {
        return provider_->completed_response();
    }

    bt::runtime_host& host() { return host_; }
    bt::instance& instance() { return *instance_; }

private:
    air_hockey_demo::action_host_state action_state() const {
        const auto& state = backend_->last_state();
        return {
            .defence_context_id = state.defence_context_id,
            .observation_step = state.observation_step,
            .episode_active = state.episode_active,
        };
    }

    void sync_blackboard(bt::tick_context& context, const air_hockey_demo::public_state& state) {
        context.bb_put("air-hockey-state",
                       bt::bb_value{std::vector<double>(state.observation.begin(), state.observation.end())},
                       "air-hockey-host");
        context.bb_put("air-hockey-context-id", bt::bb_value{state.defence_context_id},
                       "air-hockey-host");
        context.bb_put("air-hockey-observation-step", bt::bb_value{state.observation_step},
                       "air-hockey-host");
        context.bb_put("air-hockey-puck-visible", bt::bb_value{state.puck_visible},
                       "air-hockey-host");
        context.bb_put("air-hockey-episode-active", bt::bb_value{state.episode_active},
                       "air-hockey-host");
    }

    void register_callbacks() {
        bt::registry& callbacks = host_.callbacks();
        callbacks.register_condition(
            "air-hockey-episode-ended",
            [this](bt::tick_context&, std::span<const muslisp::value>) {
                return !backend_->last_state().episode_active;
            });
        callbacks.register_condition(
            "air-hockey-defence-unavailable",
            [this](bt::tick_context&, std::span<const muslisp::value>) {
                return !defence_available_;
            });
        callbacks.register_condition(
            "air-hockey-job-active",
            [](bt::tick_context& context, std::span<const muslisp::value>) {
                const bt::bb_entry* entry = context.bb_get("defence-job");
                const auto* job = entry ? std::get_if<std::int64_t>(&entry->value) : nullptr;
                return job && *job > 0;
            });
        callbacks.register_condition(
            "air-hockey-proposal-required",
            [this](bt::tick_context&, std::span<const muslisp::value>) {
                return defence_available_ && backend_->last_state().episode_active;
            });

        callbacks.register_action(
            "air-hockey-sync-state",
            [this](bt::tick_context& context, bt::node_id, bt::node_memory&,
                   std::span<const muslisp::value>) {
                sync_blackboard(context, backend_->observe_host());
                return bt::status::success;
            });
        callbacks.register_action(
            "air-hockey-fallback",
            [this](bt::tick_context&, bt::node_id, bt::node_memory&,
                   std::span<const muslisp::value>) {
                const auto& state = backend_->last_state();
                const std::array<double, air_hockey_demo::kActionDimension> target{
                    state.observation[14], state.observation[15]};
                backend_->act_target(target);
                last_fallback_target_ = target;
                ++fallback_requests_;
                return bt::status::success;
            });
        callbacks.register_action(
            "air-hockey-terminal-hold",
            [](bt::tick_context&, bt::node_id, bt::node_memory&,
               std::span<const muslisp::value>) { return bt::status::running; },
            [](bt::tick_context&, bt::node_id, bt::node_memory&) {});
        callbacks.register_action(
            "air-hockey-dispatch",
            [this](bt::tick_context& context, bt::node_id node, bt::node_memory&,
                   std::span<const muslisp::value>) {
                bt::vla_invocation* invocation =
                    latest_invocation(context.inst, bt::vla_authority_state::accepted);
                const bt::bb_entry* entry = context.bb_get("defence-action");
                const auto* action = entry ? std::get_if<std::vector<double>>(&entry->value) : nullptr;
                if (!invocation || !action) {
                    return bt::status::failure;
                }
                if (before_dispatch_) {
                    std::function<void(bt::tick_context&)> callback = std::move(before_dispatch_);
                    before_dispatch_ = {};
                    callback(context);
                }
                last_dispatch_ = dispatch_gate_.dispatch(context.inst, invocation->job_id, node, *action);
                context.bb_put("air-hockey-dispatch-reason", bt::bb_value{last_dispatch_->reason},
                               "air-hockey-dispatch-gate");
                return hold_after_dispatch_ ? bt::status::running : bt::status::success;
            });
        callbacks.register_action(
            "air-hockey-result-rejected",
            [](bt::tick_context& context, bt::node_id, bt::node_memory&,
               std::span<const muslisp::value>) {
                const bt::vla_invocation* invocation =
                    latest_invocation(context.inst, bt::vla_authority_state::rejected);
                context.bb_put("air-hockey-result-reason",
                               bt::bb_value{invocation ? invocation->authority_reason
                                                       : std::string("backend_terminal_failure")},
                               "air-hockey-commit-gate");
                return bt::status::running;
            },
            [](bt::tick_context&, bt::node_id, bt::node_memory&) {});
    }

    bt::vla_acceptance_policy policy_;
    std::string scenario_;
    bt::runtime_host host_;
    manual_clock clock_;
    std::shared_ptr<air_hockey_demo::air_hockey_env_backend> backend_;
    std::shared_ptr<gated_provider> provider_;
    air_hockey_demo::air_hockey_action_validator validator_;
    air_hockey_demo::air_hockey_action_dispatch_gate dispatch_gate_;
    bool defence_available_ = true;
    bool hold_after_dispatch_ = false;
    std::function<void(bt::tick_context&)> before_dispatch_;
    std::optional<air_hockey_demo::action_dispatch_result> last_dispatch_;
    std::optional<std::array<double, air_hockey_demo::kActionDimension>>
        last_fallback_target_;
    std::size_t fallback_requests_ = 0;
    bool events_finished_ = false;
    std::vector<std::int64_t> tick_durations_ns_;
    std::int64_t instance_handle_ = 0;
    bt::instance* instance_ = nullptr;
};

void adopt_running_invocation(scenario_rig& rig, std::uint64_t job_id) {
    check(rig.tick() == bt::status::running,
          "air-hockey wait branch should be running before provider completion");
    check(rig.invocation(job_id).authority_node != rig.invocation(job_id).requesting_node,
          "air-hockey vla-wait did not adopt invocation authority");
}

void wait_for_authority(scenario_rig& rig,
                        std::uint64_t job_id,
                        bt::vla_authority_state state) {
    wait_for(
        [&] {
            (void)rig.tick();
            return rig.invocation(job_id).authority_state == state;
        },
        "air-hockey invocation did not reach its expected authority state");
}

void wait_for_completion_drop(scenario_rig& rig) {
    wait_for(
        [&] {
            return event_has(rig.host().events(), "async_completion_dropped",
                             {"\"reason\":\"completion_after_cancel\""});
        },
        "air-hockey late completion was not recorded as dropped");
}

struct scenario_options {
    std::filesystem::path socket_path;
    std::filesystem::path tree_path;
    std::optional<std::filesystem::path> event_path;
};

void test_h1(const scenario_options& options) {
    auto completion = std::make_shared<completion_gate>();
    scenario_rig rig(options.socket_path, options.tree_path, "H1",
                     bt::vla_acceptance_policy::invocation_scoped, {completion}, true,
                     options.event_path);
    check(rig.tick() == bt::status::running, "H1 did not submit its current request");
    const std::uint64_t job = rig.only_job_id();
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    completion->release();
    wait_for_authority(rig, job, bt::vla_authority_state::accepted);
    predicate("h1_current_commit_once",
              event_count(rig.host().events(), "vla_result",
                          {"\"decision\":\"accepted\""}) == 1,
              "H1 must commit the current result exactly once");
    predicate("h1_current_dispatch_once",
              rig.accepted_dispatches() == 1 && rig.obsolete_dispatches() == 0,
              "H1 must dispatch one current action and no obsolete action");
    check(rig.step_control().state.observation_step == 1,
          "H1 current action was not consumed by one simulator/control step");
    rig.finish_events();
}

void advance_through_reacquisition(scenario_rig& rig) {
    rig.step_control();
    (void)rig.tick();
    rig.step_control();
    check(rig.host().events().snapshot().size() > 0, "context advance produced no evidence");
}

void test_h2a(const scenario_options& options) {
    auto completion = std::make_shared<completion_gate>();
    scenario_rig rig(options.socket_path, options.tree_path, "H2a",
                     bt::vla_acceptance_policy::deadline_only, {completion}, true,
                     options.event_path);
    check(rig.tick() == bt::status::running, "H2a did not submit its baseline request");
    const std::uint64_t job = rig.only_job_id();
    const std::string captured = rig.invocation(job).captured_context_id;
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    advance_through_reacquisition(rig);
    completion->release();
    wait_for_authority(rig, job, bt::vla_authority_state::accepted);
    predicate("h2a_baseline_admits_stale_result",
              rig.invocation(job).captured_context_id == captured && rig.last_dispatch().has_value() &&
                  rig.last_dispatch()->obsolete,
              "H2a must expose the deadline-only stale acceptance");
    predicate("h2a_obsolete_dispatch_observed",
              rig.accepted_dispatches() == 1 && rig.obsolete_dispatches() == 1,
              "H2a must record one bounded obsolete baseline dispatch");
    check(rig.step_control().state.observation_step == 3,
          "H2a obsolete action was not consumed inside the bounded host");
    rig.finish_events();
}

void test_h2b(const scenario_options& options) {
    auto completion = std::make_shared<completion_gate>();
    scenario_rig rig(options.socket_path, options.tree_path, "H2b",
                     bt::vla_acceptance_policy::invocation_scoped, {completion}, true,
                     options.event_path);
    check(rig.tick() == bt::status::running, "H2b did not submit its full request");
    const std::uint64_t job = rig.only_job_id();
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    advance_through_reacquisition(rig);
    completion->release();
    wait_for_authority(rig, job, bt::vla_authority_state::rejected);
    predicate("h2b_changed_context_rejected",
              rig.invocation(job).authority_reason == "context_changed" &&
                  event_has(rig.host().events(), "vla_result",
                            {"\"decision\":\"rejected\"", "\"reason\":\"context_changed\""}),
              "H2b must reject the old context with context_changed");
    predicate("h2b_zero_obsolete_dispatch",
              rig.accepted_dispatches() == 0 && rig.obsolete_dispatches() == 0,
              "H2b must dispatch no obsolete action");
    check(rig.step_control().state.observation_step == 3,
          "H2b fallback was not consumed after stale-result rejection");
    rig.finish_events();
}

void run_g6_current_delay(const scenario_options& options,
                          std::string scenario,
                          std::chrono::milliseconds delay,
                          std::string_view predicate_name) {
    auto completion = std::make_shared<completion_gate>();
    scenario_rig rig(options.socket_path, options.tree_path, std::move(scenario),
                     bt::vla_acceptance_policy::invocation_scoped, {completion}, true,
                     options.event_path);
    check(rig.tick() == bt::status::running,
          "G6 delay calibration did not submit its current request");
    const std::uint64_t job = rig.only_job_id();
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    rig.advance_clock(delay);
    completion->release();
    wait_for_authority(rig, job, bt::vla_authority_state::accepted);
    predicate(predicate_name,
              rig.accepted_dispatches() == 1 && rig.obsolete_dispatches() == 0,
              "G6 current completion did not remain authorised at its calibrated delay");
    check(rig.step_control().state.observation_step == 1,
          "G6 calibrated current action was not consumed by the simulator");
    rig.finish_events();
}

void test_g6_delay_timely(const scenario_options& options) {
    run_g6_current_delay(options, "G6-delay-timely", 20ms,
                         "g6_timely_completion_accepted");
}

void test_g6_delay_boundary(const scenario_options& options) {
    run_g6_current_delay(options, "G6-delay-boundary", 119ms,
                         "g6_boundary_completion_accepted");
}

void test_g6_delay_stale(const scenario_options& options) {
    auto completion = std::make_shared<completion_gate>();
    scenario_rig rig(options.socket_path, options.tree_path, "G6-delay-stale",
                     bt::vla_acceptance_policy::invocation_scoped, {completion}, true,
                     options.event_path);
    check(rig.tick() == bt::status::running,
          "G6 stale calibration did not submit its request");
    const std::uint64_t job = rig.only_job_id();
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    advance_through_reacquisition(rig);
    rig.advance_clock(20ms);
    completion->release();
    wait_for_authority(rig, job, bt::vla_authority_state::rejected);
    predicate("g6_stale_unexpired_completion_rejected",
              rig.invocation(job).authority_reason == "context_changed" &&
                  rig.accepted_dispatches() == 0 && rig.obsolete_dispatches() == 0,
              "G6 stale unexpired result was not rejected by context identity");
    check(rig.step_control().state.observation_step == 3,
          "G6 stale-result fallback was not consumed by the simulator");
    rig.finish_events();
}

void test_g5_fixed_shot(const scenario_options& options) {
    auto completion = std::make_shared<completion_gate>();
    completion->action = {0.0, 0.0};
    scenario_rig rig(
        options.socket_path, options.tree_path, "G5-fixed",
        bt::vla_acceptance_policy::invocation_scoped, {completion}, true,
        options.event_path, std::nullopt,
        air_hockey_demo::host_configuration{
            .blackout_start_step = 5,
            .blackout_length_steps = 3,
            .timeout_steps = 20,
            .action_lock_steps = 0,
            .replace_track_steps = {},
            .terminate_at_step = std::nullopt,
        });
    check(rig.tick() == bt::status::running,
          "G5 fixed shot did not submit its deterministic request");
    const std::uint64_t job = rig.only_job_id();
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    completion->release();
    wait_for_authority(rig, job, bt::vla_authority_state::accepted);
    check(rig.accepted_dispatches() == 1 && rig.obsolete_dispatches() == 0,
          "G5 fixed shot did not dispatch one current target");

    std::size_t steps = 0;
    while (rig.state().episode_active) {
        const air_hockey_demo::host_step_result step = rig.step_control();
        ++steps;
        if (step.state.episode_active) {
            (void)rig.tick();
        }
    }
    predicate("g5_fixed_shot_completed",
              steps > 0 && (rig.state().terminated || rig.state().truncated),
              "G5 fixed shot must reach a simulator terminal state");
    predicate("g5_fixed_shot_current_dispatch_once",
              rig.accepted_dispatches() == 1 && rig.obsolete_dispatches() == 0,
              "G5 fixed shot must consume exactly one authorised proposal");
    rig.finish_events();
}

void test_h3(const scenario_options& options) {
    auto obsolete = std::make_shared<completion_gate>();
    obsolete->ignore_cancel = true;
    obsolete->action = {0.1, 0.1};
    auto current = std::make_shared<completion_gate>();
    current->action = {0.4, -0.2};
    scenario_rig rig(options.socket_path, options.tree_path, "H3",
                     bt::vla_acceptance_policy::invocation_scoped, {obsolete, current}, true,
                     options.event_path);
    check(rig.tick() == bt::status::running, "H3 did not submit generation one");
    const std::uint64_t old_job = rig.only_job_id();
    adopt_running_invocation(rig, old_job);
    obsolete->wait_for_start();
    rig.clear_job_key();
    check(rig.tick() == bt::status::running, "H3 did not submit its replacement generation");
    const std::uint64_t current_job = rig.only_job_id();
    check(current_job != old_job && rig.invocation(current_job).generation == 2,
          "H3 replacement did not advance generation");
    adopt_running_invocation(rig, current_job);
    current->wait_for_start();
    obsolete->release();
    obsolete->wait_for_finish();
    wait_for_completion_drop(rig);
    current->release();
    wait_for_authority(rig, current_job, bt::vla_authority_state::accepted);
    predicate("h3_superseded_generation_rejected",
              event_has(rig.host().events(), "async_authority_revoked",
                        {"\"reason\":\"superseded\""}),
              "H3 must revoke the obsolete generation");
    predicate("h3_replacement_generation_progress",
              rig.accepted_dispatches() == 1 && rig.obsolete_dispatches() == 0 &&
                  event_has(rig.host().events(), "vla_result",
                            {"\"generation\":2", "\"decision\":\"accepted\""}),
              "H3 must allow the replacement generation to dispatch");
    check(rig.step_control().state.observation_step == 1,
          "H3 current replacement action was not consumed by the simulator");
    rig.finish_events();
}

void test_h4(const scenario_options& options) {
    auto completion = std::make_shared<completion_gate>();
    scenario_rig rig(options.socket_path, options.tree_path, "H4",
                     bt::vla_acceptance_policy::invocation_scoped, {completion}, true,
                     options.event_path);
    check(rig.tick() == bt::status::running, "H4 did not submit its request");
    const std::uint64_t job = rig.only_job_id();
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    rig.set_before_dispatch([&rig](bt::tick_context& context) { rig.force_context_change(&context); });
    completion->release();
    wait_for_authority(rig, job, bt::vla_authority_state::accepted);
    predicate("h4_commit_then_context_change_ordered",
              event_has(rig.host().events(), "vla_result",
                        {"\"decision\":\"accepted\""}) &&
                  rig.last_dispatch().has_value() && rig.last_dispatch()->obsolete,
              "H4 must admit before the injected consume-time context change");
    predicate("h4_dispatch_revalidation_blocks_obsolete",
              !rig.last_dispatch()->accepted && rig.last_dispatch()->reason == "context_changed" &&
                  rig.accepted_dispatches() == 0 &&
                  event_count(rig.host().events(), "cap_call_start") == 0 &&
                  event_count(rig.host().events(), "cap_call_end") == 0,
              "H4 dispatch gate must reject before any host capability call");
    rig.finish_events();
}

void test_h5(const scenario_options& options) {
    auto completion = std::make_shared<completion_gate>();
    completion->ignore_cancel = true;
    scenario_rig rig(options.socket_path, options.tree_path, "H5",
                     bt::vla_acceptance_policy::invocation_scoped, {completion}, true,
                     options.event_path);
    check(rig.tick() == bt::status::running, "H5 did not submit its request");
    const std::uint64_t job = rig.only_job_id();
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    rig.set_defence_available(false);
    (void)rig.tick();
    check(rig.invocation(job).authority_state == bt::vla_authority_state::revoked,
          "H5 branch exit did not revoke authority");
    completion->release();
    completion->wait_for_finish();
    wait_for_completion_drop(rig);
    predicate("h5_branch_exit_revokes_authority",
              rig.invocation(job).authority_reason == "branch_revoked" &&
                  event_has(rig.host().events(), "async_authority_revoked",
                            {"\"reason\":\"branch_revoked\""}),
              "H5 must revoke within the branch-exit tick");
    predicate("h5_late_completion_dropped",
              rig.accepted_dispatches() == 0 &&
                  event_has(rig.host().events(), "async_completion_dropped"),
              "H5 late completion must not dispatch");
    check(rig.step_control().state.observation_step == 1,
          "H5 authored branch-exit fallback was not consumed by the simulator");
    rig.finish_events();
}

bool run_h6_policy(const scenario_options& options,
                   const std::filesystem::path& tree_path,
                   bt::vla_acceptance_policy policy,
                   bool configure_remote) {
    auto completion = std::make_shared<completion_gate>();
    scenario_rig rig(options.socket_path, tree_path,
                     policy == bt::vla_acceptance_policy::invocation_scoped ? "H6-full"
                                                                            : "H6-baseline",
                     policy, {completion}, configure_remote,
                     options.event_path);
    check(rig.tick() == bt::status::running, "H6 did not submit its request");
    const std::uint64_t job = rig.only_job_id();
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    rig.advance_clock(121ms);
    completion->release();
    wait_for_authority(rig, job, bt::vla_authority_state::rejected);
    const auto before = rig.state().observation;
    const bool fallback_requested =
        rig.fallback_requests() > 0 && rig.last_fallback_target().has_value() &&
        *rig.last_fallback_target() ==
            std::array<double, air_hockey_demo::kActionDimension>{before[14], before[15]};
    (void)rig.step_control();
    const bool passed = rig.invocation(job).authority_reason == "deadline_expired" &&
                        rig.accepted_dispatches() == 0 && fallback_requested &&
                        event_has(rig.host().events(), "vla_result",
                                  {"\"reason\":\"deadline_expired\""});
    rig.finish_events();
    return passed;
}

void test_h6(const scenario_options& options) {
    const bool full = run_h6_policy(options, options.tree_path,
                                    bt::vla_acceptance_policy::invocation_scoped, true);
    const std::filesystem::path baseline_tree =
        options.tree_path.parent_path() / "bt_deadline_only.lisp";
    const bool baseline = run_h6_policy(options, baseline_tree,
                                        bt::vla_acceptance_policy::deadline_only, false);
    predicate("h6_deadline_expired_fallback", full && baseline,
              "H6 must reject after 120 ms and retain fallback under both policies");
}

void test_h7(const scenario_options& options) {
    auto completion = std::make_shared<completion_gate>();
    scenario_rig rig(options.socket_path, options.tree_path, "H7",
                     bt::vla_acceptance_policy::invocation_scoped, {completion}, true,
                     options.event_path);
    rig.set_hold_after_dispatch(true);
    check(rig.tick() == bt::status::running, "H7 did not submit its request");
    const std::uint64_t job = rig.only_job_id();
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    completion->release();
    wait_for_authority(rig, job, bt::vla_authority_state::accepted);
    const bt::bb_entry* action = rig.instance().bb.get("defence-action");
    check(action != nullptr, "H7 first completion did not write its action");
    const std::uint64_t accepted_write_tick = action->last_write_tick;
    const air_hockey_demo::action_dispatch_result duplicate = rig.dispatch_again(job);
    (void)rig.tick();
    check(rig.instance().bb.get("defence-action")->last_write_tick == accepted_write_tick,
          "H7 duplicate terminal polling rewrote the accepted action");
    predicate("h7_exactly_one_terminal_decision",
              event_count(rig.host().events(), "vla_result",
                          {"\"decision\":\"accepted\""}) == 1 &&
                  event_has(rig.host().events(), "vla_result",
                            {"\"decision\":\"rejected\"",
                             "\"reason\":\"duplicate_terminal_result\""}),
              "H7 must record one accepted decision and reject duplicate terminal polling");
    predicate("h7_exactly_one_dispatch",
              rig.accepted_dispatches() == 1 && !duplicate.accepted &&
                  duplicate.reason == "duplicate_dispatch" &&
                  event_count(rig.host().events(), "cap_call_end",
                              {"\"status\":\"accepted\""}) == 1,
              "H7 must reject a duplicate dispatch");
    check(rig.step_control().state.observation_step == 1,
          "H7 exactly-once action was not consumed by the simulator");
    rig.finish_events();
}

std::vector<std::string> replay_projection(const bt::event_log& events) {
    std::vector<std::string> result;
    for (const std::string& line : events.snapshot()) {
        if (line.find("\"type\":\"vla_submit\"") != std::string::npos) {
            result.emplace_back("submit:generation-1");
        } else if (line.find("\"type\":\"vla_result\"") != std::string::npos &&
                   line.find("\"decision\":\"accepted\"") != std::string::npos) {
            result.emplace_back("result:accepted");
        } else if (line.find("\"type\":\"cap_call_end\"") != std::string::npos &&
                   line.find("\"decision\":\"accepted\"") != std::string::npos) {
            result.emplace_back("dispatch:accepted-current");
        }
    }
    return result;
}

struct replay_half_result {
    std::vector<std::string> projection;
    bt::vla_response response;
    std::array<double, air_hockey_demo::kActionDimension> applied_mallet_target{};
    bool replay_mode = false;
};

replay_half_result run_replay_half(const scenario_options& options,
                                   bool configure_remote,
                                   std::optional<bt::vla_response> recorded_response = std::nullopt) {
    auto completion = std::make_shared<completion_gate>();
    scenario_rig rig(options.socket_path, options.tree_path, configure_remote ? "H8-record" : "H8-replay",
                     bt::vla_acceptance_policy::invocation_scoped, {completion}, configure_remote,
                     options.event_path, std::move(recorded_response));
    check(rig.tick() == bt::status::running, "H8 did not submit its recorded request");
    const std::uint64_t job = rig.only_job_id();
    adopt_running_invocation(rig, job);
    completion->wait_for_start();
    completion->release();
    wait_for_authority(rig, job, bt::vla_authority_state::accepted);
    check(rig.accepted_dispatches() == 1 && rig.obsolete_dispatches() == 0,
          "H8 recorded schedule did not dispatch its current result");
    const air_hockey_demo::host_step_result applied = rig.step_control();
    replay_half_result result{
        .projection = replay_projection(rig.host().events()),
        .response = rig.completed_provider_response(),
        .applied_mallet_target = {applied.state.observation[14],
                                  applied.state.observation[15]},
        .replay_mode = rig.provider_replay_mode(),
    };
    rig.finish_events();
    return result;
}

void test_h8(const scenario_options& options) {
    const replay_half_result recorded = run_replay_half(options, true);
    const replay_half_result replayed = run_replay_half(options, false, recorded.response);
    predicate("h8_replay_projection_matches",
              !recorded.replay_mode && replayed.replay_mode &&
                  recorded.projection == replayed.projection &&
                  recorded.applied_mallet_target == replayed.applied_mallet_target &&
                  recorded.response.action.u == std::vector<double>{0.25, -0.4} &&
                  replayed.response.action.u == recorded.response.action.u &&
                  recorded.projection ==
                      std::vector<std::string>{"submit:generation-1", "result:accepted",
                                               "dispatch:accepted-current"},
              "H8 must replay the recorded response and match without live inference");
}

using scenario_fn = void (*)(const scenario_options&);

const std::vector<std::pair<std::string_view, scenario_fn>> kScenarios{
    {"H1", test_h1},   {"H2a", test_h2a}, {"H2b", test_h2b}, {"H3", test_h3},
    {"H4", test_h4},   {"H5", test_h5},   {"H6", test_h6},   {"H7", test_h7},
    {"H8", test_h8},   {"G5-fixed", test_g5_fixed_shot},
    {"G6-delay-timely", test_g6_delay_timely},
    {"G6-delay-boundary", test_g6_delay_boundary},
    {"G6-delay-stale", test_g6_delay_stale},
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::cerr << "usage: muesli_bt_air_hockey_scenario_tests SCENARIO SOCKET TREE [EVENTS]\n";
        return 2;
    }
    const std::string_view requested = argv[1];
    const scenario_options options{
        .socket_path = argv[2],
        .tree_path = argv[3],
        .event_path = argc == 5 ? std::optional<std::filesystem::path>{argv[4]} : std::nullopt,
    };
    for (const auto& [name, scenario] : kScenarios) {
        if (name != requested) {
            continue;
        }
        try {
            scenario(options);
            std::cout << "[PASS] " << name << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cerr << "unknown air-hockey scenario: " << requested << '\n';
    return 2;
}
