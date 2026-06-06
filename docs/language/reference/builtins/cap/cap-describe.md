# `cap.describe`

**Signature:** `(cap.describe name) -> map`

## What It Does

Returns schema and policy metadata for one capability.
The built-in fixture capabilities include `cap.echo.v1`, `cap.navigation.v1`, `cap.motion.v1`, and `cap.tamp.v1`.

## Arguments And Return

- Arguments: capability name (string or symbol)
- Return: map with `name`, `request_schema`, `response_schema`, `safety_class`, `cost_category`, and optional adapter metadata

## Errors And Edge Cases

- unknown capability raises runtime error
- adapter metadata can include `adapter_id`, `operations`, `frames`, `groups`, `default_timeout_ms`, `supports_cancellation`, and `supports_replay`

## Examples

### Minimal

```lisp
(cap.describe "cap.echo.v1")
```

### Realistic

```lisp
(begin
  (define d (cap.describe "cap.navigation.v1"))
  (map.get d 'operations nil))
```

## Notes

- Schema entries include `name`, `type`, and `required`.
- Mock planner capabilities expose enough metadata for agents to choose operations without reading C++ internals.

## See Also

- [Reference Index](../../index.md)
- [cap.call](cap-call.md)
- [cap.list](cap-list.md)
- [VLA Integration In BTs](../../../../bt/vla-integration.md)
