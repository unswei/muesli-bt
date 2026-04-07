# `atan2`

**Signature:** `(atan2 y x) -> float`

## What It Does

Computes the signed polar angle for the point `(x, y)`.

## Arguments And Return

- Arguments: two numbers, `y` then `x`
- Return: float in radians

## Errors And Edge Cases

- Both arguments must be numeric.
- Follows the host `atan2` behaviour for zero inputs.

## Examples

### Minimal

```lisp
(atan2 1 1)
```

### Realistic

```lisp
(atan2 (- goal-y pose-y) (- goal-x pose-x))
```

## Notes

- Useful for heading and bearing calculations in control wrappers.
- Returns radians in the conventional `[-pi, pi]` range.

## See Also

- [`abs`](abs.md)
- [`clamp`](clamp.md)
- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
