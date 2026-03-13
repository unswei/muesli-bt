# ros2 backend scope

## what this is

This page turns the current ROS2 backend intent for `muesli-bt` into a concrete implementation plan.

The first ROS2 integration is intentionally modest:

- keep the core runtime ROS-agnostic
- provide an optional in-tree backend that implements `env.api.v1`
- preserve canonical `mbt.evt.v1` event logging and existing runtime-contract semantics
- make the first bring-up deterministic enough for tests, fixtures, and CI gating

This is a backend integration project, not a redesign of the runtime.

## when to use it

Use this page when you:

- plan or review ROS2 backend work
- decide which steps can be completed before Linux/ROS2 is available
- decide what belongs in `L0` generic coverage versus `L2` ROS2 conformance
- review whether a PR is leaking ROS-specific semantics into the core runtime

## current status

The first Linux transport bring-up is now implemented for:

- Ubuntu 22.04
- ROS 2 Humble
- `nav_msgs/msg/Odometry` observation input
- `geometry_msgs/msg/Twist` action output

What is implemented today:

- optional Linux-only `muesli_bt::integration_ros2` target with real ROS package discovery
- standard attach path through `(env.attach "ros2")`
- `env.configure`, `env.info`, `env.observe`, `env.act`, and `env.step` against real ROS transport
- explicit reset policy:
  - default `reset_mode` is `unsupported`
  - `reset_mode="stub"` is retained for deterministic harnesses and tests only
- Linux ROS-backed tests plus installed-package consumer smoke coverage
- Linux `L2` replay corpus covering nominal replay, clamped actions, invalid-action fallback, and reset-unsupported artefacts

What is still intentionally incomplete:

- simulator or robot reset beyond the explicit unsupported/stub policy
- broader transport coverage beyond the current `Odometry` / `Twist` path
- canonical `mbt.evt.v1` parity for ROS-backed run-loop observability beyond the current run-loop artefact checks

### first supported baseline

The first supported ROS2 baseline is intentionally narrow:

- OS: Ubuntu 22.04
- ROS distro: ROS 2 Humble
- input transport: `nav_msgs/msg/Odometry`
- output transport: `geometry_msgs/msg/Twist`
- attach path: `(env.attach "ros2")`
- live reset policy: `reset_mode="unsupported"`
- test-only reset policy: `reset_mode="stub"`

Non-goals for this first baseline:

- MoveIt integration
- ROS actions or services
- multi-robot orchestration
- simulator-specific reset integration

## how it works

### boundary

ROS2 is connected at the host/backend boundary, not inside the BT engine itself.

That means:

- `muesli-bt` core still owns BT compilation, ticking, planning calls, async orchestration, and canonical event emission
- the ROS2 integration provides a backend named `ros2` that implements the `env.*` surface
- the backend translates between ROS2 transport and canonical muesli maps

The ROS-facing pieces are:

1. optional integration library `muesli_bt::integration_ros2`
2. extension registration so `(env.attach "ros2")` remains the public attach path
3. backend instance state that owns ROS node handles, transport objects, latest observation cache, pending action state, and executor/pacing state
4. observation bridge from ROS input to canonical `ros2.obs.v1` and `ros2.state.v1`
5. action bridge from canonical `ros2.action.v1` to ROS transport
6. control-cycle bridge for `env.step`
7. optional reset bridge for `env.reset`
8. managed loop integration for `env.run-loop`
9. canonical event emission through existing runtime host APIs only

The following must remain ROS-independent:

- BT syntax and compile model
- planner semantics
- async/VLA semantics
- canonical event schema design
- replay contract shape
- blackboard semantics
- general runtime deadlines and tick semantics

### core design decisions

1. Keep ROS as transport, not as the semantic model.
2. Define canonical maps first, then transport bindings.
3. Bring up one backend instance and one robot/simulator endpoint first.
4. Use latest-sample observation semantics for the first version.
5. Keep callbacks non-blocking relative to BT tick deadlines.

