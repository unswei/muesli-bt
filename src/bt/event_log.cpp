#include "bt/event_log.hpp"

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "muesli_bt/contract/version.hpp"

namespace bt {
namespace {

std::int64_t unix_ms_now() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::uint64_t fnv1a_64(std::string_view text) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : text) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

void append_json_escaped(std::string& out, std::string_view text) {
    for (const char c : text) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
}

std::size_t escaped_json_size(std::string_view text) noexcept {
    std::size_t size = 0;
    for (const char c : text) {
        switch (c) {
            case '\\':
            case '"':
            case '\n':
            case '\r':
            case '\t':
                size += 2u;
                break;
            default:
                size += 1u;
                break;
        }
    }
    return size;
}

template <typename Integer>
void append_integer(std::string& out, Integer value) {
    char buffer[32];
    const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec != std::errc{}) {
        throw std::runtime_error("event_log: failed to format integer");
    }
    out.append(buffer, ptr);
}

template <typename Integer>
std::size_t integer_length(Integer value) noexcept {
    char buffer[32];
    const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec != std::errc{}) {
        return 0u;
    }
    return static_cast<std::size_t>(ptr - buffer);
}

}  // namespace

event_log::event_log(std::size_t ring_capacity) : ring_capacity_(ring_capacity) {
    ring_.reserve(ring_capacity_);
}

void event_log::set_enabled(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = enabled;
}

bool event_log::enabled() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

void event_log::set_ring_capacity(std::size_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_capacity_ = capacity;
    if (ring_capacity_ == 0) {
        ring_.clear();
        return;
    }
    if (ring_.size() > ring_capacity_) {
        ring_.erase(ring_.begin(), ring_.begin() + static_cast<std::ptrdiff_t>(ring_.size() - ring_capacity_));
    }
}

std::size_t event_log::ring_capacity() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return ring_capacity_;
}

void event_log::set_path(std::string path) {
    if (path.empty()) {
        throw std::invalid_argument("events.set-path: path must not be empty");
    }
    std::string updated_path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path_ = std::move(path);
        updated_path = path_;
    }
    std::lock_guard<std::mutex> file_lock(file_mutex_);
    if (file_stream_.is_open() && open_path_ != updated_path) {
        file_stream_.flush();
        file_stream_.close();
        open_path_.clear();
    }
}

const std::string& event_log::path() const noexcept {
    return path_;
}

void event_log::set_file_enabled(bool enabled) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        file_enabled_ = enabled;
    }
    if (!enabled) {
        std::lock_guard<std::mutex> file_lock(file_mutex_);
        if (file_stream_.is_open()) {
            file_stream_.flush();
            file_stream_.close();
        }
        open_path_.clear();
    }
}

bool event_log::file_enabled() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_enabled_;
}

void event_log::set_flush_on_tick_end(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_on_tick_end_ = enabled;
}

bool event_log::flush_on_tick_end() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return flush_on_tick_end_;
}

void event_log::set_flush_each_message(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    flush_each_message_ = enabled;
}

bool event_log::flush_each_message() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return flush_each_message_;
}

void event_log::set_tick_audit_enabled(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    tick_audit_enabled_ = enabled;
}

bool event_log::tick_audit_enabled() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return tick_audit_enabled_;
}

void event_log::set_tick_audit_warmup_complete(bool complete) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    tick_audit_warmup_complete_ = complete;
}

bool event_log::tick_audit_warmup_complete() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return tick_audit_warmup_complete_;
}

void event_log::set_tick_audit_strict_allocations(bool strict) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    tick_audit_strict_allocations_ = strict;
}

bool event_log::tick_audit_strict_allocations() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return tick_audit_strict_allocations_;
}

void event_log::set_run_id(std::string run_id) {
    if (run_id.empty()) {
        throw std::invalid_argument("set_run_id: run_id must not be empty");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    run_id_ = std::move(run_id);
    seq_ = 0;
    run_started_ = false;
}

std::string event_log::run_id() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return run_id_;
}

