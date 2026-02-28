# `rng.make`

**Signature:** `(rng.make seed-int) -> rng`

## What It Does

Creates a deterministic RNG handle.

## Arguments And Return

- Arguments: integer seed
- Return: rng handle

## Errors And Edge Cases

- Type/arity validation errors.

## Examples

### Minimal

```lisp
(rng.make 42)
```

### Realistic

```lisp
(begin (define r (rng.make 42)) (rng.int r 10))
```

## Notes

- Deterministic for a fixed seed.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
