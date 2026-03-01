# `let`

**Signature:** `(let ((name expr) ...) body...) -> any`

## What It Does

Creates a child lexical scope with local bindings.

## Arguments And Return

- Arguments: bindings list and body forms
- Return: final body value

## Errors And Edge Cases

- Bindings must be proper `(name expr)` pairs.

## Examples

### Minimal

```lisp
(let ((x 1) (y 2)) (+ x y))
```

### Realistic

```lisp
(begin (define x 10) (let ((x 1) (y x)) (+ x y)))
```

## Notes

- Initialisers are evaluated in the parent scope.

## See Also

- [Reference Index](../index.md)
- [Language Semantics](../../semantics.md)
