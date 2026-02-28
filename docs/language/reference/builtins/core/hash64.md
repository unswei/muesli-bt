# `hash64`

**Signature:** `(hash64 text) -> int`

## What It Does

Returns a stable 64-bit hash (non-negative integer form) for a string or symbol.

## Arguments And Return

- Arguments: string or symbol
- Return: integer hash value

## Errors And Edge Cases

- type/arity validation errors when input is not string/symbol

## Examples

### Minimal

```lisp
(hash64 "planner-seed")
```

### Realistic

```lisp
(begin
  (define base (hash64 "run-a"))
  (list base (hash64 "run-a")))
```

## Notes

- Useful for deterministic seed derivation.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
