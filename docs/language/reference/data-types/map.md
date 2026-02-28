# `map`

**Signature:** `mutable map handle`

## What It Does

Mutable hash map object.

## Arguments And Return

- Return: `map` handle

## Errors And Edge Cases

- Keys restricted to symbol/string/int/float(non-NaN).

## Examples

### Minimal

```lisp
(map.make)
```

### Realistic

```lisp
(begin (define m (map.make)) (map.set! m "k" 1) (map.get m "k" nil))
```

## Notes

- GC scans mapped values.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
