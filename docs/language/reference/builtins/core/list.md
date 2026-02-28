# `list`

**Signature:** `(list a...) -> list`

## What It Does

Builds a proper list from arguments.

## Arguments And Return

- Arguments: zero or more values
- Return: proper list

## Errors And Edge Cases

- No special-case errors beyond inner evaluation errors.

## Examples

### Minimal

```lisp
(list 1 2 3)
```

### Realistic

```lisp
(begin (define xs (list "a" "b")) xs)
```

## Notes

- `(list)` returns `nil`.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
