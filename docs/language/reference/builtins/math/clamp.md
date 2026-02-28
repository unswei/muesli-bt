# `clamp`

**Signature:** `(clamp x lo hi) -> int|float`

## What It Does

Clamps value to inclusive bounds.

## Arguments And Return

- Arguments: three numbers
- Return: int or float depending on inputs

## Errors And Edge Cases

- Error when `lo > hi`; type/arity validation errors.

## Examples

### Minimal

```lisp
(clamp 10 0 5)
```

### Realistic

```lisp
(clamp 0.2 0.3 0.9)
```

## Notes

- Useful for bounded control actions.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
