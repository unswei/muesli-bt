#pragma once

#include "bt/walking_target_dispatch.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

namespace muesli_bt::booster
{

inline constexpr const char* dispatch_request_schema =
    "humanoid.booster_dispatch_request.v1";
inline constexpr const char* dispatch_response_schema =
    "humanoid.booster_dispatch_response.v1";
inline constexpr const char* snapshot_response_schema = "humanoid.booster_snapshot.v1";

struct bridge_client_config
{
  std::string socket_path = "/tmp/muesli-booster-bridge.sock";
  std::chrono::milliseconds timeout{100};
  std::size_t max_response_bytes = 16 * 1024;
};

struct bridge_status
{
  bool ok = false;
  std::string reason = "host_policy_rejected";
};

struct robot_pose
{
  std::string frame_id;
  double x_m = 0.0;
  double y_m = 0.0;
  double yaw_rad = 0.0;
};

struct bridge_snapshot
{
  std::string ball_context_id;
  bool ball_available = false;
  std::optional<std::array<double, 3>> ball_position_m;
  std::optional<robot_pose> robot;
  bool robot_stable = false;
  bool emergency = true;
  bool motion_enabled = false;
};

struct snapshot_result
{
  bool ok = false;
  std::string reason = "host_policy_rejected";
  bridge_snapshot snapshot;
};

/// Bounded synchronous client for the local Booster Studio host bridge.
///
/// Each operation opens one Unix-domain socket connection, sends one JSON line
/// and requires one complete response line before the configured deadline.
/// Transport and protocol faults fail closed.
class bridge_client
{
public:
  explicit bridge_client(bridge_client_config config);

  [[nodiscard]] const bridge_client_config& config() const noexcept;
  [[nodiscard]] bridge_status ping() const noexcept;
  [[nodiscard]] snapshot_result snapshot() const noexcept;
  [[nodiscard]] bt::walking_target_dispatch_result dispatch(
      const bt::walking_target_dispatch_context& context,
      const bt::walking_target& target) const noexcept;

private:
  [[nodiscard]] bridge_status ping_impl() const;
  [[nodiscard]] snapshot_result snapshot_impl() const;
  [[nodiscard]] bt::walking_target_dispatch_result dispatch_impl(
      const bt::walking_target_dispatch_context& context, const bt::walking_target& target) const;
  [[nodiscard]] bridge_status validate_config() const;
  [[nodiscard]] std::optional<std::string> exchange(std::string request,
                                                     std::string& reason) const;

  bridge_client_config config_;
};

/// Runtime dispatcher that preserves the invocation metadata at the host
/// boundary and delegates the final walking command to the Studio adapter.
class bridge_walking_target_dispatcher final : public bt::walking_target_dispatcher
{
public:
  explicit bridge_walking_target_dispatcher(bridge_client_config config);

  bt::walking_target_dispatch_result dispatch(
      const bt::walking_target_dispatch_context& context,
      const bt::walking_target& target) override;

  [[nodiscard]] std::size_t dispatch_count() const noexcept;
  [[nodiscard]] const bridge_client& client() const noexcept;

private:
  bridge_client client_;
  std::atomic_size_t dispatch_count_{0};
};

} // namespace muesli_bt::booster
