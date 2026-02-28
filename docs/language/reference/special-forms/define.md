# `define`

**Signature:** `(define name expr) -> any`

## What It Does

Binds a value or function in the current lexical environment.

## Arguments And Return

- Arguments: `(define name expr)` or `(define (f args...) body...)`
- Return: bound value

## Errors And Edge Cases

- Invalid function signatures raise errors.

## Examples

### Minimal

```lisp
(define x 42)
```

### Realistic

```lisp
(begin (define (inc x) (+ x 1)) (inc 7))
```

## Notes

- Function sugar expands to `lambda`.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
