# `map.has?`

**Signature:** `(map.has? m k) -> bool`

## What It Does

Checks key presence.

## Arguments And Return

- Arguments: map, key
- Return: boolean

## Errors And Edge Cases

- Key type restrictions and type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define m (map.make)) (map.has? m "x"))
```

### Realistic

```lisp
(begin (define m (map.make)) (map.set! m "x" 1) (map.has? m "x"))
```

## Notes

- Presence-only check.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
