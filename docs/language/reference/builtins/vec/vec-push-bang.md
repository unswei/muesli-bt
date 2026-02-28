# `vec.push!`

**Signature:** `(vec.push! v x) -> int`

## What It Does

Appends value and returns inserted index.

## Arguments And Return

- Arguments: vec, value
- Return: 0-based index

## Errors And Edge Cases

- Type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define v (vec.make)) (vec.push! v 5))
```

### Realistic

```lisp
(begin (define v (vec.make 1)) (vec.push! v 10) (vec.push! v 20))
```

## Notes

- Automatically grows when needed.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
