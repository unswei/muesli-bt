# Language Semantics

This page describes how expressions are evaluated in the current implementation.

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

## Special Forms Implemented In v1

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

## Environments And Closures

- lexical scoping with parent environment chain
- each closure captures its defining environment
- function calls create a child environment for parameters

Example:

```lisp
(begin
  (define make-adder (lambda (x) (lambda (y) (+ x y))))
  (define add5 (make-adder 5))
  (add5 7)) ; 12
```

## Numeric Model

## Types

- `int`: signed 64-bit (`std::int64_t`)
- `float`: `double`

## Promotion Rules

- `int op int -> int` (except division)
- mixed int/float operations promote to float
- `/` always returns float

Examples:

```lisp
(/ 6 3) ; 2.0
(/ 5 2) ; 2.5
(+ 1 2.5) ; 3.5
```

## Arithmetic Behaviour

- `(+ ) -> 0`
- `(* ) -> 1`
- `(+ x) -> x`
- `(* x) -> x`
- `(- x) -> unary negation`
- `(/ x) -> reciprocal`

## Comparisons

Implemented:

- `=`
- `<`, `>`, `<=`, `>=`

Mixed numeric comparisons are supported and promote as needed.

## Edge Cases

### Float edge values

IEEE values are allowed:

- `nan`
- `inf`
- `-inf`

### Integer overflow policy

Integer arithmetic is checked.

Overflow raises runtime errors such as:

- `+: integer overflow`
- `-: integer overflow`
- `*: integer overflow`

## Error Types In Practice

- parse issues: `parse_error`
- eval core failures: `eval_error`
- bad callable/type situations: `type_error`
- unbound symbols: `name_error`

Most built-in argument/arity errors surface as `lisp_error` with clear messages.
