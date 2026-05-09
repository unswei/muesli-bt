# `model-service.info`

**signature:** `(model-service.info) -> map`

## what this is

Returns the current optional `muesli-model-service` bridge configuration.

## when to use it

Use it to confirm whether the runtime host has a model-service client configured before model-backed `cap.call` requests.

## how it works

The result reports whether a client is configured and echoes the current endpoint, timeout, required-mode, and replay-mode settings.

## api / syntax

```lisp
(model-service.info)
```

Result fields:

- `configured`
- `endpoint`
- `connect_timeout_ms`
- `request_timeout_ms`
- `required`
- `replay_mode`

## example

```lisp
(begin
  (define cfg (map.make))
  (map.set! cfg 'endpoint "ws://127.0.0.1:8765/v1/ws")
  (model-service.configure cfg)
  (model-service.info))
```

## gotchas

- `configured=true` means a client exists. It does not prove the service is reachable.
- Use [`model-service.check`](model-service-check.md) to prove descriptor compatibility.

## see also

- [`model-service.configure`](model-service-configure.md)
- [`model-service.check`](model-service-check.md)
- [`model-service.clear`](model-service-clear.md)
- [`cap.call`](../cap/cap-call.md)