### bring-up contract

The first ROS2 backend should present this public contract through `env.api.v1`:

- backend name: `ros2`
- env API id: `env.api.v1`
- observation schema: `ros2.obs.v1`
- state schema: `ros2.state.v1`
- action schema: `ros2.action.v1`
- explicit reset policy: either supported via stub/reset path or explicitly unsupported
- run-loop support through the existing managed `env.run-loop` path

For the in-tree skeleton backend used before Linux bring-up:

- reset policy is configurable and explicit
- action payloads use a map-based `u` payload
- malformed config keys fail cleanly
- backend-specific schema/config metadata is exposed through `env.info`

### phased work plan

Phases `0` to `5` are the work that should be completed before Linux/ROS2 transport is available.

`phase 0: lock the integration boundary on paper`

- confirm `env.api.v1` is the only public semantic surface
- confirm backend name `ros2`
- confirm ROS2 remains optional and default-off
- confirm canonical event output stays `mbt.evt.v1`
- decide the initial reset policy and non-goals list

Deliverables:

- update this page
- update [environment api (`env.api.v1`)](env-api.md)
- update [writing a backend](writing-a-backend.md)

`phase 1: finalise canonical schemas and config surface`

- formalise `ros2.obs.v1`
- formalise `ros2.state.v1`
- formalise `ros2.action.v1`
- document required fields, optional fields, units, and examples
- document action validation and fallback rules
- document the minimal `env.configure` surface

`phase 2: strengthen generic backend semantics in core tests`

- cover `env.attach`, `env.configure`, `env.info`, `env.observe`, `env.act`, `env.step`, `env.reset`, and `env.run-loop`
- cover invalid action payloads and safe fallback behaviour
- cover deadline and reset-policy behaviour
- keep this coverage in fast non-ROS test lanes

`phase 3: define deterministic fixtures before real ROS`

- define ROS2-shaped scenarios and expected counters/result maps
- define expected canonical event classes and fixture summaries
- keep fixture intent transport-agnostic until Linux implementation exists

`phase 4: review packaging and exports on macOS`

- verify `MUESLI_BT_BUILD_INTEGRATION_ROS2` naming
- verify exported target `muesli_bt::integration_ros2`
- define failure behaviour when the option is on but ROS2 dependencies are unavailable
- document consumer attach flow

`phase 5: draft Linux CI and L2 job plan`

- choose the first supported distro matrix
- define bootstrap steps and CMake flags
- define artefacts and pass/fail conditions
- keep true ROS2 gating in `L2`, not `L1`

`phase 6: implement the real Linux ROS2 backend`

- create the backend state object
- register the backend in `integrations/ros2/extension.cpp`
- implement `env.attach`, `env.configure`, `env.info`, `env.observe`, `env.act`, `env.step`, and explicit reset behaviour
- connect canonical event emission through existing host APIs

Status:

- completed for the first `Odometry` / `Twist` transport path on Ubuntu 22.04 + Humble
- completed for the first automated Linux-only `L2` replay corpus
- explicit first-milestone reset decision is now locked: real reset stays unsupported, with `stub` reserved for tests and harnesses
- still open for broader transport coverage and canonical event-stream parity

`phase 7: land L2 conformance`

- add rosbag-backed or equivalent deterministic replay evidence
- validate counters, result maps, and canonical log behaviour
- wire the suite into Linux-only `L2`

### acceptance criteria

#### architecture and boundary

- core runtime builds and passes without ROS2 present
- ROS2 integration stays optional and default-off
- the backend is reachable only through the stable extension plus `env.*` surface
- no ROS-specific assumptions are added to BT runtime core semantics

#### backend attach, configure, and info

- `(env.attach "ros2")` selects the backend
- `(env.info)` returns backend identity, schema ids, reset policy, and capability flags
- `(env.configure ...)` accepts the documented bring-up options
- malformed or unknown config keys fail cleanly

