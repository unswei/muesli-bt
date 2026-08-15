#include "booster/bridge_client.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{

using namespace std::chrono_literals;

void require(bool condition, const char* message)
{
  if (!condition)
  {
    throw std::runtime_error(message);
  }
}

class mock_bridge
{
public:
  explicit mock_bridge(std::string response)
      : directory_(create_temp_directory()), socket_path_(directory_ / "bridge.sock"),
        response_(std::move(response)), thread_([this] { serve_one(); })
  {
    while (!ready_.load(std::memory_order_acquire))
    {
      std::this_thread::yield();
    }
    if (!server_error_.empty())
    {
      thread_.join();
      throw std::runtime_error(server_error_);
    }
  }

  ~mock_bridge()
  {
    if (thread_.joinable())
    {
      thread_.join();
    }
    std::error_code ignored;
    std::filesystem::remove(socket_path_, ignored);
    std::filesystem::remove(directory_, ignored);
  }

  mock_bridge(const mock_bridge&) = delete;
  mock_bridge& operator=(const mock_bridge&) = delete;

  [[nodiscard]] const std::filesystem::path& socket_path() const noexcept { return socket_path_; }
  [[nodiscard]] const std::string& request() const noexcept { return request_; }

private:
  static std::filesystem::path create_temp_directory()
  {
    std::string pattern = "/tmp/muesli-booster-bridge-test-XXXXXX";
    if (!::mkdtemp(pattern.data()))
    {
      throw std::runtime_error("mkdtemp failed");
    }
    return pattern;
  }

  void serve_one() noexcept
  {
    const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0)
    {
      server_error_ = "mock socket creation failed";
      ready_.store(true, std::memory_order_release);
      return;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string path = socket_path_.string();
    std::copy(path.begin(), path.end(), address.sun_path);
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(listener, 1) < 0)
    {
      server_error_ = std::string("mock bind/listen failed: ") + std::strerror(errno);
      ::close(listener);
      ready_.store(true, std::memory_order_release);
      return;
    }
    ready_.store(true, std::memory_order_release);

    const int client = ::accept(listener, nullptr, nullptr);
    if (client >= 0)
    {
      char buffer[1024];
      while (request_.find('\n') == std::string::npos)
      {
        const ssize_t count = ::recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0)
        {
          break;
        }
        request_.append(buffer, static_cast<std::size_t>(count));
      }
      std::string wire_response = response_;
      wire_response.push_back('\n');
      std::size_t sent = 0;
      while (sent < wire_response.size())
      {
        const ssize_t count =
            ::send(client, wire_response.data() + sent, wire_response.size() - sent, 0);
        if (count <= 0)
        {
          break;
        }
        sent += static_cast<std::size_t>(count);
      }
      ::close(client);
    }
    ::close(listener);
  }

  std::filesystem::path directory_;
  std::filesystem::path socket_path_;
  std::string response_;
  std::string request_;
  std::string server_error_;
  std::atomic_bool ready_{false};
  std::thread thread_;
};

muesli_bt::booster::bridge_client_config config_for(const mock_bridge& server)
{
  return {.socket_path = server.socket_path().string(),
          .timeout = 500ms,
          .max_response_bytes = 16 * 1024};
}

bt::walking_target_dispatch_context invocation_context()
{
  return {.instance_handle = 3,
          .job_id = 42,
          .generation = 7,
          .requesting_node = 11,
          .authority_node = 12,
          .dispatching_node = 13,
          .job_key = "approach-job",
          .captured_context_id = "ball-0001",
          .current_context_id = "ball-0001"};
}

void test_snapshot_round_trip()
{
  mock_bridge server(
      R"({"schema_version":"humanoid.booster_snapshot.v1","ball_context_id":"ball-0001","ball_available":true,"ball_position_m":[1.2,-0.35,0.0],"robot_pose":{"frame_id":"field","x_m":0.1,"y_m":0.2,"yaw_rad":0.3},"robot_stable":true,"emergency":false,"motion_enabled":true})");
  muesli_bt::booster::bridge_client client(config_for(server));
  const auto result = client.snapshot();
  require(result.ok, "valid snapshot was rejected");
  require(result.snapshot.ball_context_id == "ball-0001", "context ID did not round trip");
  require(result.snapshot.ball_position_m.has_value(), "ball position was not parsed");
  require((*result.snapshot.ball_position_m)[0] == 1.2, "ball position changed");
  require(result.snapshot.robot.has_value(), "robot pose was not parsed");
  require(result.snapshot.robot_stable && !result.snapshot.emergency,
          "stable snapshot flags changed");
}

