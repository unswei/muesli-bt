# `cap.call`

**Signature:** `(cap.call request-map) -> result-map`

## What It Does

Calls a registered [host](../../../../terminology.md#host) capability using a map-based request/result contract.

The released baseline includes a deterministic mock capability named `cap.echo.v1`.
It is for contract tests and API smoke coverage.
It is not a motion, perception, ROS, MoveIt, or detector adapter.

When the optional `muesli-model-service` bridge is built and configured, `cap.call` can also invoke bounded model capabilities such as `cap.model.world.rollout.v1` and `cap.model.world.score_trajectory.v1`.
Those outputs remain proposals. Host-side validation still decides whether any downstream action can observe them.

## Arguments And Return

- Arguments: one request map
- Return: capability result map

Common request fields:

- `schema_version`
- `capability`
- `operation`
- `request_id` when the caller wants correlation

For `cap.echo.v1`, the request must use:

- `schema_version`: `"cap.echo.request.v1"`
- `capability`: `"cap.echo.v1"`
- `operation`: `"echo"`
- `payload`: optional value to echo

The echo result uses:

- `schema_version`: `"cap.echo.result.v1"`
- `capability`: `"cap.echo.v1"`
- `operation`: request operation
- `status`: `:ok` or `:rejected`
- `echo`: echoed payload, or the full request map when no payload is provided

For model-service capabilities, the request should use:

- `capability`: for example `"cap.model.world.rollout.v1"`
- `operation`: `"invoke"`; omitted operations default to `"invoke"`
- `request_id`: optional correlation id
- `deadline_ms`: optional request deadline
- `input`: capability-specific request body

The model-service result uses:

- `schema_version`: `"cap.model_service.result.v1"`
- `capability`: request capability
- `request_id`: echoed request id
- `status`: model-service status keyword such as `:success`, `:unavailable`, or `:invalid_output`
- `output`: decoded response output when present
- `metadata`: decoded response metadata when present
- `host_reached`: always false at this boundary; model outputs are not host execution
- `request_hash`: deterministic hash of the `MMSP v0.2` request envelope
- `response_hash`: deterministic hash of the raw response envelope
- `replay_cache_hit`: whether the result came from the local replay cache
- `validation_status`: `:accepted`, `:rejected`, or `:not_checked`
- `validation_reason_code`: reason code when validation rejects the output
- `validation_message`: human-readable validation message when available

## Errors And Edge Cases

- missing `capability` raises a runtime error
- unknown capability raises a runtime error
- configured but unreachable model service returns `:unavailable`, not a direct host action
- invalid, stale, unsafe, or policy-violating model-service outputs are rejected before host reach
- missing or unsupported `schema_version` raises a runtime error
- missing `operation` raises a runtime error
- unsupported `cap.echo.v1` operations return a result map with `status` set to `:rejected`

## Examples

### Minimal

```lisp
(begin
  (define req (map.make))
  (map.set! req 'schema_version "cap.echo.request.v1")
  (map.set! req 'capability "cap.echo.v1")
  (map.set! req 'operation "echo")
  (cap.call req))
```

### Model-service rollout

```lisp
(begin
  (define state (map.make))
  (map.set! state 'vector (list 0.0 1.0))

  (define action (map.make))
  (map.set! action 'type "joint_targets")
  (map.set! action 'values (list 0.1 0.2))
  (map.set! action 'dt_ms 20)

  (define input (map.make))
  (map.set! input 'state state)
  (map.set! input 'actions (list action))
  (map.set! input 'horizon 1)

  (define req (map.make))
  (map.set! req 'capability "cap.model.world.rollout.v1")
  (map.set! req 'operation "invoke")
  (map.set! req 'request_id "rollout-1")
  (map.set! req 'deadline_ms 500)
  (map.set! req 'input input)

  (cap.call req))
```

### Realistic

```lisp
(begin
  (define payload (map.make))
  (map.set! payload 'message "hello")

  (define req (map.make))
  (map.set! req 'schema_version "cap.echo.request.v1")
  (map.set! req 'capability "cap.echo.v1")
  (map.set! req 'operation "echo")
  (map.set! req 'request_id "echo-1")
  (map.set! req 'payload payload)

  (cap.call req))
```

## Notes

- `cap.call` is intentionally generic and map-based.
- The initial `cap.echo.v1` fixture exists to prove the registry and call path without adding real external service adapters.
- Bounded model-service calls such as `cap.model.world.rollout.v1` use the same capability-first shape once the optional bridge is configured.
- Future motion and perception adapters should stay behind their capability contracts.
- Model-service calls emit canonical `mbt.evt.v1` lifecycle events named `cap_call_start` and `cap_call_end`.
- `cap_call_end` includes request and response hashes when available.
- `cap_call_end` includes validation status and rejection reason codes when validation runs.
- The deterministic `cap.echo.v1` smoke path does not need those events until it stops being a pure registry/API fixture.

## See Also

- [Reference Index](../../index.md)
- [cap.list](cap-list.md)
- [cap.describe](cap-describe.md)
- [model-service.configure](../model-service/model-service-configure.md)
- [Host Capability Bundles](../../../../integration/host-capability-bundles.md)
- [muesli-model-service Bridge](../../../../integration/model-service-bridge.md)
