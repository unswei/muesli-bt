#include "bt/model_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace bt {
namespace {

std::string json_quote(std::string_view text) {
    std::ostringstream out;
    out << '"';
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
    out << '"';
    return out.str();
}

bool looks_like_json_value(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    if (i == text.size()) {
        return false;
    }
    const char ch = text[i];
    return ch == '{' || ch == '[' || ch == '"' || ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) ||
           text.substr(i, 4) == "true" || text.substr(i, 5) == "false" || text.substr(i, 4) == "null";
}

std::string raw_json_or_empty_object(std::string_view text) {
    return looks_like_json_value(text) ? std::string(text) : "{}";
}

std::string unquote_json_string(std::string_view raw) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
        return {};
    }
    std::string out;
    for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
        const char ch = raw[i];
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
        if (++i + 1 > raw.size()) {
            break;
        }
        switch (raw[i]) {
        case '"':
            out.push_back('"');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case '/':
            out.push_back('/');
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        default:
            break;
        }
    }
    return out;
}

void skip_ws(std::string_view text, std::size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
}

std::size_t scan_json_string(std::string_view text, std::size_t pos) {
    ++pos;
    while (pos < text.size()) {
        if (text[pos] == '\\') {
            pos += 2;
            continue;
        }
        if (text[pos] == '"') {
            return pos + 1;
        }
        ++pos;
    }
    return text.size();
}

std::size_t scan_json_value(std::string_view text, std::size_t pos) {
    skip_ws(text, pos);
    if (pos >= text.size()) {
        return pos;
    }
    if (text[pos] == '"') {
        return scan_json_string(text, pos);
    }
    if (text[pos] == '{' || text[pos] == '[') {
        const char open = text[pos];
        const char close = open == '{' ? '}' : ']';
        int depth = 1;
        ++pos;
        while (pos < text.size() && depth > 0) {
            if (text[pos] == '"') {
                pos = scan_json_string(text, pos);
                continue;
            }
            if (text[pos] == open) {
                ++depth;
            } else if (text[pos] == close) {
                --depth;
            }
            ++pos;
        }
        return pos;
    }
    while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']') {
        ++pos;
    }
    return pos;
}

std::unordered_map<std::string, std::string> parse_top_level_object(std::string_view text) {
    std::unordered_map<std::string, std::string> out;
    std::size_t pos = 0;
    skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != '{') {
        return out;
    }
    ++pos;
    while (pos < text.size()) {
        skip_ws(text, pos);
        if (pos >= text.size() || text[pos] == '}') {
            break;
        }
        if (text[pos] != '"') {
            break;
        }
        const std::size_t key_begin = pos;
        const std::size_t key_end = scan_json_string(text, pos);
        const std::string key = unquote_json_string(text.substr(key_begin, key_end - key_begin));
        pos = key_end;
        skip_ws(text, pos);
        if (pos >= text.size() || text[pos] != ':') {
            break;
        }
        ++pos;
        skip_ws(text, pos);
        const std::size_t value_begin = pos;
        const std::size_t value_end = scan_json_value(text, pos);
        out.emplace(key, std::string(text.substr(value_begin, value_end - value_begin)));
        pos = value_end;
        skip_ws(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
    return out;
}

model_service_status status_from_name(const std::string& status) {
    if (status == "success") {
        return model_service_status::success;
    }
    if (status == "running") {
        return model_service_status::running;
    }
    if (status == "action_chunk") {
        return model_service_status::action_chunk;
    }
    if (status == "partial") {
        return model_service_status::partial;
    }
    if (status == "failure") {
        return model_service_status::failure;
    }
    if (status == "cancelled") {
        return model_service_status::cancelled;
    }
    if (status == "timeout") {
        return model_service_status::timeout;
    }
    if (status == "invalid_request") {
        return model_service_status::invalid_request;
    }
    if (status == "invalid_output") {
        return model_service_status::invalid_output;
    }
    if (status == "unsafe_output") {
        return model_service_status::unsafe_output;
    }
    if (status == "resource_exhausted") {
        return model_service_status::resource_exhausted;
    }
    if (status == "internal_error") {
        return model_service_status::internal_error;
    }
    return model_service_status::unavailable;
}

bool contains_json_string_field(std::string_view text, std::string_view field, std::string_view value) {
    std::ostringstream needle;
    needle << '"' << field << "\":\"" << value << '"';
    return text.find(needle.str()) != std::string_view::npos;
}

bool contains_json_bool_field(std::string_view text, std::string_view field, bool value) {
    const std::string quoted_field = json_quote(field);
    std::size_t pos = 0;
    while ((pos = text.find(quoted_field, pos)) != std::string_view::npos) {
        pos += quoted_field.size();
        skip_ws(text, pos);
        if (pos >= text.size() || text[pos] != ':') {
            continue;
        }
        ++pos;
        skip_ws(text, pos);
        const std::string_view literal = value ? std::string_view("true") : std::string_view("false");
        if (text.substr(pos, literal.size()) == literal) {
            return true;
        }
    }
    return false;
}

std::optional<double> parse_json_number(std::string_view raw) {
    std::string text(raw);
    const char* begin = text.c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(value)) {
        return std::nullopt;
    }
    while (*end != '\0') {
        if (!std::isspace(static_cast<unsigned char>(*end))) {
            return std::nullopt;
        }
        ++end;
    }
    return value;
}

