# `*`

**Signature:** `(* x...) -> int|float`

## What It Does

Multiplies numbers.

## Arguments And Return

- Arguments: zero or more numbers
- Return: int or float

## Errors And Edge Cases

- Type errors on non-number; integer overflow errors in int mode.

## Examples

### Minimal

```lisp
(* 2 3 4)
```

### Realistic

```lisp
(* 3 0.5)
```

## Notes

- Zero-arg identity is `1`.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
