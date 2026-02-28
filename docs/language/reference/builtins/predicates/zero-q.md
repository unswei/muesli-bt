# `zero?`

**Signature:** `(zero? x) -> bool`

## What It Does

Checks numeric zero.

## Arguments And Return

- Arguments: one value
- Return: boolean

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(zero? 0)
```

### Realistic

```lisp
(zero? 0.0)
```

## Notes

- Non-numeric values return `#f`.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
