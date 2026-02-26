# Brief Lisp Introduction

This is a practical orientation for muesli-bt, not a full Lisp tutorial.

## Expressions And Lists

Lisp code is written as lists:

```lisp
(+ 1 2 3)
(list 1 2 3)
(if #t 10 20)
```

The first item is the operator, followed by arguments.

## Core Atoms

- symbols: `foo`, `target-visible`
- integers: `42`, `-7`
- floats: `3.14`, `1e-3`, `2.`
- booleans: `#t`, `#f`
- nil: `nil`
- strings: `"hello"`

## Quote And Quasiquote

Quote keeps code as data:

```lisp
'(1 2 3)
'(seq (cond target-visible) (act grasp))
```

Quasiquote is template-friendly:

```lisp
`(seq (cond ,name) ,@children)
```

- `,x` unquotes inside a quasiquote
- `,@xs` unquote-splices a list inside list context

## Practical Convenience Forms

`let` introduces lexical bindings:

```lisp
(let ((x 1) (y 2))
  (+ x y))
```

`cond` handles branch chains:

```lisp
(cond
  ((< x 0) 'neg)
  ((= x 0) 'zero)
  (else 'pos))
```

## BT Authoring In Lisp

Use `bt` and `defbt` for human-authored trees:

```lisp
(defbt patrol
  (sel
    (seq
      (cond target-visible)
      (act approach-target)
      (act grasp))
    (act search-target)))
```

`bt.compile` still exists as the low-level primitive.

## `print` vs `write`

- `print` is for human output.
- `write` and `write-to-string` are for readable data output.

Example:

```lisp
(define payload '(foo "bar" 3))
(write payload)
(define s (write-to-string payload))
```

## Loading And Saving

```lisp
(save "state.lisp" '(x 1 y 2))
(load "state.lisp")
```

`load` reads and evaluates forms from file in order and returns the last value.

Next: [Language Syntax](language/syntax.md) and [Language Semantics](language/semantics.md).
