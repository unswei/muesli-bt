#include "host_client.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace air_hockey_demo {
namespace {

constexpr std::size_t kMaximumResponseBytes = 64 * 1024;
constexpr std::size_t kMaximumJsonDepth = 32;

struct json_value {
    using array = std::vector<json_value>;
    using object = std::map<std::string, json_value>;
    std::variant<std::nullptr_t, bool, double, std::string, array, object> data;
};

[[noreturn]] void protocol_failure(std::string message) {
    throw host_protocol_error("invalid_response", std::move(message));
}

class json_parser {
public:
    explicit json_parser(std::string_view input) : input_(input) {}

    json_value parse() {
        json_value result = parse_value(0);
        skip_whitespace();
        if (position_ != input_.size()) {
            protocol_failure("response has trailing JSON data");
        }
        return result;
    }

private:
    json_value parse_value(std::size_t depth) {
        if (depth > kMaximumJsonDepth) {
            protocol_failure("response exceeds the maximum JSON depth");
        }
        skip_whitespace();
        if (position_ >= input_.size()) {
            protocol_failure("response ended inside a JSON value");
        }
        const char token = input_[position_];
        if (token == '{') {
            return json_value{parse_object(depth + 1)};
        }
        if (token == '[') {
            return json_value{parse_array(depth + 1)};
        }
        if (token == '"') {
            return json_value{parse_string()};
        }
        if (token == 't') {
            consume_literal("true");
            return json_value{true};
        }
        if (token == 'f') {
            consume_literal("false");
            return json_value{false};
        }
        if (token == 'n') {
            consume_literal("null");
            return json_value{nullptr};
        }
        return json_value{parse_number()};
    }

    json_value::object parse_object(std::size_t depth) {
        ++position_;
        json_value::object result;
        skip_whitespace();
        if (consume('}')) {
            return result;
        }
        while (true) {
            skip_whitespace();
            if (position_ >= input_.size() || input_[position_] != '"') {
                protocol_failure("response object key is not a string");
            }
            std::string key = parse_string();
            if (!result.emplace(key, json_value{nullptr}).second) {
                protocol_failure("response repeats object key: " + key);
            }
            skip_whitespace();
            if (!consume(':')) {
                protocol_failure("response object is missing ':'");
            }
            result.at(key) = parse_value(depth);
            skip_whitespace();
            if (consume('}')) {
                return result;
            }
            if (!consume(',')) {
                protocol_failure("response object is missing ','");
            }
        }
    }

    json_value::array parse_array(std::size_t depth) {
        ++position_;
        json_value::array result;
        skip_whitespace();
        if (consume(']')) {
            return result;
        }
        while (true) {
            result.push_back(parse_value(depth));
            skip_whitespace();
            if (consume(']')) {
                return result;
            }
            if (!consume(',')) {
                protocol_failure("response array is missing ','");
            }
        }
    }

    std::string parse_string() {
        ++position_;
        std::string result;
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                return result;
            }
            if (character < 0x20) {
                protocol_failure("response string contains a control character");
            }
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                protocol_failure("response string ends after an escape");
            }
            const char escaped = input_[position_++];
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    result.push_back(escaped);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u':
                    append_unicode_escape(result);
                    break;
                default:
                    protocol_failure("response string contains an invalid escape");
            }
        }
        protocol_failure("response string is unterminated");
    }

    void append_unicode_escape(std::string& output) {
        if (position_ + 4 > input_.size()) {
            protocol_failure("response string has a short Unicode escape");
        }
        unsigned int codepoint = 0;
        for (int index = 0; index < 4; ++index) {
            const char digit = input_[position_++];
            codepoint <<= 4;
            if (digit >= '0' && digit <= '9') {
                codepoint += static_cast<unsigned int>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                codepoint += static_cast<unsigned int>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                codepoint += static_cast<unsigned int>(digit - 'A' + 10);
            } else {
                protocol_failure("response string has an invalid Unicode escape");
            }
        }
        if (codepoint >= 0xd800 && codepoint <= 0xdfff) {
            protocol_failure("response string contains an unsupported surrogate escape");
        }
        if (codepoint <= 0x7f) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    double parse_number() {
        const std::size_t begin = position_;
        consume('-');
        if (position_ >= input_.size()) {
            protocol_failure("response number is incomplete");
        }
        if (input_[position_] == '0') {
            ++position_;
        } else if (input_[position_] >= '1' && input_[position_] <= '9') {
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
        } else {
            protocol_failure("response number has an invalid integer part");
        }
        if (consume('.')) {
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
            if (position_ == digits) {
                protocol_failure("response number has an invalid fraction");
            }
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
            if (position_ == digits) {
                protocol_failure("response number has an invalid exponent");
            }
        }
        const std::string token(input_.substr(begin, position_ - begin));
        char* end = nullptr;
        errno = 0;
        const double result = std::strtod(token.c_str(), &end);
        if (errno == ERANGE || end != token.c_str() + token.size() || !std::isfinite(result)) {
            protocol_failure("response number is not finite");
        }
        return result;
    }

    void consume_literal(std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            protocol_failure("response contains an invalid JSON literal");
        }
        position_ += literal.size();
    }

    bool consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void skip_whitespace() {
        while (position_ < input_.size()) {
            const char character = input_[position_];
            if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
                break;
            }
            ++position_;
        }
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

