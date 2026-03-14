# ros2 tutorial

## what this is

This page explains how the current ROS2 connection in `muesli-bt` works.

The goal is to make the boundary concrete:

- `muesli-bt` still owns BT semantics, planning semantics, async semantics, and canonical logging
- ROS2 supplies transport, timing, and host-side integration
- the adaptor stays thin, so deployability does not contaminate core runtime behaviour

## when to use it

Use this page when you want to:

- understand what the current ROS2 adaptor does and does not do
- run the first supported Linux ROS2 baseline
- embed `muesli_bt::integration_ros2` in a host application
- explain the `muesli-bt` and ROS2 boundary to another developer or reviewer

If you need the detailed implementation plan and contract surface, use [ros2 backend scope](ros2-backend-scope.md) after this page.

## how it works

### the short version

The current ROS2 adaptor is a thin host/backend bridge.

It does three practical jobs:

1. subscribe to `nav_msgs/msg/Odometry`
2. translate the latest sample into canonical `ros2.obs.v1` and `ros2.state.v1` maps
3. publish canonical `ros2.action.v1` commands as `geometry_msgs/msg/Twist`

That means the control flow looks like this:

1. a host process registers the ROS2 extension
2. Lisp code attaches the backend with `(env.attach "ros2")`
3. the backend observes ROS2 state through `env.observe`
4. BT or Lisp logic decides on an action
5. the backend publishes that action through `env.act`
6. `env.step` advances the control loop while the runtime keeps the same BT/planner/async semantics it would use elsewhere

### what stays inside muesli-bt

The following behaviour remains runtime-owned and ROS-independent:

- BT compilation and ticking
- `planner.plan` and `plan-action` semantics
- async and VLA lifecycle semantics
- canonical event schema design
- fallback and budget handling semantics

ROS2 does not become the semantic model.
It is the transport and host integration layer.

### what the ROS2 adaptor owns

The current adaptor owns:

- ROS node and executor lifecycle inside the backend instance
- topic name resolution
- schema validation for `backend_version`, `obs_schema`, `state_schema`, and `action_schema`
- translation between ROS messages and canonical map payloads
- bounded executor progress for `env.observe` and `env.step`

### what the current adaptor does not do

The current release baseline is intentionally narrow.
It does not try to be a general ROS framework.

Non-goals in this release:

- MoveIt integration
- ROS actions or services as public runtime semantics
- multi-robot orchestration
- real reset support for live robot or simulator runs
- broader topic families beyond `Odometry` in and `Twist` out

### why this shape matters

This split gives you one BT/runtime story across different host environments.

The same `muesli-bt` logic can:

- run against a simulator backend
- run against the thin ROS2 transport adaptor
- emit the same canonical contract-facing data surfaces

That is the main point of the current ROS2 work.
It proves deployability without moving semantics into ROS.

## api / syntax

### supported baseline

The current supported ROS2 baseline is:

- OS: Ubuntu 22.04
- ROS distro: ROS 2 Humble
- input: `nav_msgs/msg/Odometry`
- output: `geometry_msgs/msg/Twist`
- attach path: `(env.attach "ros2")`
- package export: `muesli_bt::integration_ros2`
- runner: `muslisp_ros2`
- live reset policy: `reset_mode="unsupported"`

### minimal attach and configure flow

```lisp
(begin
  (env.attach "ros2")

  (define cfg (map.make))
  (map.set! cfg 'backend_version "ros2.transport.v1")
  (map.set! cfg 'topic_ns "/robot")
  (map.set! cfg 'obs_source "odom")
  (map.set! cfg 'action_sink "cmd_vel")
  (map.set! cfg 'reset_mode "unsupported")
  (env.configure cfg)

  (env.info))
```

This attaches the backend, binds the supported topics, and exposes the backend identity through `env.info`.

### consumer-side CMake shape

```cmake
find_package(muesli_bt CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE muesli_bt::runtime)

if(TARGET muesli_bt::integration_ros2)
  target_link_libraries(app PRIVATE muesli_bt::integration_ros2)
endif()
```

### live runner flow

Terminal A publishes odometry on the supported topic path:

```bash
source /opt/ros/humble/setup.bash
ros2 topic pub -r 20 /robot/odom nav_msgs/msg/Odometry \
  '{header: {frame_id: "map"}, child_frame_id: "base_link", pose: {pose: {position: {x: 0.0, y: 0.0, z: 0.0}, orientation: {x: 0.0, y: 0.0, z: 0.0, w: 1.0}}}, twist: {twist: {linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}}}'
```

Terminal B runs the checked-in script through the ROS2-enabled runner:

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-ros2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF \
  -DMUESLI_BT_BUILD_PYTHON_BRIDGE=OFF \
  -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=OFF
cmake --build build/linux-ros2 -j
./build/linux-ros2/muslisp_ros2 examples/repl_scripts/ros2-live-odom-twist.lisp
```

This writes `build/linux-ros2/ros2-live-run.jsonl`.

## example

The checked-in live example uses `env.run-loop` to observe odometry and publish a bounded angular command.

```lisp
--8<-- "examples/repl_scripts/ros2-live-odom-twist.lisp"
```

What this example demonstrates:

- the ROS2 extension attach path
- stable backend configuration through canonical keys
- `env.run-loop` using ROS-backed observations
- action publication through the `cmd_vel` sink
- run-loop artefact generation without changing core runtime semantics

## gotchas

- `muslisp` by itself does not auto-register the ROS2 extension. Use `muslisp_ros2` or register `muslisp::integrations::ros2::make_extension()` in your own host.
- For scripted validation, start the odometry publisher before the runner. Otherwise the first tick may fall back safely before transport is ready.
- Live runs currently use `reset_mode="unsupported"`. Multi-episode loops therefore need a reset-capable harness rather than a live robot/simulator path.
- Schema ids are version-checked. Stay inside the current `ros2.obs.*.v1`, `ros2.state.*.v1`, and `ros2.action.*.v1` families.
- The current adaptor is a transport bridge, not a MoveIt, Nav2, or perception framework.

## see also

- [integration overview](overview.md)
- [env api (`env.api.v1`)](env-api.md)
- [ros2 backend scope](ros2-backend-scope.md)
- [conformance levels](../contracts/conformance.md)
- [consume as a package](../getting-started-consume.md)
