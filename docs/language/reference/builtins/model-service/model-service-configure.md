# `model-service.configure`

**signature:** `(model-service.configure config-map) -> boolean`

## what this is

Configures the optional `muesli-model-service` bridge for the current runtime host.

## when to use it

Use this before calling model-backed capabilities through `cap.call`, such as `cap.model.world.rollout.v1`.

Do not use it for BTs that do not need model capabilities.
`muesli-bt` works without `muesli-model-service`.

## how it works

The configuration creates a WebSocket `MMSP v0.2` client when `MUESLI_BT_BUILD_MODEL_SERVICE_BRIDGE=ON`.
If the bridge is not built, this function raises an error.

The BT source still names capabilities, not backend placement.
Hosts should normally configure this before running a tree.

## api / syntax

Supported config fields:

- `endpoint`: WebSocket endpoint, for example `"ws://127.0.0.1:8765/v1/ws"`
- `connect_timeout_ms`: non-negative integer
- `request_timeout_ms`: non-negative integer
- `required`: boolean
- `replay_mode`: string; supported values are `"live"`, `"record"`, and `"replay"`
- `replay_cache_path`: directory used for request-hash keyed response cache files
- `fault_schedule`: comma-separated deterministic fault entries for non-replay calls
- `check`: boolean; when true, run `model-service.check` immediately and fail if incompatible

## example

```lisp
(begin
  (define cfg (map.make))
  (map.set! cfg 'endpoint "ws://127.0.0.1:8765/v1/ws")
  (map.set! cfg 'connect_timeout_ms 1000)
  (map.set! cfg 'request_timeout_ms 5000)
  (map.set! cfg 'replay_mode "record")
  (map.set! cfg 'replay_cache_path "runs/model-service-cache")
  (map.set! cfg 'fault_schedule "none")
  (map.set! cfg 'check true)
  (model-service.configure cfg))
```

## gotchas

- Only `ws://` endpoints are supported by the first C++ client.
- `frame://` image refs are resolved by the service, not by this builtin.
- Model outputs are proposals. `cap.call` returns `host_reached=false`.
- Configured but unavailable service calls return `:unavailable` results.
- `check=true` verifies descriptor compatibility, not task-specific model quality.
- `record` mode calls the live service and writes raw response envelopes to the replay cache.
- `replay` mode reads from the replay cache by request hash and does not need a reachable service when the cache entry exists.
- `fault_schedule` is for deterministic tests and evidence runs. Supported entries are `none`, `delay:<ms>`, `timeout`, `unavailable`, `invalid_output`, `unsafe_output`, `stale_result`, and `policy_violation`.
- Fault entries are consumed in order for non-replay calls. Replay mode reads the cache and does not consume the schedule.

## see also

- [`model-service.info`](model-service-info.md)
- [`model-service.check`](model-service-check.md)
- [`model-service.clear`](model-service-clear.md)
- [`cap.call`](../cap/cap-call.md)
- [muesli-model-service bridge](../../../../integration/model-service-bridge.md)
