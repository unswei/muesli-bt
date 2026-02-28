# `map.keys`

**Signature:** `(map.keys m) -> list`

## What It Does

Returns list of keys in map.

## Arguments And Return

- Arguments: map
- Return: list of keys

## Errors And Edge Cases

- Type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define m (map.make)) (map.keys m))
```

### Realistic

```lisp
(begin (define m (map.make)) (map.set! m "a" 1) (map.set! m "b" 2) (map.keys m))
```

## Notes

- Key order is not guaranteed.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
