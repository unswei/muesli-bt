# `gc-stats`

**Signature:** `(gc-stats) -> nil`

## What It Does

Forces GC and prints allocation counters.

## Arguments And Return

- Arguments: none
- Return: nil

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(gc-stats)
```

### Realistic

```lisp
(begin (list 1 2 3) (gc-stats))
```

## Notes

- Useful for retention checks.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
