# Brief Lisp Introduction

This is a practical orientation for muesli-bt, not a full Lisp tutorial.
The goal is to give you only the language surface needed to read the first BT examples.

If you want a runnable pre-BT example first, start with [Example: muslisp Basics](examples/lisp-basics.md).

## Minimum Lisp For First BT Examples

For the first muesli-bt examples, focus on five things:

- lists are written in parentheses
- the first item in a list is usually the operator or form name
- symbols such as `always-true` and `target-visible` are names, not strings
- `define` binds a name to a value
- `defbt` and `bt` let you write BT DSL forms directly in Lisp syntax

You do not need quasiquote, macros, or the full language reference before reading [Example: Hello BT](examples/hello-bt.md).

## Expressions And Lists

Lisp code is written as lists:

```lisp
(+ 1 2 3)
(list 1 2 3)
(if #t 10 20)
```

The first item is the operator, followed by arguments.

## Symbols You Will See Immediately In Examples

- BT form names: `seq`, `sel`, `cond`, `act`
- callback names: `always-true`, `running-then-success`, `approach-target`
- user-defined names: `patrol`, `inst`, `goal-x`

These are all symbols.
In the BT examples, the callback names refer to host-registered leaves, while names such as `patrol` or `inst` are values you define in Lisp.

## Core Atoms

- symbols: `foo`, `target-visible`
- integers: `42`, `-7`
- floats: `3.14`, `1e-3`, `2.`
- booleans: `#t`, `#f`
- nil: `nil`
- strings: `"hello"`

## `define` And BT-Shaped Code

`define` gives a name to a value:

```lisp
(define threshold 3)
```

The first BT examples look like ordinary Lisp lists:

```lisp
(defbt hello-tree
  (seq
    (cond always-true)
    (act running-then-success 1)))
```

Read this as "define a tree named `hello-tree` whose root is a sequence".
Inside that sequence, `cond` and `act` are BT leaf forms, and `always-true` plus `running-then-success` are the callback names the host recognises.

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

You can skip quasiquote on a first read.
It becomes useful later when you generate or transform BT DSL programmatically.

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
You will mostly see `defbt` in the beginner examples because it keeps the tree definition readable.
You only need quoting when you pass raw DSL data to lower-level forms such as `bt.compile`.

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

Read next:

- [Example: Hello BT](examples/hello-bt.md)
- [Brief Behaviour Tree Introduction](bt/intro.md)
- [Language Syntax](language/syntax.md)
- [Language Semantics](language/semantics.md)
- [Language Reference Index](language/reference/index.md)
