# `vec.get`

**Signature:** `(vec.get v i) -> any`

## What It Does

Reads vector element by index.

## Arguments And Return

- Arguments: vec, integer index
- Return: element value

## Errors And Edge Cases

- Index bounds and type are validated.

## Examples

### Minimal

```lisp
(begin (define v (vec.make)) (vec.push! v 7) (vec.get v 0))
```

### Realistic

```lisp
(begin (define v (vec.make)) (vec.push! v "a") (vec.push! v "b") (vec.get v 1))
```

## Notes

- Bounds errors are explicit runtime errors.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
