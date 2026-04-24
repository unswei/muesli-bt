# host capability bundles

## what this is

A host capability bundle is a named host-side service family exposed to muesli-bt through a small, versioned contract.

Use a host capability bundle when the host provides useful work that is more specific than raw environment transport, but should not become new BT or Lisp language semantics.

Examples include:

- motion execution for an arm
- route or navigation execution
- perception or scene-state estimation
- external task services owned by a simulator, robot process, or middleware stack

## when to use it

Use a host capability bundle when all of these are true:

- the work is owned by the host, simulator, middleware, or robot platform
- the BT should request the work through a stable schema rather than call driver objects directly
- at least one concrete adapter can sit behind the same generic contract
- adding the feature to `env.*` or `planner.plan` would blur an existing boundary

Do not use a host capability bundle for ordinary control-loop IO. Use `env.*` for observation, action, step, reset, and run-loop work.

Do not use a host capability bundle for in-runtime bounded decision planning. Use `planner.plan` for planner requests that run under the planner request/result contract.

## how it works

A bundle has four parts:

1. a stable bundle id
2. one request schema
3. one result schema
4. one or more host adapters behind that schema

The bundle id describes the generic capability, not the first library used to implement it.

Good bundle ids:

- [`cap.motion.v1`](cap-motion-v1.md)
- `cap.navigation.v1`
- [`cap.perception.scene.v1`](cap-perception-scene-v1.md)

Avoid bundle ids that expose implementation details as core semantics:

- `moveit.v1`
- `nav2.v1`
- `yolo.v1`

Adapters may still report their concrete implementation in metadata. For example, a `cap.motion.v1` adapter can report `adapter: "moveit"` while keeping BT logic written against the generic motion contract.

## api / syntax

This page defines the documentation contract for `v0.6.0`.
It does not define new Lisp built-ins yet.

Future bundle APIs should follow the existing map-based style used by `env.*` and `planner.plan`.
Each public request and result must include:

- `schema_version`
- `capability`
- `operation`
- bounded work or timeout fields where the host can block
- stable status values
- adapter metadata that is optional for BT logic

Minimum status vocabulary:

- `:ok`
- `:timeout`
- `:rejected`
- `:error`
- `:unavailable`

All host-owned blocking work must have explicit budget or timeout semantics.
The host must document fallback behaviour for non-`ok` statuses.

## initial bundle families

### motion

[`cap.motion.v1`](cap-motion-v1.md) is for host-owned motion execution, especially manipulation.

The first expected adapter is MoveIt, but the public contract should stay generic.
BT logic should ask for a named motion operation, target, constraints, and budget.
The adapter decides how to map that request to MoveIt, another motion planner, or a simulator-native command service.

The motion bundle is not `planner.plan`.
`planner.plan` chooses bounded actions inside the muesli-bt planner contract.
The motion bundle asks the host to execute or validate a higher-level motion service.

### perception scene

[`cap.perception.scene.v1`](cap-perception-scene-v1.md) is for host-owned perception that produces normalised scene state.

The first likely adapter can be a YOLO-compatible detector followed by a scene-state normaliser, but the public contract should not expose raw detector classes as the core interface.
BT logic should consume stable scene facts, object poses, confidences, and timestamps.

The perception scene bundle is not `env.observe`.
`env.observe` returns the current observation map for the control loop.
The perception scene bundle may use sensors or middleware behind the host boundary to build higher-level scene state.

### navigation

The navigation bundle is for host-owned navigation services that are broader than direct `env.act` commands.

The first ROS-facing adapter might use Nav2, but the public contract should stay generic.
BT logic should request goals, constraints, progress, cancellation, and final status through the capability contract.

## example

For a BT-facing sketch, see [host capability BT example](host-capability-bt-example.md).

Illustrative request shape:

```lisp
(map.make
  'schema_version "cap.motion.request.v1"
  'capability "cap.motion.v1"
  'operation "move-to-pose"
  'target (map.make 'frame "world" 'x 0.40 'y 0.10 'z 0.25)
  'constraints (map.make 'position_tolerance_m 0.01)
  'timeout_ms 500)
```

Illustrative result shape:

```lisp
(map.make
  'schema_version "cap.motion.result.v1"
  'capability "cap.motion.v1"
  'operation "move-to-pose"
  'status :ok
  'adapter "moveit"
  'time_used_ms 184)
```

These shapes are examples for the spec direction.
They are not released runtime APIs until a concrete bundle contract lands.

## gotchas

- Do not add MoveIt, Nav2, detector, or simulator names to core BT semantics.
- Do not expand `env.*` for every host service.
- Do not treat `planner.plan` as a generic RPC mechanism.
- Do not expose raw middleware messages as the public bundle contract.
- Do not let one bundle become a catch-all service surface.

## see also

- [integration overview](overview.md)
- [cap.motion.v1](cap-motion-v1.md)
- [cap.perception.scene.v1](cap-perception-scene-v1.md)
- [host capability BT example](host-capability-bt-example.md)
- [environment api (`env.*`)](env-api.md)
- [planner.plan request/result](../planning/planner-plan.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
- [terminology](../terminology.md)