#### observation, action, and step

- `env.observe` returns a stable map with `obs_schema`, `state_schema`, timestamp, and canonical state payload
- `env.act` validates schema id and payload shape
- non-finite values are rejected
- out-of-range values are clamped or rejected according to documented policy
- `env.step` advances exactly one configured control increment without blocking on long ROS work

#### reset, run-loop, and observability

- reset behaviour is explicit and tested
- multi-episode `env.run-loop` only works when reset support is true
- when reset is unsupported and `episode_max > 1`, the result is `:unsupported`
- runtime observability remains on canonical `mbt.evt.v1`
- no ad hoc ROS-only log format is introduced for conformance

#### packaging and CI

- headers and target exports install cleanly when ROS2 integration is enabled
- generic backend semantics stay in fast CI
- true ROS2 transport checks stay in Linux-only `L2`

## api / syntax

### canonical live ROS2 run

The canonical live path currently uses the ROS2-enabled runner binary `muslisp_ros2`.

Terminal A: publish example odometry input on the supported topic path.

```bash
source /opt/ros/humble/setup.bash
ros2 topic pub -r 20 /robot/odom nav_msgs/msg/Odometry \
  '{header: {frame_id: "map"}, child_frame_id: "base_link", pose: {pose: {position: {x: 0.0, y: 0.0, z: 0.0}, orientation: {x: 0.0, y: 0.0, z: 0.0, w: 1.0}}}, twist: {twist: {linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}}}'
```

Terminal B: run the checked-in minimal ROS2 control loop script.

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

This command drives the backend through `/robot/odom` and publishes actions to `/robot/cmd_vel`.
It also writes a run-loop artefact to `build/linux-ros2/ros2-live-run.jsonl`.

Source used by the canonical live command:

```lisp
--8<-- "examples/repl_scripts/ros2-live-odom-twist.lisp"
```

### canonical rosbag replay command

The canonical replay path today is the `L2` rosbag-backed conformance lane:

```bash
source /opt/ros/humble/setup.bash
cmake -S . -B build/linux-ros2-l2 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMUESLI_BT_BUILD_INTEGRATION_ROS2=ON \
  -DMUESLI_BT_BUILD_INTEGRATION_PYBULLET=OFF \
  -DMUESLI_BT_BUILD_INTEGRATION_WEBOTS=OFF \
  -DMUESLI_BT_BUILD_PYTHON_BRIDGE=OFF \
  -DMUESLI_BT_BUILD_WEBOTS_EXAMPLES=OFF
cmake --build build/linux-ros2-l2 --target muesli_bt_conformance_l2_rosbag_tests -j
ctest --test-dir build/linux-ros2-l2 -R muesli_bt_conformance_l2_rosbag_tests --output-on-failure
python3 tools/verify_ros2_l2_artifacts.py --artifact-root build/linux-ros2-l2/ros2_l2_artifacts
```

This is the current canonical rosbag replay command because it exercises the supported Linux ROS2 transport path and verifies the emitted replay artefacts in one reproducible flow.

### backend identity via `env.info`

`env.info` for the ROS2 backend should expose at least:

- `backend`: `"ros2"`
- `env_api`: `"env.api.v1"`
- `obs_schema`: `"ros2.obs.v1"`
- `state_schema`: `"ros2.state.v1"`
- `action_schema`: `"ros2.action.v1"`
- `reset_supported`: `#t` or `#f`
- `run_loop_supported`: `#t`
- `capabilities`: capability tags such as `observe`, `act`, `step`, `reset`, `run_loop`, `event_log`
- `obs_topic` / `action_topic`: resolved ROS topic names for the current transport binding
- `node_name`: backend node identity for Linux debugging

### canonical schemas

#### `ros2.state.v1`

Purpose: compact robot state used by control logic and replay.

Required fields:

