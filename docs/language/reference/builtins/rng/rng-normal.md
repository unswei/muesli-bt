# `rng.normal`

**Signature:** `(rng.normal rng mu sigma) -> float`

## What It Does

Samples normal distribution.

## Arguments And Return

- Arguments: rng, mu, sigma
- Return: float

## Errors And Edge Cases

- `sigma` must be >= 0; type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define r (rng.make 1)) (rng.normal r 0.0 1.0))
```

### Realistic

```lisp
(begin (define r (rng.make 9)) (list (rng.normal r 0.0 0.2) (rng.normal r 0.0 0.2)))
```

## Notes

- `sigma == 0` returns `mu`.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
