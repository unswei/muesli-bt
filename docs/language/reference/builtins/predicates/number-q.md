# `number?`

**Signature:** `(number? x) -> bool`

## What It Does

Checks whether value is numeric (int or float).

## Arguments And Return

- Arguments: one value
- Return: boolean

## Errors And Edge Cases

- Arity validation errors.

## Examples

### Minimal

```lisp
(number? 3.0)
```

### Realistic

```lisp
(number? "3")
```

## Notes

- Pairs with `integer?` and `float?`.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
