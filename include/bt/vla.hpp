#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "bt/scheduler.hpp"

namespace bt {

struct image_handle_ref {
    std::int64_t id = 0;
};

struct blob_handle_ref {
    std::int64_t id = 0;
};

struct image_info {
    std::int64_t id = 0;
    std::int64_t width = 0;
    std::int64_t height = 0;
    std::int64_t channels = 0;
    std::string encoding = "rgb8";
    std::int64_t timestamp_ms = 0;
    std::string frame_id = "camera";
};

struct blob_info {
    std::int64_t id = 0;
    std::int64_t size_bytes = 0;
    std::string mime_type = "application/octet-stream";
    std::int64_t timestamp_ms = 0;
    std::string tag;
};

struct capability_field {
    std::string name;
    std::string type;
    bool required = false;
};

struct capability_descriptor {
    std::string name;
    std::vector<capability_field> request_schema;
    std::vector<capability_field> response_schema;
    std::string safety_class = "safe";
    std::string cost_category = "medium";
};

class capability_registry {
public:
    void register_capability(capability_descriptor descriptor);
    [[nodiscard]] std::vector<std::string> list() const;
    [[nodiscard]] std::optional<capability_descriptor> describe(std::string_view name) const;

private:
    std::unordered_map<std::string, capability_descriptor> capabilities_;
    mutable std::mutex mutex_;
};

enum class vla_action_type {
    continuous,
    discrete,
    sequence
};

struct vla_action {
    vla_action_type type = vla_action_type::continuous;
    std::vector<double> u{};
    std::string discrete_id{};
    std::vector<vla_action> steps{};
};

enum class vla_status {
    ok,
    timeout,
    error,
    cancelled,
    invalid
};

enum class vla_job_status {
    queued,
    running,
    streaming,
    done,
    error,
    timeout,
    cancelled
};

struct vla_model_info {
    std::string name = "rt2-stub";
    std::string version = "stub-1";
};

struct vla_observation {
    std::optional<image_handle_ref> image;
    std::optional<blob_handle_ref> blob;
    std::vector<double> state{};
    std::int64_t timestamp_ms = 0;
    std::string frame_id = "base";
};

struct vla_action_space {
    std::string type = "continuous";
    std::int64_t dims = 0;
    std::vector<std::pair<double, double>> bounds{};
    std::vector<std::string> units{};
    std::vector<std::string> semantic{};
};

struct vla_constraints {
    double max_abs_value = 1.0;
    double max_delta = 1.0;
    std::vector<std::pair<double, double>> forbidden_ranges{};
};

struct vla_request {
    std::string capability = "vla.rt2";
    std::string task_id = "task";
    std::string instruction;
    vla_observation observation{};
    vla_action_space action_space{};
    vla_constraints constraints{};
    std::int64_t deadline_ms = 200;
    std::optional<std::uint64_t> seed{};
    vla_model_info model{};

    std::string run_id = "run";
    std::uint64_t tick_index = 0;
    std::string node_name = "vla-node";
};

struct vla_response {
    vla_status status = vla_status::error;
    vla_action action{};
    double confidence = 0.0;
    std::string explanation{};
    vla_model_info model{};
    std::unordered_map<std::string, double> stats{};
};

struct vla_partial {
    std::uint64_t sequence = 0;
    std::string text_chunk{};
    std::optional<vla_action> action_candidate{};
    double confidence = 0.0;
};

struct vla_poll {
    vla_job_status status = vla_job_status::queued;
    std::optional<vla_partial> partial{};
    std::optional<vla_response> final{};
    std::unordered_map<std::string, double> stats{};
};

struct vla_record {
    std::int64_t ts_ms = 0;
    std::string run_id;
    std::uint64_t tick_index = 0;
    std::string node_name;
    std::string task_id;
    std::string capability;
    std::string model_name;
    std::string model_version;
    std::uint64_t request_hash = 0;
    std::string observation_summary;
    std::int64_t deadline_ms = 0;
    std::uint64_t seed = 0;
    std::string status;
    double latency_ms = 0.0;
    std::string response_json;
    bool cache_hit = false;
    bool replay_hit = false;
    bool superseded = false;
};

class vla_backend {
public:
    virtual ~vla_backend() = default;

    virtual vla_response infer(const vla_request& request,
                               std::function<bool(const vla_partial&)> on_partial,
                               std::atomic<bool>& cancel_flag) = 0;
};

class vla_service {
public:
    explicit vla_service(scheduler* sched);

    using vla_job_id = std::uint64_t;

