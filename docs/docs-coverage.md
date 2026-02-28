# Docs Coverage Checklist

Checklist for keeping docs complete as features evolve.

## Coverage Source Of Truth

Audit language/runtime surfaces against code before updating this checklist:

- special forms: evaluator special-form dispatch in `src/eval.cpp`
- value/data types: runtime value type enum in `include/muslisp/value.hpp`
- built-ins: `install_core_builtins` registrations in `src/builtins.cpp`

## Manual Coverage (Conceptual Pages)

- [x] Home / landing page -> [page](index.md)
- [x] Getting started -> [page](getting-started.md)
- [x] Lisp basics orientation -> [page](lisp-basics.md)
- [x] Language syntax -> [page](language/syntax.md)
- [x] Language semantics -> [page](language/semantics.md)
- [x] Built-ins overview hub -> [page](language/builtins.md)
- [x] Language reference index -> [page](language/reference/index.md)
- [x] BT intro -> [page](bt/intro.md)
- [x] BT syntax -> [page](bt/syntax.md)
- [x] BT semantics -> [page](bt/semantics.md)
- [x] Bounded-time planning overview -> [page](bt/bounded-time-planning.md)
- [x] PlanAction node reference -> [page](bt/plan-action-node.md)
- [x] Planner configuration reference -> [page](bt/planner-configuration.md)
- [x] BT blackboard -> [page](bt/blackboard.md)
- [x] BT scheduler -> [page](bt/scheduler.md)
- [x] BT integration -> [page](bt/robot-integration.md)
- [x] Tracing -> [page](observability/tracing.md)
- [x] Logging -> [page](observability/logging.md)
- [x] Profiling -> [page](observability/profiling.md)
- [x] Planner logging schema -> [page](observability/planner-logging.md)
- [x] Architecture internals -> [page](internals/architecture.md)
- [x] GC internals -> [page](internals/gc.md)
- [x] Code map internals -> [page](internals/code-map.md)
- [x] Embedding/extending internals -> [page](internals/extending.md)
- [x] Testing and verification -> [page](testing.md)
- [x] Roadmap -> [page](limitations-roadmap.md)

## Special Forms
- [x] `quote` -> [page](language/reference/special-forms/quote.md)
- [x] `if` -> [page](language/reference/special-forms/if.md)
- [x] `define` -> [page](language/reference/special-forms/define.md)
- [x] `lambda` -> [page](language/reference/special-forms/lambda.md)
- [x] `begin` -> [page](language/reference/special-forms/begin.md)
- [x] `let` -> [page](language/reference/special-forms/let.md)
- [x] `cond` -> [page](language/reference/special-forms/cond.md)
- [x] `quasiquote` -> [page](language/reference/special-forms/quasiquote.md)
- [x] `unquote` -> [page](language/reference/special-forms/unquote.md)
- [x] `unquote-splicing` -> [page](language/reference/special-forms/unquote-splicing.md)
- [x] `load` -> [page](language/reference/special-forms/load.md)
- [x] `bt` -> [page](language/reference/special-forms/bt.md)
- [x] `defbt` -> [page](language/reference/special-forms/defbt.md)

## Data Types
- [x] `nil` -> [page](language/reference/data-types/nil.md)
- [x] `boolean` -> [page](language/reference/data-types/boolean.md)
- [x] `integer` -> [page](language/reference/data-types/integer.md)
- [x] `float` -> [page](language/reference/data-types/float.md)
- [x] `symbol` -> [page](language/reference/data-types/symbol.md)
- [x] `string` -> [page](language/reference/data-types/string.md)
- [x] `cons` -> [page](language/reference/data-types/cons.md)
- [x] `primitive_fn` -> [page](language/reference/data-types/primitive-fn.md)
- [x] `closure` -> [page](language/reference/data-types/closure.md)
- [x] `vec` -> [page](language/reference/data-types/vec.md)
- [x] `map` -> [page](language/reference/data-types/map.md)
- [x] `rng` -> [page](language/reference/data-types/rng.md)
- [x] `bt_def` -> [page](language/reference/data-types/bt-def.md)
- [x] `bt_instance` -> [page](language/reference/data-types/bt-instance.md)

