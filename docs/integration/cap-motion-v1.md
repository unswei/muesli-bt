# cap.motion.v1

## what this is

`cap.motion.v1` is the first concrete host capability bundle contract for host-owned motion work.

It covers motion requests that are too high-level for direct `env.act` commands and too host-owned for `planner.plan`.
The main initial use case is manipulation, such as moving an arm to a pose or named joint state.

The contract is generic.
MoveIt is the intended first real adapter, but MoveIt is not the public semantic surface.

## when to use it

Use `cap.motion.v1` when BT logic needs to ask the host to plan, validate, or execute a motion goal through a stable contract.

Good uses:

- move an end effector to a target pose
- move joints to a named or explicit target
- check whether a target is reachable
- request cancellation of a host-owned motion job
- inspect progress or final status for a motion job

Do not use `cap.motion.v1` for:

- ordinary velocity or steering commands; use `env.act`
- per-tick observation/state updates; use `env.observe`
- bounded action selection inside the muesli-bt planner contract; use `planner.plan`
- raw MoveIt action messages or simulator-specific RPCs

## how it works

A motion adapter owns the platform-specific implementation.
The adapter may call MoveIt, a simulator motion service, a robot SDK, or a mock test service.

BT logic sends a request map with:

- a schema version
- a generic capability id
- an operation
- a target
- constraints
- bounded execution policy

The adapter returns a result map with:

- a schema version
- the same capability id
- the operation
- a stable status
- timing and optional progress metadata
- adapter metadata that BT logic can ignore

Every operation must be bounded by `timeout_ms`, `budget_ms`, or a host-documented default.
The host must document what safe fallback means for failed, rejected, or timed-out motion.

## api / syntax

This page specifies the contract shape for `cap.motion.v1`.
It does not add a released Lisp built-in by itself.

Future APIs should pass these maps through the host capability bundle mechanism.
Until that mechanism exists, examples on this page are contract examples rather than executable snippets.

### common request fields

Required:

- `schema_version`: `"cap.motion.request.v1"`
- `capability`: `"cap.motion.v1"`
- `operation`: one of the operations listed below
- `request_id`: stable string chosen by the caller or host wrapper

Common optional:

- `timeout_ms`: wall-clock timeout for host-owned work
- `budget_ms`: planning or validation budget when the operation has a bounded planning phase
- `frame`: default frame for pose targets
- `constraints`: map of generic constraints
- `safety`: map of host fallback or collision-policy hints
- `metadata`: map for caller-owned annotations

### common result fields

Required:

- `schema_version`: `"cap.motion.result.v1"`
- `capability`: `"cap.motion.v1"`
- `operation`: operation handled by the adapter
- `request_id`: request id from the matching request
- `status`: stable status keyword

Common optional:

- `job_id`: host-owned id for asynchronous work
- `time_used_ms`: elapsed host time
- `adapter`: concrete adapter name, such as `"moveit"` or `"mock"`
- `adapter_schema`: adapter-specific metadata schema id
- `progress`: normalised progress map
- `result`: operation-specific result map
- `error`: short diagnostic string for human debugging

### status values

Stable status values:

- `:ok`: operation completed successfully
- `:accepted`: asynchronous operation was accepted and has a `job_id`
- `:running`: asynchronous operation is still in progress
- `:cancelled`: operation was cancelled before completion
- `:timeout`: operation exceeded its timeout or budget
- `:rejected`: adapter rejected the request before execution
- `:unreachable`: target is not reachable under the provided constraints
- `:collision`: adapter detected a collision or safety violation
- `:error`: adapter failed for an implementation-specific reason
- `:unavailable`: no suitable motion adapter is available

Adapters may include extra diagnostic fields, but BT logic should branch only on stable status values.

## operations

### `move-to-pose`

Move an end effector or tool frame to a target pose.

Required request fields:

- `target`: pose map

Common optional request fields:

- `group`: host-defined motion group
- `link`: end-effector or tool-frame name
- `constraints`
- `execute`: boolean, default decided by the host contract

Pose map fields:

- `frame`: frame id
- `position`: map with `x`, `y`, and `z`
- `orientation`: map with `qx`, `qy`, `qz`, and `qw`

### `move-to-joints`

Move a joint group to explicit joint values or a named state.

Required request fields:

- `target`: joint target map

Joint target forms:

- explicit values: `joints` map from joint name to numeric value
- named state: `named_state` string

### `validate-target`

Check whether a target appears feasible without executing the motion.

The result should use:

- `:ok` when the target is feasible
- `:unreachable` when kinematics or constraints fail
- `:collision` when collision checking fails
- `:timeout` when validation exceeds its budget

### `cancel`

Request cancellation for a host-owned motion job.

Required request fields:

- `job_id`: id returned by an earlier `:accepted` result

Cancellation is best-effort at the host boundary.
A successful cancellation request returns `:cancelled` or `:accepted`, depending on whether the host can confirm cancellation immediately.

### `status`

Poll a host-owned motion job.

Required request fields:

- `job_id`

The result should return `:running`, `:ok`, `:cancelled`, `:timeout`, `:error`, or another stable terminal status.

## example

Illustrative `move-to-pose` request:

```lisp
(map.make
  'schema_version "cap.motion.request.v1"
  'capability "cap.motion.v1"
  'operation "move-to-pose"
  'request_id "place-disc-001"
  'group "arm"
  'link "tool0"
  'target
    (map.make
      'frame "world"
      'position (map.make 'x 0.40 'y 0.10 'z 0.25)
      'orientation (map.make 'qx 0.0 'qy 0.0 'qz 0.0 'qw 1.0))
  'constraints
    (map.make
      'position_tolerance_m 0.01
      'orientation_tolerance_rad 0.05)
  'timeout_ms 500)
```

Illustrative result:

```lisp
(map.make
  'schema_version "cap.motion.result.v1"
  'capability "cap.motion.v1"
  'operation "move-to-pose"
  'request_id "place-disc-001"
  'status :ok
  'adapter "moveit"
  'adapter_schema "cap.motion.moveit.adapter.v1"
  'time_used_ms 184)
```

Illustrative asynchronous acceptance:

```lisp
(map.make
  'schema_version "cap.motion.result.v1"
  'capability "cap.motion.v1"
  'operation "move-to-pose"
  'request_id "place-disc-001"
  'status :accepted
  'job_id "motion-job-42"
  'adapter "moveit")
```

## gotchas

- `cap.motion.v1` does not guarantee that MoveIt is present.
- Adapter names are metadata, not BT semantics.
- Raw middleware messages must stay behind the adapter.
- Frames, units, and tolerances must be explicit in every adapter-facing example.
- Long-running execution needs `job_id`, `status`, and `cancel`; do not hide it inside a blocking call without a timeout.
- Failed motion must not imply a fallback action unless the host documents that fallback.

## see also

- [host capability bundles](host-capability-bundles.md)
- [environment api (`env.*`)](env-api.md)
- [planner.plan request/result](../planning/planner-plan.md)
- [ROS2 backend scope](ros2-backend-scope.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
