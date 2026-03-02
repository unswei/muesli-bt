#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bt/ast.hpp"

namespace bt {

class event_log {
public:
    explicit event_log(std::size_t ring_capacity = 4096);

    void set_enabled(bool enabled) noexcept;
    [[nodiscard]] bool enabled() const noexcept;

    void set_ring_capacity(std::size_t capacity);
    [[nodiscard]] std::size_t ring_capacity() const noexcept;

    void set_path(std::string path);
    [[nodiscard]] const std::string& path() const noexcept;

    void set_file_enabled(bool enabled) noexcept;
    [[nodiscard]] bool file_enabled() const noexcept;

    void set_flush_on_tick_end(bool enabled) noexcept;
    [[nodiscard]] bool flush_on_tick_end() const noexcept;

    void set_run_id(std::string run_id);
    [[nodiscard]] std::string run_id() const;

    void set_tick_hz(double hz) noexcept;
    [[nodiscard]] double tick_hz() const noexcept;

    void set_git_sha(std::string git_sha);
    void set_host_info(std::string name, std::string version, std::string platform);

    void ensure_run_started(std::string_view tree_hash = "", std::string_view capabilities_json = "{\"reset\":true}");
    void emit_bt_def(const definition& def);

    std::uint64_t emit(std::string_view type, std::optional<std::uint64_t> tick, std::string_view data_json);

    [[nodiscard]] std::vector<std::string> snapshot(std::size_t max_count = 0) const;
    void clear_ring();

    void request_snapshot_bb(bool full = false) noexcept;
    [[nodiscard]] bool consume_snapshot_bb_request(bool* full = nullptr) noexcept;

    [[nodiscard]] static std::string json_escape(std::string_view text);
    [[nodiscard]] static std::string hash64_hex(std::string_view text);

private:
    [[nodiscard]] std::string make_envelope(std::string_view type,
                                            std::optional<std::uint64_t> tick,
                                            std::string_view data_json,
                                            std::uint64_t seq) const;
    void append_ring_line(const std::string& line);
    void append_file_line(const std::string& line, bool flush_now);
    [[nodiscard]] static std::string node_kind_name(node_kind kind);

    mutable std::mutex mutex_;
    mutable std::mutex file_mutex_;

    bool enabled_ = true;
    bool file_enabled_ = false;
    bool flush_on_tick_end_ = true;
    bool run_started_ = false;

    std::size_t ring_capacity_ = 4096;
    std::vector<std::string> ring_;

    std::string path_ = "logs/run.jsonl";
    std::string run_id_ = "run-0";
    std::uint64_t seq_ = 0;
    double tick_hz_ = 50.0;

    std::string git_sha_ = "unknown";
    std::string host_name_ = "muesli-bt";
    std::string host_version_ = "dev";
    std::string host_platform_ = "unknown";

    bool snapshot_bb_requested_ = false;
    bool snapshot_bb_full_ = false;
};

}  // namespace bt
