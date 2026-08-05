#pragma once

#include "bt/vla.hpp"

#include <atomic>
#include <chrono>

namespace humanoid_experiment
{

struct delayed_fake_service_config
{
  std::chrono::milliseconds delay{2500};
  double x_m = -0.45;
  double y_m = 0.08;
  double yaw_rad = 0.0;
};

// A deterministic experiment backend. It deliberately finishes after
// cancellation so the runtime's logical revocation path is observable.
class delayed_fake_service final : public bt::vla_backend
{
public:
  explicit delayed_fake_service(delayed_fake_service_config config);

  bt::vla_response infer(const bt::vla_request& request,
                         std::function<bool(const bt::vla_partial&)> on_partial,
                         std::atomic<bool>& cancel_flag) override;

private:
  delayed_fake_service_config config_;
};

} // namespace humanoid_experiment
