# `cap.describe`

**Signature:** `(cap.describe name) -> map`

## What It Does

Returns schema and policy metadata for one capability.
The initial built-in fixture capability is `cap.echo.v1`.

## Arguments And Return

- Arguments: capability name (string or symbol)
- Return: map with `name`, `request_schema`, `response_schema`, `safety_class`, `cost_category`

## Errors And Edge Cases

- unknown capability raises runtime error

## Examples

### Minimal

```lisp
(cap.describe "cap.echo.v1")
```

### Realistic

```lisp
(begin
  (define d (cap.describe "vla.rt2"))
  (map.get d 'request_schema nil))
```

## Notes

- Schema entries include `name`, `type`, and `required`.

## See Also

- [Reference Index](../../index.md)
- [cap.call](cap-call.md)
- [cap.list](cap-list.md)
- [VLA Integration In BTs](../../../../bt/vla-integration.md)