const json_value::object& require_object(const json_value& value, std::string_view where) {
    if (const auto* result = std::get_if<json_value::object>(&value.data)) {
        return *result;
    }
    protocol_failure(std::string(where) + " must be an object");
}

const json_value::array& require_array(const json_value& value, std::string_view where) {
    if (const auto* result = std::get_if<json_value::array>(&value.data)) {
        return *result;
    }
    protocol_failure(std::string(where) + " must be an array");
}

const std::string& require_string(const json_value& value, std::string_view where) {
    if (const auto* result = std::get_if<std::string>(&value.data)) {
        return *result;
    }
    protocol_failure(std::string(where) + " must be a string");
}

bool require_boolean(const json_value& value, std::string_view where) {
    if (const auto* result = std::get_if<bool>(&value.data)) {
        return *result;
    }
    protocol_failure(std::string(where) + " must be a boolean");
}

double require_number(const json_value& value, std::string_view where) {
    if (const auto* result = std::get_if<double>(&value.data); result && std::isfinite(*result)) {
        return *result;
    }
    protocol_failure(std::string(where) + " must be a finite number");
}

std::int64_t require_integer(const json_value& value, std::string_view where) {
    const double number = require_number(value, where);
    if (std::floor(number) != number || number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        number > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        protocol_failure(std::string(where) + " must be an integer");
    }
    return static_cast<std::int64_t>(number);
}

const json_value& require_member(const json_value::object& object,
                                 std::string_view key,
                                 std::string_view where) {
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        protocol_failure(std::string(where) + " is missing key: " + std::string(key));
    }
    return found->second;
}

void require_keys(const json_value::object& object,
                  std::initializer_list<std::string_view> expected,
                  std::string_view where) {
    if (object.size() != expected.size()) {
        protocol_failure(std::string(where) + " has an unexpected property count");
    }
    for (const std::string_view key : expected) {
        (void)require_member(object, key, where);
    }
}

bool has_decimal_suffix(std::string_view text,
                        std::string_view prefix,
                        std::size_t minimum_digits) {
    if (!text.starts_with(prefix) || text.size() < prefix.size() + minimum_digits) {
        return false;
    }
    return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(prefix.size()), text.end(),
                       [](char character) { return character >= '0' && character <= '9'; });
}

bool valid_episode_id(std::string_view text) {
    return has_decimal_suffix(text, "episode-", 6);
}

bool valid_context_id(std::string_view text, std::string_view episode_id) {
    const std::string prefix = std::string(episode_id) + "/track-";
    return valid_episode_id(episode_id) && has_decimal_suffix(text, prefix, 4);
}

