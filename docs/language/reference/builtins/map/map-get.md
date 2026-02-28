# `map.get`

**Signature:** `(map.get m k default) -> any`

## What It Does

Returns value for key or fallback default.

## Arguments And Return

- Arguments: map, key, default
- Return: mapped value or default

## Errors And Edge Cases

- Key type restrictions and type/arity validation errors.

## Examples

### Minimal

```lisp
(begin (define m (map.make)) (map.get m "x" nil))
```

### Realistic

```lisp
(begin (define m (map.make)) (map.set! m "x" 10) (map.get m "x" 0))
```

## Notes

- Keys: symbol/string/int/float (non-NaN float).

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
