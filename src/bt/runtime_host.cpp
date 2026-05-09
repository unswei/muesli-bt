#include "bt/runtime_host.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

#include "muesli_bt/contract/events.hpp"
#include "muslisp/error.hpp"
#include "muslisp/gc.hpp"
#include "muslisp/printer.hpp"

namespace bt {
namespace {

class system_clock_service final : public clock_interface {
public:
    std::chrono::steady_clock::time_point now() const override { return std::chrono::steady_clock::now(); }
};

class demo_robot_service final : public robot_interface {
public:
    bool battery_ok(tick_context&) override { return true; }

    bool target_visible(tick_context& ctx) override {
        const bb_entry* entry = ctx.bb_get("target-visible");
        if (!entry) {
            return false;
        }
        if (const bool* b = std::get_if<bool>(&entry->value)) {
            return *b;
        }
        if (const std::int64_t* i = std::get_if<std::int64_t>(&entry->value)) {
            return *i != 0;
        }
        return false;
    }

    status approach_target(tick_context&, node_memory& mem) override {
        if (mem.i0 == 0) {
            mem.i0 = 1;
            return status::running;
        }
        mem.i0 = 0;
        return status::success;
    }

    status grasp(tick_context&, node_memory&) override { return status::success; }

    status search_target(tick_context& ctx, node_memory&) override {
        ctx.bb_put("target-visible", bb_value{true}, "robot.search-target");
        return status::success;
    }
};

std::string require_key_arg(const std::span<const muslisp::value> args, std::size_t index, const std::string& where) {
    if (index >= args.size()) {
        throw std::runtime_error(where + ": missing key argument");
    }
    if (muslisp::is_symbol(args[index])) {
        return muslisp::symbol_name(args[index]);
    }
    if (muslisp::is_string(args[index])) {
        return muslisp::string_value(args[index]);
    }
    throw std::runtime_error(where + ": key must be symbol or string");
}

std::int64_t require_int_arg(const std::span<const muslisp::value> args, std::size_t index, const std::string& where) {
    if (index >= args.size() || !muslisp::is_integer(args[index])) {
        throw std::runtime_error(where + ": expected integer argument at index " + std::to_string(index));
    }
    return muslisp::integer_value(args[index]);
}

double require_floaty_arg(const std::span<const muslisp::value> args, std::size_t index, const std::string& where) {
    if (index >= args.size()) {
        throw std::runtime_error(where + ": missing numeric argument");
    }
    if (muslisp::is_integer(args[index])) {
        return static_cast<double>(muslisp::integer_value(args[index]));
    }
    if (muslisp::is_float(args[index])) {
        return muslisp::float_value(args[index]);
    }
    throw std::runtime_error(where + ": expected numeric argument");
}

std::vector<double> bb_value_as_vector(const bb_value& value, const std::string& where) {
    if (const auto* i = std::get_if<std::int64_t>(&value)) {
        return {static_cast<double>(*i)};
    }
    if (const auto* f = std::get_if<double>(&value)) {
        return {*f};
    }
    if (const auto* vec = std::get_if<std::vector<double>>(&value)) {
        return *vec;
    }
    throw std::runtime_error(where + ": expected numeric or numeric-vector blackboard value");
}

bool bb_value_truthy(const bb_value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return false;
    }
    if (const auto* b = std::get_if<bool>(&value)) {
        return *b;
    }
    if (const auto* i = std::get_if<std::int64_t>(&value)) {
        return *i != 0;
    }
    if (const auto* f = std::get_if<double>(&value)) {
        return std::isfinite(*f) && std::fabs(*f) > 1e-9;
    }
    if (const auto* s = std::get_if<std::string>(&value)) {
        return !s->empty() && *s != "0" && *s != "false";
    }
    if (const auto* vec = std::get_if<std::vector<double>>(&value)) {
        return !vec->empty();
    }
    return true;
}

std::string require_key_arg_or_default(const std::span<const muslisp::value> args,
                                       std::size_t index,
                                       const std::string& where,
                                       std::string default_key) {
    if (index >= args.size()) {
        return default_key;
    }
    return require_key_arg(args, index, where);
}

job_status status_from_memory(std::int64_t raw) {
    if (raw < 0 || raw > static_cast<std::int64_t>(job_status::unknown)) {
        return job_status::unknown;
    }
    return static_cast<job_status>(raw);
}

std::int64_t status_to_memory(job_status st) {
    return static_cast<std::int64_t>(st);
}

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

std::filesystem::path model_service_cache_file(const model_service_config& config, const std::string& request_hash) {
    return std::filesystem::path(config.replay_cache_path) / (request_hash + ".json");
}

std::string trim_ascii(std::string text) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

std::string json_escape_string_fragment(std::string_view text) {
    std::ostringstream out;
    for (unsigned char ch : text) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (ch < 0x20) {
                out << "\\u00";
                constexpr char hex[] = "0123456789abcdef";
                out << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
            } else {
                out << static_cast<char>(ch);
            }
            break;
        }
    }
    return out.str();
}

