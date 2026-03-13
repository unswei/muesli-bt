# ROS2 Integration

This directory contains the current ROS2 integration for `muesli-bt`.

Current scope:

- exported extension factory: `muslisp::integrations::ros2::make_extension()`
- backend registration name: `ros2`
- first supported Linux baseline: Ubuntu 22.04 + ROS 2 Humble
- current transport path: `nav_msgs/msg/Odometry` input and `geometry_msgs/msg/Twist` output
- canonical key usage in backend payloads:
  - `obs_schema`
  - `state_schema`
  - `action_schema`
- real ROS transport for `env.observe` / `env.act` / `env.step`
- explicit reset policy: real runs use `reset_mode=\"unsupported\"`; `stub` is reserved for tests and harnesses

Still not implemented:

- broader topic/action/service transport binding
- richer ROS graph lifecycle control beyond the current backend-owned node/executor path
- production robot adapter wiring beyond the thin `Odometry` -> `Twist` bridge

Build option:

- `-DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON`
