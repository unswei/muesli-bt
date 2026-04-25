# `cap.call`

**Signature:** `(cap.call request-map) -> result-map`

## What It Does

Calls a registered [host](../../../../terminology.md#host) capability using a map-based request/result contract.

The initial implementation includes a deterministic mock capability named `cap.echo.v1`.
It is for contract tests and API smoke coverage.
It is not a motion, perception, ROS, MoveIt, or detector adapter.

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

## Errors And Edge Cases

- missing `capability` raises a runtime error
- unknown capability raises a runtime error
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
- Future motion and perception adapters should stay behind their capability contracts.

## See Also

- [Reference Index](../../index.md)
- [cap.list](cap-list.md)
- [cap.describe](cap-describe.md)
- [Host Capability Bundles](../../../../integration/host-capability-bundles.md)
