# `null?`

**Signature:** `(null? x) -> bool`

## What It Does

Checks if value is `nil`.

## Arguments And Return

- Arguments: one value
- Return: boolean

## Errors And Edge Cases

- Arity error unless one argument.

## Examples

### Minimal

```lisp
(null? nil)
```

### Realistic

```lisp
(null? (cdr (list 1)))
```

## Notes

- Common list-end check.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
