# `vec.len`

**Signature:** `(vec.len v) -> int`

## What It Does

Returns vector length.

## Arguments And Return

- Arguments: vec
- Return: integer length

## Errors And Edge Cases

- Type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define v (vec.make)) (vec.len v))
```

### Realistic

```lisp
(begin (define v (vec.make)) (vec.push! v 1) (vec.len v))
```

## Notes

- Length is element count, not reserved capacity.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
