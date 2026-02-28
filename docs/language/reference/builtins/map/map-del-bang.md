# `map.del!`

**Signature:** `(map.del! m k) -> bool`

## What It Does

Deletes key if present.

## Arguments And Return

- Arguments: map, key
- Return: `#t` when deleted, else `#f`

## Errors And Edge Cases

- Key type restrictions and type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define m (map.make)) (map.del! m "x"))
```

### Realistic

```lisp
(begin (define m (map.make)) (map.set! m "x" 1) (map.del! m "x"))
```

## Notes

- Safe on missing keys.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
