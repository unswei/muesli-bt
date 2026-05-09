#include "bt/model_service.hpp"

namespace bt {

model_service_response unavailable_model_service_client::call(const model_service_request& request) {
    model_service_response out;
    out.id = request.id;
    out.status = model_service_status::unavailable;
    out.error_code = "model_service_unconfigured";
    out.error_message = "muesli-model-service bridge is not configured";
    out.error_retryable = true;
    out.host_reached = false;
    return out;
}

const char* model_service_operation_name(model_service_operation op) noexcept {
    switch (op) {
    case model_service_operation::describe:
        return "describe";
    case model_service_operation::invoke:
        return "invoke";
    case model_service_operation::start:
        return "start";
    case model_service_operation::step:
        return "step";
    case model_service_operation::cancel:
        return "cancel";
    case model_service_operation::status:
        return "status";
    case model_service_operation::close:
        return "close";
    }
    return "unknown";
}

const char* model_service_status_name(model_service_status status) noexcept {
    switch (status) {
    case model_service_status::success:
        return "success";
    case model_service_status::running:
        return "running";
    case model_service_status::action_chunk:
        return "action_chunk";
    case model_service_status::partial:
        return "partial";
    case model_service_status::failure:
        return "failure";
    case model_service_status::cancelled:
        return "cancelled";
    case model_service_status::timeout:
        return "timeout";
    case model_service_status::invalid_request:
        return "invalid_request";
    case model_service_status::invalid_output:
        return "invalid_output";
    case model_service_status::unavailable:
        return "unavailable";
    case model_service_status::unsafe_output:
        return "unsafe_output";
    case model_service_status::resource_exhausted:
        return "resource_exhausted";
    case model_service_status::internal_error:
        return "internal_error";
    }
    return "unknown";
}

bool model_service_status_terminal(model_service_status status) noexcept {
    switch (status) {
    case model_service_status::running:
    case model_service_status::action_chunk:
    case model_service_status::partial:
        return false;
    case model_service_status::success:
    case model_service_status::failure:
    case model_service_status::cancelled:
    case model_service_status::timeout:
    case model_service_status::invalid_request:
    case model_service_status::invalid_output:
    case model_service_status::unavailable:
    case model_service_status::unsafe_output:
    case model_service_status::resource_exhausted:
    case model_service_status::internal_error:
        return true;
    }
    return true;
}

}  // namespace bt
