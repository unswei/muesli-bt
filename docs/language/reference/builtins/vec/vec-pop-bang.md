# `vec.pop!`

**Signature:** `(vec.pop! v) -> any`

## What It Does

Removes and returns last element.

## Arguments And Return

- Arguments: vec
- Return: removed value

## Errors And Edge Cases

- Errors on empty vector; type/arity validation.

## Examples

### Minimal

```lisp
(begin (define v (vec.make)) (vec.push! v 8) (vec.pop! v))
```

### Realistic

```lisp
(begin (define v (vec.make)) (vec.push! v "x") (vec.push! v "y") (vec.pop! v))
```

## Notes

- Mutates vector length.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
