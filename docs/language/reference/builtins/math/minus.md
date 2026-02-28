# `-`

**Signature:** `(- x [y...]) -> int|float`

## What It Does

Subtracts numbers, unary form negates.

## Arguments And Return

- Arguments: one or more numbers
- Return: int or float

## Errors And Edge Cases

- Type errors on non-number; integer overflow errors in int mode.

## Examples

### Minimal

```lisp
(- 9 2 3)
```

### Realistic

```lisp
(- 7)
```

## Notes

- Unary form negates.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
