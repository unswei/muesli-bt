# `save`

**Signature:** `(save "path" x) -> #t`

## What It Does

Saves readable value to file.

## Arguments And Return

- Arguments: path string and value
- Return: `#t` on success

## Errors And Edge Cases

- Path/readability/write failures raise errors.

## Examples

### Minimal

```lisp
(save "tmp.lisp" 42)
```

### Realistic

```lisp
(save "tree-state.lisp" (list "mode" 1 #t))
```

## Notes

- Designed for `load` round-trips.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