std::optional<model_service_response> read_model_service_cache(const model_service_config& config,
                                                               const std::string& request_hash) {
    if (config.replay_cache_path.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path path = model_service_cache_file(config, request_hash);
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    model_service_response response = model_service_response_from_json(text);
    response.request_hash = request_hash;
    response.response_hash = event_log::hash64_hex(response.raw_json.empty() ? model_service_response_to_json(response)
                                                                             : response.raw_json);
    response.replay_cache_hit = true;
    response.host_reached = false;
    return response;
}

void write_model_service_cache(const model_service_config& config,
                               const std::string& request_hash,
                               const model_service_response& response) {
    if (config.replay_cache_path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(config.replay_cache_path, ec);
    if (ec) {
        return;
    }
    const std::filesystem::path path = model_service_cache_file(config, request_hash);
    std::ofstream out(path);
    if (!out) {
        return;
    }
    out << (response.raw_json.empty() ? model_service_response_to_json(response) : response.raw_json);
}

model_service_response model_service_replay_miss(const model_service_request& request, const std::string& request_hash) {
    model_service_response out;
    out.id = request.id;
    out.status = model_service_status::unavailable;
    out.error_code = "model_service_replay_miss";
    out.error_message = "model-service replay cache miss";
    out.error_retryable = false;
    out.host_reached = false;
    out.request_hash = request_hash;
    out.raw_json = model_service_response_to_json(out);
    out.response_hash = event_log::hash64_hex(out.raw_json);
    return out;
}

std::string model_service_fault_output_json(const model_service_request& request, std::string_view fault) {
    if (fault == "invalid_output") {
        return "{}";
    }
    if (request.capability == "cap.model.world.rollout.v1") {
        if (fault == "unsafe_output") {
            return "{\"predicted_states\":[],\"unsafe\":true}";
        }
        if (fault == "stale_result") {
            return "{\"predicted_states\":[],\"stale\":true}";
        }
        if (fault == "policy_violation") {
            return "{\"predicted_states\":[],\"policy_violation\":true}";
        }
        return "{\"predicted_states\":[]}";
    }
    if (request.capability == "cap.model.world.score_trajectory.v1") {
        if (fault == "unsafe_output") {
            return "{\"score\":0.0,\"unsafe\":true}";
        }
        if (fault == "stale_result") {
            return "{\"score\":0.0,\"stale\":true}";
        }
        if (fault == "policy_violation") {
            return "{\"score\":0.0,\"policy_violation\":true}";
        }
        return "{\"score\":0.0}";
    }
    if (request.capability == "cap.vla.action_chunk.v1") {
        if (fault == "unsafe_output") {
            return "{\"actions\":[],\"unsafe\":true}";
        }
        if (fault == "stale_result") {
            return "{\"actions\":[],\"stale\":true}";
        }
        if (fault == "policy_violation") {
            return "{\"actions\":[],\"policy_violation\":true}";
        }
        return "{\"actions\":[]}";
    }
    if (request.capability == "cap.vla.propose_nav_goal.v1") {
        if (fault == "unsafe_output") {
            return "{\"goal\":{},\"unsafe\":true}";
        }
        if (fault == "stale_result") {
            return "{\"goal\":{},\"stale\":true}";
        }
        if (fault == "policy_violation") {
            return "{\"goal\":{},\"policy_violation\":true}";
        }
        return "{\"goal\":{}}";
    }
    return "{}";
}

std::optional<model_service_response> injected_model_service_fault(const model_service_request& request,
                                                                   std::string fault) {
    fault = trim_ascii(std::move(fault));
    if (fault.empty() || fault == "none" || fault == "pass") {
        return std::nullopt;
    }
    if (fault.rfind("delay:", 0) == 0) {
        const std::string raw_ms = trim_ascii(fault.substr(std::string("delay:").size()));
        if (!raw_ms.empty()) {
            long long delay_ms = 0;
            try {
                delay_ms = std::max<long long>(0, std::stoll(raw_ms));
            } catch (...) {
                delay_ms = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        return std::nullopt;
    }

    model_service_response out;
    out.id = request.id;
    out.host_reached = false;
    out.metadata_json = "{\"fault_injected\":true,\"fault\":\"" + json_escape_string_fragment(fault) + "\"}";

    if (fault == "timeout") {
        out.status = model_service_status::timeout;
        out.error_code = "model_service_fault_timeout";
        out.error_message = "deterministic model-service timeout fault";
        out.error_retryable = true;
    } else if (fault == "unavailable" || fault == "backend_unavailable") {
        out.status = model_service_status::unavailable;
        out.error_code = "model_service_fault_unavailable";
        out.error_message = "deterministic model-service unavailable fault";
        out.error_retryable = true;
    } else if (fault == "invalid_output" || fault == "unsafe_output" ||
               fault == "stale_result" || fault == "policy_violation") {
        out.status = model_service_status::success;
        out.output_json = model_service_fault_output_json(request, fault);
    } else {
        out.status = model_service_status::internal_error;
        out.error_code = "model_service_fault_unknown";
        out.error_message = "unknown deterministic model-service fault: " + fault;
        out.error_retryable = false;
    }

    out.raw_json = model_service_response_to_json(out);
    return out;
}

}  // namespace

runtime_host::runtime_host()
    : scheduler_(0),
      logs_(4096),
      events_(8192),
      vla_(&scheduler_),
      owned_clock_(std::make_unique<system_clock_service>()),
      owned_robot_(std::make_unique<demo_robot_service>()) {
    install_demo_callbacks(*this);

    clock_ = owned_clock_.get();
    robot_ = owned_robot_.get();
    events_.set_host_info("muesli-bt", "dev", "native");
    events_.set_run_id("run-" + std::to_string(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count()));
    planner_.set_record_listener([this](const planner_record& rec, const std::string& json) {
        std::optional<std::uint64_t> tick{};
        if (rec.tick_index > 0) {
            tick = rec.tick_index;
        }
        std::string data = std::string("{\"record\":") + json + '}';
        (void)events_.emit("planner_v1", tick, data);
    });
    vla_.set_record_listener([this](const vla_record& rec, const std::string& json) {
        std::optional<std::uint64_t> tick{};
        if (rec.tick_index > 0) {
            tick = rec.tick_index;
        }
        std::ostringstream data;
        data << "{\"job_id\":\"" << rec.request_hash << "\",\"node_id\":0,\"status\":\"" << rec.status
             << "\",\"digest\":\"" << event_log::hash64_hex(json) << "\",\"record\":" << json << '}';
        (void)events_.emit("vla_result", tick, data.str());

        if (rec.completion_dropped) {
            std::ostringstream drop_data;
            drop_data << "{\"job_id\":\"" << rec.request_hash
                      << "\",\"node_id\":0,\"reason\":\"completion_after_cancel\"}";
            (void)events_.emit(muesli_bt::contract::kEventAsyncCompletionDropped, tick, drop_data.str());
        }
    });
}

std::int64_t runtime_host::store_definition(definition def) {
    const std::int64_t handle = next_definition_handle_++;
    definitions_[handle] = std::move(def);
    return handle;
}

std::int64_t runtime_host::create_instance(std::int64_t definition_handle) {
    const definition* def = find_definition(definition_handle);
    if (!def) {
        throw std::invalid_argument("create_instance: unknown definition handle");
    }

    const std::int64_t handle = next_instance_handle_++;
    auto inst = std::make_unique<instance>(def);
    inst->instance_handle = handle;
    set_tick_budget_ms(*inst, 20);
    events_.emit_bt_def(*def);
    instances_[handle] = std::move(inst);
    return handle;
}

definition* runtime_host::find_definition(std::int64_t handle) {
    const auto it = definitions_.find(handle);
    return it == definitions_.end() ? nullptr : &it->second;
}

const definition* runtime_host::find_definition(std::int64_t handle) const {
    const auto it = definitions_.find(handle);
    return it == definitions_.end() ? nullptr : &it->second;
}

instance* runtime_host::find_instance(std::int64_t handle) {
    const auto it = instances_.find(handle);
    return it == instances_.end() ? nullptr : it->second.get();
}

const instance* runtime_host::find_instance(std::int64_t handle) const {
    const auto it = instances_.find(handle);
    return it == instances_.end() ? nullptr : it->second.get();
}

status runtime_host::tick_instance(std::int64_t handle) {
    instance* inst = find_instance(handle);
    if (!inst) {
        throw std::invalid_argument("tick_instance: unknown instance handle");
    }

    services svc;
    svc.sched = &scheduler_;
    svc.obs.trace = &inst->trace;
    svc.obs.logger = &logs_;
    svc.obs.events = &events_;
    svc.clock = clock_;
    svc.robot = robot_;
    svc.planner = &planner_;
    svc.vla = &vla_;

    return tick(*inst, registry_, svc);
}

void runtime_host::reset_instance(std::int64_t handle) {
    instance* inst = find_instance(handle);
    if (!inst) {
        throw std::invalid_argument("reset_instance: unknown instance handle");
    }
    reset(*inst);
}

registry& runtime_host::callbacks() noexcept {
    return registry_;
}

const registry& runtime_host::callbacks() const noexcept {
    return registry_;
}

scheduler& runtime_host::scheduler_ref() {
    return scheduler_;
}

const scheduler& runtime_host::scheduler_ref() const {
    return scheduler_;
}

planner_service& runtime_host::planner_ref() {
    return planner_;
}

const planner_service& runtime_host::planner_ref() const {
    return planner_;
}

vla_service& runtime_host::vla_ref() {
    return vla_;
}

const vla_service& runtime_host::vla_ref() const {
    return vla_;
}

void runtime_host::set_model_service_client(model_service_config config, std::unique_ptr<model_service_client> client) {
    model_service_config_ = std::move(config);
    model_service_client_ = std::move(client);
    model_service_fault_index_ = 0;
}

void runtime_host::clear_model_service_client() noexcept {
    model_service_client_.reset();
    model_service_config_ = model_service_config{};
    model_service_fault_index_ = 0;
}

bool runtime_host::model_service_configured() const noexcept {
    return static_cast<bool>(model_service_client_);
}

const model_service_config& runtime_host::model_service_config_ref() const noexcept {
    return model_service_config_;
}

model_service_response runtime_host::call_model_service(const model_service_request& request) {
    const std::string request_json = model_service_request_to_json(request);
    const std::string request_hash = event_log::hash64_hex(request_json);
    const std::string replay_mode = request.replay_mode.empty() ? model_service_config_.replay_mode
                                                                : request.replay_mode;
    if (replay_mode == "replay") {
        if (std::optional<model_service_response> cached = read_model_service_cache(model_service_config_, request_hash);
            cached.has_value()) {
            validate_model_service_response(request, *cached);
            return *cached;
        }
        return model_service_replay_miss(request, request_hash);
    }

    model_service_response response;
    if (model_service_fault_index_ < model_service_config_.fault_schedule.size()) {
        const std::string fault = model_service_config_.fault_schedule[model_service_fault_index_++];
        if (std::optional<model_service_response> injected = injected_model_service_fault(request, fault);
            injected.has_value()) {
            response = std::move(*injected);
        }
    }
    if (response.id.empty() && !model_service_client_) {
        unavailable_model_service_client unavailable;
        response = unavailable.call(request);
    } else if (response.id.empty()) {
        response = model_service_client_->call(request);
    }

    response.request_hash = request_hash;
    if (response.raw_json.empty()) {
        response.raw_json = model_service_response_to_json(response);
    }
    response.response_hash = event_log::hash64_hex(response.raw_json);
    response.replay_cache_hit = false;
    validate_model_service_response(request, response);
    if (replay_mode == "record") {
        write_model_service_cache(model_service_config_, request_hash, response);
    }
    return response;
}

model_service_compatibility_result runtime_host::check_model_service_compatibility() {
    if (!model_service_client_) {
        unavailable_model_service_client unavailable;
        return bt::check_model_service_compatibility(unavailable, model_service_required_capabilities(),
                                                     model_service_config_.request_timeout_ms);
    }
    return bt::check_model_service_compatibility(*model_service_client_, model_service_required_capabilities(),
                                                 model_service_config_.request_timeout_ms);
}

memory_log_sink& runtime_host::logs() noexcept {
    return logs_;
}

const memory_log_sink& runtime_host::logs() const noexcept {
    return logs_;
}

event_log& runtime_host::events() noexcept {
    return events_;
}

const event_log& runtime_host::events() const noexcept {
    return events_;
}

void runtime_host::set_clock_interface(clock_interface* clock) noexcept {
    clock_ = clock ? clock : owned_clock_.get();
}

clock_interface* runtime_host::clock_interface_ptr() noexcept {
    return clock_;
}

const clock_interface* runtime_host::clock_interface_ptr() const noexcept {
    return clock_;
}

void runtime_host::set_robot_interface(robot_interface* robot) noexcept {
    robot_ = robot ? robot : owned_robot_.get();
}

robot_interface* runtime_host::robot_interface_ptr() noexcept {
    return robot_;
}

const robot_interface* runtime_host::robot_interface_ptr() const noexcept {
    return robot_;
}

void runtime_host::enable_deterministic_test_mode(std::uint64_t planner_seed,
                                                  std::string run_id,
                                                  std::int64_t unix_ms_start,
                                                  std::int64_t unix_ms_step) {
    if (run_id.empty()) {
        run_id = "deterministic-run";
    }
    deterministic_test_mode_enabled_ = true;
    planner_.set_base_seed(planner_seed);
    events_.set_run_id(std::move(run_id));
    events_.set_deterministic_time(unix_ms_start, unix_ms_step);
}

void runtime_host::disable_deterministic_test_mode() noexcept {
    deterministic_test_mode_enabled_ = false;
    events_.clear_deterministic_time();
}

bool runtime_host::deterministic_test_mode_enabled() const noexcept {
    return deterministic_test_mode_enabled_;
}

void runtime_host::clear_logs() {
    logs_.clear();
    events_.clear_ring();
}

void runtime_host::clear_all() {
    definitions_.clear();
    instances_.clear();
    registry_.clear();
    logs_.clear();
    events_.clear_ring();
    planner_.clear_records();
    vla_.clear_records();
    clear_model_service_client();
}

std::string runtime_host::dump_instance_stats(std::int64_t handle) const {
    const instance* inst = find_instance(handle);
    if (!inst) {
        throw std::invalid_argument("dump_instance_stats: unknown instance handle");
    }
    return dump_stats(*inst);
}

std::string runtime_host::dump_instance_trace(std::int64_t handle) const {
    const instance* inst = find_instance(handle);
    if (!inst) {
        throw std::invalid_argument("dump_instance_trace: unknown instance handle");
    }
    return dump_trace(*inst);
}

std::string runtime_host::dump_instance_blackboard(std::int64_t handle) const {
    const instance* inst = find_instance(handle);
    if (!inst) {
        throw std::invalid_argument("dump_instance_blackboard: unknown instance handle");
    }
    return dump_blackboard(*inst);
}

std::string runtime_host::dump_scheduler_stats() const {
    const scheduler_profile_stats stats = scheduler_.stats_snapshot();

    std::ostringstream out;
    out << "submitted=" << stats.submitted << '\n';
    out << "started=" << stats.started << '\n';
    out << "completed=" << stats.completed << '\n';
    out << "failed=" << stats.failed << '\n';
    out << "cancelled=" << stats.cancelled << '\n';
    out << "queue_delay_last_ns=" << stats.queue_delay.last.count() << '\n';
    out << "run_time_last_ns=" << stats.run_time.last.count() << '\n';
    return out.str();
}

std::string runtime_host::dump_logs() const {
    std::ostringstream out;
    const auto events = events_.snapshot();
    for (const auto& line : events) {
        out << line << '\n';
    }
    return out.str();
}

std::string runtime_host::dump_planner_records(std::size_t max_count) const {
    return planner_.dump_recent_records(max_count);
}

std::string runtime_host::dump_vla_records(std::size_t max_count) const {
    return vla_.dump_recent_records(max_count);
}

runtime_host& default_runtime_host() {
    static runtime_host host;
    runtime_host* host_ptr = &host;
    muslisp::default_gc().set_lifecycle_listener([host_ptr](const muslisp::gc_lifecycle_event& event) {
        (void)host_ptr->events().emit(event.begin ? muesli_bt::contract::kEventGcBegin
                                                  : muesli_bt::contract::kEventGcEnd,
                                      std::nullopt,
                                      gc_lifecycle_payload_json(event));
    });
    return host;
}

void install_demo_callbacks(runtime_host& host) {
    registry& reg = host.callbacks();

    reg.register_condition("always-true", [](tick_context&, std::span<const muslisp::value>) { return true; });
    reg.register_condition("always-false", [](tick_context&, std::span<const muslisp::value>) { return false; });

    reg.register_condition("bb-has", [](tick_context& ctx, std::span<const muslisp::value> args) {
        const std::string key = require_key_arg(args, 0, "bb-has");
        return ctx.bb_get(key) != nullptr;
    });

    reg.register_condition("bb-truthy", [](tick_context& ctx, std::span<const muslisp::value> args) {
        const std::string key = require_key_arg(args, 0, "bb-truthy");
        const bb_entry* entry = ctx.bb_get(key);
        if (!entry) {
            return false;
        }
        return bb_value_truthy(entry->value);
    });

    reg.register_condition("battery-ok", [](tick_context& ctx, std::span<const muslisp::value>) {
        if (!ctx.svc.robot) {
            return false;
        }
        return ctx.svc.robot->battery_ok(ctx);
    });

    reg.register_condition("target-visible", [](tick_context& ctx, std::span<const muslisp::value>) {
        if (!ctx.svc.robot) {
            return false;
        }
        return ctx.svc.robot->target_visible(ctx);
    });

    reg.register_action("always-success",
                        [](tick_context&, node_id, node_memory&, std::span<const muslisp::value>) { return status::success; });

    reg.register_action("always-fail",
                        [](tick_context&, node_id, node_memory&, std::span<const muslisp::value>) { return status::failure; });

    reg.register_action("running-then-success", [](tick_context&, node_id, node_memory& mem, std::span<const muslisp::value> args) {
        const std::int64_t target = args.empty() ? 1 : require_int_arg(args, 0, "running-then-success");
        if (mem.i0 < target) {
            ++mem.i0;
            return status::running;
        }
        mem.i0 = 0;
        return status::success;
    });

    reg.register_action("bb-put-int", [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
        const std::string key = require_key_arg(args, 0, "bb-put-int");
        const std::int64_t v = require_int_arg(args, 1, "bb-put-int");
        ctx.bb_put(key, bb_value{v}, "bb-put-int");
        return status::success;
    });

    reg.register_action("bb-put-float", [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
        const std::string key = require_key_arg(args, 0, "bb-put-float");
        const double v = require_floaty_arg(args, 1, "bb-put-float");
        ctx.bb_put(key, bb_value{v}, "bb-put-float");
        return status::success;
    });

    reg.register_action("select-action",
                        [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
                            const std::string source_key = require_key_arg(args, 0, "select-action");
                            const std::int64_t branch_id = require_int_arg(args, 1, "select-action");
                            std::string out_key = "action_vec";
                            if (args.size() > 2) {
                                out_key = require_key_arg(args, 2, "select-action");
                            }

                            const bb_entry* source = ctx.bb_get(source_key);
                            if (!source) {
                                return status::failure;
                            }

                            std::vector<double> action;
                            try {
                                action = bb_value_as_vector(source->value, "select-action");
                            } catch (const std::exception&) {
                                return status::failure;
                            }
                            if (action.size() < 2) {
                                return status::failure;
                            }

                            ctx.bb_put(out_key, bb_value{std::move(action)}, "select-action");
                            ctx.bb_put("active_branch", bb_value{branch_id}, "select-action");
                            return status::success;
                        });

    reg.register_condition("goal-reached-1d", [](tick_context& ctx, std::span<const muslisp::value> args) {
        const std::string state_key = require_key_arg_or_default(args, 0, "goal-reached-1d", "state");
        const double goal = args.size() > 1 ? require_floaty_arg(args, 1, "goal-reached-1d") : 1.0;
        const double tol = args.size() > 2 ? require_floaty_arg(args, 2, "goal-reached-1d") : 0.05;

        const bb_entry* state_entry = ctx.bb_get(state_key);
        if (!state_entry) {
            return false;
        }
        const std::vector<double> state = bb_value_as_vector(state_entry->value, "goal-reached-1d");
        if (state.empty()) {
            return false;
        }
        return std::fabs(goal - state[0]) <= tol;
    });

    reg.register_action("apply-planned-1d", [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
        const std::string state_key = require_key_arg_or_default(args, 0, "apply-planned-1d", "state");
        const std::string action_key = require_key_arg_or_default(args, 1, "apply-planned-1d", "action");
        const std::string out_key = require_key_arg_or_default(args, 2, "apply-planned-1d", state_key);

        const bb_entry* state_entry = ctx.bb_get(state_key);
        const bb_entry* action_entry = ctx.bb_get(action_key);
        if (!state_entry || !action_entry) {
            return status::failure;
        }

        const std::vector<double> state = bb_value_as_vector(state_entry->value, "apply-planned-1d state");
        const std::vector<double> action = bb_value_as_vector(action_entry->value, "apply-planned-1d action");
        if (state.empty() || action.empty()) {
            return status::failure;
        }

        const double a = std::clamp(action[0], -1.0, 1.0);
        const double x2 = state[0] + 0.25 * a;
        ctx.bb_put(out_key, bb_value{x2}, "apply-planned-1d");
        return status::success;
    });

    reg.register_condition("ptz-target-centred", [](tick_context& ctx, std::span<const muslisp::value> args) {
        const std::string state_key = require_key_arg_or_default(args, 0, "ptz-target-centred", "ptz-state");
        const double tol = args.size() > 1 ? require_floaty_arg(args, 1, "ptz-target-centred") : 0.05;
        const bb_entry* state_entry = ctx.bb_get(state_key);
        if (!state_entry) {
            return false;
        }
        const std::vector<double> state = bb_value_as_vector(state_entry->value, "ptz-target-centred state");
        if (state.size() < 4) {
            return false;
        }
        const double dist = std::sqrt(state[2] * state[2] + state[3] * state[3]);
        return dist <= tol;
    });

    reg.register_action("apply-planned-ptz", [](tick_context& ctx, node_id, node_memory&, std::span<const muslisp::value> args) {
        const std::string state_key = require_key_arg_or_default(args, 0, "apply-planned-ptz", "ptz-state");
        const std::string action_key = require_key_arg_or_default(args, 1, "apply-planned-ptz", "ptz-action");
        const std::string out_key = require_key_arg_or_default(args, 2, "apply-planned-ptz", state_key);

        const bb_entry* state_entry = ctx.bb_get(state_key);
        const bb_entry* action_entry = ctx.bb_get(action_key);
        if (!state_entry || !action_entry) {
            return status::failure;
        }

        const std::vector<double> state = bb_value_as_vector(state_entry->value, "apply-planned-ptz state");
        const std::vector<double> action = bb_value_as_vector(action_entry->value, "apply-planned-ptz action");
        if (state.size() < 4 || action.size() < 2) {
            return status::failure;
        }

        const double pan = state[0];
        const double tilt = state[1];
        const double bx = state[2];
        const double by = state[3];
        const double vx = state.size() > 4 ? state[4] : 0.0;
        const double vy = state.size() > 5 ? state[5] : 0.0;

        const double dpan = std::clamp(action[0], -0.25, 0.25);
        const double dtilt = std::clamp(action[1], -0.25, 0.25);

        const double pan2 = std::clamp(pan + dpan, -1.5, 1.5);
        const double tilt2 = std::clamp(tilt + dtilt, -1.0, 1.0);
        const double bx2 = std::clamp(bx + vx - dpan * 1.15, -2.0, 2.0);
        const double by2 = std::clamp(by + vy - dtilt * 1.15, -2.0, 2.0);

        std::vector<double> next_state{pan2, tilt2, bx2, by2, vx, vy};
        ctx.bb_put(out_key, bb_value{next_state}, "apply-planned-ptz");
        return status::success;
    });

    reg.register_action("approach-target",
                        [](tick_context& ctx, node_id, node_memory& mem, std::span<const muslisp::value>) {
                            if (!ctx.svc.robot) {
                                return status::failure;
                            }
                            return ctx.svc.robot->approach_target(ctx, mem);
                        });

    reg.register_action("grasp", [](tick_context& ctx, node_id, node_memory& mem, std::span<const muslisp::value>) {
        if (!ctx.svc.robot) {
            return status::failure;
        }
        return ctx.svc.robot->grasp(ctx, mem);
    });

    reg.register_action("search-target", [](tick_context& ctx, node_id, node_memory& mem, std::span<const muslisp::value>) {
        if (!ctx.svc.robot) {
            return status::failure;
        }
        return ctx.svc.robot->search_target(ctx, mem);
    });

    reg.register_action("async-sleep-ms", [](tick_context& ctx, node_id, node_memory& mem, std::span<const muslisp::value> args) {
        if (!ctx.svc.sched) {
            return status::failure;
        }

        const std::int64_t delay_ms = args.empty() ? 1 : require_int_arg(args, 0, "async-sleep-ms");

        if (!mem.b0) {
            job_request req;
            req.task_name = "async-sleep-ms";
            req.fn = [delay_ms] {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                job_result out;
                out.payload = delay_ms;
                return out;
            };

            const job_id id = ctx.svc.sched->submit(std::move(req));
            mem.i0 = static_cast<std::int64_t>(id);
            mem.b0 = true;
            mem.i1 = status_to_memory(job_status::queued);
            ctx.scheduler_event(trace_event_kind::scheduler_submit,
                                id,
                                job_status::queued,
                                "scheduler submitted task async-sleep-ms");
            return status::running;
        }

        const job_id id = static_cast<job_id>(mem.i0);
        const job_info info = ctx.svc.sched->get_info(id);

        const job_status prev_status = status_from_memory(mem.i1);
        if (info.status != prev_status) {
            if (info.status == job_status::running) {
                ctx.scheduler_event(trace_event_kind::scheduler_start, id, info.status, "scheduler started task async-sleep-ms");
            } else if (info.status == job_status::cancelled) {
                ctx.scheduler_event(trace_event_kind::scheduler_cancel, id, info.status, "scheduler cancelled task async-sleep-ms");
            } else if (info.status == job_status::done || info.status == job_status::failed) {
                std::string message = "scheduler finished task async-sleep-ms";
                if (!info.error_text.empty()) {
                    message += ": ";
                    message += info.error_text;
                }
                ctx.scheduler_event(trace_event_kind::scheduler_finish, id, info.status, std::move(message));
            }
            mem.i1 = status_to_memory(info.status);
        }

        if (info.status == job_status::queued || info.status == job_status::running) {
            return status::running;
        }

        mem.b0 = false;
        mem.i0 = 0;
        mem.i1 = status_to_memory(job_status::unknown);
        if (info.status == job_status::done) {
            job_result out;
            (void)ctx.svc.sched->try_get_result(id, out);
            return status::success;
        }

        return status::failure;
    });
}

}  // namespace bt
