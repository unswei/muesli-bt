# Built-ins Overview

This page is a summary and link hub for built-ins. Detailed behaviour, errors, and runnable examples are documented in [Language Reference Index](reference/index.md).

## Core

- list/pair: `cons`, `car`, `cdr`, `list`
- predicates: `null?`, `eq?`

## Math And Predicates

- arithmetic: `+`, `-`, `*`, `/`
- comparisons: `=`, `<`, `>`, `<=`, `>=`
- helpers: `sqrt`, `log`, `exp`, `atan2`, `abs`, `clamp`
- predicates: `number?`, `int?`, `integer?`, `float?`, `zero?`

## Sampling And Time

- RNG: `rng.make`, `rng.uniform`, `rng.normal`, `rng.int`
- Clock: `time.now-ms`
- Hashing: `hash64`
- JSON: `json.encode`, `json.decode`

## Mutable Containers

- vectors: `vec.make`, `vec.len`, `vec.get`, `vec.set!`, `vec.push!`, `vec.pop!`, `vec.clear!`, `vec.reserve!`
- maps: `map.make`, `map.get`, `map.has?`, `map.set!`, `map.del!`, `map.keys`
- priority queues: `pq.make`, `pq.len`, `pq.empty?`, `pq.push!`, `pq.peek`, `pq.pop!`

## IO And Runtime Introspection

- IO: `print`, `write`, `write-to-string`, `save`
- Heap/GC: `heap-stats`, `gc-stats`

## Planning Services

- planning call: `planner.plan`
- canonical event stream: `events.enable`, `events.enable-tick-audit`, `events.set-path`, `events.set-flush-each-message`, `events.set-ring-size`, `events.dump`, `events.snapshot-bb`
- planner seed controls: `planner.set-base-seed`, `planner.get-base-seed`
- capabilities: `cap.list`, `cap.describe`, `cap.call`
- async VLA jobs: `vla.submit`, `vla.poll`, `vla.cancel`
- observation handles: `image.make`, `image.info`, `blob.make`, `blob.info`

## Environment Capability Interface

- canonical interface: `env.info`, `env.attach`, `env.configure`, `env.reset`, `env.observe`, `env.act`, `env.step`, `env.run-loop`, `env.debug-draw`

## BT Integration

- authoring/compile: `bt.compile`
- runtime: `bt.new-instance`, `bt.tick`, `bt.reset`, `bt.status->symbol`
- persistence: `bt.to-dsl`, `bt.save-dsl`, `bt.load-dsl`, `bt.save`, `bt.load`
- observability/config: `bt.stats`, `bt.blackboard.dump`, `bt.scheduler.stats`, `bt.set-tick-budget-ms`, plus canonical `events.*`

Special-form authoring sugar lives in the language reference:

- `bt` -> [reference page](reference/special-forms/bt.md)
- `defbt` -> [reference page](reference/special-forms/defbt.md)

## See Also

- [Language Reference Index](reference/index.md)
- [Language Semantics](semantics.md)
