#include "delayed_fake_service.hpp"

#include <algorithm>
#include <thread>
#include <utility>

namespace humanoid_experiment
{

delayed_fake_service::delayed_fake_service(delayed_fake_service_config config)
    : config_(std::move(config))
{
}

bt::vla_response delayed_fake_service::infer(const bt::vla_request& request,
                                             std::function<bool(const bt::vla_partial&)>,
                                             std::atomic<bool>&)
{
  const auto completion_time = std::chrono::steady_clock::now() + config_.delay;
  while (std::chrono::steady_clock::now() < completion_time)
  {
    const auto remaining = completion_time - std::chrono::steady_clock::now();
    std::this_thread::sleep_for(
        std::min(std::chrono::duration_cast<std::chrono::milliseconds>(remaining),
                 std::chrono::milliseconds(5)));
  }

  bt::vla_response response;
  response.status = bt::vla_status::ok;
  response.model = request.model;
  response.action.type = bt::vla_action_type::continuous;
  response.action.frame_id = request.action_space.frame_id;
  response.action.u = {config_.x_m, config_.y_m, config_.yaw_rad};
  response.confidence = 1.0;
  response.explanation = "deterministic delayed approach pose";
  response.stats["configured_delay_ms"] = static_cast<double>(config_.delay.count());
  return response;
}

} // namespace humanoid_experiment
