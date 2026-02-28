# `vec.set!`

**Signature:** `(vec.set! v i x) -> x`

## What It Does

Overwrites element at index.

## Arguments And Return

- Arguments: vec, integer index, value
- Return: assigned value

## Errors And Edge Cases

- Index bounds and type are validated.

## Examples

### Minimal

```lisp
(begin (define v (vec.make)) (vec.push! v 1) (vec.set! v 0 9))
```

### Realistic

```lisp
(begin (define v (vec.make)) (vec.push! v "old") (vec.set! v 0 "new") (vec.get v 0))
```

## Notes

- Container mutation without general `set!` form.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
