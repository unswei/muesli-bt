# cap.navigation.v1

!!! note "status"
    Status: experimental mock adapter.
    This page defines the public navigation capability boundary. The built-in mock adapter is deterministic. A real Nav2 adapter is still optional roadmap work.

## what this is

`cap.navigation.v1` is the generic host capability contract for navigation tasks that are broader than direct `env.act` velocity commands.

The first intended real adapter is Nav2. Nav2 is adapter metadata, not BT syntax.

## when to use it

Use `cap.navigation.v1` when BT logic needs to request a navigation goal, path, progress update, or cancellation through a stable map contract.

Do not use it for direct wheel commands, low-level obstacle avoidance, or raw Nav2 action messages.

## how it works

BT logic calls `(cap.call request-map)`.

The current built-in mock adapter returns deterministic results and emits canonical `cap_call_start` and `cap_call_end` events. A future Nav2 adapter should map the same request shape to ROS2 action clients, starting with `NavigateToPose`.

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
- optional `job_id`, `progress`, and `path`

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

The real Nav2 adapter must stay optional and default-off.

## see also

- [host capability bundles](host-capability-bundles.md)
- [ROS2 backend scope](ros2-backend-scope.md)
- [cap.call](../language/reference/builtins/cap/cap-call.md)
- [cap.motion.v1](cap-motion-v1.md)
- [roadmap to 1.0](../roadmap-to-1.0.md)