## Built-ins
### Core list/pair operations
- [x] `car` -> [page](language/reference/builtins/core/car.md)
- [x] `cdr` -> [page](language/reference/builtins/core/cdr.md)
- [x] `cons` -> [page](language/reference/builtins/core/cons.md)
- [x] `eq?` -> [page](language/reference/builtins/core/eq-q.md)
- [x] `hash64` -> [page](language/reference/builtins/core/hash64.md)
- [x] `list` -> [page](language/reference/builtins/core/list.md)
- [x] `null?` -> [page](language/reference/builtins/core/null-q.md)

### Math and comparisons
- [x] `*` -> [page](language/reference/builtins/math/mul.md)
- [x] `+` -> [page](language/reference/builtins/math/plus.md)
- [x] `-` -> [page](language/reference/builtins/math/minus.md)
- [x] `/` -> [page](language/reference/builtins/math/div.md)
- [x] `<` -> [page](language/reference/builtins/math/lt.md)
- [x] `<=` -> [page](language/reference/builtins/math/lte.md)
- [x] `=` -> [page](language/reference/builtins/math/eq.md)
- [x] `>` -> [page](language/reference/builtins/math/gt.md)
- [x] `>=` -> [page](language/reference/builtins/math/gte.md)
- [x] `abs` -> [page](language/reference/builtins/math/abs.md)
- [x] `clamp` -> [page](language/reference/builtins/math/clamp.md)
- [x] `exp` -> [page](language/reference/builtins/math/exp.md)
- [x] `log` -> [page](language/reference/builtins/math/log.md)
- [x] `sqrt` -> [page](language/reference/builtins/math/sqrt.md)

### Type and numeric predicates
- [x] `float?` -> [page](language/reference/builtins/predicates/float-q.md)
- [x] `int?` -> [page](language/reference/builtins/predicates/int-q.md)
- [x] `integer?` -> [page](language/reference/builtins/predicates/integer-q.md)
- [x] `number?` -> [page](language/reference/builtins/predicates/number-q.md)
- [x] `zero?` -> [page](language/reference/builtins/predicates/zero-q.md)

### Time
- [x] `time.now-ms` -> [page](language/reference/builtins/time/time-now-ms.md)

### Random number generation
- [x] `rng.int` -> [page](language/reference/builtins/rng/rng-int.md)
- [x] `rng.make` -> [page](language/reference/builtins/rng/rng-make.md)
- [x] `rng.normal` -> [page](language/reference/builtins/rng/rng-normal.md)
- [x] `rng.uniform` -> [page](language/reference/builtins/rng/rng-uniform.md)

### Planning services
- [x] `planner.get-base-seed` -> [page](language/reference/builtins/planner/planner-get-base-seed.md)
- [x] `planner.logs.dump` -> [page](language/reference/builtins/planner/planner-logs-dump.md)
- [x] `planner.mcts` -> [page](language/reference/builtins/planner/planner-mcts.md)
- [x] `planner.set-base-seed` -> [page](language/reference/builtins/planner/planner-set-base-seed.md)
- [x] `planner.set-log-enabled` -> [page](language/reference/builtins/planner/planner-set-log-enabled.md)
- [x] `planner.set-log-path` -> [page](language/reference/builtins/planner/planner-set-log-path.md)

### Mutable vectors
- [x] `vec.clear!` -> [page](language/reference/builtins/vec/vec-clear-bang.md)
- [x] `vec.get` -> [page](language/reference/builtins/vec/vec-get.md)
- [x] `vec.len` -> [page](language/reference/builtins/vec/vec-len.md)
- [x] `vec.make` -> [page](language/reference/builtins/vec/vec-make.md)
- [x] `vec.pop!` -> [page](language/reference/builtins/vec/vec-pop-bang.md)
- [x] `vec.push!` -> [page](language/reference/builtins/vec/vec-push-bang.md)
- [x] `vec.reserve!` -> [page](language/reference/builtins/vec/vec-reserve-bang.md)
- [x] `vec.set!` -> [page](language/reference/builtins/vec/vec-set-bang.md)

