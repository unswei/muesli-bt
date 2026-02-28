# `vec.make`

**Signature:** `(vec.make [capacity]) -> vec`

## What It Does

Creates mutable vector handle.

## Arguments And Return

- Arguments: optional non-negative integer capacity
- Return: vec handle

## Errors And Edge Cases

- Capacity must be non-negative integer.

## Examples

### Minimal

```lisp
(vec.make)
```

### Realistic

```lisp
(vec.make 8)
```

## Notes

- Default capacity is small.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
