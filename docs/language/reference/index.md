# Language Reference Index

This index links to one reference page per implemented language feature and builtin.

Use [Language Syntax](../syntax.md) and [Language Semantics](../semantics.md) for conceptual behaviour; use this section for exact signatures and edge cases.

## Core Syntax And Special Forms

- [`quote`](special-forms/quote.md)
- [`if`](special-forms/if.md)
- [`define`](special-forms/define.md)
- [`lambda`](special-forms/lambda.md)
- [`begin`](special-forms/begin.md)
- [`let`](special-forms/let.md)
- [`cond`](special-forms/cond.md)
- [`and`](special-forms/and.md)
- [`or`](special-forms/or.md)
- [`quasiquote`](special-forms/quasiquote.md)
- [`unquote`](special-forms/unquote.md)
- [`unquote-splicing`](special-forms/unquote-splicing.md)
- [`load`](special-forms/load.md)
- [`bt`](special-forms/bt.md)
- [`defbt`](special-forms/defbt.md)

## Data Types

- [`nil`](data-types/nil.md)
- [`boolean`](data-types/boolean.md)
- [`integer`](data-types/integer.md)
- [`float`](data-types/float.md)
- [`symbol`](data-types/symbol.md)
- [`string`](data-types/string.md)
- [`cons`](data-types/cons.md)
- [`primitive_fn`](data-types/primitive-fn.md)
- [`closure`](data-types/closure.md)
- [`vec`](data-types/vec.md)
- [`map`](data-types/map.md)
- [`pq`](data-types/pq.md)
- [`rng`](data-types/rng.md)
- [`bt_def`](data-types/bt-def.md)
- [`bt_instance`](data-types/bt-instance.md)
- [`image_handle`](data-types/image-handle.md)
- [`blob_handle`](data-types/blob-handle.md)

## Built-ins

### Core list/pair operations

- [`car`](builtins/core/car.md)
- [`cdr`](builtins/core/cdr.md)
- [`cons`](builtins/core/cons.md)
- [`eq?`](builtins/core/eq-q.md)
- [`hash64`](builtins/core/hash64.md)
- [`list`](builtins/core/list.md)
- [`null?`](builtins/core/null-q.md)

### Math and comparisons

- [`*`](builtins/math/mul.md)
- [`+`](builtins/math/plus.md)
- [`-`](builtins/math/minus.md)
- [`/`](builtins/math/div.md)
- [`<`](builtins/math/lt.md)
- [`<=`](builtins/math/lte.md)
- [`=`](builtins/math/eq.md)
- [`>`](builtins/math/gt.md)
- [`>=`](builtins/math/gte.md)
- [`abs`](builtins/math/abs.md)
- [`clamp`](builtins/math/clamp.md)
- [`exp`](builtins/math/exp.md)
- [`log`](builtins/math/log.md)
- [`sqrt`](builtins/math/sqrt.md)

### Type and numeric predicates

- [`float?`](builtins/predicates/float-q.md)
- [`int?`](builtins/predicates/int-q.md)
- [`integer?`](builtins/predicates/integer-q.md)
- [`number?`](builtins/predicates/number-q.md)
- [`zero?`](builtins/predicates/zero-q.md)

### Time

- [`time.now-ms`](builtins/time/time-now-ms.md)

### JSON

- [`json.encode`](builtins/json/json-encode.md)
- [`json.decode`](builtins/json/json-decode.md)

### Random number generation

- [`rng.int`](builtins/rng/rng-int.md)
- [`rng.make`](builtins/rng/rng-make.md)
- [`rng.normal`](builtins/rng/rng-normal.md)
- [`rng.uniform`](builtins/rng/rng-uniform.md)

### Planning services

- [`planner.get-base-seed`](builtins/planner/planner-get-base-seed.md)
- [`planner.logs.dump`](builtins/planner/planner-logs-dump.md)
- [`planner.plan`](builtins/planner/planner-plan.md)
- [`planner.set-base-seed`](builtins/planner/planner-set-base-seed.md)
- [`planner.set-log-enabled`](builtins/planner/planner-set-log-enabled.md)
- [`planner.set-log-path`](builtins/planner/planner-set-log-path.md)

### Environment capability interface

- [`env.info`](builtins/env/env-info.md)
- [`env.attach`](builtins/env/env-attach.md)
- [`env.configure`](builtins/env/env-configure.md)
- [`env.reset`](builtins/env/env-reset.md)
- [`env.observe`](builtins/env/env-observe.md)
- [`env.act`](builtins/env/env-act.md)
- [`env.step`](builtins/env/env-step.md)
- [`env.run-loop`](builtins/env/env-run-loop.md)
- [`env.debug-draw`](builtins/env/env-debug-draw.md)

