# `and`

Short-circuiting logical conjunction special form.

## Signature

```lisp
(and expr1 expr2 ...)
```

## Behaviour

- Arguments are evaluated left-to-right.
- Evaluation stops at the first falsey value (`#f` or `nil`) and returns it.
- If all values are truthy, returns the last value.
- With zero arguments, returns `#t`.

## Examples

```lisp
(and)                      ; => #t
(and #t 7)                 ; => 7
(and #f (side-effect))     ; => #f, second form not evaluated
(and 1 2 3)                ; => 3
```
