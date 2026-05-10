# `model-service.check`

**signature:** `(model-service.check) -> map`

## what this is

Checks whether the configured `muesli-model-service` endpoint is compatible with the first `MMSP v0.2` bridge profile.

## when to use it

Use it after `model-service.configure` and before running BT logic that depends on model-backed capabilities.

## how it works

The runtime sends a `describe` request and verifies:

- protocol version is `0.2`
- the response status is `success`
- the response contains `output.capabilities`
- required public capability ids are present
- each required descriptor has the expected `mode`
- each required descriptor declares request and response schemas
- each required descriptor declares cancellation and deadline support
- each required descriptor declares freshness and replay fields

The first required capability set is:

- `cap.model.world.rollout.v1`
- `cap.model.world.score_trajectory.v1`
- `cap.vla.action_chunk.v1`
- `cap.vla.propose_nav_goal.v1`

## api / syntax

```lisp
(model-service.check)
```

Result fields:

- `compatible`
- `request_id`
- `required_capabilities`
- `missing_capabilities`
- `invalid_capabilities`
- `descriptor_errors`
- `error_code`, when incompatible
- `error`, when incompatible

## example

```lisp
(begin
  (define cfg (map.make))
  (map.set! cfg 'endpoint "ws://127.0.0.1:8765/v1/ws")
  (model-service.configure cfg)
  (model-service.check))
```

## gotchas

- This is a compatibility gate, not a health monitor.
- It checks descriptor compatibility. It does not prove model quality, task fitness, or host-side action safety.
- An unconfigured service returns `compatible=false`.

## see also

- [`model-service.configure`](model-service-configure.md)
- [`model-service.info`](model-service-info.md)
- [`cap.call`](../cap/cap-call.md)
