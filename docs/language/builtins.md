# Built-ins Reference

This page lists the current built-in surface in muesli-bt v1.1.

## Core list and predicates

- `cons`, `car`, `cdr`, `list`
- `null?`, `eq?`

## Arithmetic and comparisons

- arithmetic: `+`, `-`, `*`, `/`
- comparisons: `=`, `<`, `>`, `<=`, `>=`
- predicates: `number?`, `int?`, `integer?`, `float?`, `zero?`

### Maths helpers

- `(sqrt x) -> float`
- `(log x) -> float`
- `(exp x) -> float`
- `(abs x) -> int|float`
- `(clamp x lo hi) -> int|float`

Errors:

- `sqrt`: `x` must be `>= 0`
- `log`: `x` must be `> 0`
- `abs`: integer overflow on `-9223372036854775808`
- `clamp`: `lo` must be `<= hi`

## Time

- `(time.now-ms) -> int`

Returns monotonic milliseconds from `steady_clock` (not wall clock time).

## RNG built-ins

- `(rng.make seed-int) -> rng`
- `(rng.uniform rng lo hi) -> float`
- `(rng.normal rng mu sigma) -> float`
- `(rng.int rng n) -> int` in `[0, n-1]`

Validation:

- `rng.uniform`: `lo <= hi`
- `rng.normal`: `sigma >= 0`
- `rng.int`: `n > 0`

Implementation note: RNG uses a deterministic SplitMix64-based stream.

## Mutable vector built-ins

- `(vec.make [capacity]) -> vec` (default capacity is `4`)
- `(vec.len v) -> int`
- `(vec.get v i) -> any`
- `(vec.set! v i x) -> x`
- `(vec.push! v x) -> int` (returns new index)
- `(vec.pop! v) -> any`
- `(vec.clear! v) -> nil`
- `(vec.reserve! v cap) -> nil`

Validation:

- indices must satisfy `0 <= i < (vec.len v)`
- capacities must be non-negative integers
- `vec.pop!` on empty vector raises an error

## Mutable map built-ins

- `(map.make) -> map`
- `(map.get m k default) -> any`
- `(map.has? m k) -> bool`
- `(map.set! m k v) -> v`
- `(map.del! m k) -> bool`
- `(map.keys m) -> list`

Key policy in v1.1:

- allowed key types: symbol, string, integer, float
- float keys must not be `NaN`
- keys are type-sensitive (`1` and `1.0` are different keys)

## Printing, serialisation, and files

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

### Heap introspection

- `(heap-stats)`
- `(gc-stats)`

## BT built-ins

### Authoring/compilation

- `bt.compile`
- `bt` (special form)
- `defbt` (special form)

### Instances and ticking

- `bt.new-instance`
- `bt.tick`
- `bt.reset`
- `bt.status->symbol`

### Persistence

- `bt.to-dsl`
- `bt.save-dsl`
- `bt.load-dsl`
- `bt.save`
- `bt.load`

### Observability/config

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

## Error message pattern

Built-ins use explicit messages, for example:

- `name: expected N arguments, got M`
- `name: expected number`
- `name: expected vec`
- `name: expected map`
- `name: expected rng`
