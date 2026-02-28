# `cons`

**Signature:** `(cons a b) -> cons`

## What It Does

Constructs a pair cell.

## Arguments And Return

- Arguments: two values
- Return: cons pair

## Errors And Edge Cases

- Arity/type errors for invalid input.

## Examples

### Minimal

```lisp
(cons 1 2)
```

### Realistic

```lisp
(begin (define p (cons "a" "b")) (car p))
```

## Notes

- Core pair primitive.

## See Also

- [Reference Index](../../index.md)
- [Language Semantics](../../../semantics.md)