void event_log::set_tick_hz(double hz) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hz > 0.0) {
        tick_hz_ = hz;
    }
}

double event_log::tick_hz() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return tick_hz_;
}

void event_log::set_git_sha(std::string git_sha) {
    std::lock_guard<std::mutex> lock(mutex_);
    git_sha_ = git_sha.empty() ? "unknown" : std::move(git_sha);
}

void event_log::set_host_info(std::string name, std::string version, std::string platform) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!name.empty()) {
        host_name_ = std::move(name);
    }
    if (!version.empty()) {
        host_version_ = std::move(version);
    }
    if (!platform.empty()) {
        host_platform_ = std::move(platform);
    }
}

void event_log::ensure_run_started(std::string_view tree_hash, std::string_view capabilities_json) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (run_started_) {
            return;
        }
    }

    std::ostringstream data;
    data << "{\"git_sha\":\"" << json_escape(git_sha_) << "\","
         << "\"host\":{\"name\":\"" << json_escape(host_name_) << "\",\"version\":\"" << json_escape(host_version_)
         << "\",\"platform\":\"" << json_escape(host_platform_) << "\"},"
         << "\"contract_version\":\"" << runtime_contract_version() << "\","
         << "\"contract_id\":\"" << runtime_contract_id() << "\","
         << "\"tick_hz\":" << tick_hz_ << ","
         << "\"tree_hash\":\"" << json_escape(std::string(tree_hash)) << "\","
         << "\"capabilities\":" << capabilities_json << '}';
    (void)emit("run_start", std::nullopt, data.str());

    std::lock_guard<std::mutex> lock(mutex_);
    run_started_ = true;
}

void event_log::emit_bt_def(const definition& def) {
    std::ostringstream graph;
    graph << "root=" << def.root;
    for (const node& n : def.nodes) {
        graph << "|id=" << n.id << "|kind=" << static_cast<int>(n.kind) << "|name=" << n.leaf_name;
        for (node_id child : n.children) {
            graph << "->" << child;
        }
    }
    const std::string tree_hash = hash64_hex(graph.str());

    std::ostringstream data;
    data << "{\"tree_name\":\"bt\","
         << "\"dsl\":\"" << json_escape("<runtime-definition>") << "\","
         << "\"tree_hash\":\"" << tree_hash << "\","
         << "\"nodes\":[";
    for (std::size_t i = 0; i < def.nodes.size(); ++i) {
        if (i != 0) {
            data << ',';
        }
        const node& n = def.nodes[i];
        data << "{\"id\":" << n.id << ",\"kind\":\"" << node_kind_name(n.kind) << "\",\"name\":\""
             << json_escape(n.leaf_name.empty() ? ("node-" + std::to_string(n.id)) : n.leaf_name) << "\"}";
    }
    data << "],\"edges\":[";
    bool first_edge = true;
    for (const node& n : def.nodes) {
        for (std::size_t i = 0; i < n.children.size(); ++i) {
            if (!first_edge) {
                data << ',';
            }
            first_edge = false;
            data << "{\"parent\":" << n.id << ",\"child\":" << n.children[i] << ",\"index\":" << i << '}';
        }
    }
    data << "]}";
    ensure_run_started(tree_hash);
    (void)emit("bt_def", std::nullopt, data.str());
}

