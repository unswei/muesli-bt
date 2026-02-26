# Built-ins Reference

This is the current built-in surface, grouped by purpose.

## Core List And Predicate Built-ins

- `cons`, `car`, `cdr`, `list`
- `null?`, `eq?`

## Arithmetic And Comparisons

- arithmetic: `+`, `-`, `*`, `/`
- comparisons: `=`, `<`, `>`, `<=`, `>=`
- predicates: `number?`, `int?`, `integer?`, `float?`, `zero?`

## Printing, Serialisation, And Files

### `print`

- signature: `(print a b c ...)`
- purpose: human-facing output
- return: `nil`

### `write`

- signature: `(write x)`
- purpose: readable output suitable for parse-back
- return: `x`

### `write-to-string`

- signature: `(write-to-string x)`
- purpose: readable string representation
- return: string

### `save`

- signature: `(save "path" x)`
- purpose: save readable value to file for `load` consumption
- return: `#t` on success

### `load` (special form)

- signature: `(load "path")`
- purpose: parse and evaluate file forms sequentially
- return: final evaluated form (or `nil` for empty files)

### Heap Introspection

- `(heap-stats)`
- `(gc-stats)`

## BT Built-ins

## Authoring/Compilation

- `bt.compile`
- `bt` (special form)
- `defbt` (special form)

### `bt.compile`

- signature: `(bt.compile '<bt-form>)`
- return: `bt_def`

### `bt` (special form)

- signature: `(bt <bt-form>)`
- return: `bt_def`
- note: compiles one raw DSL form at evaluation time

### `defbt` (special form)

- signature: `(defbt name <bt-form>)`
- return: compiled `bt_def` bound to `name`

## Instances And Ticking

- `bt.new-instance`
- `bt.tick`
- `bt.reset`
- `bt.status->symbol`

## BT Persistence

- `bt.to-dsl`
- `bt.save-dsl`
- `bt.load-dsl`
- `bt.save`
- `bt.load`

### `bt.to-dsl`

- signature: `(bt.to-dsl bt-def)`
- return: canonical BT DSL form (data)

### `bt.save-dsl` / `bt.load-dsl`

- signatures:
  - `(bt.save-dsl bt-def "tree.lisp")`
  - `(bt.load-dsl "tree.lisp")`
- purpose: portable DSL persistence

### `bt.save` / `bt.load`

- signatures:
  - `(bt.save bt-def "tree.mbt")`
  - `(bt.load "tree.mbt")`
- purpose: versioned compiled serialisation (`MBT1`)

## BT Observability And Config

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

## Error Message Pattern

Built-ins report explicit messages such as:

- `name: expected N arguments, got M`
- `name: expected number`
- `name: expected bt_instance`
