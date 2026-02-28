# `closure`

**Signature:** `lambda function`

## What It Does

User closure with lexical capture.

## Arguments And Return

- Return: closure value

## Errors And Edge Cases

- Arity mismatch at call-time raises errors.

## Examples

### Minimal

```lisp
(lambda (x) (+ x 1))
```

### Realistic

```lisp
(begin (define f (lambda (x) (lambda (y) (+ x y)))) ((f 2) 4))
```

## Notes

- Not serializable via `write`/`save`.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
