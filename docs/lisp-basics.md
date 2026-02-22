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

## Prefix Notation

```lisp
(+ 2 3)   ; 5
(* 4 5)   ; 20
(> 7 2)   ; #t
```

## Core Atoms

- symbols: `foo`, `target-visible`
- integers: `42`, `-7`
- floats: `3.14`, `1e-3`, `2.`
- booleans: `#t`, `#f`
- nil: `nil`

## Quoting: Data vs Evaluation

Quote prevents evaluation and keeps a form as data:

```lisp
'(1 2 3)
'(seq (cond target-visible) (act grasp))
```

This matters for BTs: in v1, BT DSL (domain-specific language) forms are usually passed as quoted Lisp data to `bt.compile`.

```lisp
(bt.compile '(seq (cond battery-ok) (act approach-target)))
```

## Tiny End-To-End Example

```lisp
(begin
  (define x 5)
  (define y 7)
  (+ x y))
```

Next: [Language Syntax](language/syntax.md) and [Language Semantics](language/semantics.md).
