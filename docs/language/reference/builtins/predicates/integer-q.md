# `integer?`

**Signature:** `(integer? x) -> bool`

## What It Does

Checks whether value is integer.

## Arguments And Return

- Arguments: one value
- Return: boolean

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(integer? 3)
```

### Realistic

```lisp
(integer? 3.2)
```

## Notes

- Equivalent to `int?`.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
