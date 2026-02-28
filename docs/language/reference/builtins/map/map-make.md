# `map.make`

**Signature:** `(map.make) -> map`

## What It Does

Creates empty mutable map.

## Arguments And Return

- Arguments: none
- Return: map handle

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(map.make)
```

### Realistic

```lisp
(begin (define m (map.make)) (map.set! m "k" 1))
```

## Notes

- Use `map.*` APIs for access.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
