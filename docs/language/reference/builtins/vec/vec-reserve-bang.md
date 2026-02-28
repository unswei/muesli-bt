# `vec.reserve!`

**Signature:** `(vec.reserve! v cap) -> nil`

## What It Does

Reserves vector capacity.

## Arguments And Return

- Arguments: vec, non-negative integer capacity
- Return: nil

## Errors And Edge Cases

- Capacity and type validation errors.

## Examples

### Minimal

```lisp
(begin (define v (vec.make)) (vec.reserve! v 32))
```

### Realistic

```lisp
(begin (define v (vec.make 1)) (vec.reserve! v 128) (vec.push! v 1))
```

## Notes

- Performance hint; does not change length.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
