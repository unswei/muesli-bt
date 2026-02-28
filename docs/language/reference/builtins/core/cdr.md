# `cdr`

**Signature:** `(cdr pair) -> any`

## What It Does

Returns second slot of a cons.

## Arguments And Return

- Arguments: one cons
- Return: second slot value

## Errors And Edge Cases

- Errors if argument is not a cons.

## Examples

### Minimal

```lisp
(cdr (cons 1 2))
```

### Realistic

```lisp
(cdr (list 1 2 3))
```

## Notes

- List tail access.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
