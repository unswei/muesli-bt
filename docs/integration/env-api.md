# Environment API (`env.api.v1`)

`env.api.v1` is the backend-agnostic capability surface used by muesli-bt control loops.

A [host](../terminology.md#host) (backend) can implement this surface for Webots, PyBullet, ROS2, or hardware.

## Core Calls And Intent

- `env.info`
: return backend identity, capability flags, and backend-specific schema/config metadata.

- `env.attach`
: attach/select backend instance.

- `env.configure`
: apply runtime options (rate, scenario params, model knobs).

- `env.observe`
: read sensors/state and return observation map.

- `env.act`
: submit the action for the current control cycle.

- `env.step`
: advance to the next control tick.

- `env.reset`
: optional reset/restart of episode or robot state.

- `env.run-loop`
: managed loop wrapper around observe/tick/act/step with deadline/fallback handling.

## Canonical Key Naming

Backends should use consistent schema key names:

- observation maps: `obs_schema`
- action maps: `action_schema`
- backend state payloads (when present): `state_schema`

Backends may add more fields, but should not invent alternate key names for these three concepts.

## Backend Metadata Contract

`env.info` always exposes:

- `api_version`
- `attached`
- `backend`
- `backend_version`
- `supports`
- optional `notes`

Backends may expose additional metadata when attached. For example, the ROS2 skeleton backend exposes:

- `env_api`
- `obs_schema`
- `state_schema`
- `action_schema`
- `reset_supported`
- `run_loop_supported`
- `capabilities`
- backend-specific `config`

## Step Semantics

`env.step` means "advance one control increment".

- simulation: advance simulation time by configured steps
- real-time backend: wait/poll until next control cycle boundary
- hardware backend: drive one read-compute-write loop iteration

## Run-Loop Semantics

`env.run-loop` executes a backend-managed loop that can enforce:

- deadlines per tick
- fallback to last safe action when planning/control misses deadlines
- structured tick records for logs/telemetry
- multi-episode execution via `episode_max`/`step_max` when backend reset is supported

If `episode_max > 1` and backend reset is unsupported, `env.run-loop` returns `:unsupported`.

## Backend Validation Expectations

Backends should:

- reject malformed config values cleanly
- document whether unknown config keys are rejected or ignored
- validate action schema ids and payload shapes
- make reset support explicit rather than implicit
- keep transport-specific details out of the `env.api.v1` semantics

## Formal Reference

- [env.info](../language/reference/builtins/env/env-info.md)
- [env.attach](../language/reference/builtins/env/env-attach.md)
- [env.configure](../language/reference/builtins/env/env-configure.md)
- [env.observe](../language/reference/builtins/env/env-observe.md)
- [env.act](../language/reference/builtins/env/env-act.md)
- [env.step](../language/reference/builtins/env/env-step.md)
- [env.run-loop](../language/reference/builtins/env/env-run-loop.md)
- [env.reset](../language/reference/builtins/env/env-reset.md)
- [ROS2 backend scope](ros2-backend-scope.md)
