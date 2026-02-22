# Built-ins Reference

This is the project built-in surface, grouped by purpose.

## Core List And Predicate Built-ins

### `cons`

- purpose: construct a pair/list cell
- signature: `(cons a d)`
- returns: cons cell
- example:

```lisp
(cons 1 (list 2 3)) ; (1 2 3)
```

- common errors: arity mismatch

### `car`

- purpose: first element of cons
- signature: `(car pair)`
- returns: first value
- common errors: non-cons input

### `cdr`

- purpose: rest of cons
- signature: `(cdr pair)`
- returns: tail value
- common errors: non-cons input

### `list`

- purpose: build proper list from arguments
- signature: `(list a b c ...)`
- returns: proper list

### `null?`

- purpose: nil predicate
- signature: `(null? x)`
- returns: `#t` if `x` is `nil`, else `#f`

### `eq?`

- purpose: equality check for core value semantics
- signature: `(eq? a b)`
- returns: boolean

## Arithmetic And Comparisons

### Arithmetic

- `+`, `-`, `*`, `/`

Examples:

```lisp
(+ 1 2 3)   ; 6
(- 10 3 2)  ; 5
(* 2 3 4)   ; 24
(/ 5 2)     ; 2.5
```

### Comparisons

- `=`, `<`, `>`, `<=`, `>=`

Examples:

```lisp
(= 3 3.0)   ; #t
(< 3 3.5)   ; #t
(>= 4 4.0)  ; #t
```

Common errors:

- non-numeric arguments
- arity below minimum for comparisons

## Numeric Predicates

### `number?`

- signature: `(number? x)`
- returns: true for int/float

### `int?` and `integer?`

- signature: `(int? x)` or `(integer? x)`
- returns: true for int only

### `float?`

- signature: `(float? x)`
- returns: true for float only

### `zero?`

- signature: `(zero? x)`
- returns: true if numeric zero

## Printing And Heap Helpers

### `print`

- signature: `(print a b c ...)`
- returns: `nil`
- side effect: writes printed values to stdout

### `heap-stats`

- signature: `(heap-stats)`
- returns: `nil`
- side effect: prints current heap counters

### `gc-stats`

- signature: `(gc-stats)`
- returns: `nil`
- side effect: triggers GC, then prints counters

## BT Built-ins

## Core

### `bt.compile`

- purpose: compile quoted BT DSL
- signature: `(bt.compile '<bt-form>)`
- returns: `bt_def`

### `bt.new-instance`

- purpose: create mutable runtime instance from definition
- signature: `(bt.new-instance bt-def)`
- returns: `bt_instance`

### `bt.tick`

- purpose: tick one instance once
- signatures:
  - `(bt.tick bt-inst)`
  - `(bt.tick bt-inst '((key value) ...))`
- returns: symbol `success` / `failure` / `running`

### `bt.reset`

- purpose: clear per-node memory and blackboard for instance
- signature: `(bt.reset bt-inst)`
- returns: `nil`

### `bt.status->symbol`

- purpose: validate a status symbol
- signature: `(bt.status->symbol 'success)`
- returns: same symbol

## Inspectability and Config

- `bt.stats`
- `bt.trace.dump`
- `bt.trace.snapshot`
- `bt.blackboard.dump`
- `bt.logs.dump`
- `bt.logs.snapshot`
- `bt.log.dump` (alias)
- `bt.log.snapshot` (alias)
- `bt.scheduler.stats`
- `bt.set-tick-budget-ms`
- `bt.set-trace-enabled`
- `bt.set-read-trace-enabled`
- `bt.clear-trace`
- `bt.clear-logs`

Example:

```lisp
(bt.set-trace-enabled inst #t)
(bt.tick inst)
(bt.trace.snapshot inst)
```

## Arity And Type Error Pattern

Built-ins report explicit messages such as:

- `name: expected N arguments, got M`
- `name: expected number`
- `name: expected bt_instance`

These are designed to be readable in REPL and test output.