std::uint64_t event_log::emit(std::string_view type, std::optional<std::uint64_t> tick, std::string_view data_json) {
    bool file_enabled = false;
    bool flush_on_tick_end = true;
    bool flush_each_message = false;
    bool capture_stats_enabled = false;
    bool needs_serialised_line = false;
    std::uint64_t seq = 0;
    std::string run_id;
    std::int64_t unix_ms = 0;
    line_listener listener{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        seq = ++seq_;
        if (!enabled_) {
            return seq;
        }
        file_enabled = file_enabled_;
        flush_on_tick_end = flush_on_tick_end_;
        flush_each_message = flush_each_message_;
        capture_stats_enabled = capture_stats_enabled_;
        run_id = run_id_;
        if (deterministic_time_enabled_) {
            unix_ms = deterministic_unix_ms_;
            deterministic_unix_ms_ += deterministic_step_ms_;
        } else {
            unix_ms = unix_ms_now();
        }
        listener = line_listener_;
        needs_serialised_line = file_enabled || ring_capacity_ != 0u || static_cast<bool>(listener);
    }

    std::string line;
    std::size_t serialised_size = 0u;
    if (needs_serialised_line) {
        line = serialise_event_line(type, run_id, unix_ms, seq, tick, data_json);
        serialised_size = line.size();
    } else if (capture_stats_enabled) {
        serialised_size = serialise_event_line_size(type, run_id, unix_ms, seq, tick, data_json);
    }

    if (capture_stats_enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++captured_event_count_;
        captured_byte_count_ += static_cast<std::uint64_t>(serialised_size);
    }

    if (needs_serialised_line) {
        append_ring_line(line);
        if (file_enabled) {
            const bool flush_now = flush_each_message || !flush_on_tick_end || type == "tick_end";
            append_file_line(line, flush_now);
        }
    }
    if (listener) {
        listener(line);
    }
    return seq;
}

void event_log::set_line_listener(line_listener listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    line_listener_ = std::move(listener);
}

void event_log::clear_line_listener() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    line_listener_ = {};
}

bool event_log::has_line_listener() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<bool>(line_listener_);
}

void event_log::set_deterministic_time(std::int64_t start_unix_ms, std::int64_t step_ms) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    deterministic_time_enabled_ = true;
    deterministic_unix_ms_ = start_unix_ms;
    deterministic_step_ms_ = step_ms;
}

void event_log::clear_deterministic_time() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    deterministic_time_enabled_ = false;
    deterministic_unix_ms_ = 0;
    deterministic_step_ms_ = 1;
}

bool event_log::deterministic_time_enabled() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return deterministic_time_enabled_;
}

void event_log::set_capture_stats_enabled(bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    capture_stats_enabled_ = enabled;
}

bool event_log::capture_stats_enabled() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return capture_stats_enabled_;
}

void event_log::clear_capture_stats() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    captured_event_count_ = 0u;
    captured_byte_count_ = 0u;
}

event_log_stats event_log::capture_stats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return event_log_stats{
        .event_count = captured_event_count_,
        .byte_count = captured_byte_count_,
    };
}

std::string_view event_log::runtime_contract_version() noexcept {
    return muesli_bt::contract::kRuntimeContractVersion;
}

std::string_view event_log::runtime_contract_id() noexcept {
    return muesli_bt::contract::kRuntimeContractId;
}

std::size_t event_log::serialise_event_line_size(std::string_view type,
                                                 std::string_view run_id,
                                                 std::int64_t unix_ms,
                                                 std::uint64_t seq,
                                                 std::optional<std::uint64_t> tick,
                                                 std::string_view data_json) {
    std::size_t size = 0u;
    size += sizeof("{\"schema\":\"mbt.evt.v1\",\"contract_version\":\"") - 1u;
    size += escaped_json_size(runtime_contract_version());
    size += sizeof("\",\"type\":\"") - 1u;
    size += escaped_json_size(type);
    size += sizeof("\",\"run_id\":\"") - 1u;
    size += escaped_json_size(run_id);
    size += sizeof("\",\"unix_ms\":") - 1u;
    size += integer_length(unix_ms);
    size += sizeof(",\"seq\":") - 1u;
    size += integer_length(seq);
    if (tick.has_value()) {
        size += sizeof(",\"tick\":") - 1u;
        size += integer_length(*tick);
    }
    size += sizeof(",\"data\":") - 1u;
    size += data_json.size();
    size += 1u;  // closing brace
    return size;
}

