# `heap-stats`

**Signature:** `(heap-stats) -> nil`

## What It Does

Prints heap allocation counters.

## Arguments And Return

- Arguments: none
- Return: nil

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(heap-stats)
```

### Realistic

```lisp
(begin (list 1 2 3) (heap-stats))
```

## Notes

- Output goes to stdout.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
