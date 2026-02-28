# `begin`

**Signature:** `(begin expr...) -> any`

## What It Does

Evaluates expressions in order and returns the final value.

## Arguments And Return

- Arguments: zero or more expressions
- Return: final expression value or `nil`

## Errors And Edge Cases

- No special-case errors besides inner expression failures.

## Examples

### Minimal

```lisp
(begin (define x 1) (+ x 2))
```

### Realistic

```lisp
(begin (define a 3) (define b 4) (* a b))
```

## Notes

- Useful in scripts and bodies.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
