# `map.set!`

**Signature:** `(map.set! m k v) -> v`

## What It Does

Inserts or overwrites key-value entry.

## Arguments And Return

- Arguments: map, key, value
- Return: value assigned

## Errors And Edge Cases

- Key type restrictions and type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define m (map.make)) (map.set! m "x" 1))
```

### Realistic

```lisp
(begin (define m (map.make)) (map.set! m "x" 1) (map.set! m "x" 2) (map.get m "x" 0))
```

## Notes

- Map mutation primitive.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