std::string escape_json(std::string_view input) {
    std::ostringstream output;
    for (const unsigned char character : input) {
        switch (character) {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (character < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(character) << std::dec;
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    return output.str();
}

void encode_json_value(std::ostringstream& output, const json_value& value) {
    if (std::holds_alternative<std::nullptr_t>(value.data)) {
        output << "null";
    } else if (const auto* boolean = std::get_if<bool>(&value.data)) {
        output << (*boolean ? "true" : "false");
    } else if (const auto* number = std::get_if<double>(&value.data)) {
        output << std::setprecision(std::numeric_limits<double>::max_digits10) << *number;
    } else if (const auto* text = std::get_if<std::string>(&value.data)) {
        output << '"' << escape_json(*text) << '"';
    } else if (const auto* array = std::get_if<json_value::array>(&value.data)) {
        output << '[';
        for (std::size_t index = 0; index < array->size(); ++index) {
            if (index > 0) {
                output << ',';
            }
            encode_json_value(output, (*array)[index]);
        }
        output << ']';
    } else {
        const auto& object = std::get<json_value::object>(value.data);
        output << '{';
        bool first = true;
        for (const auto& [key, item] : object) {
            if (!first) {
                output << ',';
            }
            first = false;
            output << '"' << escape_json(key) << "\":";
            encode_json_value(output, item);
        }
        output << '}';
    }
}

std::string encode_json(const json_value& value) {
    std::ostringstream output;
    encode_json_value(output, value);
    return output.str();
}

public_state parse_public_state(const json_value& value) {
    const auto& object = require_object(value, "public state");
    require_keys(object,
                 {"observation_schema", "observation", "observation_step", "puck_visible",
                  "action_locked", "episode_active", "terminated", "truncated",
                  "defence_context_id", "episode_id"},
                 "public state");
    public_state state;
    state.observation_schema = require_string(require_member(object, "observation_schema", "public state"),
                                              "public state observation_schema");
    if (state.observation_schema != "airhockey.public_observation.v1") {
        protocol_failure("public state has an unsupported observation schema");
    }
    const auto& observation = require_array(require_member(object, "observation", "public state"),
                                            "public state observation");
    if (observation.size() != kObservationDimension) {
        protocol_failure("public state observation dimension is not 19");
    }
    for (std::size_t index = 0; index < observation.size(); ++index) {
        state.observation[index] = require_number(observation[index], "public state observation value");
        if (state.observation[index] < -1.0 || state.observation[index] > 1.0) {
            protocol_failure("public state observation is outside [-1, 1]");
        }
    }
    state.observation_step =
        require_integer(require_member(object, "observation_step", "public state"), "observation_step");
    state.puck_visible = require_boolean(require_member(object, "puck_visible", "public state"), "puck_visible");
    state.action_locked = require_boolean(require_member(object, "action_locked", "public state"), "action_locked");
    state.episode_active =
        require_boolean(require_member(object, "episode_active", "public state"), "episode_active");
    state.terminated = require_boolean(require_member(object, "terminated", "public state"), "terminated");
    state.truncated = require_boolean(require_member(object, "truncated", "public state"), "truncated");
    state.defence_context_id =
        require_string(require_member(object, "defence_context_id", "public state"), "defence_context_id");
    state.episode_id = require_string(require_member(object, "episode_id", "public state"), "episode_id");
    if (state.observation_step < 0 || !valid_context_id(state.defence_context_id, state.episode_id) ||
        (state.terminated && state.truncated) ||
        state.episode_active == (state.terminated || state.truncated) ||
        (!state.episode_active && state.action_locked) ||
        state.observation.back() != (state.puck_visible ? 1.0 : 0.0)) {
        protocol_failure("public state violates lifecycle or visibility invariants");
    }
    return state;
}

host_configuration parse_configuration(const json_value& value) {
    const auto& object = require_object(value, "configuration");
    require_keys(object,
                 {"blackout_start_step", "blackout_length_steps", "timeout_steps",
                  "action_lock_steps", "replace_track_steps", "terminate_at_step"},
                 "configuration");
    host_configuration configuration;
    configuration.blackout_start_step = require_integer(
        require_member(object, "blackout_start_step", "configuration"), "blackout_start_step");
    configuration.blackout_length_steps = require_integer(
        require_member(object, "blackout_length_steps", "configuration"), "blackout_length_steps");
    configuration.timeout_steps =
        require_integer(require_member(object, "timeout_steps", "configuration"), "timeout_steps");
    configuration.action_lock_steps = require_integer(
        require_member(object, "action_lock_steps", "configuration"), "action_lock_steps");
    const auto& replacements = require_array(
        require_member(object, "replace_track_steps", "configuration"), "replace_track_steps");
    std::set<std::int64_t> unique_replacements;
    for (const json_value& step : replacements) {
        const std::int64_t parsed = require_integer(step, "replace_track_steps item");
        configuration.replace_track_steps.push_back(parsed);
        unique_replacements.insert(parsed);
    }
    const json_value& terminate = require_member(object, "terminate_at_step", "configuration");
    if (!std::holds_alternative<std::nullptr_t>(terminate.data)) {
        configuration.terminate_at_step = require_integer(terminate, "terminate_at_step");
    }
    constexpr std::int64_t kMaximumStep = 1000000;
    if (configuration.blackout_start_step < 0 ||
        configuration.blackout_start_step > kMaximumStep ||
        configuration.blackout_length_steps < 0 ||
        configuration.blackout_length_steps > kMaximumStep ||
        configuration.timeout_steps < 1 || configuration.timeout_steps > kMaximumStep ||
        configuration.action_lock_steps < 0 ||
        configuration.action_lock_steps > configuration.timeout_steps ||
        configuration.blackout_start_step + configuration.blackout_length_steps >
            configuration.timeout_steps ||
        replacements.size() > 128 || unique_replacements.size() != replacements.size() ||
        std::any_of(configuration.replace_track_steps.begin(),
                    configuration.replace_track_steps.end(),
                    [&](std::int64_t step) {
                        return step < 1 || step > configuration.timeout_steps;
                    }) ||
        (configuration.terminate_at_step.has_value() &&
         (*configuration.terminate_at_step < 1 ||
          *configuration.terminate_at_step > configuration.timeout_steps))) {
        protocol_failure("configuration violates the air-hockey v1 bounds");
    }
    return configuration;
}

std::string read_socket_response(const std::filesystem::path& socket_path, std::string_view request) {
    const std::string path = socket_path.string();
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
        throw std::invalid_argument("air-hockey host socket path is empty or too long");
    }
    const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) {
        throw std::system_error(errno, std::generic_category(), "create air-hockey host socket");
    }
    const auto close_descriptor = [&] { (void)::close(descriptor); };
    const timeval timeout{.tv_sec = 2, .tv_usec = 0};
    if (::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        const int error = errno;
        close_descriptor();
        throw std::system_error(error, std::generic_category(),
                                "set air-hockey host socket timeout");
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (::connect(descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        const int error = errno;
        close_descriptor();
        throw std::system_error(error, std::generic_category(), "connect to air-hockey host");
    }

    std::string framed(request);
    framed.push_back('\n');
    std::size_t sent = 0;
    while (sent < framed.size()) {
#ifdef MSG_NOSIGNAL
        constexpr int kSendFlags = MSG_NOSIGNAL;
#else
        constexpr int kSendFlags = 0;
#endif
        const ssize_t count =
            ::send(descriptor, framed.data() + sent, framed.size() - sent, kSendFlags);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            const int error = count < 0 ? errno : EPIPE;
            close_descriptor();
            throw std::system_error(error, std::generic_category(), "send air-hockey host request");
        }
        sent += static_cast<std::size_t>(count);
    }
    (void)::shutdown(descriptor, SHUT_WR);

    std::string response;
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t count = ::recv(descriptor, buffer.data(), buffer.size(), 0);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            const int error = errno;
            close_descriptor();
            throw std::system_error(error, std::generic_category(), "read air-hockey host response");
        }
        if (count == 0) {
            break;
        }
        response.append(buffer.data(), static_cast<std::size_t>(count));
        if (response.size() > kMaximumResponseBytes) {
            close_descriptor();
            protocol_failure("host response exceeds 65536 bytes");
        }
    }
    close_descriptor();
    if (response.empty() || response.back() != '\n' || response.find('\n') != response.size() - 1) {
        protocol_failure("host response is not one newline-terminated JSON object");
    }
    response.pop_back();
    return response;
}

