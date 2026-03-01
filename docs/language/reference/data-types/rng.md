# `rng`

**Signature:** `RNG handle`

## What It Does

Deterministic random stream state handle.

## Arguments And Return

- Return: `rng` handle

## Errors And Edge Cases

- Create with `rng.make` before use.

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

- State is [host](../../../terminology.md#host)-managed, handle is GC-traced.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
