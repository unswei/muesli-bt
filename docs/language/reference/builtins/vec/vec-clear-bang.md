# `vec.clear!`

**Signature:** `(vec.clear! v) -> nil`

## What It Does

Removes all elements.

## Arguments And Return

- Arguments: vec
- Return: nil

## Errors And Edge Cases

- Type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define v (vec.make)) (vec.clear! v))
```

### Realistic

```lisp
(begin (define v (vec.make)) (vec.push! v 1) (vec.clear! v) (vec.len v))
```

## Notes

- Keeps same vector handle.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