std::string number_json(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("air-hockey action must be finite");
    }
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return output.str();
}

}  // namespace

host_protocol_error::host_protocol_error(std::string code, std::string message)
    : std::runtime_error(message), code_(std::move(code)) {}

const std::string& host_protocol_error::code() const noexcept {
    return code_;
}

host_client::host_client(std::filesystem::path socket_path) : socket_path_(std::move(socket_path)) {
    if (!socket_path_.is_absolute()) {
        throw std::invalid_argument("air-hockey host socket path must be absolute");
    }
}

std::string host_client::exchange(const std::string& operation, const std::string& payload_json) {
    std::ostringstream request_id;
    request_id << "cpp-" << std::setw(6) << std::setfill('0') << next_request_id_++;
    const std::string id = request_id.str();
    const std::string request = "{\"op\":\"" + escape_json(operation) + "\",\"payload\":" +
                                payload_json + ",\"request_id\":\"" + id +
                                "\",\"schema_version\":\"airhockey.host.request.v1\"}";
    const json_value response = json_parser(read_socket_response(socket_path_, request)).parse();
    const auto& object = require_object(response, "host response");
    const bool ok = require_boolean(require_member(object, "ok", "host response"), "host response ok");
    require_keys(object,
                 ok ? std::initializer_list<std::string_view>{"schema_version", "request_id", "op", "ok", "result"}
                    : std::initializer_list<std::string_view>{"schema_version", "request_id", "op", "ok", "error"},
                 "host response");
    if (require_string(require_member(object, "schema_version", "host response"), "schema_version") !=
            "airhockey.host.response.v1" ||
        require_string(require_member(object, "request_id", "host response"), "request_id") != id ||
        require_string(require_member(object, "op", "host response"), "operation") != operation) {
        protocol_failure("host response identity does not match its request");
    }
    if (!ok) {
        const auto& error = require_object(require_member(object, "error", "host response"), "host error");
        require_keys(error, {"code", "message"}, "host error");
        throw host_protocol_error(require_string(require_member(error, "code", "host error"), "host error code"),
                                  require_string(require_member(error, "message", "host error"),
                                                 "host error message"));
    }
    return encode_json(require_member(object, "result", "host response"));
}

