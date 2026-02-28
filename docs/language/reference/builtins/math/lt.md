# `<`

**Signature:** `(< a b ...) -> bool`

## What It Does

Strict increasing numeric comparison.

## Arguments And Return

- Arguments: two or more numbers
- Return: boolean

## Errors And Edge Cases

- Type errors on non-number; arity validation.

## Examples

### Minimal

```lisp
(< 1 2)
```

### Realistic

```lisp
(< 1 2 3)
```

## Notes

- Chain semantics across all adjacent pairs.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