    [[nodiscard]] vla_job_id submit(const vla_request& request);
    [[nodiscard]] vla_poll poll(vla_job_id id);
    bool cancel(vla_job_id id);

    void register_backend(std::string name, std::shared_ptr<vla_backend> backend);
    void set_default_backend(std::string name);
    [[nodiscard]] std::string default_backend() const;

    [[nodiscard]] capability_registry& capabilities() noexcept;
    [[nodiscard]] const capability_registry& capabilities() const noexcept;

    [[nodiscard]] image_handle_ref create_image(std::int64_t width,
                                                std::int64_t height,
                                                std::int64_t channels,
                                                std::string encoding,
                                                std::int64_t timestamp_ms,
                                                std::string frame_id);
    [[nodiscard]] blob_handle_ref create_blob(std::int64_t size_bytes,
                                              std::string mime_type,
                                              std::int64_t timestamp_ms,
                                              std::string tag);
    [[nodiscard]] std::optional<image_info> get_image_info(image_handle_ref handle) const;
    [[nodiscard]] std::optional<blob_info> get_blob_info(blob_handle_ref handle) const;

    [[nodiscard]] std::string dump_recent_records(std::size_t max_count = 200) const;
    [[nodiscard]] std::vector<vla_record> recent_records(std::size_t max_count = 200) const;
    void clear_records();

    void set_log_path(std::string path);
    [[nodiscard]] const std::string& log_path() const noexcept;
    void set_log_enabled(bool enabled) noexcept;
    [[nodiscard]] bool log_enabled() const noexcept;

    void set_cache_ttl_ms(std::int64_t ttl_ms);
    [[nodiscard]] std::int64_t cache_ttl_ms() const noexcept;
    void set_cache_capacity(std::size_t capacity);
    [[nodiscard]] std::size_t cache_capacity() const noexcept;

    [[nodiscard]] static std::uint64_t hash64(std::string_view text) noexcept;
    [[nodiscard]] static std::uint64_t hash_request(const vla_request& request);

private:
    struct cache_entry {
        vla_response response;
        std::chrono::steady_clock::time_point expires_at{};
    };

    struct job_state {
        vla_job_id id = 0;
        vla_request request;
        vla_job_status status = vla_job_status::queued;
        std::atomic<bool> cancel_requested{false};
        std::optional<vla_response> final{};
        std::optional<vla_partial> latest_partial{};
        std::uint64_t partial_count = 0;
        std::uint64_t request_hash = 0;
        bool cache_hit = false;
        bool replay_hit = false;
        bool superseded = false;
        job_id scheduler_job_id = 0;
        std::chrono::steady_clock::time_point submitted_at{};
        std::chrono::steady_clock::time_point started_at{};
        std::chrono::steady_clock::time_point finished_at{};
        std::string error{};
    };

    void append_record(const vla_record& record);
    [[nodiscard]] std::string record_to_json(const vla_record& record) const;
    void append_jsonl_line(const std::string& line);
    [[nodiscard]] std::string response_to_json(const vla_response& response) const;

    [[nodiscard]] std::shared_ptr<vla_backend> resolve_backend(const vla_request& request) const;
    [[nodiscard]] std::string make_owner_key(const vla_request& request) const;
    void evict_cache_if_needed();

    scheduler* sched_ = nullptr;
    capability_registry capabilities_;

    mutable std::mutex mutex_;
    mutable std::mutex file_mutex_;
    std::uint64_t next_job_id_ = 1;
    std::unordered_map<vla_job_id, std::shared_ptr<job_state>> jobs_;
    std::unordered_map<std::string, vla_job_id> active_owner_jobs_;
    std::unordered_map<std::string, std::shared_ptr<vla_backend>> backends_;
    std::string default_backend_ = "rt2-stub";

    std::unordered_map<std::uint64_t, cache_entry> cache_;
    std::size_t cache_capacity_ = 256;
    std::int64_t cache_ttl_ms_ = 750;

    std::unordered_map<std::uint64_t, vla_response> replay_store_;

    std::int64_t next_image_id_ = 1;
    std::int64_t next_blob_id_ = 1;
    std::unordered_map<std::int64_t, image_info> images_;
    std::unordered_map<std::int64_t, blob_info> blobs_;

    std::vector<vla_record> records_;
    std::size_t record_capacity_ = 4096;
    bool log_enabled_ = true;
    std::string log_path_ = "vla-records.jsonl";
};

[[nodiscard]] const char* vla_status_name(vla_status status) noexcept;
[[nodiscard]] const char* vla_job_status_name(vla_job_status status) noexcept;
[[nodiscard]] std::string vla_action_type_name(vla_action_type type);

}  // namespace bt