- `state_schema`: `"ros2.state.v1"`
- `frame_id`: string
- `child_frame_id`: string or empty string
- `t_ms`: integer timestamp in milliseconds
- `pose`: map with `x`, `y`, `z`, `qx`, `qy`, `qz`, `qw`
- `twist`: map with `vx`, `vy`, `vz`, `wx`, `wy`, `wz`

Optional fields:

- `state_vec`
- `covariance`
- `source`

#### `ros2.obs.v1`

Purpose: observation returned by `env.observe`.

Required fields:

- `obs_schema`: `"ros2.obs.v1"`
- `state_schema`: `"ros2.state.v1"`
- `t_ms`: integer timestamp in milliseconds
- `state`: map conforming to `ros2.state.v1`

Optional fields:

- `state_vec`
- `ranges`
- `image`
- `flags`
- `tick_hint`
- `episode`
- `step`

#### `ros2.action.v1`

Purpose: action accepted by `env.act`.

Required fields:

- `action_schema`: `"ros2.action.v1"`
- `t_ms`: integer timestamp in milliseconds
- `u`: map with:
  - `linear_x`
  - `linear_y`
  - `angular_z`

For the first in-tree implementation, map-based action fields are the public contract. Dense vector actions are out of scope for the initial adapter.

### current Linux transport binding

The first Linux transport path is intentionally narrow:

- `env.observe` reads the latest `nav_msgs/msg/Odometry` sample from `obs_source`
- `env.act` publishes `geometry_msgs/msg/Twist` to `action_sink`
- `topic_ns` prefixes both paths unless the configured topic is already absolute

This keeps the first real backend reviewable and deterministic enough for tests.

### `env.configure` keys for bring-up

The initial documented config keys are:

- `control_hz`
- `observe_timeout_ms`
- `step_timeout_ms`
- `use_sim_time`
- `require_fresh_obs`
- `action_clamp`
- `topic_ns`
- `obs_source`
- `action_sink`
- `reset_mode`

## example

Linux bring-up on Ubuntu 22.04 + Humble:

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
ctest --test-dir build/linux-ros2 --output-on-failure
```

```lisp
(begin
  (env.attach "ros2")

  (define cfg (map.make))
  (map.set! cfg 'control_hz 50)
  (map.set! cfg 'topic_ns "/robot")
  (map.set! cfg 'obs_source "odom")
  (map.set! cfg 'action_sink "cmd_vel")
  (map.set! cfg 'reset_mode "unsupported")
  (env.configure cfg)

  (define action (map.make))
  (define u (map.make))
  (map.set! action 'action_schema "ros2.action.v1")
  (map.set! action 't_ms 0)
  (map.set! u 'linear_x 0.1)
  (map.set! u 'linear_y 0.0)
  (map.set! u 'angular_z 0.2)
  (map.set! action 'u u)

  (env.act action)
  (env.step)
  (env.observe))
```

## gotchas

- ROS distro and Ubuntu version mismatch will fail before compile.
- Shells that run CMake on Linux need the ROS setup script sourced first.
- `MUESLI_BT_BUILD_INTEGRATION_ROS2=ON` now expects real ROS package discovery. Without a sourced ROS shell, CMake will fail while looking for `rclcpp`.
- `muslisp` alone does not auto-register the ROS2 extension. Use `muslisp_ros2` or embed `muslisp::integrations::ros2::make_extension(...)` in your own host.
- The first Linux backend only binds `Odometry` in and `Twist` out. Broader transport support belongs in later PRs.
- `reset_mode="stub"` is for deterministic harnesses and tests. It is not a full simulator or robot reset.
- Do not let ROS message names or executor details leak into core semantics.
- Do not bypass canonical event output with alternate ROS-only logs.
- Keep reset behaviour explicit from the first PR series.

## see also

- [integration overview](overview.md)
- [environment api (`env.api.v1`)](env-api.md)
- [writing a backend](writing-a-backend.md)
- [conformance levels](../contracts/conformance.md)
