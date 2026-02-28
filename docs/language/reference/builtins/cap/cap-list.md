# `cap.list`

**Signature:** `(cap.list) -> list`

## What It Does

Lists registered host capabilities available to Lisp.

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
- [cap.describe](cap-describe.md)
- [VLA Integration In BTs](../../../../bt/vla-integration.md)