### Capability introspection

- [`cap.list`](builtins/cap/cap-list.md)
- [`cap.describe`](builtins/cap/cap-describe.md)

### Media handles

- [`image.make`](builtins/media/image-make.md)
- [`image.info`](builtins/media/image-info.md)
- [`blob.make`](builtins/media/blob-make.md)
- [`blob.info`](builtins/media/blob-info.md)

### VLA async services

- [`vla.submit`](builtins/vla/vla-submit.md)
- [`vla.poll`](builtins/vla/vla-poll.md)
- [`vla.cancel`](builtins/vla/vla-cancel.md)
- [`vla.logs.dump`](builtins/vla/vla-logs-dump.md)
- [`vla.set-log-path`](builtins/vla/vla-set-log-path.md)
- [`vla.set-log-enabled`](builtins/vla/vla-set-log-enabled.md)
- [`vla.clear-logs`](builtins/vla/vla-clear-logs.md)

### Mutable vectors

- [`vec.clear!`](builtins/vec/vec-clear-bang.md)
- [`vec.get`](builtins/vec/vec-get.md)
- [`vec.len`](builtins/vec/vec-len.md)
- [`vec.make`](builtins/vec/vec-make.md)
- [`vec.pop!`](builtins/vec/vec-pop-bang.md)
- [`vec.push!`](builtins/vec/vec-push-bang.md)
- [`vec.reserve!`](builtins/vec/vec-reserve-bang.md)
- [`vec.set!`](builtins/vec/vec-set-bang.md)

### Mutable maps

- [`map.del!`](builtins/map/map-del-bang.md)
- [`map.get`](builtins/map/map-get.md)
- [`map.has?`](builtins/map/map-has-q.md)
- [`map.keys`](builtins/map/map-keys.md)
- [`map.make`](builtins/map/map-make.md)
- [`map.set!`](builtins/map/map-set-bang.md)

### Priority queues

- [`pq.make`](builtins/pq/pq-make.md)
- [`pq.len`](builtins/pq/pq-len.md)
- [`pq.empty?`](builtins/pq/pq-empty-q.md)
- [`pq.push!`](builtins/pq/pq-push-bang.md)
- [`pq.peek`](builtins/pq/pq-peek.md)
- [`pq.pop!`](builtins/pq/pq-pop-bang.md)

### IO and persistence

- [`print`](builtins/io/print.md)
- [`save`](builtins/io/save.md)
- [`write`](builtins/io/write.md)
- [`write-to-string`](builtins/io/write-to-string.md)

### GC and heap stats

- [`gc-stats`](builtins/gc/gc-stats.md)
- [`heap-stats`](builtins/gc/heap-stats.md)

### BT integration primitives

- [`bt.blackboard.dump`](builtins/bt/bt-blackboard-dump.md)
- [`bt.clear-logs`](builtins/bt/bt-clear-logs.md)
- [`bt.clear-trace`](builtins/bt/bt-clear-trace.md)
- [`bt.compile`](builtins/bt/bt-compile.md)
- [`bt.export-dot`](builtins/bt/bt-export-dot.md)
- [`bt.load`](builtins/bt/bt-load.md)
- [`bt.load-dsl`](builtins/bt/bt-load-dsl.md)
- [`bt.log.dump`](builtins/bt/bt-log-dump.md)
- [`bt.log.snapshot`](builtins/bt/bt-log-snapshot.md)
- [`bt.logs.dump`](builtins/bt/bt-logs-dump.md)
- [`bt.logs.snapshot`](builtins/bt/bt-logs-snapshot.md)
- [`bt.new-instance`](builtins/bt/bt-new-instance.md)
- [`bt.reset`](builtins/bt/bt-reset.md)
- [`bt.save`](builtins/bt/bt-save.md)
- [`bt.save-dsl`](builtins/bt/bt-save-dsl.md)
- [`bt.scheduler.stats`](builtins/bt/bt-scheduler-stats.md)
- [`bt.set-read-trace-enabled`](builtins/bt/bt-set-read-trace-enabled.md)
- [`bt.set-tick-budget-ms`](builtins/bt/bt-set-tick-budget-ms.md)
- [`bt.set-trace-enabled`](builtins/bt/bt-set-trace-enabled.md)
- [`bt.stats`](builtins/bt/bt-stats.md)
- [`bt.status->symbol`](builtins/bt/bt-status-to-symbol.md)
- [`bt.tick`](builtins/bt/bt-tick.md)
- [`bt.to-dsl`](builtins/bt/bt-to-dsl.md)
- [`bt.trace.dump`](builtins/bt/bt-trace-dump.md)
- [`bt.trace.snapshot`](builtins/bt/bt-trace-snapshot.md)