bool parse_finite_number_array(std::string_view raw) {
    std::size_t pos = 0;
    skip_ws(raw, pos);
    if (pos >= raw.size() || raw[pos] != '[') {
        return false;
    }
    ++pos;
    skip_ws(raw, pos);
    if (pos < raw.size() && raw[pos] == ']') {
        return false;
    }

    bool saw_value = false;
    while (pos < raw.size()) {
        skip_ws(raw, pos);
        const std::size_t begin = pos;
        while (pos < raw.size() && raw[pos] != ',' && raw[pos] != ']') {
            ++pos;
        }
        const std::optional<double> value = parse_json_number(raw.substr(begin, pos - begin));
        if (!value.has_value()) {
            return false;
        }
        saw_value = true;
        skip_ws(raw, pos);
        if (pos < raw.size() && raw[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < raw.size() && raw[pos] == ']') {
            ++pos;
            skip_ws(raw, pos);
            return saw_value && pos == raw.size();
        }
        return false;
    }
    return false;
}

std::optional<std::pair<std::string, std::string>> validate_action_chunk_shape(std::string_view actions_raw) {
    std::size_t pos = 0;
    skip_ws(actions_raw, pos);
    if (pos >= actions_raw.size() || actions_raw[pos] != '[') {
        return std::make_pair("model_service_actions_not_array", "VLA action chunk actions must be an array");
    }
    ++pos;
    skip_ws(actions_raw, pos);
    if (pos < actions_raw.size() && actions_raw[pos] == ']') {
        return std::make_pair("model_service_actions_empty", "VLA action chunk actions must not be empty");
    }
    if (pos >= actions_raw.size() || actions_raw[pos] != '{') {
        return std::make_pair("model_service_action_not_object", "VLA action chunk action entries must be objects");
    }

    const std::size_t first_begin = pos;
    const std::size_t first_end = scan_json_value(actions_raw, pos);
    const auto action = parse_top_level_object(actions_raw.substr(first_begin, first_end - first_begin));

    const auto type_it = action.find("type");
    if (type_it == action.end() || unquote_json_string(type_it->second).empty()) {
        return std::make_pair("model_service_action_type_missing", "VLA action chunk action is missing type");
    }

    const auto values_it = action.find("values");
    if (values_it == action.end()) {
        return std::make_pair("model_service_action_values_missing", "VLA action chunk action is missing values");
    }
    if (!parse_finite_number_array(values_it->second)) {
        return std::make_pair("model_service_action_values_invalid",
                              "VLA action chunk action values must be a non-empty finite numeric array");
    }

    const auto dt_it = action.find("dt_ms");
    if (dt_it == action.end()) {
        return std::make_pair("model_service_action_dt_missing", "VLA action chunk action is missing dt_ms");
    }
    const std::optional<double> dt_ms = parse_json_number(dt_it->second);
    if (!dt_ms.has_value() || *dt_ms <= 0.0) {
        return std::make_pair("model_service_action_dt_invalid", "VLA action chunk dt_ms must be positive and finite");
    }

    return std::nullopt;
}

void reject_model_service_output(model_service_response& response,
                                 model_service_status status,
                                 std::string reason_code,
                                 std::string message) {
    response.status = status;
    response.host_reached = false;
    response.validation_checked = true;
    response.validation_ok = false;
    response.validation_reason_code = std::move(reason_code);
    response.validation_message = std::move(message);
    if (response.error_code.empty()) {
        response.error_code = response.validation_reason_code;
    }
    if (response.error_message.empty()) {
        response.error_message = response.validation_message;
    }
    response.error_retryable = false;
}

}  // namespace

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