host_info host_client::info() {
    const json_value result = json_parser(exchange("info", "{}")).parse();
    const auto& object = require_object(result, "info result");
    require_keys(object,
                 {"protocol_version", "backend", "operations", "observation", "action",
                  "control_period_ms", "max_deadline_ms", "privileged_fields_available"},
                 "info result");
    host_info info;
    info.protocol_version = require_string(require_member(object, "protocol_version", "info result"),
                                           "protocol_version");
    info.backend = require_string(require_member(object, "backend", "info result"), "backend");
    const auto& operations = require_array(require_member(object, "operations", "info result"), "operations");
    const std::array<std::string_view, 7> expected_operations{
        "info", "configure", "reset", "observe", "act", "step", "close"};
    if (operations.size() != expected_operations.size()) {
        protocol_failure("host reports an unexpected operation set");
    }
    for (std::size_t index = 0; index < operations.size(); ++index) {
        if (require_string(operations[index], "operation") != expected_operations[index]) {
            protocol_failure("host reports an unexpected operation order");
        }
    }
    const auto& observation = require_object(require_member(object, "observation", "info result"), "observation info");
    require_keys(observation, {"schema", "dimension"}, "observation info");
    info.observation_schema =
        require_string(require_member(observation, "schema", "observation info"), "observation schema");
    info.observation_dimension =
        require_integer(require_member(observation, "dimension", "observation info"), "observation dimension");
    const auto& action = require_object(require_member(object, "action", "info result"), "action info");
    require_keys(action, {"schema", "dimension", "minimum", "maximum"}, "action info");
    info.action_schema = require_string(require_member(action, "schema", "action info"), "action schema");
    info.action_dimension = require_integer(require_member(action, "dimension", "action info"), "action dimension");
    if (require_number(require_member(action, "minimum", "action info"), "action minimum") != -1.0 ||
        require_number(require_member(action, "maximum", "action info"), "action maximum") != 1.0) {
        protocol_failure("host reports unsupported action bounds");
    }
    info.control_period_ms =
        require_integer(require_member(object, "control_period_ms", "info result"), "control_period_ms");
    info.max_deadline_ms =
        require_integer(require_member(object, "max_deadline_ms", "info result"), "max_deadline_ms");
    info.privileged_fields_available = require_boolean(
        require_member(object, "privileged_fields_available", "info result"), "privileged_fields_available");
    if (info.protocol_version != "airhockey.host.v1" ||
        (info.backend != "fake_direct_launch" && info.backend != "acra_direct_launch") ||
        info.observation_schema != "airhockey.public_observation.v1" ||
        info.action_schema != "airhockey.normalised_mallet_target.v1" ||
        info.observation_dimension != static_cast<std::int64_t>(kObservationDimension) ||
        info.action_dimension != static_cast<std::int64_t>(kActionDimension) ||
        info.control_period_ms != 20 || info.max_deadline_ms != 120 || info.privileged_fields_available) {
        protocol_failure("host info is incompatible with the air-hockey v1 contract");
    }
    return info;
}

