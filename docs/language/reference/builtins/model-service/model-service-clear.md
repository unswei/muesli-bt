# `model-service.clear`

**signature:** `(model-service.clear) -> nil`

## what this is

Clears the current optional `muesli-model-service` client from the runtime host.

## when to use it

Use it in tests, scripts, or host setup code when a run should stop using model-service capabilities.

## how it works

After clearing the client, model-backed `cap.call` requests return deterministic `:unavailable` results instead of contacting the service.

## api / syntax

```lisp
(model-service.clear)
```

## example

```lisp
(begin
  (model-service.clear)
  (model-service.info))
```

## gotchas

- This does not stop an external `muesli-model-service` process.
- It only changes the current `muesli-bt` runtime host configuration.

## see also

- [`model-service.configure`](model-service-configure.md)
- [`model-service.info`](model-service-info.md)
- [`cap.call`](../cap/cap-call.md)
