# `rng.uniform`

**Signature:** `(rng.uniform rng lo hi) -> float`

## What It Does

Samples uniform float in `[lo, hi]`.

## Arguments And Return

- Arguments: rng, lo, hi
- Return: float

## Errors And Edge Cases

- `lo` must be <= `hi`; type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define r (rng.make 1)) (rng.uniform r 0.0 1.0))
```

### Realistic

```lisp
(begin (define r (rng.make 5)) (list (rng.uniform r -1.0 1.0) (rng.uniform r -1.0 1.0)))
```

## Notes

- Returns bound directly when `lo == hi`.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
