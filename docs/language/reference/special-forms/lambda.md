# `lambda`

**Signature:** `(lambda (args...) body...) -> closure`

## What It Does

Creates a lexical closure.

## Arguments And Return

- Arguments: parameter list plus one or more body forms
- Return: closure

## Errors And Edge Cases

- Parameters must be symbols.

## Examples

### Minimal

```lisp
(lambda (x) (+ x 1))
```

### Realistic

```lisp
(begin (define add5 ((lambda (x) (lambda (y) (+ x y))) 5)) (add5 3))
```

## Notes

- Closures capture defining environment.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
