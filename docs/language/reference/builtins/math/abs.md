# `abs`

**Signature:** `(abs x) -> int|float`

## What It Does

Computes absolute value.

## Arguments And Return

- Arguments: one number
- Return: int for int input, float for float input

## Errors And Edge Cases

- Integer overflow on minimum int64; type/arity validation errors.

## Examples

### Minimal

```lisp
(abs -7)
```

### Realistic

```lisp
(abs -2.5)
```

## Notes

- Preserves numeric category.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
