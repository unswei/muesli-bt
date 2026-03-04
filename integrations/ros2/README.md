# ROS2 Integration Skeleton

This directory contains the initial ROS2 integration skeleton for `muesli-bt`.

Current scope:

- exported extension factory: `muslisp::integrations::ros2::make_extension()`
- backend registration name: `ros2`
- canonical key usage in backend payloads:
  - `obs_schema`
  - `state_schema`
  - `action_schema`
- deterministic local stub behaviour for `env.observe` / `env.act` / `env.step`

Not implemented yet:

- ROS topic/action/service transport binding
- ROS graph lifecycle integration
- production robot adapter wiring

Build option:

- `-DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON`