std::string event_log::serialise_event_line(std::string_view type,
                                            std::string_view run_id,
                                            std::int64_t unix_ms,
                                            std::uint64_t seq,
                                            std::optional<std::uint64_t> tick,
                                            std::string_view data_json) {
    std::string out;
    out.reserve(serialise_event_line_size(type, run_id, unix_ms, seq, tick, data_json));

    out += "{\"schema\":\"mbt.evt.v1\",\"contract_version\":\"";
    append_json_escaped(out, runtime_contract_version());
    out += "\",\"type\":\"";
    append_json_escaped(out, type);
    out += "\",\"run_id\":\"";
    append_json_escaped(out, run_id);
    out += "\",\"unix_ms\":";
    append_integer(out, unix_ms);
    out += ",\"seq\":";
    append_integer(out, seq);
    if (tick.has_value()) {
        out += ",\"tick\":";
        append_integer(out, *tick);
    }
    out += ",\"data\":";
    out.append(data_json);
    out.push_back('}');
    return out;
}

std::vector<std::string> event_log::snapshot(std::size_t max_count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (max_count == 0 || max_count >= ring_.size()) {
        return ring_;
    }
    return std::vector<std::string>(ring_.end() - static_cast<std::ptrdiff_t>(max_count), ring_.end());
}

void event_log::clear_ring() {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_.clear();
}

void event_log::request_snapshot_bb(bool full) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_bb_requested_ = true;
    snapshot_bb_full_ = full;
}

bool event_log::consume_snapshot_bb_request(bool* full) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_bb_requested_) {
        if (full) {
            *full = false;
        }
        return false;
    }
    snapshot_bb_requested_ = false;
    if (full) {
        *full = snapshot_bb_full_;
    }
    return true;
}

std::string event_log::json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string event_log::hash64_hex(std::string_view text) {
    const std::uint64_t h = fnv1a_64(text);
    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << h;
    return out.str();
}

void event_log::append_ring_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ring_capacity_ == 0) {
        return;
    }
    if (ring_.size() == ring_capacity_) {
        ring_.erase(ring_.begin());
    }
    ring_.push_back(line);
}

void event_log::append_file_line(const std::string& line, bool flush_now) {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        path = path_;
    }
    std::lock_guard<std::mutex> file_lock(file_mutex_);
    if (!file_stream_.is_open() || open_path_ != path) {
        if (file_stream_.is_open()) {
            file_stream_.flush();
            file_stream_.close();
        }
        std::filesystem::path fs_path(path);
        if (fs_path.has_parent_path()) {
            std::filesystem::create_directories(fs_path.parent_path());
        }
        file_stream_.open(path, std::ios::app);
        if (!file_stream_) {
            file_stream_.close();
            open_path_.clear();
            return;
        }
        open_path_ = path;
    }
    file_stream_ << line << '\n';
    if (!file_stream_) {
        file_stream_.close();
        open_path_.clear();
        return;
    }
    if (flush_now) {
        file_stream_.flush();
        if (!file_stream_) {
            file_stream_.close();
            open_path_.clear();
        }
    }
}

std::string event_log::node_kind_name(node_kind kind) {
    switch (kind) {
        case node_kind::seq:
            return "seq";
        case node_kind::sel:
            return "sel";
        case node_kind::invert:
            return "invert";
        case node_kind::repeat:
            return "repeat";
        case node_kind::retry:
            return "retry";
        case node_kind::cond:
            return "cond";
        case node_kind::act:
            return "act";
        case node_kind::succeed:
            return "succeed";
        case node_kind::fail:
            return "fail";
        case node_kind::running:
            return "running";
        case node_kind::plan_action:
            return "plan_action";
        case node_kind::vla_request:
            return "vla_request";
        case node_kind::vla_wait:
            return "vla_wait";
        case node_kind::vla_cancel:
            return "vla_cancel";
        case node_kind::mem_seq:
            return "mem_seq";
        case node_kind::mem_sel:
            return "mem_sel";
        case node_kind::async_seq:
            return "async_seq";
        case node_kind::reactive_seq:
            return "reactive_seq";
        case node_kind::reactive_sel:
            return "reactive_sel";
    }
    return "unknown";
}

}  // namespace bt
