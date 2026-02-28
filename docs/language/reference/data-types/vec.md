# `vec`

**Signature:** `mutable vector handle`

## What It Does

Growable mutable vector object.

## Arguments And Return

- Return: `vec` handle

## Errors And Edge Cases

- Use `vec.*` built-ins for operations.

## Examples

### Minimal

```lisp
(vec.make)
```

### Realistic

```lisp
(begin (define v (vec.make)) (vec.push! v 1) (vec.get v 0))
```

## Notes

- GC scans vector elements.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
