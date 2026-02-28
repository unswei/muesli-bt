# `>=`

**Signature:** `(>= a b ...) -> bool`

## What It Does

Non-increasing numeric comparison.

## Arguments And Return

- Arguments: two or more numbers
- Return: boolean

## Errors And Edge Cases

- Type errors on non-number; arity validation.

## Examples

### Minimal

```lisp
(>= 2 2)
```

### Realistic

```lisp
(>= 5 5 3 1)
```

## Notes

- Chain semantics across all adjacent pairs.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
