# `vla.logs.dump`

**Signature:** `(vla.logs.dump [max_count]) -> string`

## What It Does

Returns recent VLA records as JSON lines text.

## Arguments And Return

- Arguments: optional non-negative integer limit
- Return: string

## Errors And Edge Cases

- negative count raises runtime error

## Examples

### Minimal

```lisp
(vla.logs.dump)
```

### Realistic

```lisp
(vla.logs.dump 50)
```

## Notes

- Useful for replay/regression analysis.

## See Also

- [Reference Index](../../index.md)
- [VLA Logging Schema](../../../../observability/vla-logging.md)
