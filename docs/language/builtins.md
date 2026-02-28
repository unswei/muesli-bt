# Built-ins Overview

This page is a summary and link hub for built-ins. Detailed behavior, errors, and runnable examples are documented in [Language Reference Index](reference/index.md).

## Core

- list/pair: `cons`, `car`, `cdr`, `list`
- predicates: `null?`, `eq?`

## Math And Predicates

- arithmetic: `+`, `-`, `*`, `/`
- comparisons: `=`, `<`, `>`, `<=`, `>=`
- helpers: `sqrt`, `log`, `exp`, `abs`, `clamp`
- predicates: `number?`, `int?`, `integer?`, `float?`, `zero?`

## Sampling And Time

- RNG: `rng.make`, `rng.uniform`, `rng.normal`, `rng.int`
- Clock: `time.now-ms`

## Mutable Containers

- vectors: `vec.make`, `vec.len`, `vec.get`, `vec.set!`, `vec.push!`, `vec.pop!`, `vec.clear!`, `vec.reserve!`
- maps: `map.make`, `map.get`, `map.has?`, `map.set!`, `map.del!`, `map.keys`

## IO And Runtime Introspection

- IO: `print`, `write`, `write-to-string`, `save`
- Heap/GC: `heap-stats`, `gc-stats`

## BT Integration

- authoring/compile: `bt.compile`
- runtime: `bt.new-instance`, `bt.tick`, `bt.reset`, `bt.status->symbol`
- persistence: `bt.to-dsl`, `bt.save-dsl`, `bt.load-dsl`, `bt.save`, `bt.load`
- observability/config: `bt.stats`, `bt.trace.dump`, `bt.trace.snapshot`, `bt.blackboard.dump`, `bt.logs.dump`, `bt.logs.snapshot`, `bt.log.dump`, `bt.log.snapshot`, `bt.scheduler.stats`, `bt.set-tick-budget-ms`, `bt.set-trace-enabled`, `bt.set-read-trace-enabled`, `bt.clear-trace`, `bt.clear-logs`

Special-form authoring sugar lives in the language reference:

- `bt` -> [reference page](reference/special-forms/bt.md)
- `defbt` -> [reference page](reference/special-forms/defbt.md)

## See Also

- [Language Reference Index](reference/index.md)
- [Language Semantics](semantics.md)