void test_ping_round_trip()
{
  mock_bridge server(R"({"schema_version":"humanoid.booster_bridge.v1","ok":true})");
  muesli_bt::booster::bridge_client client(config_for(server));
  require(client.ping().ok, "valid bridge ping was rejected");
}

void test_dispatch_preserves_invocation_scope()
{
  mock_bridge server(
      R"({"schema_version":"humanoid.booster_dispatch_response.v1","accepted":true,"reason":"","field_target":{"frame_id":"field","x_m":0.75,"y_m":-0.27,"yaw_rad":0.0}})");
  muesli_bt::booster::bridge_walking_target_dispatcher dispatcher(config_for(server));
  const auto result = dispatcher.dispatch(
      invocation_context(),
      bt::walking_target{.frame_id = "ball_context", .x_m = -0.45, .y_m = 0.08, .yaw_rad = 0.0});
  require(result.accepted, "valid host dispatch was rejected");
  require(dispatcher.dispatch_count() == 1, "dispatch count was not recorded");
  const std::string& request = server.request();
  require(request.find("\"job_id\":\"job-42\"") != std::string::npos,
          "job ID was omitted from dispatch request");
  require(request.find("\"generation\":7") != std::string::npos,
          "generation was omitted from dispatch request");
  require(request.find("\"requesting_node_id\":11") != std::string::npos,
          "requesting node was omitted from dispatch request");
  require(request.find("\"job_key\":\"approach-job\"") != std::string::npos,
          "job key was omitted from dispatch request");
  require(request.find("\"captured_context_id\":\"ball-0001\"") != std::string::npos,
          "captured context was omitted from dispatch request");
}

void test_unknown_host_reason_fails_closed()
{
  mock_bridge server(
      R"({"schema_version":"humanoid.booster_dispatch_response.v1","accepted":false,"reason":"motion_disabled","field_target":null})");
  muesli_bt::booster::bridge_client client(config_for(server));
  const auto result = client.dispatch(
      invocation_context(),
      bt::walking_target{.frame_id = "ball_context", .x_m = -0.45, .y_m = 0.08, .yaw_rad = 0.0});
  require(!result.accepted && result.reason == "host_policy_rejected",
          "unknown adapter rejection did not fail closed");
}

void test_malformed_and_oversized_responses_fail_closed()
{
  {
    mock_bridge server(
        R"({"schema_version":"humanoid.booster_snapshot.v1","schema_version":"humanoid.booster_snapshot.v1"})");
    muesli_bt::booster::bridge_client client(config_for(server));
    require(!client.snapshot().ok, "duplicate JSON keys were accepted");
  }
  {
    mock_bridge server(std::string(256, 'x'));
    auto config = config_for(server);
    config.max_response_bytes = 32;
    muesli_bt::booster::bridge_client client(std::move(config));
    require(!client.snapshot().ok, "oversized response was accepted");
  }
}

void test_unavailable_bridge_fails_closed()
{
  muesli_bt::booster::bridge_client client(
      {.socket_path = "/tmp/muesli-booster-definitely-absent.sock",
       .timeout = 20ms,
       .max_response_bytes = 1024});
  const auto result = client.snapshot();
  require(!result.ok, "absent bridge was reported as available");

  muesli_bt::booster::bridge_client relative_path_client(
      {.socket_path = "relative.sock", .timeout = 20ms, .max_response_bytes = 1024});
  require(!relative_path_client.ping().ok, "relative bridge path was accepted");
}

} // namespace

int main()
{
  try
  {
    test_snapshot_round_trip();
    test_ping_round_trip();
    test_dispatch_preserves_invocation_scope();
    test_unknown_host_reason_fails_closed();
    test_malformed_and_oversized_responses_fail_closed();
    test_unavailable_bridge_fails_closed();
    std::cout << "booster bridge tests passed\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "booster bridge test failure: " << error.what() << '\n';
    return 1;
  }
}
