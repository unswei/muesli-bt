# `or`

Short-circuiting logical disjunction special form.

## Signature

```lisp
(or expr1 expr2 ...)
```

## Behaviour

- Arguments are evaluated left-to-right.
- Evaluation stops at the first truthy value and returns it.
- If no truthy value is found, returns `nil`.
- With zero arguments, returns `nil`.

## Examples

```lisp
(or)                      ; => nil
(or #f nil 42)            ; => 42
(or 1 (side-effect))      ; => 1, second form not evaluated
(or #f nil)               ; => nil
```