### Mutable maps
- [x] `map.del!` -> [page](language/reference/builtins/map/map-del-bang.md)
- [x] `map.get` -> [page](language/reference/builtins/map/map-get.md)
- [x] `map.has?` -> [page](language/reference/builtins/map/map-has-q.md)
- [x] `map.keys` -> [page](language/reference/builtins/map/map-keys.md)
- [x] `map.make` -> [page](language/reference/builtins/map/map-make.md)
- [x] `map.set!` -> [page](language/reference/builtins/map/map-set-bang.md)

### IO and persistence
- [x] `print` -> [page](language/reference/builtins/io/print.md)
- [x] `save` -> [page](language/reference/builtins/io/save.md)
- [x] `write` -> [page](language/reference/builtins/io/write.md)
- [x] `write-to-string` -> [page](language/reference/builtins/io/write-to-string.md)

### GC and heap stats
- [x] `gc-stats` -> [page](language/reference/builtins/gc/gc-stats.md)
- [x] `heap-stats` -> [page](language/reference/builtins/gc/heap-stats.md)

### BT integration primitives
- [x] `bt.blackboard.dump` -> [page](language/reference/builtins/bt/bt-blackboard-dump.md)
- [x] `bt.clear-logs` -> [page](language/reference/builtins/bt/bt-clear-logs.md)
- [x] `bt.clear-trace` -> [page](language/reference/builtins/bt/bt-clear-trace.md)
- [x] `bt.compile` -> [page](language/reference/builtins/bt/bt-compile.md)
- [x] `bt.load` -> [page](language/reference/builtins/bt/bt-load.md)
- [x] `bt.load-dsl` -> [page](language/reference/builtins/bt/bt-load-dsl.md)
- [x] `bt.log.dump` -> [page](language/reference/builtins/bt/bt-log-dump.md)
- [x] `bt.log.snapshot` -> [page](language/reference/builtins/bt/bt-log-snapshot.md)
- [x] `bt.logs.dump` -> [page](language/reference/builtins/bt/bt-logs-dump.md)
- [x] `bt.logs.snapshot` -> [page](language/reference/builtins/bt/bt-logs-snapshot.md)
- [x] `bt.new-instance` -> [page](language/reference/builtins/bt/bt-new-instance.md)
- [x] `bt.reset` -> [page](language/reference/builtins/bt/bt-reset.md)
- [x] `bt.save` -> [page](language/reference/builtins/bt/bt-save.md)
- [x] `bt.save-dsl` -> [page](language/reference/builtins/bt/bt-save-dsl.md)
- [x] `bt.scheduler.stats` -> [page](language/reference/builtins/bt/bt-scheduler-stats.md)
- [x] `bt.set-read-trace-enabled` -> [page](language/reference/builtins/bt/bt-set-read-trace-enabled.md)
- [x] `bt.set-tick-budget-ms` -> [page](language/reference/builtins/bt/bt-set-tick-budget-ms.md)
- [x] `bt.set-trace-enabled` -> [page](language/reference/builtins/bt/bt-set-trace-enabled.md)
- [x] `bt.stats` -> [page](language/reference/builtins/bt/bt-stats.md)
- [x] `bt.status->symbol` -> [page](language/reference/builtins/bt/bt-status-to-symbol.md)
- [x] `bt.tick` -> [page](language/reference/builtins/bt/bt-tick.md)
- [x] `bt.to-dsl` -> [page](language/reference/builtins/bt/bt-to-dsl.md)
- [x] `bt.trace.dump` -> [page](language/reference/builtins/bt/bt-trace-dump.md)
- [x] `bt.trace.snapshot` -> [page](language/reference/builtins/bt/bt-trace-snapshot.md)