std::string model_service_response_to_json(const model_service_response& response) {
    std::ostringstream out;
    out << "{\"version\":" << json_quote(response.version) << ",\"id\":" << json_quote(response.id)
        << ",\"status\":" << json_quote(model_service_status_name(response.status))
        << ",\"output\":" << raw_json_or_empty_object(response.output_json);
    if (response.session_id.empty()) {
        out << ",\"session_id\":null";
    } else {
        out << ",\"session_id\":" << json_quote(response.session_id);
    }
    if (response.error_code.empty() && response.error_message.empty()) {
        out << ",\"error\":null";
    } else {
        out << ",\"error\":{\"code\":" << json_quote(response.error_code)
            << ",\"message\":" << json_quote(response.error_message)
            << ",\"retryable\":" << (response.error_retryable ? "true" : "false") << '}';
    }
    out << ",\"metadata\":" << raw_json_or_empty_object(response.metadata_json) << '}';
    return out.str();
}

std::string model_service_request_to_json(const model_service_request& request) {
    std::ostringstream out;
    out << "{\"version\":" << json_quote(request.version) << ",\"id\":" << json_quote(request.id)
        << ",\"op\":" << json_quote(model_service_operation_name(request.op));
    if (!request.capability.empty()) {
        out << ",\"capability\":" << json_quote(request.capability);
    }
    if (!request.session_id.empty()) {
        out << ",\"session_id\":" << json_quote(request.session_id);
    }
    if (request.deadline_ms > 0) {
        out << ",\"deadline_ms\":" << request.deadline_ms;
    }
    if (request.trace.has_value()) {
        out << ",\"trace\":{\"run_id\":" << json_quote(request.trace->run_id)
            << ",\"tree_id\":" << json_quote(request.trace->tree_id) << ",\"tick_id\":" << request.trace->tick_id
            << ",\"node_id\":" << json_quote(request.trace->node_id) << '}';
    }
    out << ",\"input\":" << raw_json_or_empty_object(request.input_json);
    out << ",\"refs\":[";
    for (std::size_t i = 0; i < request.refs_json.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << raw_json_or_empty_object(request.refs_json[i]);
    }
    out << ']';
    out << ",\"replay\":{\"mode\":" << json_quote(request.replay_mode.empty() ? "live" : request.replay_mode) << "}}";
    return out.str();
}

model_service_response model_service_response_from_json(const std::string& text) {
    const auto object = parse_top_level_object(text);
    model_service_response out;
    out.raw_json = text;
    if (auto it = object.find("version"); it != object.end()) {
        out.version = unquote_json_string(it->second);
    }
    if (auto it = object.find("id"); it != object.end()) {
        out.id = unquote_json_string(it->second);
    }
    if (auto it = object.find("status"); it != object.end()) {
        out.status = status_from_name(unquote_json_string(it->second));
    }
    if (auto it = object.find("output"); it != object.end() && it->second != "null") {
        out.output_json = it->second;
    }
    if (auto it = object.find("session_id"); it != object.end() && it->second != "null") {
        out.session_id = unquote_json_string(it->second);
    }
    if (auto it = object.find("metadata"); it != object.end() && it->second != "null") {
        out.metadata_json = it->second;
    }
    if (auto it = object.find("error"); it != object.end() && it->second != "null") {
        const auto error = parse_top_level_object(it->second);
        if (auto eit = error.find("code"); eit != error.end()) {
            out.error_code = unquote_json_string(eit->second);
        }
        if (auto eit = error.find("message"); eit != error.end()) {
            out.error_message = unquote_json_string(eit->second);
        }
        if (auto eit = error.find("retryable"); eit != error.end()) {
            out.error_retryable = eit->second == "true";
        }
    }
    out.host_reached = false;
    return out;
}

