# `rng.int`

**Signature:** `(rng.int rng n) -> int`

## What It Does

Samples integer in `[0, n-1]`.

## Arguments And Return

- Arguments: rng, integer n
- Return: integer

## Errors And Edge Cases

- `n` must be > 0; type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define r (rng.make 3)) (rng.int r 10))
```

### Realistic

```lisp
(begin (define r (rng.make 3)) (list (rng.int r 5) (rng.int r 5)))
```

## Notes

- Uses unbiased range sampling.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
