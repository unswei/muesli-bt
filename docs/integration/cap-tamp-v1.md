# cap.tamp.v1

!!! note "status"
    Status: experimental mock adapter.
    This page defines the task-and-motion planning capability boundary. The first intended concrete backend is PDDLStream with PyBullet fixtures.

## what this is

`cap.tamp.v1` is the generic host capability contract for task-and-motion planning (TAMP).

It sits above `planner.plan`. A TAMP backend may produce symbolic steps, motion feasibility checks, or a guarded BT fragment proposal. `planner.plan` remains the bounded in-runtime action-selection API.

## when to use it

Use `cap.tamp.v1` when a task needs symbolic choices plus geometric or kinematic feasibility.

Do not use it for direct robot IO, raw PDDL files in BT source, or long blocking planning inside one tick.

## how it works

BT logic calls `(cap.call request-map)`.

The built-in mock adapter returns a deterministic plan whose steps target other generic capabilities. A future PDDLStream/PyBullet adapter should keep PDDL files, streams, PyBullet objects, and solver handles behind the adapter boundary.

The result may include an `agent_proposal.v1` fragment with `fragment_contract` set to `guarded-task-plan.v1`.

## api / syntax

Required request fields:

- `schema_version`: `"cap.tamp.request.v1"`
- `capability`: `"cap.tamp.v1"`
- `operation`: `solve`, `validate-plan`, `cancel`, or `status`

Common optional fields:

- `request_id`
- `planner`
- `problem`
- `context`
- `timeout_ms` or `deadline_ms`
- `job_id` for `cancel` and `status`

Stable result fields:

- `schema_version`: `"cap.tamp.result.v1"`
- `capability`: `"cap.tamp.v1"`
- `operation`
- `status`
- `adapter`
- `adapter_schema`
- `host_reached`
- `request_hash`
- `response_hash`
- `validation_status`
- optional `plan`, `proposal`, `job_id`, and `progress`

Mock validation currently checks:

- `planner`, when present, must be `pddlstream-pybullet` or `mock-pddlstream-pybullet`
- `context`, when present, must be a map
- `validate-plan` requires `plan`
- `plan` must be a list
- `cancel` and `status` require `job_id`
- `timeout_ms` and `deadline_ms`, when present, must be non-negative integers
- an explicit `adapter` must be `mock-pddlstream-pybullet`

## example

```lisp
(begin
  (define req (map.make))
  (map.set! req 'schema_version "cap.tamp.request.v1")
  (map.set! req 'capability "cap.tamp.v1")
  (map.set! req 'operation "solve")
  (map.set! req 'request_id "tamp-1")
  (map.set! req 'planner "pddlstream-pybullet")
  (cap.call req))
```

## gotchas

`cap.tamp.v1` does not make generated task logic safe by itself. A generated BT fragment must still pass proposal-envelope validation and a fragment contract such as `guarded-task-plan.v1`.

The first real backend should be optional. Core builds must not require PDDLStream, PyBullet, or ROS.

## see also

- [agent-proposed task logic](agent-proposed-task-logic.md)
- [host capability bundles](host-capability-bundles.md)
- [cap.motion.v1](cap-motion-v1.md)
- [cap.navigation.v1](cap-navigation-v1.md)
- [planner.plan request/result](../planning/planner-plan.md)
