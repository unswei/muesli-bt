# Language Semantics

This page describes how expressions are evaluated in the current implementation.

Use the [Language Reference Index](reference/index.md) when you need exact signatures, argument validation, or edge-case behavior for individual forms and built-ins.

## Evaluation Model

### Self-Evaluating Values

These evaluate to themselves:

- `nil`
- booleans
- integers
- floats
- strings
- primitive function values
- closures
- BT handles (`bt_def`, `bt_instance`)

### Symbols

Symbols are resolved by lexical environment lookup.

If a symbol is unbound, evaluation raises `name_error`.

### Lists

A list is either:

- a special form
- a function application

## Special Forms

### `quote`

```lisp
(quote x)
'x
```

Returns raw data without evaluating it.

### `if`

```lisp
(if condition then-expr else-expr)
```

- evaluates condition first
- if truthy, evaluates `then-expr`
- otherwise evaluates `else-expr` (or returns `nil` if omitted)

### `define`

Variable form:

```lisp
(define x 10)
```

Function sugar form:

```lisp
(define (inc x) (+ x 1))
```

### `lambda`

```lisp
(lambda (x y) (+ x y))
```

Creates a closure that captures its lexical environment.

### `begin`

```lisp
(begin expr1 expr2 expr3)
```

Evaluates expressions in order and returns the final result.

### `let`

```lisp
(let ((x 1) (y 2))
  (+ x y))
```

Semantics:

- creates a child lexical environment
- evaluates all initialisers in the parent environment
- binds names in the child
- evaluates body forms in the child

### `cond`

```lisp
(cond
  ((< x 0) 'neg)
  ((= x 0) 'zero)
  (else 'pos))
```

Semantics:

- evaluates clauses top-to-bottom
- for first truthy test, evaluates that clause body and returns final body value
- `else` is allowed only as final clause
- no match returns `nil`

### `quasiquote`

```lisp
`(a ,x ,@xs)
```

Semantics:

- `(quasiquote X)` walks `X` as data
- `(unquote Y)` evaluates `Y` at depth 1
- `(unquote-splicing Y)` evaluates `Y` and splices list items at depth 1 in list contexts
- nested quasiquotes increase depth
- misuse raises explicit runtime errors

### `bt`

```lisp
(bt (seq (cond always-true) (act always-success)))
```

Compiles one raw BT DSL form at evaluation time. Equivalent to `bt.compile` over quoted DSL.

### `defbt`

```lisp
(defbt patrol (sel ...))
```

Defines a symbol to a compiled `bt_def`. Equivalent to `(define patrol (bt ...))`.

### `load`

```lisp
(load "path/to/script.lisp")
```

Semantics:

- reads file source
- parses and evaluates forms sequentially in current environment
- returns final value (or `nil` for empty file)
- errors include filename context

## Environments And Closures

- lexical scoping with parent environment chain
- each closure captures its defining environment
- function calls create a child environment for parameters

## Numeric Model

### Types

- `int`: signed 64-bit (`std::int64_t`)
- `float`: `double`

### Promotion Rules

- `int op int -> int` (except division)
- mixed int/float operations promote to float
- `/` always returns float

### Arithmetic Behaviour

- `(+ ) -> 0`
- `(* ) -> 1`
- `(+ x) -> x`
- `(* x) -> x`
- `(- x) -> unary negation`
- `(/ x) -> reciprocal`

### Comparisons

Implemented:

- `=`
- `<`, `>`, `<=`, `>=`

Mixed numeric comparisons are supported and promote as needed.

### Integer Overflow Policy

Integer arithmetic is checked.

Overflow raises runtime errors such as:

- `+: integer overflow`
- `-: integer overflow`
- `*: integer overflow`

## Printing Semantics

- `print` is display-oriented and returns `nil`.
- `write` / `write-to-string` produce reader-friendly output and reject non-readable values.

## Error Types In Practice

- parse issues: `parse_error`
- eval core failures: `eval_error`
- bad callable/type situations: `type_error`
- unbound symbols: `name_error`

Most built-in argument/arity errors surface as `lisp_error` with explicit messages.

## See Also

- [Language Syntax](syntax.md)
- [Built-ins Overview](builtins.md)
- [Language Reference Index](reference/index.md)
