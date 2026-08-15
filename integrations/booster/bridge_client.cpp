#include "booster/bridge_client.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace muesli_bt::booster
{
namespace
{

struct json_value
{
  using array = std::vector<json_value>;
  using object = std::map<std::string, json_value, std::less<>>;
  std::variant<std::nullptr_t, bool, double, std::string, array, object> data{nullptr};
};

class json_parser
{
public:
  explicit json_parser(std::string_view input) : input_(input) {}

  bool parse(json_value& output)
  {
    skip_space();
    if (!parse_value(output, 0))
    {
      return false;
    }
    skip_space();
    return position_ == input_.size();
  }

private:
  static constexpr std::size_t max_depth = 16;

  void skip_space()
  {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\t' ||
            input_[position_] == '\r' || input_[position_] == '\n'))
    {
      ++position_;
    }
  }

  bool consume(char expected)
  {
    if (position_ >= input_.size() || input_[position_] != expected)
    {
      return false;
    }
    ++position_;
    return true;
  }

  bool parse_value(json_value& output, std::size_t depth)
  {
    if (depth > max_depth || position_ >= input_.size())
    {
      return false;
    }
    switch (input_[position_])
    {
    case 'n':
      if (input_.substr(position_, 4) != "null")
      {
        return false;
      }
      position_ += 4;
      output.data = nullptr;
      return true;
    case 't':
      if (input_.substr(position_, 4) != "true")
      {
        return false;
      }
      position_ += 4;
      output.data = true;
      return true;
    case 'f':
      if (input_.substr(position_, 5) != "false")
      {
        return false;
      }
      position_ += 5;
      output.data = false;
      return true;
    case '"':
    {
      std::string value;
      if (!parse_string(value))
      {
        return false;
      }
      output.data = std::move(value);
      return true;
    }
    case '[':
      return parse_array(output, depth + 1);
    case '{':
      return parse_object(output, depth + 1);
    default:
      return parse_number(output);
    }
  }

  static bool append_codepoint(std::string& output, std::uint32_t codepoint)
  {
    if (codepoint <= 0x7f)
    {
      output.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7ff)
    {
      output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
    else if (codepoint <= 0xffff)
    {
      if (codepoint >= 0xd800 && codepoint <= 0xdfff)
      {
        return false;
      }
      output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
    else if (codepoint <= 0x10ffff)
    {
      output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
    else
    {
      return false;
    }
    return true;
  }

  bool parse_hex4(std::uint32_t& output)
  {
    if (position_ + 4 > input_.size())
    {
      return false;
    }
    output = 0;
    for (int i = 0; i < 4; ++i)
    {
      const char ch = input_[position_++];
      output <<= 4;
      if (ch >= '0' && ch <= '9')
      {
        output |= static_cast<std::uint32_t>(ch - '0');
      }
      else if (ch >= 'a' && ch <= 'f')
      {
        output |= static_cast<std::uint32_t>(ch - 'a' + 10);
      }
      else if (ch >= 'A' && ch <= 'F')
      {
        output |= static_cast<std::uint32_t>(ch - 'A' + 10);
      }
      else
      {
        return false;
      }
    }
    return true;
  }

  bool parse_string(std::string& output)
  {
    if (!consume('"'))
    {
      return false;
    }
    while (position_ < input_.size())
    {
      const unsigned char ch = static_cast<unsigned char>(input_[position_++]);
      if (ch == '"')
      {
        return true;
      }
      if (ch < 0x20)
      {
        return false;
      }
      if (ch != '\\')
      {
        output.push_back(static_cast<char>(ch));
        continue;
      }
      if (position_ >= input_.size())
      {
        return false;
      }
      const char escaped = input_[position_++];
      switch (escaped)
      {
      case '"':
      case '\\':
      case '/':
        output.push_back(escaped);
        break;
      case 'b':
        output.push_back('\b');
        break;
      case 'f':
        output.push_back('\f');
        break;
      case 'n':
        output.push_back('\n');
        break;
      case 'r':
        output.push_back('\r');
        break;
      case 't':
        output.push_back('\t');
        break;
      case 'u':
      {
        std::uint32_t codepoint = 0;
        if (!parse_hex4(codepoint))
        {
          return false;
        }
        if (codepoint >= 0xd800 && codepoint <= 0xdbff)
        {
          if (position_ + 2 > input_.size() || input_.substr(position_, 2) != "\\u")
          {
            return false;
          }
          position_ += 2;
          std::uint32_t low = 0;
          if (!parse_hex4(low) || low < 0xdc00 || low > 0xdfff)
          {
            return false;
          }
          codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
        }
        if (!append_codepoint(output, codepoint))
        {
          return false;
        }
        break;
      }
      default:
        return false;
      }
    }
    return false;
  }

  bool parse_number(json_value& output)
  {
    const std::size_t begin = position_;
    if (position_ < input_.size() && input_[position_] == '-')
    {
      ++position_;
    }
    if (position_ >= input_.size())
    {
      return false;
    }
    if (input_[position_] == '0')
    {
      ++position_;
      if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
      {
        return false;
      }
    }
    else if (input_[position_] >= '1' && input_[position_] <= '9')
    {
      while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
      {
        ++position_;
      }
    }
    else
    {
      return false;
    }
    if (position_ < input_.size() && input_[position_] == '.')
    {
      ++position_;
      const std::size_t fraction_begin = position_;
      while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
      {
        ++position_;
      }
      if (position_ == fraction_begin)
      {
        return false;
      }
    }
    if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E'))
    {
      ++position_;
      if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-'))
      {
        ++position_;
      }
      const std::size_t exponent_begin = position_;
      while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
      {
        ++position_;
      }
      if (position_ == exponent_begin)
      {
        return false;
      }
    }

    double value = 0.0;
    const std::string_view raw = input_.substr(begin, position_ - begin);
    const auto result = std::from_chars(raw.data(), raw.data() + raw.size(), value);
    if (result.ec != std::errc{} || result.ptr != raw.data() + raw.size() || !std::isfinite(value))
    {
      return false;
    }
    output.data = value;
    return true;
  }

  bool parse_array(json_value& output, std::size_t depth)
  {
    if (!consume('['))
    {
      return false;
    }
    json_value::array values;
    skip_space();
    if (consume(']'))
    {
      output.data = std::move(values);
      return true;
    }
    while (true)
    {
      json_value value;
      if (!parse_value(value, depth))
      {
        return false;
      }
      values.push_back(std::move(value));
      skip_space();
      if (consume(']'))
      {
        output.data = std::move(values);
        return true;
      }
      if (!consume(','))
      {
        return false;
      }
      skip_space();
    }
  }

  bool parse_object(json_value& output, std::size_t depth)
  {
    if (!consume('{'))
    {
      return false;
    }
    json_value::object values;
    skip_space();
    if (consume('}'))
    {
      output.data = std::move(values);
      return true;
    }
    while (true)
    {
      std::string key;
      if (!parse_string(key))
      {
        return false;
      }
      skip_space();
      if (!consume(':'))
      {
        return false;
      }
      skip_space();
      json_value value;
      if (!parse_value(value, depth))
      {
        return false;
      }
      if (!values.emplace(std::move(key), std::move(value)).second)
      {
        return false;
      }
      skip_space();
      if (consume('}'))
      {
        output.data = std::move(values);
        return true;
      }
      if (!consume(','))
      {
        return false;
      }
      skip_space();
    }
  }

  std::string_view input_;
  std::size_t position_ = 0;
};

const json_value::object* as_object(const json_value& value)
{
  return std::get_if<json_value::object>(&value.data);
}

const json_value* field(const json_value::object& object, std::string_view name)
{
  const auto it = object.find(name);
  return it == object.end() ? nullptr : &it->second;
}

const std::string* string_field(const json_value::object& object, std::string_view name)
{
  const json_value* value = field(object, name);
  return value ? std::get_if<std::string>(&value->data) : nullptr;
}

const bool* bool_field(const json_value::object& object, std::string_view name)
{
  const json_value* value = field(object, name);
  return value ? std::get_if<bool>(&value->data) : nullptr;
}

const double* number_field(const json_value::object& object, std::string_view name)
{
  const json_value* value = field(object, name);
  return value ? std::get_if<double>(&value->data) : nullptr;
}

bool is_null(const json_value* value)
{
  return value && std::holds_alternative<std::nullptr_t>(value->data);
}

bool parse_root_object(std::string_view text, json_value& root, const json_value::object*& object)
{
  json_parser parser(text);
  if (!parser.parse(root))
  {
    return false;
  }
  object = as_object(root);
  return object != nullptr;
}

bool parse_vector3(const json_value* value, std::array<double, 3>& output)
{
  if (!value)
  {
    return false;
  }
  const auto* array = std::get_if<json_value::array>(&value->data);
  if (!array || array->size() != output.size())
  {
    return false;
  }
  for (std::size_t i = 0; i < output.size(); ++i)
  {
    const double* number = std::get_if<double>(&(*array)[i].data);
    if (!number || !std::isfinite(*number))
    {
      return false;
    }
    output[i] = *number;
  }
  return true;
}

std::string json_quote(std::string_view text)
{
  std::ostringstream output;
  output << '"';
  constexpr char hex[] = "0123456789abcdef";
  for (const unsigned char ch : text)
  {
    switch (ch)
    {
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
      if (ch < 0x20)
      {
        output << "\\u00" << hex[(ch >> 4) & 0xf] << hex[ch & 0xf];
      }
      else
      {
        output << static_cast<char>(ch);
      }
      break;
    }
  }
  output << '"';
  return output.str();
}

std::string safe_reason(std::string_view reason)
{
  if (reason == "robot_unstable" || reason == "invalid_pose" || reason == "invalid_frame" ||
      reason == "ball_stale" || reason == "context_changed" ||
      reason == "walking_controller_rejected" || reason == "host_policy_rejected")
  {
    return std::string(reason);
  }
  return "host_policy_rejected";
}

#if !defined(_WIN32)
class socket_handle
{
public:
  explicit socket_handle(int descriptor) : descriptor_(descriptor) {}
  ~socket_handle()
  {
    if (descriptor_ >= 0)
    {
      ::close(descriptor_);
    }
  }
  socket_handle(const socket_handle&) = delete;
  socket_handle& operator=(const socket_handle&) = delete;
  [[nodiscard]] int get() const noexcept { return descriptor_; }

private:
  int descriptor_ = -1;
};

int remaining_milliseconds(std::chrono::steady_clock::time_point deadline)
{
  const auto now = std::chrono::steady_clock::now();
  if (now >= deadline)
  {
    return 0;
  }
  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
  return static_cast<int>(std::min<std::int64_t>(remaining + 1,
                                                 std::numeric_limits<int>::max()));
}

bool wait_for(int descriptor, short events, std::chrono::steady_clock::time_point deadline)
{
  while (true)
  {
    pollfd descriptor_state{.fd = descriptor, .events = events, .revents = 0};
    const int result = ::poll(&descriptor_state, 1, remaining_milliseconds(deadline));
    if (result > 0)
    {
      if ((descriptor_state.revents & POLLNVAL) != 0)
      {
        return false;
      }
      // A peer may close immediately after writing its single response. POSIX
      // still permits recv() to drain those bytes when poll reports POLLHUP.
      return (descriptor_state.revents & (events | POLLERR | POLLHUP)) != 0;
    }
    if (result == 0 || errno != EINTR)
    {
      return false;
    }
  }
}
#endif

} // namespace

bridge_client::bridge_client(bridge_client_config config) : config_(std::move(config)) {}

const bridge_client_config& bridge_client::config() const noexcept
{
  return config_;
}

bridge_status bridge_client::validate_config() const
{
  if (config_.socket_path.empty() || config_.socket_path.front() != '/' ||
      config_.timeout.count() <= 0 || config_.timeout > std::chrono::seconds(60) ||
      config_.max_response_bytes == 0 || config_.max_response_bytes > 16 * 1024)
  {
    return {.ok = false, .reason = "invalid_bridge_config"};
  }
#if !defined(_WIN32)
  if (config_.socket_path.size() >= sizeof(sockaddr_un{}.sun_path))
  {
    return {.ok = false, .reason = "socket_path_too_long"};
  }
#endif
  return {.ok = true, .reason = {}};
}

std::optional<std::string> bridge_client::exchange(std::string request,
                                                    std::string& reason) const
{
  const bridge_status config_status = validate_config();
  if (!config_status.ok)
  {
    reason = config_status.reason;
    return std::nullopt;
  }
#if defined(_WIN32)
  (void)request;
  reason = "unix_socket_unavailable";
  return std::nullopt;
#else
  try
  {
    if (request.size() + 1 > 16 * 1024)
    {
      reason = "request_too_large";
      return std::nullopt;
    }
    request.push_back('\n');
    socket_handle socket(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (socket.get() < 0)
    {
      reason = "socket_create_failed";
      return std::nullopt;
    }
#if defined(SO_NOSIGPIPE)
    const int enabled = 1;
    (void)::setsockopt(socket.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    const int original_flags = ::fcntl(socket.get(), F_GETFL, 0);
    if (original_flags < 0 || ::fcntl(socket.get(), F_SETFL, original_flags | O_NONBLOCK) < 0)
    {
      reason = "socket_configuration_failed";
      return std::nullopt;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::copy(config_.socket_path.begin(), config_.socket_path.end(), address.sun_path);
    const auto deadline = std::chrono::steady_clock::now() + config_.timeout;
    if (::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0)
    {
      if (errno != EINPROGRESS || !wait_for(socket.get(), POLLOUT, deadline))
      {
        reason = "bridge_unavailable";
        return std::nullopt;
      }
      int socket_error = 0;
      socklen_t socket_error_size = sizeof(socket_error);
      if (::getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) < 0 ||
          socket_error != 0)
      {
        reason = "bridge_unavailable";
        return std::nullopt;
      }
    }

    std::size_t sent = 0;
    while (sent < request.size())
    {
      if (!wait_for(socket.get(), POLLOUT, deadline))
      {
        reason = "bridge_timeout";
        return std::nullopt;
      }
#if defined(MSG_NOSIGNAL)
      constexpr int send_flags = MSG_NOSIGNAL;
#else
      constexpr int send_flags = 0;
#endif
      const ssize_t count =
          ::send(socket.get(), request.data() + sent, request.size() - sent, send_flags);
      if (count > 0)
      {
        sent += static_cast<std::size_t>(count);
      }
      else if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
      {
        reason = "bridge_write_failed";
        return std::nullopt;
      }
    }

    std::string response;
    response.reserve(std::min<std::size_t>(config_.max_response_bytes, 4096));
    while (true)
    {
      if (!wait_for(socket.get(), POLLIN, deadline))
      {
        reason = "bridge_timeout";
        return std::nullopt;
      }
      char buffer[1024];
      const ssize_t count = ::recv(socket.get(), buffer, sizeof(buffer), 0);
      if (count == 0)
      {
        reason = "truncated_response";
        return std::nullopt;
      }
      if (count < 0)
      {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        {
          continue;
        }
        reason = "bridge_read_failed";
        return std::nullopt;
      }
      const std::string_view chunk(buffer, static_cast<std::size_t>(count));
      const std::size_t newline = chunk.find('\n');
      const std::size_t bytes_to_append = newline == std::string_view::npos ? chunk.size() : newline;
      if (response.size() + bytes_to_append > config_.max_response_bytes)
      {
        reason = "response_too_large";
        return std::nullopt;
      }
      response.append(chunk.data(), bytes_to_append);
      if (newline != std::string_view::npos)
      {
        if (newline + 1 != chunk.size())
        {
          reason = "multiple_responses";
          return std::nullopt;
        }
        reason.clear();
        return response;
      }
    }
  }
  catch (...)
  {
    reason = "bridge_client_failure";
    return std::nullopt;
  }
#endif
}

bridge_status bridge_client::ping() const noexcept
{
  try
  {
    return ping_impl();
  }
  catch (...)
  {
    return {.ok = false, .reason = "invalid_bridge_response"};
  }
}

bridge_status bridge_client::ping_impl() const
{
  std::string reason;
  const std::optional<std::string> response = exchange("{\"op\":\"ping\"}", reason);
  if (!response)
  {
    return {.ok = false, .reason = std::move(reason)};
  }
  json_value root;
  const json_value::object* object = nullptr;
  if (!parse_root_object(*response, root, object))
  {
    return {.ok = false, .reason = "invalid_bridge_response"};
  }
  const std::string* schema = string_field(*object, "schema_version");
  const bool* ok = bool_field(*object, "ok");
  if (!schema || *schema != "humanoid.booster_bridge.v1" || !ok || !*ok)
  {
    return {.ok = false, .reason = "invalid_bridge_response"};
  }
  return {.ok = true, .reason = {}};
}

snapshot_result bridge_client::snapshot() const noexcept
{
  try
  {
    return snapshot_impl();
  }
  catch (...)
  {
    return {.ok = false, .reason = "invalid_bridge_response", .snapshot = {}};
  }
}

snapshot_result bridge_client::snapshot_impl() const
{
  std::string reason;
  const std::optional<std::string> response = exchange("{\"op\":\"snapshot\"}", reason);
  if (!response)
  {
    return {.ok = false, .reason = std::move(reason), .snapshot = {}};
  }

  json_value root;
  const json_value::object* object = nullptr;
  if (!parse_root_object(*response, root, object))
  {
    return {.ok = false, .reason = "invalid_bridge_response", .snapshot = {}};
  }
  const std::string* schema = string_field(*object, "schema_version");
  const std::string* context_id = string_field(*object, "ball_context_id");
  const bool* ball_available = bool_field(*object, "ball_available");
  const bool* robot_stable = bool_field(*object, "robot_stable");
  const bool* emergency = bool_field(*object, "emergency");
  const bool* motion_enabled = bool_field(*object, "motion_enabled");
  if (!schema || *schema != snapshot_response_schema || !context_id || !ball_available ||
      !robot_stable || !emergency || !motion_enabled)
  {
    return {.ok = false, .reason = "invalid_bridge_response", .snapshot = {}};
  }

  bridge_snapshot snapshot{.ball_context_id = *context_id,
                           .ball_available = *ball_available,
                           .robot_stable = *robot_stable,
                           .emergency = *emergency,
                           .motion_enabled = *motion_enabled};
  const json_value* ball_position = field(*object, "ball_position_m");
  if (snapshot.ball_available)
  {
    std::array<double, 3> position{};
    if (!parse_vector3(ball_position, position) || snapshot.ball_context_id.empty())
    {
      return {.ok = false, .reason = "invalid_bridge_response", .snapshot = {}};
    }
    snapshot.ball_position_m = position;
  }
  else if (!is_null(ball_position))
  {
    return {.ok = false, .reason = "invalid_bridge_response", .snapshot = {}};
  }

  const json_value* robot_value = field(*object, "robot_pose");
  if (!is_null(robot_value))
  {
    const json_value::object* robot_object = robot_value ? as_object(*robot_value) : nullptr;
    if (!robot_object)
    {
      return {.ok = false, .reason = "invalid_bridge_response", .snapshot = {}};
    }
    const std::string* frame_id = string_field(*robot_object, "frame_id");
    const double* x_m = number_field(*robot_object, "x_m");
    const double* y_m = number_field(*robot_object, "y_m");
    const double* yaw_rad = number_field(*robot_object, "yaw_rad");
    if (!frame_id || frame_id->empty() || !x_m || !y_m || !yaw_rad)
    {
      return {.ok = false, .reason = "invalid_bridge_response", .snapshot = {}};
    }
    snapshot.robot = robot_pose{.frame_id = *frame_id,
                                .x_m = *x_m,
                                .y_m = *y_m,
                                .yaw_rad = *yaw_rad};
  }
  return {.ok = true, .reason = {}, .snapshot = std::move(snapshot)};
}

bt::walking_target_dispatch_result bridge_client::dispatch(
    const bt::walking_target_dispatch_context& context,
    const bt::walking_target& target) const noexcept
{
  try
  {
    return dispatch_impl(context, target);
  }
  catch (...)
  {
    return {.accepted = false, .reason = "host_policy_rejected"};
  }
}

bt::walking_target_dispatch_result bridge_client::dispatch_impl(
    const bt::walking_target_dispatch_context& context,
    const bt::walking_target& target) const
{
  if (context.job_id == 0 || context.generation == 0 || context.captured_context_id.empty() ||
      !std::isfinite(target.x_m) || !std::isfinite(target.y_m) ||
      !std::isfinite(target.yaw_rad) || target.frame_id.empty())
  {
    return {.accepted = false, .reason = "host_policy_rejected"};
  }

  std::ostringstream request;
  request.precision(std::numeric_limits<double>::max_digits10);
  request << "{\"op\":\"dispatch\",\"schema_version\":" << json_quote(dispatch_request_schema)
          << ",\"job_id\":" << json_quote("job-" + std::to_string(context.job_id))
          << ",\"generation\":" << context.generation << ",\"requesting_node_id\":"
          << context.requesting_node << ",\"authority_node_id\":" << context.authority_node
          << ",\"dispatching_node_id\":" << context.dispatching_node << ",\"job_key\":"
          << json_quote(context.job_key) << ",\"captured_context_id\":"
          << json_quote(context.captured_context_id) << ",\"current_context_id\":"
          << json_quote(context.current_context_id) << ",\"target\":{\"frame_id\":"
          << json_quote(target.frame_id) << ",\"x_m\":" << target.x_m << ",\"y_m\":"
          << target.y_m << ",\"yaw_rad\":" << target.yaw_rad << "}}";

  std::string reason;
  const std::optional<std::string> response = exchange(request.str(), reason);
  if (!response)
  {
    return {.accepted = false, .reason = "host_policy_rejected"};
  }
  json_value root;
  const json_value::object* object = nullptr;
  if (!parse_root_object(*response, root, object))
  {
    return {.accepted = false, .reason = "host_policy_rejected"};
  }
  const std::string* schema = string_field(*object, "schema_version");
  const bool* accepted = bool_field(*object, "accepted");
  const std::string* response_reason = string_field(*object, "reason");
  const json_value* field_target = field(*object, "field_target");
  if (!schema || *schema != dispatch_response_schema || !accepted || !response_reason)
  {
    return {.accepted = false, .reason = "host_policy_rejected"};
  }
  if (!*accepted)
  {
    if (response_reason->empty() || !is_null(field_target))
    {
      return {.accepted = false, .reason = "host_policy_rejected"};
    }
    return {.accepted = false, .reason = safe_reason(*response_reason)};
  }

  const json_value::object* target_object = field_target ? as_object(*field_target) : nullptr;
  const std::string* frame_id = target_object ? string_field(*target_object, "frame_id") : nullptr;
  const double* x_m = target_object ? number_field(*target_object, "x_m") : nullptr;
  const double* y_m = target_object ? number_field(*target_object, "y_m") : nullptr;
  const double* yaw_rad = target_object ? number_field(*target_object, "yaw_rad") : nullptr;
  if (!response_reason->empty() || !frame_id || *frame_id != "field" || !x_m || !y_m || !yaw_rad)
  {
    return {.accepted = false, .reason = "host_policy_rejected"};
  }
  return {.accepted = true, .reason = {}};
}

bridge_walking_target_dispatcher::bridge_walking_target_dispatcher(bridge_client_config config)
    : client_(std::move(config))
{
}

bt::walking_target_dispatch_result bridge_walking_target_dispatcher::dispatch(
    const bt::walking_target_dispatch_context& context, const bt::walking_target& target)
{
  dispatch_count_.fetch_add(1, std::memory_order_relaxed);
  return client_.dispatch(context, target);
}

std::size_t bridge_walking_target_dispatcher::dispatch_count() const noexcept
{
  return dispatch_count_.load(std::memory_order_relaxed);
}

const bridge_client& bridge_walking_target_dispatcher::client() const noexcept
{
  return client_;
}

} // namespace muesli_bt::booster
