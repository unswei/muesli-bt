#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bt {

inline constexpr const char* model_service_protocol_version = "0.2";

enum class model_service_operation {
    describe,
    invoke,
    start,
    step,
    cancel,
    status,
    close
};

enum class model_service_status {
    success,
    running,
    action_chunk,
    partial,
    failure,
    cancelled,
    timeout,
    invalid_request,
    invalid_output,
    unavailable,
    unsafe_output,
    resource_exhausted,
    internal_error
};

struct model_service_trace {
    std::string run_id;
    std::string tree_id;
    std::uint64_t tick_id = 0;
    std::string node_id;
};

struct model_service_config {
    std::string endpoint = "ws://127.0.0.1:8765/v1/ws";
    std::int64_t connect_timeout_ms = 200;
    std::int64_t request_timeout_ms = 500;
    bool required = false;
    std::string replay_mode = "live";
    std::string replay_cache_path;
    std::vector<std::string> fault_schedule;
};

struct model_service_request {
    std::string version = model_service_protocol_version;
    std::string id;
    model_service_operation op = model_service_operation::invoke;
    std::string capability;
    std::int64_t deadline_ms = 0;
    std::optional<model_service_trace> trace;
    std::string input_json = "{}";
    std::vector<std::string> refs_json{};
    std::string replay_mode;
    std::string session_id;
};

struct model_service_response {
    std::string version = model_service_protocol_version;
    std::string id;
    model_service_status status = model_service_status::unavailable;
    std::string output_json = "{}";
    std::string session_id;
    std::string error_code;
    std::string error_message;
    bool error_retryable = false;
    std::string metadata_json = "{}";
    bool host_reached = false;
    std::string raw_json;
    std::string request_hash;
    std::string response_hash;
    bool replay_cache_hit = false;
    bool validation_checked = false;
    bool validation_ok = false;
    std::string validation_reason_code;
    std::string validation_message;
};

struct model_service_compatibility_result {
    bool compatible = false;
    std::string request_id;
    std::string error_code;
    std::string error_message;
    std::vector<std::string> required_capabilities{};
    std::vector<std::string> missing_capabilities{};
    std::vector<std::string> invalid_capabilities{};
    std::vector<std::string> descriptor_errors{};
};

class model_service_client {
public:
    virtual ~model_service_client() = default;

    [[nodiscard]] virtual model_service_response call(const model_service_request& request) = 0;
};

class unavailable_model_service_client final : public model_service_client {
public:
    [[nodiscard]] model_service_response call(const model_service_request& request) override;
};

[[nodiscard]] std::string model_service_request_to_json(const model_service_request& request);
[[nodiscard]] std::string model_service_response_to_json(const model_service_response& response);
[[nodiscard]] model_service_response model_service_response_from_json(const std::string& text);
void validate_model_service_response(const model_service_request& request, model_service_response& response);
[[nodiscard]] std::vector<std::string> model_service_required_capabilities();
[[nodiscard]] model_service_compatibility_result
check_model_service_compatibility(model_service_client& client,
                                  std::vector<std::string> required_capabilities = model_service_required_capabilities(),
                                  std::int64_t deadline_ms = 500);

[[nodiscard]] std::unique_ptr<model_service_client>
make_websocket_model_service_client(model_service_config config);

[[nodiscard]] const char* model_service_operation_name(model_service_operation op) noexcept;
[[nodiscard]] const char* model_service_status_name(model_service_status status) noexcept;
[[nodiscard]] bool model_service_status_terminal(model_service_status status) noexcept;

}  // namespace bt
