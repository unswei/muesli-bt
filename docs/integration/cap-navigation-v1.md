# cap.navigation.v1

!!! note "status"
    Status: experimental mock adapter plus optional ROS2/Nav2 action-client adapter.
    This page defines the public navigation capability boundary. The built-in mock adapter is deterministic. The optional Nav2 adapter is fake-action-server tested. The wheeled flagship real-stack capture path is checked in separately and remains pending until a Nav2 machine captures it.

## what this is

`cap.navigation.v1` is the generic host capability contract for navigation tasks that are broader than direct `env.act` velocity commands.

The first real adapter boundary is Nav2 `NavigateToPose`. Nav2 is adapter metadata, not BT syntax.

## when to use it

Use `cap.navigation.v1` when BT logic needs to request a navigation goal, path, progress update, or cancellation through a stable map contract.

Do not use it for direct wheel commands, low-level obstacle avoidance, or raw Nav2 action messages.

## how it works

BT logic calls `(cap.call request-map)`.

`cap.call` dispatches registered host capability backends before falling back to model-service capabilities, built-in mock adapters, and `cap.echo.v1`.

The built-in mock adapter returns deterministic results and emits canonical `cap_call_start` and `cap_call_end` events.

When `MUESLI_BT_BUILD_INTEGRATION_ROS2=ON`, the ROS2 extension registers a `cap.navigation.v1` backend with `adapter_id="nav2"`. The first Nav2 adapter implements a real ROS2 action client for `nav2_msgs/action/NavigateToPose` and is tested against an in-process fake action server.

The checked-in [Nav2 capability fake-server evidence](../evidence/nav2-capability-fake-server.md) runs the adapter through Lisp `(cap.call request-map)` and validates the representative capability-call event logs.

The checked-in [wheeled flagship navigation-capability evidence](../evidence/wheeled-flagship-nav-capability.md) shows an experimental opt-in flagship variant delegating the goal-seeking lane to this capability contract while existing flagship wrappers keep using the canonical shared tree.

The [wheeled flagship Nav2 real-stack evidence](../evidence/wheeled-flagship-nav2-real-stack.md) page documents the capture path for the same variant against a live Nav2 stack. The checked-in marker does not claim captured real-stack evidence until that command has been run on a Nav2 machine.

## api / syntax

Required request fields:

- `schema_version`: `"cap.navigation.request.v1"`
- `capability`: `"cap.navigation.v1"`
- `operation`: `navigate-to-pose`, `navigate-through-poses`, `get-path`, `cancel`, or `status`

Common optional fields:

- `request_id`
- `target` or `poses`
- `timeout_ms` or `deadline_ms`
- `job_id` for `cancel` and `status`
- `action_name` for adapter tests or non-default Nav2 action names; the Nav2 adapter defaults to `/navigate_to_pose`

Stable result fields:

- `schema_version`: `"cap.navigation.result.v1"`
- `capability`: `"cap.navigation.v1"`
- `operation`
- `status`
- `adapter`
- `adapter_schema`
- `host_reached`
- `request_hash`
- `response_hash`
- `validation_status`
- optional `job_id`, `progress`, and `path`

Mock validation currently checks:

- `navigate-to-pose` and `get-path` require `target`
- `target` must be a map with `frame`, numeric `x`, and numeric `y`
- `target.frame` must be `map` or `odom`
- `navigate-through-poses` requires a non-empty `poses` list
- `cancel` and `status` require `job_id`
- `timeout_ms` and `deadline_ms`, when present, must be non-negative integers
- an explicit `adapter` must be `mock-nav2`

The optional Nav2 adapter currently implements:

- `navigate-to-pose`
- `status`
- `cancel`

The optional Nav2 adapter intentionally rejects unsupported operations such as `navigate-through-poses` and `get-path` until those ROS action or service boundaries are added deliberately.

The optional Nav2 adapter maps feedback into `progress` fields:

- `current_pose`
- `navigation_time_ms`
- `estimated_time_remaining_ms`
- `number_of_recoveries`
- `distance_remaining_m`

## example

```lisp
(begin
  (define target (map.make))
  (map.set! target 'frame "map")
  (map.set! target 'x 1.0)
  (map.set! target 'y 2.0)

  (define req (map.make))
  (map.set! req 'schema_version "cap.navigation.request.v1")
  (map.set! req 'capability "cap.navigation.v1")
  (map.set! req 'operation "navigate-to-pose")
  (map.set! req 'request_id "nav-1")
  (map.set! req 'target target)
  (map.set! req 'timeout_ms 1000)
  (cap.call req))
```

## gotchas

The built-in adapter is a mock. It proves the user-facing contract, event shape, and replay hashes. It does not prove Nav2, a physical robot, or a live ROS action server.

The optional Nav2 adapter proves the ROS2 action-client boundary against a fake action server. Real-stack evidence uses the separate wheeled flagship capture path and still does not imply physical robot evidence.

Core-only builds keep using the deterministic mock adapter and do not require ROS2, Nav2, or `nav2_msgs`.

## see also

- [host capability bundles](host-capability-bundles.md)
- [Nav2 capability fake-server evidence](../evidence/nav2-capability-fake-server.md)
- [wheeled flagship navigation-capability evidence](../evidence/wheeled-flagship-nav-capability.md)
- [wheeled flagship Nav2 real-stack evidence](../evidence/wheeled-flagship-nav2-real-stack.md)
- [ROS2 backend scope](ros2-backend-scope.md)
- [cap.call](../language/reference/builtins/cap/cap-call.md)
- [cap.motion.v1](cap-motion-v1.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
