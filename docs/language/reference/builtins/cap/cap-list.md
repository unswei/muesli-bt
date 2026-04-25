# `cap.list`

**Signature:** `(cap.list) -> list`

## What It Does

Lists registered [host](../../../../terminology.md#host) capabilities available to Lisp.
The initial built-in registry includes `cap.echo.v1` for deterministic API smoke coverage, alongside host-registered capabilities such as `vla.rt2`.

## Arguments And Return

- Arguments: none
- Return: list of capability names (strings)

## Errors And Edge Cases

- Returns empty list if no capabilities are registered.

## Examples

### Minimal

```lisp
(cap.list)
```

### Realistic

```lisp
(begin
  (define names (cap.list))
  (car names))
```

## Notes

- Capability names are host-defined and stable per runtime configuration.

## See Also

- [Reference Index](../../index.md)
- [cap.call](cap-call.md)
- [cap.describe](cap-describe.md)
- [VLA Integration In BTs](../../../../bt/vla-integration.md)
