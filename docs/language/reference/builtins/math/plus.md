# `+`

**Signature:** `(+ x...) -> int|float`

## What It Does

Adds numbers.

## Arguments And Return

- Arguments: zero or more numbers
- Return: int or float

## Errors And Edge Cases

- Type errors on non-number; integer overflow errors in int mode.

## Examples

### Minimal

```lisp
(+ 1 2 3)
```

### Realistic

```lisp
(+ 1 2.5)
```

## Notes

- Zero-arg identity is `0`.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
