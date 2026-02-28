# `car`

**Signature:** `(car pair) -> any`

## What It Does

Returns first slot of a cons.

## Arguments And Return

- Arguments: one cons
- Return: first slot value

## Errors And Edge Cases

- Errors if argument is not a cons.

## Examples

### Minimal

```lisp
(car (cons 1 2))
```

### Realistic

```lisp
(car (list "head" "tail"))
```

## Notes

- List head access.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
