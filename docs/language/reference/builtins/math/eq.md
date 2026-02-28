# `=`

**Signature:** `(= a b ...) -> bool`

## What It Does

Numeric equality chain comparison.

## Arguments And Return

- Arguments: two or more numbers
- Return: boolean

## Errors And Edge Cases

- Type errors on non-number; arity validation.

## Examples

### Minimal

```lisp
(= 3 3.0)
```

### Realistic

```lisp
(= 1 1 1)
```

## Notes

- Supports mixed int/float.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
