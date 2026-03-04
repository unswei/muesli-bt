# ros2 backend scope

## what this is

This page defines the implementation scope for the first in-tree ROS2 backend for `muesli-bt`.

The goal is to make scope explicit before coding so CI, packaging, and conformance expectations stay aligned.

## when to use it

Use this page when you:

- plan ROS2 backend implementation tasks
- review PRs that touch ROS2 integration paths
- decide what is required for `L1`/`L2` conformance gating

## how it works

### phase boundaries

`phase 1: build + packaging skeleton`

Current status: implemented.

- add a dedicated integration target (`muesli_bt::integration_ros2`)
- keep build optional and default-off unless explicitly enabled
- install public ROS2 integration headers and export target metadata
- keep core runtime buildable when ROS2 is absent

`phase 2: minimal env.api.v1 bridge`

- implement backend attach/configure/info hooks for ROS2
- implement `env.observe`, `env.act`, `env.step` with deterministic test stubs
- enforce canonical key naming (`obs_schema`, `action_schema`, `state_schema`)
- wire canonical `mbt.evt.v1` event emission through existing runtime host APIs

`phase 3: run-loop + conformance`

- support `env.run-loop` observer + multi-episode semantics where reset is supported
- add backend-specific conformance tests and fixture cases
- wire ROS2-specific checks into `L2` conformance job

### in scope

- Linux-first support (Ubuntu 22.04/24.04 ROS2 toolchains)
- `env.api.v1` compatibility and deterministic testing hooks
- packaging/export behaviour consistent with current PyBullet/Webots integration targets

### out of scope (initial bring-up)

- production robot safety certification
- multi-robot orchestration and distributed graph management
- custom ROS message generators beyond required adapter payloads

## api / syntax

Planned build surface:

- CMake option: `MUESLI_BT_BUILD_INTEGRATION_ROS2=ON|OFF` (default `OFF` during bring-up)
- exported target: `muesli_bt::integration_ros2`

Planned backend contract surface:

- `env.info` advertises backend identity and schema ids
- `env.observe` returns observation maps with `obs_schema`
- `env.act` accepts canonical action maps with `action_schema`
- `env.run-loop` result maps keep compatibility keys (`episodes`, `ticks`) plus extended counters

## example

Minimal expected observation/action shapes for ROS2 backend bring-up:

```lisp
(define obs
  (map.make
    'obs_schema "ros2.obs.v1"
    'state_schema "ros2.state.v1"
    'state_vec (list x y yaw speed)
    't_ms now_ms))

(define action
  (map.make
    'action_schema "ros2.action.v1"
    'u (list steer throttle)))
```

## gotchas

- ROS distro/Ubuntu mismatch will fail dependency resolution before compile.
- `source /opt/ros/<distro>/setup.bash` is required in shells that run CMake.
- Keep ROS callbacks non-blocking relative to BT tick deadlines; long operations must stay async.
- Do not bypass canonical event stream with ad-hoc ROS logging formats.

## see also

- [integration overview](overview.md)
- [writing a backend](writing-a-backend.md)
- [environment api (`env.api.v1`)](env-api.md)
- [conformance levels](../contracts/conformance.md)