host_configuration host_client::configure(const host_configuration& configuration) {
    std::ostringstream payload;
    payload << "{\"action_lock_steps\":" << configuration.action_lock_steps
            << ",\"blackout_length_steps\":" << configuration.blackout_length_steps
            << ",\"blackout_start_step\":" << configuration.blackout_start_step
            << ",\"replace_track_steps\":[";
    for (std::size_t index = 0; index < configuration.replace_track_steps.size(); ++index) {
        if (index > 0) {
            payload << ',';
        }
        payload << configuration.replace_track_steps[index];
    }
    payload << "],\"terminate_at_step\":";
    if (configuration.terminate_at_step.has_value()) {
        payload << *configuration.terminate_at_step;
    } else {
        payload << "null";
    }
    payload << ",\"timeout_steps\":" << configuration.timeout_steps << '}';
    const json_value result = json_parser(exchange("configure", payload.str())).parse();
    const auto& object = require_object(result, "configure result");
    require_keys(object, {"configuration"}, "configure result");
    return parse_configuration(require_member(object, "configuration", "configure result"));
}

public_state host_client::reset(std::optional<std::uint32_t> seed) {
    const std::string payload = seed.has_value() ? "{\"seed\":" + std::to_string(*seed) + "}" : "{}";
    const json_value result = json_parser(exchange("reset", payload)).parse();
    const auto& object = require_object(result, "reset result");
    require_keys(object, {"state"}, "reset result");
    return parse_public_state(require_member(object, "state", "reset result"));
}

public_state host_client::observe() {
    const json_value result = json_parser(exchange("observe", "{}")).parse();
    const auto& object = require_object(result, "observe result");
    require_keys(object, {"state"}, "observe result");
    return parse_public_state(require_member(object, "state", "observe result"));
}

void host_client::act(const std::array<double, kActionDimension>& action) {
    if (std::any_of(action.begin(), action.end(), [](double value) {
            return !std::isfinite(value) || value < -1.0 || value > 1.0;
        })) {
        throw std::invalid_argument("air-hockey action must be finite and within [-1, 1]");
    }
    const std::string payload = "{\"action\":[" + number_json(action[0]) + ',' + number_json(action[1]) + "]}";
    const json_value result = json_parser(exchange("act", payload)).parse();
    const auto& object = require_object(result, "act result");
    require_keys(object, {"accepted", "requested_action", "action_locked"}, "act result");
    if (!require_boolean(require_member(object, "accepted", "act result"), "act accepted")) {
        protocol_failure("host returned a successful but unaccepted action");
    }
    const auto& echoed = require_array(require_member(object, "requested_action", "act result"), "requested_action");
    if (echoed.size() != kActionDimension || require_number(echoed[0], "requested_action") != action[0] ||
        require_number(echoed[1], "requested_action") != action[1]) {
        protocol_failure("host did not echo the requested action exactly");
    }
    (void)require_boolean(require_member(object, "action_locked", "act result"), "action_locked");
}

host_step_result host_client::step() {
    const json_value result = json_parser(exchange("step", "{}")).parse();
    const auto& object = require_object(result, "step result");
    require_keys(object, {"state", "reward"}, "step result");
    return host_step_result{
        .state = parse_public_state(require_member(object, "state", "step result")),
        .reward = require_number(require_member(object, "reward", "step result"), "reward"),
    };
}

void host_client::close() {
    const json_value result = json_parser(exchange("close", "{}")).parse();
    const auto& object = require_object(result, "close result");
    require_keys(object, {"closed"}, "close result");
    if (!require_boolean(require_member(object, "closed", "close result"), "closed")) {
        protocol_failure("host close response did not confirm closure");
    }
}

}  // namespace air_hockey_demo