void validate_model_service_response(const model_service_request& request, model_service_response& response) {
    response.host_reached = false;
    if (request.op != model_service_operation::invoke && request.op != model_service_operation::step) {
        return;
    }
    if (response.status != model_service_status::success && response.status != model_service_status::action_chunk) {
        return;
    }

    response.validation_checked = true;
    response.validation_ok = false;
    response.validation_reason_code.clear();
    response.validation_message.clear();

    std::string_view output = response.output_json;
    std::size_t pos = 0;
    skip_ws(output, pos);
    if (pos >= output.size() || output[pos] != '{') {
        reject_model_service_output(response,
                                    model_service_status::invalid_output,
                                    "model_service_output_not_object",
                                    "model-service output must be a JSON object");
        return;
    }

    const auto object = parse_top_level_object(output);
    if (contains_json_bool_field(output, "unsafe", true) ||
        contains_json_bool_field(output, "unsafe_output", true)) {
        reject_model_service_output(response,
                                    model_service_status::unsafe_output,
                                    "model_service_unsafe_output",
                                    "model-service output was marked unsafe");
        return;
    }
    if (contains_json_bool_field(output, "policy_violation", true)) {
        reject_model_service_output(response,
                                    model_service_status::unsafe_output,
                                    "model_service_policy_violation",
                                    "model-service output violated host policy");
        return;
    }
    if (contains_json_bool_field(output, "policy_violating", true)) {
        reject_model_service_output(response,
                                    model_service_status::unsafe_output,
                                    "model_service_policy_violation",
                                    "model-service output violated host policy");
        return;
    }
    if (contains_json_bool_field(output, "stale", true) ||
        contains_json_bool_field(output, "stale_result", true)) {
        reject_model_service_output(response,
                                    model_service_status::invalid_output,
                                    "model_service_stale_result",
                                    "model-service output was marked stale");
        return;
    }
    if (contains_json_bool_field(output, "late", true) ||
        contains_json_bool_field(output, "late_result", true) ||
        contains_json_bool_field(output, "deadline_missed", true)) {
        reject_model_service_output(response,
                                    model_service_status::invalid_output,
                                    "model_service_late_result",
                                    "model-service output was marked late");
        return;
    }

    auto require_field = [&](std::string_view field, std::string reason_code, std::string message) -> bool {
        if (object.find(std::string(field)) != object.end()) {
            return true;
        }
        reject_model_service_output(response,
                                    model_service_status::invalid_output,
                                    std::move(reason_code),
                                    std::move(message));
        return false;
    };

    if (request.capability == "cap.model.world.rollout.v1") {
        if (!require_field("predicted_states",
                           "model_service_missing_predicted_states",
                           "world rollout output is missing predicted_states")) {
            return;
        }
    } else if (request.capability == "cap.model.world.score_trajectory.v1") {
        if (!require_field("score", "model_service_missing_score", "trajectory score output is missing score")) {
            return;
        }
    } else if (request.capability == "cap.vla.action_chunk.v1") {
        if (!require_field("actions", "model_service_missing_actions", "VLA action chunk output is missing actions")) {
            return;
        }
        if (const auto invalid = validate_action_chunk_shape(object.at("actions")); invalid.has_value()) {
            reject_model_service_output(response,
                                        model_service_status::invalid_output,
                                        invalid->first,
                                        invalid->second);
            return;
        }
    } else if (request.capability == "cap.vla.propose_nav_goal.v1") {
        if (object.find("goal") == object.end() && object.find("nav_goal") == object.end()) {
            reject_model_service_output(response,
                                        model_service_status::invalid_output,
                                        "model_service_missing_nav_goal",
                                        "VLA nav-goal output is missing goal or nav_goal");
            return;
        }
    }

    response.validation_ok = true;
}

std::vector<std::string> model_service_required_capabilities() {
    return {
        "cap.model.world.rollout.v1",
        "cap.model.world.score_trajectory.v1",
        "cap.vla.action_chunk.v1",
        "cap.vla.propose_nav_goal.v1",
    };
}

model_service_compatibility_result
check_model_service_compatibility(model_service_client& client,
                                  std::vector<std::string> required_capabilities,
                                  std::int64_t deadline_ms) {
    model_service_compatibility_result result;
    result.required_capabilities = std::move(required_capabilities);
    result.request_id = "model-service-describe-compatibility";

    model_service_request request;
    request.id = result.request_id;
    request.op = model_service_operation::describe;
    request.deadline_ms = deadline_ms;

    const model_service_response response = client.call(request);
    if (response.version != model_service_protocol_version) {
        result.error_code = "model_service_protocol_mismatch";
        result.error_message = "describe returned protocol version '" + response.version + "', expected '" +
                               model_service_protocol_version + "'";
        return result;
    }
    if (response.status != model_service_status::success) {
        result.error_code = response.error_code.empty() ? "model_service_describe_failed" : response.error_code;
        result.error_message = response.error_message.empty() ? "model-service describe did not return success"
                                                              : response.error_message;
        return result;
    }
    if (response.output_json.find("\"capabilities\"") == std::string::npos) {
        result.error_code = "model_service_descriptor_invalid";
        result.error_message = "describe response is missing output.capabilities";
        return result;
    }

    for (const std::string& capability : result.required_capabilities) {
        if (!contains_json_string_field(response.output_json, "id", capability)) {
            result.missing_capabilities.push_back(capability);
        }
    }
    if (!result.missing_capabilities.empty()) {
        result.error_code = "model_service_capability_missing";
        result.error_message = "describe response is missing required model-service capabilities";
        return result;
    }

    result.compatible = true;
    return result;
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
