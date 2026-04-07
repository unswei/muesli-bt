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
- dedicated GitHub release packaging on Ubuntu 22.04 + Humble for the ROS-enabled artefact path in `v0.3.1`

What is implemented today:

- optional Linux-only `muesli_bt::integration_ros2` target with real ROS package discovery
- standard attach path through `(env.attach "ros2")`
- `env.configure`, `env.info`, `env.observe`, `env.act`, and `env.step` against real ROS transport
- explicit reset policy:

    - default `reset_mode` is `unsupported`
    - `reset_mode="stub"` is retained for deterministic harnesses and tests only

- Linux ROS-backed tests, installed-package consumer smoke coverage, and live runner validation via `muslisp_ros2`
- Linux `L2` replay corpus covering nominal replay, clamped actions, invalid-action fallback, and reset-unsupported artefacts

What is still intentionally incomplete:

- simulator or robot reset beyond the explicit unsupported/stub policy
- broader transport coverage beyond the current `Odometry` / `Twist` path
- explicit ROS time-source reporting and replay-verification tooling beyond the direct canonical event-log path

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
- backend version: `ros2.transport.v1`
- observation schema: `ros2.obs.v1`
- state schema: `ros2.state.v1`
- action schema: `ros2.action.v1`
- explicit reset policy: either supported via stub/reset path or explicitly unsupported
- run-loop support through the existing managed `env.run-loop` path

For the in-tree skeleton backend used before Linux bring-up:

- reset policy is configurable and explicit
- action payloads use a map-based `u` payload
- malformed config keys fail cleanly
- malformed schema ids or incompatible schema majors fail cleanly
- backend-specific schema/config metadata is exposed through `env.info`

### phased work plan

This page now tracks two kinds of ROS work:

- completed baseline phases that led to the released `v0.3.x` ROS2 thin-adaptor surface
- next ROS-focused phases that map onto the broader [roadmap to 1.0](../roadmap-to-1.0.md)

`phases 0 to 7: baseline bring-up and release packaging`

These phases are complete for the first released ROS2 baseline.

- phase `0`: lock the integration boundary on paper
- phase `1`: finalise canonical schemas and config surface
- phase `2`: strengthen generic backend semantics in core tests
- phase `3`: define deterministic fixtures before real ROS
- phase `4`: review packaging and exports
- phase `5`: draft Linux CI and `L2` job plan
- phase `6`: implement the real Linux ROS2 backend
- phase `7`: land `L2` conformance

Delivered baseline:

- `(env.attach "ros2")` remains the only public attach path
- `env.info` exposes `backend_version`, `obs_schema`, `state_schema`, `action_schema`, reset policy, and capability metadata
- the first live transport path is released for `Odometry` in and `Twist` out on Ubuntu 22.04 + Humble
- Linux ROS-backed tests, rosbag-backed `L2`, install/export coverage, and consumer smoke all exist
- `v0.3.1` adds a dedicated Ubuntu 22.04 + ROS 2 Humble release artefact for the ROS-enabled package path

`phase 8: v0.4.0 observability parity`

Delivered on `main`:

- ROS-backed runs now emit direct canonical `mbt.evt.v1` logs through `event_log_path`
- ROS-backed replay and conformance now treat the canonical event log as the primary artefact
- the ROS time-source policy is now logged and documented explicitly (`time_source`, `use_sim_time`, `obs_timestamp_source`)
- `tools/verify_ros2_l2_artifacts.py` is now the documented replay verification command for the ROS `L2` artefact lane
- long multi-episode runs now emit `episode_begin`, `episode_end`, and `run_end` in the canonical stream

Delivered exit target:

- a ROS-backed run emits validated canonical `mbt.evt.v1` output directly
- replay verification checks event ordering, schema validity, and selected decision invariants

`phase 9: v0.5.0 same BT, different IO transport`

- choose one canonical wheeled BT and reuse it across PyBullet, Webots, and ROS2
- prefer a goal-seeking behaviour derived from odometry/pose plus bounded obstacle context, not line-follow or wall-follow logic tied to simulator-specific sensors
- treat the existing Webots cluttered-goal demo as the best starting point for the shared behaviour
- keep differences at attach/config and transport wiring only
- add scripted checks that compare key behaviour or decision-trace invariants across the transports

Exit target:

- the ROS2 thin adaptor is clearly a transport/backend surface, not a ROS-specific semantic fork
- the chosen flagship runs on the released `Odometry` -> `Twist` contract without broadening the ROS surface first

`phase 10: v0.6.0 host capability bundles at the ROS boundary`

- keep `env.*` as the direct transport surface
- keep `planner.plan` as the in-runtime bounded decision planner
- define host capability bundle rules for richer external services such as manipulation, navigation, and perception
- document that higher-level ROS libraries such as MoveIt and Nav2 belong behind separate host capability contracts rather than inside core semantics

Exit target:

- the split between `env.*`, `planner.plan`, and external host capability bundles is explicit in docs and examples

`phase 11: v0.8.0 ROS-backed Isaac evidence packaging`

- keep the existing Isaac Sim / ROS2 H1 demo documented as a deployability and modern-simulator evidence point
- keep Isaac outside core semantics and off the critical CI path unless it becomes cheap and reproducible

`phase 12: v0.9.0 manipulation, perception, and MoveIt-backed proof`

- treat MoveIt as the first concrete host capability bundle instance, specifically an arm-motion or goal-execution capability
- add a usable perception path for the manipulator scenario rather than relying on hidden oracle state
- preferred target: a bounded Towers of Hanoi arm demo if the simulator, MoveIt path, and perception stack stay reproducible
- acceptable fallback: a simpler manipulator benchmark with the same host-capability split if Hanoi proves too heavy before `v1.0.0`

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
- ROS-backed `L2` evidence verifies canonical `mbt.evt.v1` output first, with any run-loop record JSONL treated as secondary compatibility output only

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
It can also write a canonical event log through `event_log_path`, for example `build/linux-ros2/ros2-live-run/events.jsonl`.

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
python3 tools/validate_log.py build/linux-ros2-l2/ros2_l2_artifacts/ros2_h1_success
```

This is the current canonical rosbag replay command because it exercises the supported Linux ROS2 transport path and verifies the emitted replay artefacts in one reproducible flow.
The verifier treats the canonical `mbt.evt.v1` log as the primary replay/conformance artefact.

### backend identity via `env.info`

`env.info` for the ROS2 backend should expose at least:

- `backend`: `"ros2"`
- `backend_version`: `"ros2.transport.v1"`
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

The released baseline is the `v1` family:

- `ros2.obs.v1`
- `ros2.state.v1`
- `ros2.action.v1`

Tests and conformance harnesses may use suffixes such as `ros2.obs.test.v1` or `ros2.action.conformance.v1`, but the schema id must stay in the matching `ros2.obs.*.v1`, `ros2.state.*.v1`, or `ros2.action.*.v1` family. `v2+` ids are intentionally rejected by the current backend.

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

- `backend_version` for explicit transport compatibility checks; when set, it must be `ros2.transport.v1`
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

### time-source policy

For the current ROS2 backend, the time policy is:

- `use_sim_time=#t` means the ROS node clock follows ROS simulation time
- `use_sim_time=#f` means the ROS node clock follows ROS wall time
- observation `t_ms` uses the incoming message header stamp when it is present
- if a message does not carry a usable header stamp, observation `t_ms` falls back to the ROS node clock
- canonical event ordering still relies on `seq`, not timestamp fields alone

The backend exposes this policy through `env.info`:

- `time_source`: `ros_sim_time` or `ros_wall_time`
- `obs_timestamp_source`: `message_header_or_node_clock`

The canonical `run_start` event for `env.run-loop` also records:

- `capabilities.time_source`
- `capabilities.use_sim_time`
- `capabilities.obs_timestamp_source`

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
  (map.set! cfg 'backend_version "ros2.transport.v1")
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
- `backend_version`, `obs_schema`, `state_schema`, and `action_schema` are version-checked. Stay within the current `ros2.*.v1` families until the contract is intentionally bumped.
- Do not let ROS message names or executor details leak into core semantics.
- Do not bypass canonical event output with alternate ROS-only logs.
- Keep reset behaviour explicit from the first PR series.

## see also

- [integration overview](overview.md)
- [environment api (`env.api.v1`)](env-api.md)
- [writing a backend](writing-a-backend.md)
- [cross-transport flagship for v0.5](cross-transport-flagship.md)
- [conformance levels](../contracts/conformance.md)
